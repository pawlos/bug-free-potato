/*
 * sdl2_mixer.c — software mixer for the potatOS SDL_mixer shim.
 *
 * Layered on top of SDL_OpenAudio (sdl2_thread.c → AC97 audio worker pthread).
 * Mix_OpenAudioDevice opens a single audio stream where the SDL callback
 * is mixer_callback() below; that callback runs the music hook, sums the
 * active Mix_Chunks across the channel slots, then runs the post-mix hook.
 *
 * Mix_Chunks are expected to already be in the output format
 * (typically AUDIO_S16SYS, 2 ch, 44100 Hz) — Wolf4SDL's SD_PrepareSound runs
 * SDL_BuildAudioCVT/SDL_ConvertAudio at load time to guarantee that.
 *
 * Limitations vs. real SDL_mixer:
 *   - no fade-in/out, no Mix_LoadWAV (decoded chunks only via QuickLoad_RAW)
 *   - no group "newer" / "count" tracking; oldest = first active in group
 *   - timestamps used by Mix_GroupOldest are coarse (process-wide counter)
 *   - master/group volume layered as multiplicative fixed-point integers
 */

#include "SDL_mixer.h"
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"
#include "syscall.h"
#include <string.h>
#include <stdlib.h>

#define MIXER_MAX_CHANNELS MIX_CHANNELS

typedef struct {
    Mix_Chunk *chunk;
    Uint32 pos;            /* byte offset into chunk->abuf */
    int    loops;          /* loops remaining; -1 = infinite */
    Uint8  pan_left;       /* 0..255 */
    Uint8  pan_right;      /* 0..255 */
    int    volume;         /* per-channel volume 0..MIX_MAX_VOLUME */
    int    active;
    int    paused;
    int    reserved;
    int    group;
    Uint32 start_seq;      /* ordering for Mix_GroupOldest */
} MixerChannel;

static int          g_mix_open = 0;
static int          g_mix_freq = 44100;
static Uint16       g_mix_format = AUDIO_S16SYS;
static int          g_mix_channels = 2;
static MixerChannel g_chans[MIXER_MAX_CHANNELS];
static Uint32       g_play_seq = 0;

static void (*g_music_hook)(void*, Uint8*, int) = (void(*)(void*,Uint8*,int))0;
static void  *g_music_hook_arg = (void*)0;
static void (*g_postmix)(void*, Uint8*, int) = (void(*)(void*,Uint8*,int))0;
static void  *g_postmix_arg = (void*)0;
static void (*g_channel_finished)(int) = (void(*)(int))0;
static void (*g_music_finished)(void) = (void(*)(void))0;

static int          g_music_volume = MIX_MAX_VOLUME;
static int          g_music_paused = 0;
static SDL_mutex   *g_mix_lock = (SDL_mutex*)0;

/* --------------------------------------------------------------------------
 * Audio callback — runs on the AC97 audio worker thread.
 *
 * Output buffer is `len` bytes of S16 native-endian stereo (the mixer is
 * opened that way; if a caller wanted a different format they'd be on their
 * own).  Steps:
 *   1. zero output
 *   2. music hook fills it (volume-scaled afterward)
 *   3. each active channel mixes its chunk samples on top with pan + vol
 *   4. post-mix hook gets the final buffer
 * -------------------------------------------------------------------------- */
static void mixer_callback(void *udata, Uint8 *out, int out_bytes)
{
    (void)udata;
    Sint16 *o = (Sint16*)out;
    int frames = out_bytes / 4;  /* S16 stereo = 4 bytes/frame */

    memset(out, 0, out_bytes);

    /* Music hook fills the buffer, then we attenuate by music volume. */
    if (g_music_hook && !g_music_paused) {
        g_music_hook(g_music_hook_arg, out, out_bytes);
        if (g_music_volume != MIX_MAX_VOLUME) {
            for (int i = 0; i < frames * 2; i++) {
                int v = (int)o[i] * g_music_volume / MIX_MAX_VOLUME;
                o[i] = (Sint16)v;
            }
        }
    }

    if (g_mix_lock) SDL_LockMutex(g_mix_lock);
    for (int ch = 0; ch < MIXER_MAX_CHANNELS; ch++) {
        MixerChannel *c = &g_chans[ch];
        if (!c->active || c->paused || !c->chunk || !c->chunk->abuf) continue;

        Sint16 *src = (Sint16*)(c->chunk->abuf + c->pos);
        int avail_bytes = (int)c->chunk->alen - (int)c->pos;
        int avail_frames = avail_bytes / 4;
        int play_frames = (avail_frames < frames) ? avail_frames : frames;

        int chunk_vol = (c->chunk->volume) ? c->chunk->volume : MIX_MAX_VOLUME;
        int vol = c->volume * chunk_vol / MIX_MAX_VOLUME;
        int pl  = c->pan_left;
        int pr  = c->pan_right;

        for (int f = 0; f < play_frames; f++) {
            int l = src[f*2 + 0];
            int r = src[f*2 + 1];
            l = (l * pl * vol) / (255 * MIX_MAX_VOLUME);
            r = (r * pr * vol) / (255 * MIX_MAX_VOLUME);
            int ol = (int)o[f*2 + 0] + l;
            int or_= (int)o[f*2 + 1] + r;
            if (ol >  32767) ol =  32767;
            if (ol < -32768) ol = -32768;
            if (or_>  32767) or_=  32767;
            if (or_< -32768) or_= -32768;
            o[f*2 + 0] = (Sint16)ol;
            o[f*2 + 1] = (Sint16)or_;
        }

        c->pos += (Uint32)(play_frames * 4);
        if (c->pos >= c->chunk->alen) {
            if (c->loops != 0) {
                if (c->loops > 0) c->loops--;
                c->pos = 0;
            } else {
                c->active = 0;
                if (g_channel_finished) g_channel_finished(ch);
            }
        }
    }
    if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);

    if (g_postmix) g_postmix(g_postmix_arg, out, out_bytes);
}

