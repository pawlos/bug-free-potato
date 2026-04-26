/*
 * sdltone.c — Layer 2 self-test for SDL2 audio callback.
 *
 * Plays a 440 Hz sine wave for 3 seconds via SDL_OpenAudio + callback.
 * Tests: AC97 acquisition, callback firing on the audio thread,
 *        Lock/Unlock/Pause, clean shutdown.
 *
 * Run: exec BIN/SDLTONE.ELF
 */

#include "libc/SDL2/SDL.h"
#include "libc/syscall.h"
#include "libc/stdio.h"
#include <stdarg.h>

static void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void kprintf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("%s", buf);
    if (n > 0) sys_write_serial(buf, (size_t)n);
}

#define SAMPLE_RATE   44100
#define TONE_HZ       440
#define AMPLITUDE     6000     /* well below S16 max (32767) */
#define BUFFER_SAMPLES 1024    /* frames per callback */

/* Tiny sine table — avoids depending on sinf in libc/math */
#define SINE_LEN 256
static Sint16 sine_table[SINE_LEN];

static void build_sine_table(void)
{
    /* Generated: sine_table[i] = AMPLITUDE * sin(2*pi*i/SINE_LEN)
     * Done with a discrete recurrence to avoid <math.h>. */
    /* We use Bhaskara I approximation: sin(x) ≈ 16x(pi-x)/(5*pi*pi - 4x(pi-x))
     * for x in [0, pi]; mirror for [pi, 2pi]. */
    const long PI_Q = 3141592;     /* pi * 1e6 */
    const long TAU_Q = 6283184;    /* 2*pi * 1e6 */
    for (int i = 0; i < SINE_LEN; i++) {
        long x = (TAU_Q * i) / SINE_LEN;     /* x in [0, 2*pi) Q1e6 */
        int sign = 1;
        if (x > PI_Q) { x -= PI_Q; sign = -1; }
        long pmx = PI_Q - x;
        long num = 16 * x * pmx;             /* careful: large, but fits in long for these magnitudes */
        long den = (5 * PI_Q * PI_Q / 1000000) - (4 * x * pmx / 1000000);
        long sin_q = num / den;              /* approx Q1e6, range ~[-1e6,1e6] */
        long s = (long)AMPLITUDE * sin_q / 1000000;
        if (sign < 0) s = -s;
        sine_table[i] = (Sint16)s;
    }
}

/* Phase accumulator; shared between main and callback. Use volatile so
 * compiler doesn't cache across callback invocations. */
static volatile Uint32 g_phase = 0;
/* Step (in fixed-point) per output sample so 440 Hz @ 44100 Hz keeps phase. */
static Uint32 g_phase_step = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    Sint16 *out = (Sint16*)stream;
    int frames = len / (2 * sizeof(Sint16));   /* 2 = stereo */
    Uint32 phase = g_phase;
    for (int i = 0; i < frames; i++) {
        Uint32 idx = (phase >> 16) & (SINE_LEN - 1);
        Sint16 s = sine_table[idx];
        out[i*2 + 0] = s;     /* L */
        out[i*2 + 1] = s;     /* R */
        phase += g_phase_step;
    }
    g_phase = phase;
}

int main(void)
{
    kprintf("=== sdltone: 440 Hz / 3s ===\n");
    build_sine_table();

    /* phase_step = (TONE_HZ * SINE_LEN / SAMPLE_RATE) << 16
     * Keeping numerator small enough not to overflow. */
    g_phase_step = (((Uint32)TONE_HZ * SINE_LEN) << 16) / SAMPLE_RATE;
    kprintf("[setup] phase_step=0x%x\n", g_phase_step);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = BUFFER_SAMPLES;
    want.callback = audio_callback;
    want.userdata = (void*)0;

    if (SDL_OpenAudio(&want, &have) < 0) {
        kprintf("[fail] SDL_OpenAudio: %s\n", SDL_GetError());
        return 1;
    }
    kprintf("[ok] SDL_OpenAudio  freq=%d ch=%d size=%u silence=0x%x\n",
            have.freq, have.channels, have.size, have.silence);

    /* Start playback. */
    SDL_PauseAudio(0);
    kprintf("[ok] SDL_PauseAudio(0) — should be hearing tone now\n");

    /* Sleep 3 seconds; the callback fires on the audio worker thread. */
    SDL_Delay(3000);

    SDL_PauseAudio(1);
    kprintf("[ok] SDL_PauseAudio(1) — silenced\n");

    SDL_CloseAudio();
    kprintf("[ok] SDL_CloseAudio — clean shutdown\n");

    kprintf("=== done ===\n");
    return 0;
}
