/* mus_opl.c — MUS music playback via OPL3 emulation for potatOS Doom
 *
 * Parses MUS format from Doom WAD lumps, drives Nuked-OPL3 emulator,
 * renders stereo 16-bit PCM at 11025 Hz for mixing with SFX.
 *
 * Uses 9 melodic OPL voices (OPL2 compatible subset).
 * Percussion on MUS channel 15 uses GENMIDI instruments 128-174.
 */

#include "mus_opl.h"
#include "opl3.h"
#include <string.h>
#include <stdint.h>

/* ── MUS format ──────────────────────────────────────────────── */

typedef struct {
    char     id[4];            /* "MUS\x1A" */
    uint16_t score_len;
    uint16_t score_ofs;
    uint16_t pri_channels;
    uint16_t sec_channels;
    uint16_t num_instr;
    uint16_t reserved;
    /* uint16_t instruments[num_instr] follows */
} mus_header_t;

#define MUS_EV_RELEASE   0
#define MUS_EV_PLAY      1
#define MUS_EV_PITCH     2
#define MUS_EV_SYSTEM    3
#define MUS_EV_CTRL      4
#define MUS_EV_MEASURE   5
#define MUS_EV_FINISH    6

/* ── GENMIDI format ──────────────────────────────────────────── */

#define GENMIDI_HEADER     "#OPL_II#"
#define GENMIDI_NUM_INSTR  128
#define GENMIDI_NUM_PERC   47
#define GENMIDI_FLAG_FIXED  0x0001
#define GENMIDI_FLAG_DOUBLE 0x0002

typedef struct __attribute__((packed)) {
    uint8_t tremolo;    /* reg 0x20: AM/VIB/EG/KSR/MULT */
    uint8_t attack;     /* reg 0x60: Attack/Decay */
    uint8_t sustain;    /* reg 0x80: Sustain/Release */
    uint8_t waveform;   /* reg 0xE0: Waveform select */
    uint8_t scale;      /* reg 0x40: KSL (bits 7-6) */
    uint8_t level;      /* reg 0x40: Output level (bits 5-0) */
} genmidi_op_t;

typedef struct __attribute__((packed)) {
    genmidi_op_t modulator;
    uint8_t      feedback;   /* reg 0xC0: Feedback/Connection */
    genmidi_op_t carrier;
    uint8_t      unused;
    int16_t      base_note_offset;
} genmidi_voice_t;

typedef struct __attribute__((packed)) {
    uint16_t       flags;
    uint8_t        fine_tuning;
    uint8_t        fixed_note;
    genmidi_voice_t voices[2];
} genmidi_instr_t;

/* ── OPL constants ───────────────────────────────────────────── */

#define OPL_NUM_VOICES  9
#define OPL_RATE        11025

#define OPL_REG_TREMOLO    0x20
#define OPL_REG_LEVEL      0x40
#define OPL_REG_ATTACK     0x60
#define OPL_REG_SUSTAIN    0x80
#define OPL_REG_FNUM_LO    0xA0
#define OPL_REG_KEYON      0xB0
#define OPL_REG_FEEDBACK   0xC0
#define OPL_REG_WAVEFORM   0xE0

/* Operator offsets for 9-voice mode (modulator and carrier per voice) */
static const uint8_t op_mod[OPL_NUM_VOICES] = {
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12
};
static const uint8_t op_car[OPL_NUM_VOICES] = {
    0x03, 0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x13, 0x14, 0x15
};

/* F-numbers for semitones C-B, calibrated for OPL block 4 (MIDI octave 5).
 * freq = fnum * 49716 / 2^(20-block)
 * block = (note/12) - 1, so MIDI octave 5 → block 4. */
static const uint16_t base_fnum[12] = {
    345, 365, 387, 410, 434, 460, 487, 516, 547, 580, 614, 651
};

/* ── Voice state ─────────────────────────────────────────────── */

typedef struct {
    int          active;
    int          channel;     /* MUS channel (0-15) */
    int          note;        /* original MIDI note for matching */
    int          priority;
    unsigned int age;
    uint16_t     fnum;        /* shadow of current OPL F-number */
    uint8_t      block;       /* shadow of current OPL block */
} opl_voice_t;

/* ── Channel state ───────────────────────────────────────────── */