/* -------------------------------------------------------------------------- */

int Mix_OpenAudio(int freq, Uint16 fmt, int chans, int chunksize)
{
    return Mix_OpenAudioDevice(freq, fmt, chans, chunksize, (const char*)0, 0);
}

int Mix_OpenAudioDevice(int freq, Uint16 fmt, int chans, int chunksize,
                        const char *dev, int allowed_changes)
{
    (void)dev; (void)allowed_changes;
    if (g_mix_open) return 0;

    SDL_AudioSpec want, got;
    memset(&want, 0, sizeof(want));
    memset(&got,  0, sizeof(got));
    want.freq     = freq;
    want.format   = fmt;
    want.channels = (Uint8)chans;
    want.samples  = (Uint16)chunksize;
    want.callback = mixer_callback;
    want.userdata = (void*)0;

    if (SDL_OpenAudio(&want, &got) < 0) return -1;

    g_mix_freq     = got.freq;
    g_mix_format   = got.format;
    g_mix_channels = got.channels;
    g_mix_lock     = SDL_CreateMutex();

    memset(g_chans, 0, sizeof(g_chans));
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
        g_chans[i].volume    = MIX_MAX_VOLUME;
        g_chans[i].pan_left  = 255;
        g_chans[i].pan_right = 255;
        g_chans[i].group     = -1;
    }
    g_play_seq = 0;
    g_mix_open = 1;
    SDL_PauseAudio(0);
    return 0;
}

void Mix_CloseAudio(void)
{
    if (!g_mix_open) return;
    SDL_CloseAudio();
    if (g_mix_lock) { SDL_DestroyMutex(g_mix_lock); g_mix_lock = (SDL_mutex*)0; }
    g_mix_open = 0;
}

int Mix_QuerySpec(int *freq, Uint16 *fmt, int *chans)
{
    if (!g_mix_open) return 0;
    if (freq)  *freq  = g_mix_freq;
    if (fmt)   *fmt   = g_mix_format;
    if (chans) *chans = g_mix_channels;
    return 1;
}

int Mix_AllocateChannels(int n) { (void)n; return MIXER_MAX_CHANNELS; }

int Mix_ReserveChannels(int n)
{
    if (n < 0) n = 0;
    if (n > MIXER_MAX_CHANNELS) n = MIXER_MAX_CHANNELS;
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++)
        g_chans[i].reserved = (i < n);
    return n;
}

int Mix_GroupChannels(int from, int to, int tag)
{
    if (from < 0) from = 0;
    if (to >= MIXER_MAX_CHANNELS) to = MIXER_MAX_CHANNELS - 1;
    int n = 0;
    for (int i = from; i <= to; i++) { g_chans[i].group = tag; n++; }
    return n;
}

int Mix_GroupAvailable(int tag)
{
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++)
        if (g_chans[i].group == tag && !g_chans[i].active) return i;
    return -1;
}

int Mix_GroupOldest(int tag)
{
    int oldest = -1;
    Uint32 oldest_seq = 0xFFFFFFFFu;
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
        if (g_chans[i].group == tag && g_chans[i].active &&
            g_chans[i].start_seq < oldest_seq) {
            oldest = i;
            oldest_seq = g_chans[i].start_seq;
        }
    }
    return oldest;
}

int Mix_GroupNewer(int tag)
{
    int newest = -1;
    Uint32 newest_seq = 0;
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
        if (g_chans[i].group == tag && g_chans[i].active &&
            g_chans[i].start_seq >= newest_seq) {
            newest = i;
            newest_seq = g_chans[i].start_seq;
        }
    }
    return newest;
}

