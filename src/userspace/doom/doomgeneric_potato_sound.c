/* doomgeneric_potato_sound.c — potatOS AC97 sound module for doomgeneric
 *
 * Implements DG_sound_module (8-channel software mixer → AC97 PCM output)
 * and DG_music_module (OPL3-emulated MUS playback via Nuked-OPL3).
 *
 * Audio pipeline:
 *   SFX: WAD lump (DMX 8-bit unsigned mono @ 11025 Hz)
 *        → per-channel volume/pan → mix to stereo 16-bit @ 11025 Hz
 *   Music: MUS lump → OPL3 emulator → stereo 16-bit @ 11025 Hz
 *   Both mixed into g_mix[] → SYS_AUDIO_WRITE → kernel AC97::play_pcm()
 *
 * Timing: Update() is called every game tic (35 Hz ≈ 28.57 ms).
 *   SAMPLES_PER_TIC = 11025/35 = 315 samples/tic.
 *   We accumulate SUBMIT_TICS tics before each AC97 submission,
 *   but also submit immediately when AC97 goes idle.
 */

#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "deh_str.h"

#include "libc/syscall.h"
#include "mus_opl.h"

/* ── constants ───────────────────────────────────────────────────────────── */

#define SFX_RATE          11025          /* Hz — native DMX sample rate      */
#define SAMPLES_PER_TIC   315            /* 11025 / 35                        */
#define SUBMIT_TICS       8              /* tics per AC97 submission (~228ms) */
#define SUBMIT_THRESHOLD  (SAMPLES_PER_TIC * SUBMIT_TICS)  /* 2520 frames    */
/* Double the submit threshold for the accumulation buffer size              */
#define MIX_SAMPLES       (SUBMIT_THRESHOLD * 2)           /* 5040 frames    */
#define NUM_CHANNELS      8

/* ── DMX format helpers ──────────────────────────────────────────────────── */
/* DMX header layout (little-endian):
 *   bytes 0-1: format ID (0x0003 = sound effect)
 *   bytes 2-3: sample rate
 *   bytes 4-7: sample count (uint32)
 *   bytes 8+ : 8-bit unsigned PCM samples
 */
static int dmx_samplecount(const unsigned char *d)
{
    return (int)d[4] | ((int)d[5] << 8) | ((int)d[6] << 16) | ((int)d[7] << 24);
}

/* ── channel state ───────────────────────────────────────────────────────── */

typedef struct {
    boolean         active;
    const unsigned char *data;   /* WAD lump data offset by DMX header (8 bytes) */
    int             num_samples;
    int             pos;
    int             lvol;        /* left  volume 0-127 */
    int             rvol;        /* right volume 0-127 */
} SfxChan;

static SfxChan g_ch[NUM_CHANNELS];
static boolean g_sound_ok       = false;
static boolean g_use_sfx_prefix = false;

/* Stereo interleaved 16-bit mix buffer [L, R, L, R, ...] */
static short   g_mix[MIX_SAMPLES * 2];
static int     g_mix_fill = 0;   /* stereo frames filled */

/* ── internal helpers ────────────────────────────────────────────────────── */

static void set_chan_vol(int ch, int vol, int sep)
{
    /* sep: 0 = full-left, 128 = centre, 255 = full-right */
    g_ch[ch].lvol = vol * (255 - sep) / 256;
    g_ch[ch].rvol = vol * sep         / 256;
}

/* Mix n stereo frames into g_mix starting at frame index base. */
static void mix_slice(int base, int n)
{
    int i, c;
    for (i = 0; i < n; i++) {
        int L = 0, R = 0;
        for (c = 0; c < NUM_CHANNELS; c++) {
            SfxChan *ch = &g_ch[c];
            if (!ch->active) continue;
            if (ch->pos >= ch->num_samples) {
                ch->active = false;
                continue;
            }
            /* Convert 8-bit unsigned to 16-bit signed, scale by volume */
            int s = ((int)ch->data[ch->pos++] - 128) << 8;  /* -32768..32512 */
            L += s * ch->lvol / 127;
            R += s * ch->rvol / 127;
        }
        /* Clamp to int16 range */
        if (L >  32767) L =  32767; else if (L < -32768) L = -32768;
        if (R >  32767) R =  32767; else if (R < -32768) R = -32768;
        g_mix[(base + i) * 2 + 0] = (short)L;
        g_mix[(base + i) * 2 + 1] = (short)R;
    }
}

/* Submit the filled portion of g_mix to AC97 (if idle and non-empty). */
static void try_submit(void)
{
    if (g_mix_fill <= 0) return;
    if (sys_audio_is_playing() != 0) return;  /* busy or absent */
    /* bytes = frames * 2 channels * 2 bytes/sample */
    long r = sys_audio_write(g_mix,
                             (unsigned long)(g_mix_fill * 4),
                             (unsigned int)SFX_RATE);
    if (r > 0) g_mix_fill = 0;
    else if (g_mix_fill >= MIX_SAMPLES) g_mix_fill = 0;  /* full — drop oldest */
}

/* ── sound_module_t callbacks ────────────────────────────────────────────── */

static boolean Sfx_Init(boolean use_sfx_prefix)
{
    int i;
    if (sys_audio_is_playing() < 0) return false;  /* no AC97 */
    g_use_sfx_prefix = use_sfx_prefix;
    for (i = 0; i < NUM_CHANNELS; i++) g_ch[i].active = false;
    g_mix_fill = 0;
    g_sound_ok = true;
    return true;
}

static void Sfx_Shutdown(void)
{
    g_sound_ok = false;
}