#define MUS_MAX_CHANNELS 16

typedef struct {
    int program;      /* MIDI instrument number (0-127) */
    int volume;       /* 0-127 */
    int pan;          /* 0-127 (64=center) */
    int pitch_bend;   /* 0-255 (128=center) */
    int expression;   /* 0-127 */
} mus_channel_t;

/* ── Global state ────────────────────────────────────────────── */

static opl3_chip     g_opl;
static opl_voice_t   g_voices[OPL_NUM_VOICES];
static mus_channel_t g_channels[MUS_MAX_CHANNELS];
static unsigned int  g_voice_age;

static const genmidi_instr_t *g_main_instr;   /* 128 melodic instruments */
static const genmidi_instr_t *g_perc_instr;   /* 47 percussion instruments */

static const uint8_t *g_mus_data;
static int            g_mus_len;
static const uint8_t *g_mus_ptr;
static const uint8_t *g_mus_end;
static int            g_mus_looping;
static int            g_mus_playing;
static int            g_mus_delay;            /* remaining MUS ticks (1/140 sec) */
static int            g_samples_until_tick;   /* samples until next MUS tick */

static int            g_music_volume = 100;
static int            g_inited;

/* ── OPL helpers ─────────────────────────────────────────────── */

static void opl_write(uint16_t reg, uint8_t val)
{
    OPL3_WriteReg(&g_opl, reg, val);
}

static void set_voice_instrument(int v, const genmidi_voice_t *data)
{
    uint8_t m = op_mod[v];
    uint8_t c = op_car[v];

    opl_write(OPL_REG_TREMOLO  + m, data->modulator.tremolo);
    opl_write(OPL_REG_ATTACK   + m, data->modulator.attack);
    opl_write(OPL_REG_SUSTAIN  + m, data->modulator.sustain);
    opl_write(OPL_REG_WAVEFORM + m, data->modulator.waveform);

    opl_write(OPL_REG_TREMOLO  + c, data->carrier.tremolo);
    opl_write(OPL_REG_ATTACK   + c, data->carrier.attack);
    opl_write(OPL_REG_SUSTAIN  + c, data->carrier.sustain);
    opl_write(OPL_REG_WAVEFORM + c, data->carrier.waveform);

    /* Feedback/connection + output both L+R (bits 4,5 = 0x30) */
    opl_write(OPL_REG_FEEDBACK + v, data->feedback | 0x30);
}

static void set_voice_volume(int v, const genmidi_voice_t *data, int volume)
{
    uint8_t m = op_mod[v];
    uint8_t c = op_car[v];
    int modulating = (data->feedback & 0x01) == 0;  /* FM: modulator doesn't output */

    /* Scale carrier level by volume: 0x3F = max attenuation, 0 = min */
    int car_level = data->carrier.level & 0x3F;
    car_level = 0x3F - ((0x3F - car_level) * volume / 127);
    opl_write(OPL_REG_LEVEL + c, (data->carrier.scale & 0xC0) | (car_level & 0x3F));

    if (!modulating) {
        /* Additive mode: scale modulator volume too */
        int mod_level = data->modulator.level & 0x3F;
        mod_level = 0x3F - ((0x3F - mod_level) * volume / 127);
        opl_write(OPL_REG_LEVEL + m, (data->modulator.scale & 0xC0) | (mod_level & 0x3F));
    } else {
        opl_write(OPL_REG_LEVEL + m, (data->modulator.scale & 0xC0) | (data->modulator.level & 0x3F));
    }
}

static void note_to_freq(int note, int pitch_bend, uint16_t *fnum_out, uint8_t *block_out)
{
    /* pitch_bend: 0-255, 128=center, ±2 semitones range.
     * Map to fixed-point note: note * 256 + bend offset. */
    int note_x256 = note * 256 + (pitch_bend - 128) * 4;
    if (note_x256 < 0) note_x256 = 0;
    if (note_x256 > 127 * 256) note_x256 = 127 * 256;

    int whole_note = note_x256 / 256;
    int frac       = note_x256 & 255;

    int semitone = whole_note % 12;
    int octave   = whole_note / 12;
    int block    = octave - 1;

    uint16_t fnum = base_fnum[semitone];

    /* Linear interpolation for pitch bend smoothness */
    if (frac > 0 && whole_note < 127) {
        int next_semi = (semitone + 1) % 12;
        uint16_t next_fnum = base_fnum[next_semi];
        if (next_semi == 0)
            next_fnum = base_fnum[0] * 2;  /* octave wrap: double freq */
        fnum = fnum + (uint16_t)((int)(next_fnum - fnum) * frac / 256);
    }

    if (block < 0) {
        fnum >>= (-block);
        block = 0;
    } else if (block > 7) {
        block = 7;
    }

    *fnum_out  = fnum;
    *block_out = (uint8_t)block;
}