int Mix_GroupCount(int tag)
{
    int n = 0;
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++)
        if (g_chans[i].group == tag) n++;
    return n;
}

int Mix_SetPanning(int chan, Uint8 left, Uint8 right)
{
    if (chan < 0 || chan >= MIXER_MAX_CHANNELS) return 0;
    g_chans[chan].pan_left = left;
    g_chans[chan].pan_right = right;
    return 1;
}

int Mix_SetDistance(int chan, Uint8 dist) { (void)chan; (void)dist; return 0; }
int Mix_SetPosition(int chan, Sint16 angle, Uint8 dist) { (void)chan; (void)angle; (void)dist; return 0; }
int Mix_SetReverseStereo(int chan, int flip) { (void)chan; (void)flip; return 0; }

Mix_Chunk *Mix_LoadWAV(const char *file)        { (void)file; return (Mix_Chunk*)0; }
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc) { (void)src; (void)freesrc; return (Mix_Chunk*)0; }
Mix_Chunk *Mix_QuickLoad_WAV(Uint8 *mem)        { (void)mem; return (Mix_Chunk*)0; }

Mix_Chunk *Mix_QuickLoad_RAW(Uint8 *mem, Uint32 len)
{
    Mix_Chunk *c = (Mix_Chunk*)malloc(sizeof(Mix_Chunk));
    if (!c) return (Mix_Chunk*)0;
    c->allocated = 0;
    c->abuf      = mem;
    c->alen      = len;
    c->volume    = MIX_MAX_VOLUME;
    return c;
}

void Mix_FreeChunk(Mix_Chunk *c)
{
    if (!c) return;
    /* Stop any channel currently playing this chunk so we don't read freed
     * memory from the audio worker. */
    if (g_mix_lock) SDL_LockMutex(g_mix_lock);
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
        if (g_chans[i].chunk == c) {
            g_chans[i].active = 0;
            g_chans[i].chunk  = (Mix_Chunk*)0;
        }
    }
    if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);
    if (c->allocated && c->abuf) free(c->abuf);
    free(c);
}

Mix_Music *Mix_LoadMUS(const char *file) { (void)file; return (Mix_Music*)0; }
Mix_Music *Mix_LoadMUS_RW(SDL_RWops *rw, int freesrc) { (void)rw; (void)freesrc; return (Mix_Music*)0; }
void       Mix_FreeMusic(Mix_Music *m)   { (void)m; }

int Mix_PlayChannel(int chan, Mix_Chunk *c, int loops)
{
    return Mix_PlayChannelTimed(chan, c, loops, -1);
}

int Mix_PlayChannelTimed(int chan, Mix_Chunk *c, int loops, int ticks)
{
    (void)ticks;
    if (!g_mix_open || !c || !c->abuf) return -1;
    if (g_mix_lock) SDL_LockMutex(g_mix_lock);
    if (chan < 0) {
        for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
            if (!g_chans[i].active && !g_chans[i].reserved) { chan = i; break; }
        }
        if (chan < 0) {
            if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);
            return -1;
        }
    }
    if (chan >= MIXER_MAX_CHANNELS) {
        if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);
        return -1;
    }
    g_chans[chan].chunk     = c;
    g_chans[chan].pos       = 0;
    g_chans[chan].loops     = loops;
    g_chans[chan].active    = 1;
    g_chans[chan].paused    = 0;
    g_chans[chan].start_seq = ++g_play_seq;
    if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);
    return chan;
}

int Mix_FadeInChannel(int chan, Mix_Chunk *c, int loops, int ms)
{
    (void)ms;
    return Mix_PlayChannel(chan, c, loops);
}

int Mix_PlayMusic(Mix_Music *m, int loops)        { (void)m; (void)loops; return 0; }
int Mix_FadeInMusic(Mix_Music *m, int loops, int ms) { (void)m; (void)loops; (void)ms; return 0; }

int Mix_HaltChannel(int chan)
{
    if (!g_mix_open) return 0;
    if (g_mix_lock) SDL_LockMutex(g_mix_lock);
    if (chan < 0) {
        for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
            if (g_chans[i].active) {
                g_chans[i].active = 0;
                if (g_channel_finished) g_channel_finished(i);
            }
        }
    } else if (chan < MIXER_MAX_CHANNELS && g_chans[chan].active) {
        g_chans[chan].active = 0;
        if (g_channel_finished) g_channel_finished(chan);
    }
    if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);
    return 0;
}

