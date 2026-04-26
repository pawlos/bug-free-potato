/*
 * sdl2_thread.c — Layer 1 + Layer 2 of the SDL2 shim (threading + audio).
 *
 * Kept separate from sdl2.c because sdl2.c includes stb_image.h, which
 * unconditionally pulls in the host's <stdlib.h> — that drags in the host's
 * <bits/pthreadtypes.h> and conflicts with our libc/pthread.h.
 *
 * This TU intentionally does NOT include stb_image.h.
 *
 * Layer 1: SDL_Thread, SDL_mutex, SDL_cond, SDL_sem (pthread-backed).
 * Layer 2: SDL_OpenAudio* / Lock / Pause / Close (audio worker pthread).
 *
 * NOTE: libc malloc is NOT thread-safe. SDL audio callbacks must be
 * allocation-free, and worker threads should preallocate their working set.
 */

#include "SDL2/SDL.h"
#include "pthread.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"

/* SDL_SetError lives in sdl2.c; declare it here. */
int SDL_SetError(const char *fmt, ...);

/* ═══════════════════════════════════════════════════════════════════════════
 * Layer 1 — Thread / Mutex / Cond / Sem
 * ═══════════════════════════════════════════════════════════════════════════ */

/* ── Thread ─────────────────────────────────────────────────────────────── */

struct SDL_Thread {
    pthread_t tid;
    int       detached;
};

struct sdl_thread_args {
    SDL_ThreadFunction fn;
    void *data;
};

static void* sdl_thread_trampoline(void *arg)
{
    struct sdl_thread_args a = *(struct sdl_thread_args*)arg;
    free(arg);
    int rc = a.fn(a.data);
    return (void*)(unsigned long)(unsigned int)rc;
}

SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
    (void)name;
    if (!fn) return (SDL_Thread*)0;

    struct sdl_thread_args *a = (struct sdl_thread_args*)malloc(sizeof(*a));
    if (!a) { SDL_SetError("oom"); return (SDL_Thread*)0; }
    a->fn = fn;
    a->data = data;

    SDL_Thread *t = (SDL_Thread*)malloc(sizeof(*t));
    if (!t) { free(a); SDL_SetError("oom"); return (SDL_Thread*)0; }
    t->detached = 0;

    int rc = pthread_create(&t->tid, (pthread_attr_t*)0, sdl_thread_trampoline, a);
    if (rc != 0) {
        free(a);
        free(t);
        SDL_SetError("pthread_create failed");
        return (SDL_Thread*)0;
    }
    return t;
}

void SDL_WaitThread(SDL_Thread *t, int *status)
{
    if (!t) { if (status) *status = -1; return; }
    void *retval = (void*)0;
    pthread_join(t->tid, &retval);
    if (status) *status = (int)(unsigned int)(unsigned long)retval;
    free(t);
}

void SDL_DetachThread(SDL_Thread *t)
{
    /* Underlying pthread keeps running; we never join it. The kernel zombie
     * slot leaks until the process exits — acceptable for typical usage. */
    if (!t) return;
    free(t);
}

SDL_threadID SDL_ThreadID(void)
{
    return (SDL_threadID)pthread_self();
}

SDL_threadID SDL_GetThreadID(SDL_Thread *t)
{
    return t ? (SDL_threadID)t->tid : 0;
}

/* ── Mutex ──────────────────────────────────────────────────────────────── */

struct SDL_mutex {
    pthread_mutex_t m;
};

SDL_mutex* SDL_CreateMutex(void)
{
    SDL_mutex *m = (SDL_mutex*)malloc(sizeof(*m));
    if (!m) return (SDL_mutex*)0;
    pthread_mutex_init(&m->m, (pthread_mutexattr_t*)0);
    return m;
}

void SDL_DestroyMutex(SDL_mutex *m)
{
    if (!m) return;
    pthread_mutex_destroy(&m->m);
    free(m);
}

int SDL_LockMutex(SDL_mutex *m)
{
    if (!m) return -1;
    return pthread_mutex_lock(&m->m);
}

int SDL_TryLockMutex(SDL_mutex *m)
{
    if (!m) return -1;
    return pthread_mutex_trylock(&m->m) == 0 ? 0 : SDL_MUTEX_TIMEDOUT;
}

int SDL_UnlockMutex(SDL_mutex *m)
{
    if (!m) return -1;
    return pthread_mutex_unlock(&m->m);
}

/* ── Condition Variable ─────────────────────────────────────────────────── */

struct SDL_cond {
    pthread_cond_t c;
};