static void voice_key_on(int v, int note, int pitch_bend)
{
    uint16_t fnum;
    uint8_t  block;
    note_to_freq(note, pitch_bend, &fnum, &block);

    g_voices[v].fnum  = fnum;
    g_voices[v].block = block;

    opl_write(OPL_REG_FNUM_LO + v, fnum & 0xFF);
    opl_write(OPL_REG_KEYON   + v, 0x20 | (block << 2) | ((fnum >> 8) & 0x03));
}

static void voice_key_off(int v)
{
    /* Clear key-on bit (bit 5) but keep frequency for clean release */
    opl_write(OPL_REG_KEYON + v,
              (g_voices[v].block << 2) | ((g_voices[v].fnum >> 8) & 0x03));
}

/* ── Voice allocation ────────────────────────────────────────── */

static int find_voice(int channel, int note)
{
    for (int i = 0; i < OPL_NUM_VOICES; i++) {
        if (g_voices[i].active && g_voices[i].channel == channel
            && g_voices[i].note == note)
            return i;
    }
    return -1;
}

static int alloc_voice(void)
{
    /* Prefer inactive voice */
    for (int i = 0; i < OPL_NUM_VOICES; i++) {
        if (!g_voices[i].active)
            return i;
    }
    /* Steal lowest-priority, oldest voice */
    int best = 0;
    for (int i = 1; i < OPL_NUM_VOICES; i++) {
        if (g_voices[i].priority < g_voices[best].priority ||
            (g_voices[i].priority == g_voices[best].priority &&
             g_voices[i].age < g_voices[best].age))
            best = i;
    }
    voice_key_off(best);
    g_voices[best].active = 0;
    return best;
}

/* ── Volume ──────────────────────────────────────────────────── */

static int effective_volume(int channel)
{
    mus_channel_t *ch = &g_channels[channel];
    return ch->volume * ch->expression * g_music_volume / (127 * 127);
}

/* ── Instrument lookup ───────────────────────────────────────── */

static const genmidi_instr_t *get_instrument(int channel, int note)
{
    if (channel == 15) {
        int idx = note - 35;
        if (idx < 0 || idx >= GENMIDI_NUM_PERC) return 0;
        return &g_perc_instr[idx];
    }
    int prog = g_channels[channel].program;
    if (prog < 0 || prog >= GENMIDI_NUM_INSTR) return 0;
    return &g_main_instr[prog];
}

/* ── MUS event handlers ──────────────────────────────────────── */

static void mus_note_on(int channel, int note, int volume)
{
    const genmidi_instr_t *instr = get_instrument(channel, note);
    if (!instr) return;

    if (volume >= 0)
        g_channels[channel].volume = volume;

    int play_note = note;
    if (instr->flags & GENMIDI_FLAG_FIXED)
        play_note = instr->fixed_note;
    play_note += instr->voices[0].base_note_offset;
    if (play_note < 0)   play_note = 0;
    if (play_note > 127) play_note = 127;

    int v = alloc_voice();

    set_voice_instrument(v, &instr->voices[0]);
    set_voice_volume(v, &instr->voices[0], effective_volume(channel));
    voice_key_on(v, play_note, g_channels[channel].pitch_bend);

    g_voices[v].active   = 1;
    g_voices[v].channel  = channel;
    g_voices[v].note     = note;
    g_voices[v].priority = effective_volume(channel);
    g_voices[v].age      = ++g_voice_age;
}

static void mus_note_off(int channel, int note)
{
    int v = find_voice(channel, note);
    if (v >= 0) {
        voice_key_off(v);
        g_voices[v].active = 0;
    }
}

