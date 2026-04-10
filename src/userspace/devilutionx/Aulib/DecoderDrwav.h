// WAV decoder for Aulib shim. Decodes PCM 8/16-bit mono/stereo WAV
// files, resampling on the fly to the active Aulib output rate.
#pragma once

#include <cstdint>
#include <chrono>
#include <vector>

#include <Aulib/Decoder.h>

namespace Aulib {

class DecoderDrwav final : public Decoder {
public:
    DecoderDrwav() = default;
    ~DecoderDrwav() override = default;

    bool open(SDL_RWops* rwops) override;
    int  getRate() const override { return native_rate_; }
    int  getChannels() const override { return native_channels_; }
    int  decodeStereoS16(int16_t* dst, int frames) override;
    bool rewind() override;
    std::chrono::microseconds duration() const override;

private:
    std::vector<int16_t> samples_;   // interleaved PCM in native channel order
    int   native_rate_      = 22050;
    int   native_channels_  = 1;
    int   total_frames_     = 0;     // number of frames in `samples_`
    int   out_phase_        = 0;     // output frame counter (post-resample)
};

} // namespace Aulib
