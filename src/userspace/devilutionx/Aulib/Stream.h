// Aulib::Stream shim — owns a Decoder + Resampler and is driven by the
// global software mixer. Volume/pan are applied during mixing.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include <Aulib/aulib.h>
#include <Aulib/Decoder.h>
#include <Aulib/Resampler.h>

struct SDL_RWops;

namespace Aulib {

class Stream {
public:
    using Callback = std::function<void(Stream&)>;

    Stream(SDL_RWops* rwops, std::unique_ptr<Decoder> decoder,
           std::unique_ptr<Resampler> resampler, bool closeRwOnDestroy);
    ~Stream();

    Stream(const Stream&)            = delete;
    Stream& operator=(const Stream&) = delete;

    // Finalize initialization. Must be called before play().
    bool open();

    // Start playback. iterations=0 loops forever. Returns true on success.
    bool play(int iterations = 1);

    // Stop playback (fires finish callback, if set).
    void stop();

    [[nodiscard]] bool isPlaying() const { return playing_; }

    void setVolume(float v)          { volume_ = v; }
    void setStereoPosition(float p)  { pan_ = p; }
    void mute()                      { muted_ = true; }
    void unmute()                    { muted_ = false; }

    void setFinishCallback(Callback&& cb) { finish_cb_ = std::move(cb); }

    [[nodiscard]] std::chrono::microseconds duration() const;

    // ── Mixer-private helpers ────────────────────────────────────────────
    // Called by Aulib::process() to pull audio.
    // Returns number of stereo frames written to `dst`. A return of 0 means
    // end-of-stream (caller will consult handle_eof()).
    int mix_decode(int16_t* dst, int frames);

    // Called by the mixer when the underlying decoder hit EOF.
    // Handles looping (iterations_left_ > 0) or transitions to stopped.
    void handle_eof();

    [[nodiscard]] float vol_left()  const;
    [[nodiscard]] float vol_right() const;
    [[nodiscard]] bool  muted()     const { return muted_; }

private:
    SDL_RWops*                  rwops_          = nullptr;
    std::unique_ptr<Decoder>    decoder_;
    std::unique_ptr<Resampler>  resampler_;
    bool                        close_rw_       = false;

    bool   opened_          = false;
    bool   playing_         = false;
    bool   muted_           = false;
    int    iterations_left_ = 0;    // 0 means infinite if looping_
    bool   looping_         = false;
    float  volume_          = 1.0f;
    float  pan_             = 0.0f; // -1 = full left, +1 = full right

    Callback finish_cb_;
};

} // namespace Aulib