SDL_cond* SDL_CreateCond(void)
{
    SDL_cond *c = (SDL_cond*)malloc(sizeof(*c));
    if (!c) return (SDL_cond*)0;
    pthread_cond_init(&c->c, (pthread_condattr_t*)0);
    return c;
}

void SDL_DestroyCond(SDL_cond *c)
{
    if (!c) return;
    pthread_cond_destroy(&c->c);
    free(c);
}

int SDL_CondWait(SDL_cond *c, SDL_mutex *m)
{
    if (!c || !m) return -1;
    return pthread_cond_wait(&c->c, &m->m);
}

int SDL_CondSignal(SDL_cond *c)
{
    if (!c) return -1;
    return pthread_cond_signal(&c->c);
}

int SDL_CondBroadcast(SDL_cond *c)
{
    if (!c) return -1;
    return pthread_cond_broadcast(&c->c);
}

/* ── Semaphore ──────────────────────────────────────────────────────────── */

struct SDL_sem {
    pthread_mutex_t lock;
    pthread_cond_t  not_zero;
    Uint32          count;
};

SDL_sem* SDL_CreateSemaphore(Uint32 initial_value)
{
    SDL_sem *s = (SDL_sem*)malloc(sizeof(*s));
    if (!s) return (SDL_sem*)0;
    pthread_mutex_init(&s->lock, (pthread_mutexattr_t*)0);
    pthread_cond_init(&s->not_zero, (pthread_condattr_t*)0);
    s->count = initial_value;
    return s;
}

void SDL_DestroySemaphore(SDL_sem *s)
{
    if (!s) return;
    pthread_cond_destroy(&s->not_zero);
    pthread_mutex_destroy(&s->lock);
    free(s);
}

int SDL_SemWait(SDL_sem *s)
{
    if (!s) return -1;
    pthread_mutex_lock(&s->lock);
    while (s->count == 0)
        pthread_cond_wait(&s->not_zero, &s->lock);
    s->count--;
    pthread_mutex_unlock(&s->lock);
    return 0;
}

int SDL_SemTryWait(SDL_sem *s)
{
    if (!s) return -1;
    int rc = SDL_MUTEX_TIMEDOUT;
    pthread_mutex_lock(&s->lock);
    if (s->count > 0) { s->count--; rc = 0; }
    pthread_mutex_unlock(&s->lock);
    return rc;
}

int SDL_SemPost(SDL_sem *s)
{
    if (!s) return -1;
    pthread_mutex_lock(&s->lock);
    s->count++;
    pthread_cond_signal(&s->not_zero);
    pthread_mutex_unlock(&s->lock);
    return 0;
}

