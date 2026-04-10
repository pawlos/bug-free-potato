// Base class for all Aulib audio decoders.
#pragma once

#include <chrono>
#include <cstdint>

struct SDL_RWops;
typedef struct SDL_RWops SDL_IOStream;  // alias for sdl_compat.h

namespace Aulib {

class Decoder {
public:
    virtual ~Decoder() = default;

    // Parse the container header and prepare for decoding.
    virtual bool open(SDL_RWops* rwops) = 0;

    // Native sample rate from the file.
    virtual int getRate() const = 0;

    // Channel count from the file (1 = mono, 2 = stereo).
    virtual int getChannels() const = 0;

    // Decode up to `frames` stereo frames at the output sample rate,
    // writing interleaved int16_t L/R samples into `dst`.
    // Returns the number of frames actually written.
    // Zero return means end-of-stream.
    virtual int decodeStereoS16(int16_t* dst, int frames) = 0;

    // Reset playback to the start of the stream.
    virtual bool rewind() = 0;

    // Total length of the stream.
    virtual std::chrono::microseconds duration() const = 0;
};

} // namespace Aulib