static void mus_pitch_bend(int channel, int bend)
{
    g_channels[channel].pitch_bend = bend;

    for (int i = 0; i < OPL_NUM_VOICES; i++) {
        if (!g_voices[i].active || g_voices[i].channel != channel)
            continue;
        const genmidi_instr_t *instr = get_instrument(channel, g_voices[i].note);
        if (!instr) continue;

        int play_note = g_voices[i].note;
        if (instr->flags & GENMIDI_FLAG_FIXED)
            play_note = instr->fixed_note;
        play_note += instr->voices[0].base_note_offset;
        if (play_note < 0)   play_note = 0;
        if (play_note > 127) play_note = 127;

        voice_key_on(i, play_note, bend);
    }
}

static void mus_controller(int channel, int ctrl, int value)
{
    mus_channel_t *ch = &g_channels[channel];

    switch (ctrl) {
    case 0:  /* Program change */
        ch->program = value;
        break;
    case 3:  /* Volume */
        ch->volume = value;
        for (int i = 0; i < OPL_NUM_VOICES; i++) {
            if (g_voices[i].active && g_voices[i].channel == channel) {
                const genmidi_instr_t *instr = get_instrument(channel, g_voices[i].note);
                if (instr)
                    set_voice_volume(i, &instr->voices[0], effective_volume(channel));
            }
        }
        break;
    case 4:  /* Pan */
        ch->pan = value;
        break;
    case 5:  /* Expression */
        ch->expression = value;
        break;
    default:
        break;
    }
}

/* ── MUS event decoder ───────────────────────────────────────── */

static void process_mus_event(void)
{
    if (g_mus_ptr >= g_mus_end) {
        if (g_mus_looping) {
            const mus_header_t *hdr = (const mus_header_t *)g_mus_data;
            g_mus_ptr = g_mus_data + hdr->score_ofs;
        } else {
            g_mus_playing = 0;
        }
        return;
    }

    uint8_t desc     = *g_mus_ptr++;
    int     last     = (desc >> 7) & 1;
    int     ev_type  = (desc >> 4) & 7;
    int     channel  = desc & 0x0F;

    switch (ev_type) {
    case MUS_EV_RELEASE: {
        if (g_mus_ptr >= g_mus_end) break;
        int note = *g_mus_ptr++ & 0x7F;
        mus_note_off(channel, note);
        break;
    }
    case MUS_EV_PLAY: {
        if (g_mus_ptr >= g_mus_end) break;
        uint8_t b = *g_mus_ptr++;
        int note = b & 0x7F;
        int vol  = -1;
        if (b & 0x80) {
            if (g_mus_ptr >= g_mus_end) break;
            vol = *g_mus_ptr++ & 0x7F;
        }
        mus_note_on(channel, note, vol);
        break;
    }
    case MUS_EV_PITCH: {
        if (g_mus_ptr >= g_mus_end) break;
        int bend = *g_mus_ptr++;
        mus_pitch_bend(channel, bend);
        break;
    }
    case MUS_EV_SYSTEM: {
        if (g_mus_ptr >= g_mus_end) break;
        g_mus_ptr++;  /* system event value — ignore */
        break;
    }
    case MUS_EV_CTRL: {
        if (g_mus_ptr + 1 >= g_mus_end) break;
        int ctrl  = *g_mus_ptr++ & 0x7F;
        int value = *g_mus_ptr++ & 0x7F;
        mus_controller(channel, ctrl, value);
        break;
    }
    case MUS_EV_MEASURE:
        break;
    case MUS_EV_FINISH:
        if (g_mus_looping) {
            const mus_header_t *hdr = (const mus_header_t *)g_mus_data;
            g_mus_ptr = g_mus_data + hdr->score_ofs;
        } else {
            g_mus_playing = 0;
        }
        return;  /* don't process delay on finish */
    default:
        break;
    }

    /* Read variable-length delay if "last" flag is set */
    if (last) {
        int delay = 0;
        uint8_t b;
        do {
            if (g_mus_ptr >= g_mus_end) break;
            b = *g_mus_ptr++;
            delay = delay * 128 + (b & 0x7F);
        } while (b & 0x80);
        g_mus_delay += delay;
    }
}

/* ── Public API ──────────────────────────────────────────────── */