static int Sfx_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[9];
    int i;
    if (g_use_sfx_prefix) {
        name[0] = 'd'; name[1] = 's';
        for (i = 0; i < 6 && sfx->name[i]; i++) name[2 + i] = sfx->name[i];
        name[2 + i] = '\0';
    } else {
        for (i = 0; i < 8 && sfx->name[i]; i++) name[i] = sfx->name[i];
        name[i] = '\0';
    }
    return W_CheckNumForName(name);
}

static void Sfx_Update(void)
{
    int n, i;
    if (!g_sound_ok) return;

    /* Mix one tic's worth of audio into the accumulation buffer */
    n = SAMPLES_PER_TIC;
    if (g_mix_fill + n > MIX_SAMPLES) n = MIX_SAMPLES - g_mix_fill;
    if (n > 0) {
        mix_slice(g_mix_fill, n);

        /* Render OPL music and add to SFX mix */
        if (mus_opl_is_playing()) {
            short mus_buf[SAMPLES_PER_TIC * 2];
            mus_opl_render(mus_buf, n);
            for (i = 0; i < n * 2; i++) {
                int sum = (int)g_mix[g_mix_fill * 2 + i] + (int)mus_buf[i];
                if (sum >  32767) sum =  32767;
                if (sum < -32768) sum = -32768;
                g_mix[g_mix_fill * 2 + i] = (short)sum;
            }
        }

        g_mix_fill += n;
    }

    /* Submit to hardware: when threshold reached, or when AC97 is idle */
    if (g_mix_fill >= SUBMIT_THRESHOLD)
        try_submit();
    else
        try_submit();  /* also submit early if AC97 went idle between tics */
}

static void Sfx_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel < 0 || channel >= NUM_CHANNELS) return;
    set_chan_vol(channel, vol, sep);
}

static int Sfx_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    const unsigned char *raw;
    int n;
    if (!g_sound_ok) return -1;
    if (channel < 0 || channel >= NUM_CHANNELS) channel = 0;
    if (sfx->lumpnum < 0) return -1;

    raw = (const unsigned char *)W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    if (!raw) return -1;

    n = dmx_samplecount(raw);
    if (n <= 0) return -1;

    g_ch[channel].active      = true;
    g_ch[channel].data        = raw + 8;  /* skip 8-byte DMX header */
    g_ch[channel].num_samples = n;
    g_ch[channel].pos         = 0;
    set_chan_vol(channel, vol, sep);
    return channel;
}

static void Sfx_StopSound(int channel)
{
    if (channel >= 0 && channel < NUM_CHANNELS)
        g_ch[channel].active = false;
}

static boolean Sfx_SoundIsPlaying(int channel)
{
    if (channel < 0 || channel >= NUM_CHANNELS) return false;
    return g_ch[channel].active;
}

static void Sfx_CacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds; (void)num_sounds;  /* lazy loading — no pre-cache needed */
}

/* ── music_module_t callbacks (OPL3-emulated MUS playback) ───────────────── */

static boolean g_music_paused = false;

static boolean Mus_Init(void)
{
    int lump = W_CheckNumForName("GENMIDI");
    if (lump < 0) return false;
    void *data = W_CacheLumpNum(lump, PU_STATIC);
    int len = W_LumpLength(lump);
    return mus_opl_init(data, len) ? true : false;
}

static void Mus_Shutdown(void)
{
    mus_opl_stop();
}

static void Mus_SetVolume(int v)
{
    mus_opl_set_volume(v);
}

static void Mus_Pause(void)
{
    g_music_paused = true;
    mus_opl_stop();
}

static void Mus_Resume(void)
{
    g_music_paused = false;
    /* Music will be restarted by Doom calling Play again if needed */
}

static void *Mus_Register(void *d, int l)
{
    return mus_opl_register(d, l);
}

static void Mus_Unregister(void *h)
{
    (void)h;
    /* MUS data is WAD-cached, nothing to free */
}

static void Mus_Play(void *h, boolean looping)
{
    (void)h;
    g_music_paused = false;
    mus_opl_play(looping ? 1 : 0);
}

static void Mus_Stop(void)
{
    mus_opl_stop();
}

static boolean Mus_IsPlaying(void)
{
    return mus_opl_is_playing() && !g_music_paused ? true : false;
}

static void Mus_Poll(void)
{
    /* Music rendering happens in Sfx_Update via mus_opl_render */
}

/* ── globals expected by i_sound.c (normally in i_sdlsound.c) ───────────── */
/* i_sound.c declares these extern and binds them to config variables when
 * FEATURE_SOUND is defined.  We provide zero-valued stubs so the linker is
 * satisfied; our mixer ignores resampling entirely. */
int   use_libsamplerate   = 0;
float libsamplerate_scale = 0.65f;

/* ── module definitions ──────────────────────────────────────────────────── */

static snddevice_t sfx_devices[] = { SNDDEVICE_SB };
static snddevice_t mus_devices[] = { SNDDEVICE_SB };

sound_module_t DG_sound_module = {
    sfx_devices,
    1,
    Sfx_Init,
    Sfx_Shutdown,
    Sfx_GetSfxLumpNum,
    Sfx_Update,
    Sfx_UpdateSoundParams,
    Sfx_StartSound,
    Sfx_StopSound,
    Sfx_SoundIsPlaying,
    Sfx_CacheSounds,
};

music_module_t DG_music_module = {
    mus_devices,
    1,
    Mus_Init,
    Mus_Shutdown,
    Mus_SetVolume,
    Mus_Pause,
    Mus_Resume,
    Mus_Register,
    Mus_Unregister,
    Mus_Play,
    Mus_Stop,
    Mus_IsPlaying,
    Mus_Poll,
};