Uint32 SDL_SemValue(SDL_sem *s)
{
    if (!s) return 0;
    pthread_mutex_lock(&s->lock);
    Uint32 v = s->count;
    pthread_mutex_unlock(&s->lock);
    return v;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Layer 2 — Audio callback subsystem
 *
 * Worker pthread loop:
 *   lock → callback (or silence if paused) → unlock → submit to AC97
 *   (busy-poll the AC97 ring with 1ms sleeps when both DMA slots are full)
 *
 * One global device — only one game at a time owns AC97.
 * SDL game contract: callback runs on worker thread. Game must
 * SDL_LockAudio() before mutating state the callback reads.
 * ═══════════════════════════════════════════════════════════════════════════ */

struct AudioDevice {
    SDL_AudioSpec   spec;
    Uint8          *buffer;
    pthread_t       worker;
    pthread_mutex_t lock;
    int             paused;
    int             exit_requested;
    int             ac97_open;
    int             active;
};

static struct AudioDevice g_audio_dev;

static void *audio_worker(void *arg)
{
    struct AudioDevice *dev = (struct AudioDevice *)arg;

    /* Acquire AC97. format=0 → AC97 native (S16LSB stereo). */
    if (sys_audio_open(dev->spec.freq, dev->spec.channels, 0) >= 0)
        dev->ac97_open = 1;

    while (!dev->exit_requested) {
        pthread_mutex_lock(&dev->lock);
        if (dev->paused || !dev->spec.callback) {
            memset(dev->buffer, dev->spec.silence, dev->spec.size);
        } else {
            dev->spec.callback(dev->spec.userdata, dev->buffer, (int)dev->spec.size);
        }
        pthread_mutex_unlock(&dev->lock);

        if (dev->ac97_open) {
            /* sys_audio_write: 1=ok, 0=busy (DMA slots full), -1=device gone. */
            long rc;
            for (;;) {
                rc = sys_audio_write(dev->buffer, dev->spec.size, dev->spec.freq);
                if (rc != 0) break;
                if (dev->exit_requested) break;
                sys_sleep_ms(1);
            }
            if (rc < 0) break;  /* AC97 went away */
        } else {
            /* No AC97 — pace the loop so we don't burn CPU. Compute the
             * approximate buffer duration in ms; sleep that long. */
            Uint8 bps = (dev->spec.format & 0xFF) / 8;
            if (bps == 0) bps = 1;
            Uint32 frame_bytes = (Uint32)dev->spec.channels * bps;
            if (frame_bytes == 0) frame_bytes = 1;
            Uint32 duration_ms = (dev->spec.size * 1000)
                                 / (dev->spec.freq * frame_bytes);
            if (duration_ms == 0) duration_ms = 1;
            sys_sleep_ms(duration_ms);
        }
    }

    if (dev->ac97_open) {
        sys_audio_close();
        dev->ac97_open = 0;
    }
    return (void*)0;
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
    const SDL_AudioSpec *desired, SDL_AudioSpec *obtained, int allowed_changes)
{
    (void)device; (void)iscapture; (void)allowed_changes;

    if (!desired) {
        SDL_SetError("desired is null");
        return 0;
    }
    if (g_audio_dev.active) {
        SDL_SetError("audio already open");
        return 0;
    }

    /* Compute spec.size and spec.silence from format/channels/samples per SDL. */
    g_audio_dev.spec = *desired;
    Uint8 bits = (Uint8)(desired->format & 0xFF);
    if (bits == 0) bits = 16;
    Uint8 bytes_per_sample = bits / 8;
    if (bytes_per_sample == 0) bytes_per_sample = 1;
    int is_signed = (desired->format & 0x8000) ? 1 : 0;
    g_audio_dev.spec.silence = (!is_signed && bits == 8) ? 0x80 : 0x00;
    g_audio_dev.spec.size = (Uint32)desired->samples
                          * (Uint32)desired->channels
                          * (Uint32)bytes_per_sample;
    if (obtained) *obtained = g_audio_dev.spec;

    g_audio_dev.buffer = (Uint8 *)malloc(g_audio_dev.spec.size);
    if (!g_audio_dev.buffer) {
        SDL_SetError("oom audio buffer");
        return 0;
    }
    memset(g_audio_dev.buffer, g_audio_dev.spec.silence, g_audio_dev.spec.size);

    pthread_mutex_init(&g_audio_dev.lock, (pthread_mutexattr_t*)0);
    g_audio_dev.paused = 1;          /* SDL starts paused — game unpauses */
    g_audio_dev.exit_requested = 0;
    g_audio_dev.ac97_open = 0;
    g_audio_dev.active = 1;

    if (pthread_create(&g_audio_dev.worker, (pthread_attr_t*)0,
                       audio_worker, &g_audio_dev) != 0) {
        free(g_audio_dev.buffer);
        g_audio_dev.buffer = (Uint8*)0;
        pthread_mutex_destroy(&g_audio_dev.lock);
        g_audio_dev.active = 0;
        SDL_SetError("pthread_create failed");
        return 0;
    }
    return 1;  /* device id */
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
    (void)dev;
    if (!g_audio_dev.active) return;
    g_audio_dev.exit_requested = 1;
    pthread_join(g_audio_dev.worker, (void**)0);
    free(g_audio_dev.buffer);
    g_audio_dev.buffer = (Uint8*)0;
    pthread_mutex_destroy(&g_audio_dev.lock);
    g_audio_dev.active = 0;
}

void SDL_LockAudioDevice(SDL_AudioDeviceID dev)
{
    (void)dev;
    if (g_audio_dev.active) pthread_mutex_lock(&g_audio_dev.lock);
}

void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev)
{
    (void)dev;
    if (g_audio_dev.active) pthread_mutex_unlock(&g_audio_dev.lock);
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
    (void)dev;
    if (g_audio_dev.active) g_audio_dev.paused = pause_on ? 1 : 0;
}

/* Legacy (non-device) audio API — maps to device id 1. */
int SDL_OpenAudio(const SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    SDL_AudioDeviceID id = SDL_OpenAudioDevice((const char*)0, 0,
                                               desired, obtained, 0);
    return id == 1 ? 0 : -1;
}

void SDL_CloseAudio(void)             { SDL_CloseAudioDevice(1); }
void SDL_PauseAudio(int pause_on)     { SDL_PauseAudioDevice(1, pause_on); }
void SDL_LockAudio(void)              { SDL_LockAudioDevice(1); }
void SDL_UnlockAudio(void)            { SDL_UnlockAudioDevice(1); }

/* Queue model — orthogonal to callback model; remains a stub for v1. */
int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data, Uint32 len)
{ (void)dev; (void)data; (void)len; return 0; }

Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev)
{ (void)dev; return 0; }

void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev)
{ (void)dev; }
