/* music_potato.c -- potatOS music stubs for Duke3D
 *
 * Replaces Game/src/midi/sdl_midi.c. No MIDI playback on potatOS (yet).
 */

#include "audiolib/music.h"
#include <stdint.h>

char *MUSIC_ErrorString(int ErrorNumber)
{
    (void)ErrorNumber;
    return "No music on potatOS";
}

int MUSIC_Init(int SoundCard, int Address)
{
    (void)SoundCard; (void)Address;
    return MUSIC_Ok;
}

int MUSIC_Shutdown(void)
{
    return MUSIC_Ok;
}

void MUSIC_SetMaxFMMidiChannel(int channel) { (void)channel; }
void MUSIC_SetVolume(int volume) { (void)volume; }
void MUSIC_SetMidiChannelVolume(int channel, int volume) { (void)channel; (void)volume; }
void MUSIC_ResetMidiChannelVolumes(void) { }
int MUSIC_GetVolume(void) { return 0; }
void MUSIC_SetLoopFlag(int loopflag) { (void)loopflag; }
int MUSIC_SongPlaying(void) { return 0; }
void MUSIC_Continue(void) { }
void MUSIC_Pause(void) { }

int MUSIC_StopSong(void)
{
    return MUSIC_Ok;
}

int MUSIC_PlaySong(char *songFilename, int loopflag)
{
    (void)songFilename; (void)loopflag;
    return MUSIC_Ok;
}

void MUSIC_SetContext(int context) { (void)context; }
int MUSIC_GetContext(void) { return 0; }
void MUSIC_SetSongTick(uint32_t PositionInTicks) { (void)PositionInTicks; }
void MUSIC_SetSongTime(uint32_t milliseconds) { (void)milliseconds; }
void MUSIC_SetSongPosition(int measure, int beat, int tick) { (void)measure; (void)beat; (void)tick; }
void MUSIC_GetSongPosition(songposition *pos) { (void)pos; }
void MUSIC_GetSongLength(songposition *pos) { (void)pos; }
int MUSIC_FadeVolume(int tovolume, int milliseconds) { (void)tovolume; (void)milliseconds; return MUSIC_Ok; }
int MUSIC_FadeActive(void) { return 0; }
void MUSIC_StopFade(void) { }
void MUSIC_RerouteMidiChannel(int channel, int (*function)(int, int, int)) { (void)channel; (void)function; }
void MUSIC_RegisterTimbreBank(uint8_t *timbres) { (void)timbres; }

void PlayMusic(char *filename)
{
    (void)filename;
}
