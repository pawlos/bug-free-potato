/* mus_opl.h — MUS music playback via OPL3 emulation for potatOS Doom */

#ifndef MUS_OPL_H
#define MUS_OPL_H

#include <stdint.h>

/* Initialize OPL3 and load GENMIDI instrument data.
 * genmidi_data: raw GENMIDI lump from WAD (starts with "#OPL_II#")
 * Returns 1 on success, 0 on failure. */
int mus_opl_init(const void *genmidi_data, int genmidi_len);

/* Register a MUS lump for playback. Returns opaque handle or NULL. */
void *mus_opl_register(const void *data, int len);

/* Start playing the registered MUS data. */
void mus_opl_play(int looping);

/* Stop playback, silence all voices. */
void mus_opl_stop(void);

/* Set music volume (0-127). */
void mus_opl_set_volume(int vol);

/* Returns 1 if music is currently playing. */
int mus_opl_is_playing(void);

/* Render `frames` stereo 16-bit PCM frames into buf (frames * 2 int16_t).
 * Advances the MUS sequencer and OPL emulation. */
void mus_opl_render(int16_t *buf, int frames);

#endif
