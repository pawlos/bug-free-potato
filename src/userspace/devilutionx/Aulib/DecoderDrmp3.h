// MP3 decoder for Aulib shim — stubbed (silent) until dr_mp3.h is vendored.
#pragma once

#include <cstdint>
#include <chrono>

#include <Aulib/Decoder.h>

namespace Aulib {

class DecoderDrmp3 final : public Decoder {
public:
    DecoderDrmp3() = default;
    ~DecoderDrmp3() override = default;

    bool open(SDL_RWops* /*rwops*/) override { return true; }
    int  getRate() const override { return 22050; }
    int  getChannels() const override { return 2; }
    int  decodeStereoS16(int16_t* /*dst*/, int /*frames*/) override { return 0; }
    bool rewind() override { return true; }
    std::chrono::microseconds duration() const override {
        return std::chrono::microseconds{0};
    }
};

} // namespace Aulib
