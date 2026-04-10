// Aulib shim for potatOS — minimal replacement for SDL_audiolib that
// routes decoded audio through the kernel's double-buffered AC97 driver.
#pragma once

#include <string>
#include <cstdint>

#include <SDL.h>  // for SDL_AudioFormat

namespace Aulib {

// Initialize the audio subsystem. Returns true on success.
// `format` is an SDL_AudioFormat (we only honor AUDIO_S16SYS).
// `device` is ignored — potatOS has one audio device.
bool init(int freq, SDL_AudioFormat format, int channels, int frameSize,
          const std::string& device = std::string{});

// Tear down the audio subsystem and release the kernel device.
void quit();

// Query the active output configuration.
int sampleRate();
int channelCount();
int frameSize();
SDL_AudioFormat sampleFormat();

// Drive the software mixer — decodes one chunk from every active Stream,
// mixes them, and submits the result to the kernel. Safe to call repeatedly.
void process();

} // namespace Aulib
