/* SDL_mixer.h shim for potatOS — software mixer over our SDL_OpenAudio +
 * AC97 audio thread.  Implementation in sdl2_mixer.c.  Wolf4SDL's audio path
 * (SD_PrepareSound → Mix_PlayChannel for SFX, Mix_HookMusic for IMF/OPL3
 * music) is the validation target; new ports may need additional surface.
 */
#pragma once

#include "SDL2/SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIX_DEFAULT_FREQUENCY 44100
#define MIX_DEFAULT_FORMAT    AUDIO_S16SYS
#define MIX_DEFAULT_CHANNELS  2
#define MIX_MAX_VOLUME        128
#define MIX_CHANNELS          8

typedef struct Mix_Chunk {
    int allocated;
    Uint8 *abuf;
    Uint32 alen;
    Uint8  volume;
} Mix_Chunk;

typedef struct Mix_Music Mix_Music;

typedef void (*Mix_EffectFunc_t)(int chan, void *stream, int len, void *udata);
typedef void (*Mix_EffectDone_t)(int chan, void *udata);

int  Mix_OpenAudio(int freq, Uint16 fmt, int chans, int chunksize);
int  Mix_OpenAudioDevice(int freq, Uint16 fmt, int chans, int chunksize,
                         const char *dev, int allowed_changes);
void Mix_CloseAudio(void);
int  Mix_QuerySpec(int *freq, Uint16 *fmt, int *chans);
int  Mix_AllocateChannels(int n);
int  Mix_ReserveChannels(int n);
int  Mix_GroupChannels(int from, int to, int tag);
int  Mix_GroupAvailable(int tag);
int  Mix_GroupOldest(int tag);
int  Mix_GroupNewer(int tag);
int  Mix_GroupCount(int tag);
int  Mix_SetPanning(int chan, Uint8 left, Uint8 right);
int  Mix_SetDistance(int chan, Uint8 dist);
int  Mix_SetPosition(int chan, Sint16 angle, Uint8 dist);
int  Mix_SetReverseStereo(int chan, int flip);

Mix_Chunk *Mix_LoadWAV(const char *file);
Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc);
Mix_Chunk *Mix_QuickLoad_RAW(Uint8 *mem, Uint32 len);
Mix_Chunk *Mix_QuickLoad_WAV(Uint8 *mem);
void       Mix_FreeChunk(Mix_Chunk *c);

Mix_Music *Mix_LoadMUS(const char *file);
Mix_Music *Mix_LoadMUS_RW(SDL_RWops *rw, int freesrc);
void       Mix_FreeMusic(Mix_Music *m);

int  Mix_PlayChannel(int chan, Mix_Chunk *c, int loops);
int  Mix_PlayChannelTimed(int chan, Mix_Chunk *c, int loops, int ticks);
int  Mix_FadeInChannel(int chan, Mix_Chunk *c, int loops, int ms);
int  Mix_PlayMusic(Mix_Music *m, int loops);
int  Mix_FadeInMusic(Mix_Music *m, int loops, int ms);
int  Mix_HaltChannel(int chan);
int  Mix_HaltGroup(int tag);
int  Mix_HaltMusic(void);
int  Mix_FadeOutChannel(int chan, int ms);
int  Mix_FadeOutMusic(int ms);
int  Mix_Playing(int chan);
int  Mix_PlayingMusic(void);
int  Mix_Paused(int chan);
int  Mix_PausedMusic(void);
void Mix_Pause(int chan);
void Mix_Resume(int chan);
void Mix_PauseMusic(void);
void Mix_ResumeMusic(void);
void Mix_RewindMusic(void);

int  Mix_Volume(int chan, int volume);
int  Mix_VolumeChunk(Mix_Chunk *c, int volume);
int  Mix_VolumeMusic(int volume);

int  Mix_RegisterEffect(int chan, Mix_EffectFunc_t f, Mix_EffectDone_t d, void *u);
int  Mix_UnregisterEffect(int chan, Mix_EffectFunc_t f);

void Mix_HookMusic(void (*mix_func)(void*, Uint8*, int), void *arg);
void Mix_SetPostMix(void (*mix_func)(void*, Uint8*, int), void *arg);
void Mix_ChannelFinished(void (*cb)(int));
void Mix_HookMusicFinished(void (*cb)(void));

typedef enum {
    MIX_NO_FADING, MIX_FADING_OUT, MIX_FADING_IN
} Mix_Fading;
Mix_Fading Mix_FadingMusic(void);
Mix_Fading Mix_FadingChannel(int chan);

const char* Mix_GetError(void);

#define MIX_INIT_FLAC  0x01
#define MIX_INIT_MOD   0x02
#define MIX_INIT_MP3   0x08
#define MIX_INIT_OGG   0x10
#define MIX_INIT_MID   0x20
#define MIX_INIT_OPUS  0x40
int  Mix_Init(int flags);
void Mix_Quit(void);

#ifdef __cplusplus
}
#endif