int Mix_HaltGroup(int tag)
{
    if (!g_mix_open) return 0;
    if (g_mix_lock) SDL_LockMutex(g_mix_lock);
    for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
        if (g_chans[i].group == tag && g_chans[i].active) {
            g_chans[i].active = 0;
            if (g_channel_finished) g_channel_finished(i);
        }
    }
    if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);
    return 0;
}

int Mix_HaltMusic(void)                           { return 0; }
int Mix_FadeOutChannel(int chan, int ms)          { (void)ms; return Mix_HaltChannel(chan); }
int Mix_FadeOutMusic(int ms)                      { (void)ms; return 0; }

int Mix_Playing(int chan)
{
    if (!g_mix_open) return 0;
    if (chan < 0) {
        int n = 0;
        for (int i = 0; i < MIXER_MAX_CHANNELS; i++) if (g_chans[i].active) n++;
        return n;
    }
    if (chan >= MIXER_MAX_CHANNELS) return 0;
    return g_chans[chan].active ? 1 : 0;
}

int Mix_PlayingMusic(void) { return g_music_hook != (void(*)(void*,Uint8*,int))0; }

int Mix_Paused(int chan)
{
    if (chan < 0 || chan >= MIXER_MAX_CHANNELS) return 0;
    return g_chans[chan].paused ? 1 : 0;
}

int  Mix_PausedMusic(void)        { return g_music_paused; }
void Mix_Pause(int chan)
{
    if (chan < 0) {
        for (int i = 0; i < MIXER_MAX_CHANNELS; i++) g_chans[i].paused = 1;
    } else if (chan < MIXER_MAX_CHANNELS) {
        g_chans[chan].paused = 1;
    }
}
void Mix_Resume(int chan)
{
    if (chan < 0) {
        for (int i = 0; i < MIXER_MAX_CHANNELS; i++) g_chans[i].paused = 0;
    } else if (chan < MIXER_MAX_CHANNELS) {
        g_chans[chan].paused = 0;
    }
}
void Mix_PauseMusic(void)         { g_music_paused = 1; }
void Mix_ResumeMusic(void)        { g_music_paused = 0; }
void Mix_RewindMusic(void)        {}

int Mix_Volume(int chan, int volume)
{
    if (chan < 0) {
        /* Apply to all channels — return previous of channel 0. */
        int prev = g_chans[0].volume;
        for (int i = 0; i < MIXER_MAX_CHANNELS; i++) {
            if (volume >= 0)
                g_chans[i].volume = (volume > MIX_MAX_VOLUME) ? MIX_MAX_VOLUME : volume;
        }
        return prev;
    }
    if (chan >= MIXER_MAX_CHANNELS) return 0;
    int prev = g_chans[chan].volume;
    if (volume >= 0)
        g_chans[chan].volume = (volume > MIX_MAX_VOLUME) ? MIX_MAX_VOLUME : volume;
    return prev;
}

int Mix_VolumeChunk(Mix_Chunk *c, int volume)
{
    if (!c) return -1;
    int prev = c->volume;
    if (volume >= 0)
        c->volume = (Uint8)((volume > MIX_MAX_VOLUME) ? MIX_MAX_VOLUME : volume);
    return prev;
}

int Mix_VolumeMusic(int volume)
{
    int prev = g_music_volume;
    if (volume >= 0)
        g_music_volume = (volume > MIX_MAX_VOLUME) ? MIX_MAX_VOLUME : volume;
    return prev;
}

int Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *u)
{ (void)chan; (void)f; (void)d; (void)u; return 0; }
int Mix_UnregisterEffect(int chan, Mix_EffectFunc_t f)
{ (void)chan; (void)f; return 0; }

void Mix_HookMusic(void (*mix_func)(void*, Uint8*, int), void *arg)
{
    if (g_mix_lock) SDL_LockMutex(g_mix_lock);
    g_music_hook     = mix_func;
    g_music_hook_arg = arg;
    if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);
}

void Mix_SetPostMix(void (*mix_func)(void*, Uint8*, int), void *arg)
{
    if (g_mix_lock) SDL_LockMutex(g_mix_lock);
    g_postmix     = mix_func;
    g_postmix_arg = arg;
    if (g_mix_lock) SDL_UnlockMutex(g_mix_lock);
}

void Mix_ChannelFinished(void (*cb)(int))   { g_channel_finished = cb; }
void Mix_HookMusicFinished(void (*cb)(void)){ g_music_finished = cb; }

Mix_Fading Mix_FadingMusic(void)            { return MIX_NO_FADING; }
Mix_Fading Mix_FadingChannel(int chan)      { (void)chan; return MIX_NO_FADING; }

const char *Mix_GetError(void)              { return SDL_GetError(); }

int  Mix_Init(int flags)                    { (void)flags; return 0; }
void Mix_Quit(void)                         {}