int mus_opl_init(const void *genmidi_data, int genmidi_len)
{
    int min_size = 8 + (GENMIDI_NUM_INSTR + GENMIDI_NUM_PERC) * (int)sizeof(genmidi_instr_t);
    if (genmidi_len < min_size)
        return 0;

    const uint8_t *p = (const uint8_t *)genmidi_data;
    if (memcmp(p, GENMIDI_HEADER, 8) != 0)
        return 0;

    g_main_instr = (const genmidi_instr_t *)(p + 8);
    g_perc_instr = g_main_instr + GENMIDI_NUM_INSTR;

    OPL3_Reset(&g_opl, OPL_RATE);

    /* Enable waveform select (allows non-sine waveforms) */
    opl_write(0x01, 0x20);

    memset(g_voices, 0, sizeof(g_voices));
    g_voice_age   = 0;
    g_mus_playing = 0;
    g_mus_data    = 0;
    g_inited      = 1;
    return 1;
}

void *mus_opl_register(const void *data, int len)
{
    if (!g_inited || !data || len < (int)sizeof(mus_header_t))
        return 0;

    const mus_header_t *hdr = (const mus_header_t *)data;
    if (hdr->id[0] != 'M' || hdr->id[1] != 'U' ||
        hdr->id[2] != 'S' || hdr->id[3] != 0x1A)
        return 0;

    g_mus_data = (const uint8_t *)data;
    g_mus_len  = len;
    return (void *)data;
}

void mus_opl_play(int looping)
{
    if (!g_inited || !g_mus_data) return;

    const mus_header_t *hdr = (const mus_header_t *)g_mus_data;
    g_mus_ptr     = g_mus_data + hdr->score_ofs;
    g_mus_end     = g_mus_data + g_mus_len;
    g_mus_looping = looping;
    g_mus_playing = 1;
    g_mus_delay   = 0;
    g_samples_until_tick = 0;

    for (int i = 0; i < MUS_MAX_CHANNELS; i++) {
        g_channels[i].program    = 0;
        g_channels[i].volume     = 100;
        g_channels[i].pan        = 64;
        g_channels[i].pitch_bend = 128;
        g_channels[i].expression = 127;
    }

    /* Reset OPL chip for clean start */
    OPL3_Reset(&g_opl, OPL_RATE);
    opl_write(0x01, 0x20);

    for (int i = 0; i < OPL_NUM_VOICES; i++) {
        g_voices[i].active = 0;
        g_voices[i].fnum   = 0;
        g_voices[i].block  = 0;
    }
    g_voice_age = 0;
}

void mus_opl_stop(void)
{
    g_mus_playing = 0;
    for (int i = 0; i < OPL_NUM_VOICES; i++) {
        if (g_voices[i].active) {
            voice_key_off(i);
            g_voices[i].active = 0;
        }
    }
}

void mus_opl_set_volume(int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;
    g_music_volume = vol;
}

int mus_opl_is_playing(void)
{
    return g_mus_playing;
}

void mus_opl_render(int16_t *buf, int frames)
{
    if (!g_mus_playing) {
        memset(buf, 0, frames * 4);
        return;
    }

    int pos = 0;
    while (pos < frames && g_mus_playing) {
        /* Process events whose time has arrived */
        if (g_samples_until_tick <= 0) {
            /* Consume events until we get a delay or end */
            while (g_mus_playing && g_mus_delay <= 0)
                process_mus_event();

            if (g_mus_delay > 0) {
                /* Convert MUS ticks to samples: ticks * 11025 / 140 */
                g_samples_until_tick = g_mus_delay * OPL_RATE / 140;
                if (g_samples_until_tick <= 0) g_samples_until_tick = 1;
                g_mus_delay = 0;
            }
        }

        if (!g_mus_playing) {
            memset(buf + pos * 2, 0, (frames - pos) * 4);
            break;
        }

        /* Render OPL samples until next tick or end of buffer */
        int chunk = g_samples_until_tick;
        if (chunk <= 0) chunk = 1;
        if (chunk > frames - pos) chunk = frames - pos;

        OPL3_GenerateStream(&g_opl, buf + pos * 2, chunk);
        pos += chunk;
        g_samples_until_tick -= chunk;
    }
}
