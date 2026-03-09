/* dsl_potato.c -- potatOS audio driver for Duke3D's audiolib
 *
 * Replaces Game/src/audiolib/dsl.c (SDL_mixer backend).
 * The MultiVoc mixer fills a PCM buffer via a callback; we submit
 * chunks to the AC97 hardware via SYS_AUDIO_WRITE.
 *
 * Phase 1: stub (no sound). Can be wired to AC97 later.
 */

#include "audiolib/dsl.h"
#include "libc/string.h"
#include "libc/syscall.h"

#include <stdint.h>

extern volatile int MV_MixPage;

static int mixer_initialized = 0;
static void (*_CallBackFunc)(void) = NULL;
static volatile char *_BufferStart = NULL;
static int _BufferSize = 0;
static int _NumDivisions = 0;
static int _SampleRate = 0;
static int _MixMode = 0;

char *DSL_ErrorString(int ErrorNumber)
{
    (void)ErrorNumber;
    return "potatOS audio driver";
}

int DSL_Init(void)
{
    return DSL_Ok;
}

void DSL_Shutdown(void)
{
    DSL_StopPlayback();
}

int DSL_BeginBufferedPlayback(char *BufferStart,
    int BufferSize, int NumDivisions, unsigned SampleRate,
    int MixMode, void (*CallBackFunc)(void))
{
    _CallBackFunc = CallBackFunc;
    _BufferStart = BufferStart;
    _BufferSize = BufferSize / NumDivisions;
    _NumDivisions = NumDivisions;
    _SampleRate = SampleRate;
    _MixMode = MixMode;
    mixer_initialized = 1;
    return DSL_Ok;
}

void DSL_StopPlayback(void)
{
    mixer_initialized = 0;
    _CallBackFunc = NULL;
}

unsigned DSL_GetPlaybackRate(void)
{
    return _SampleRate;
}

/* Accumulation buffer: collect multiple small mixer chunks before
 * submitting to AC97.  Each MV_ServiceVoc call produces _BufferSize
 * bytes (1024 @ 16-bit stereo = 256 frames = ~5.8 ms at 44100 Hz).
 * We accumulate ACCUM_BUFS calls (~46 ms) for gapless playback. */
#define ACCUM_BUFS  8
#define ACCUM_MAX   (1024 * ACCUM_BUFS)   /* 8192 bytes */
static char  accum_buf[ACCUM_MAX];
static int   accum_fill = 0;

/* Called from the game's main loop to pump audio.
 * We mix several buffers and submit a large chunk to AC97 when idle. */
void DSL_PumpAudio(void)
{
    if (!mixer_initialized || !_CallBackFunc) return;

    /* If AC97 is still playing, nothing to do yet */
    if (sys_audio_is_playing() == 1) return;

    /* If we already have a full accumulation buffer, submit it now */
    if (accum_fill >= ACCUM_MAX) {
        sys_audio_write(accum_buf, accum_fill, _SampleRate);
        accum_fill = 0;
        return;
    }

    /* Mix as many chunks as we can fit */
    while (accum_fill + _BufferSize <= ACCUM_MAX) {
        _CallBackFunc();   /* MV_ServiceVoc: mixes into MV_MixBuffer[MV_MixPage] */
        char *src = (char *)&_BufferStart[MV_MixPage * _BufferSize];
        memcpy(accum_buf + accum_fill, src, _BufferSize);
        accum_fill += _BufferSize;
    }

    /* Submit the full accumulation buffer */
    if (accum_fill > 0) {
        sys_audio_write(accum_buf, accum_fill, _SampleRate);
        accum_fill = 0;
    }
}

uint32_t DisableInterrupts(void)
{
    return 0;
}

void RestoreInterrupts(uint32_t flags)
{
    (void)flags;
}
