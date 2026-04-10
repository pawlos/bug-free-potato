/*
 * aulib_potato.cpp — Aulib shim for potatOS.
 *
 * Replaces SDL_audiolib with a minimal software mixer that:
 *  - Owns the kernel AC97 device via sys_audio_open()/sys_audio_close().
 *  - Decodes WAV files directly (see DecoderDrwav below).
 *  - Mixes all active Aulib::Stream objects into a stereo int16_t buffer
 *    and submits it to the kernel via sys_audio_write().
 *
 * The mixer is driven by Aulib::process() which is called from the
 * SDL_PollEvent() hook in our SDL2 shim (so every frame of the game loop
 * keeps audio flowing).
 */

#include <Aulib/aulib.h>
#include <Aulib/Stream.h>
#include <Aulib/Decoder.h>
#include <Aulib/Resampler.h>
#include <Aulib/DecoderDrwav.h>
#include <Aulib/DecoderDrmp3.h>
#include <Aulib/ResamplerSdl.h>

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>

extern "C" {
#include "syscall.h"
}

extern "C" int serial_printf(const char *fmt, ...);

namespace Aulib {

// ── Output configuration ──────────────────────────────────────────────
static int             g_sample_rate    = 22050;
static int             g_channels       = 2;
static int             g_buffer_frames  = 2048;
static SDL_AudioFormat g_format         = AUDIO_S16SYS;
static bool            g_initialized    = false;

int sampleRate() { return g_sample_rate; }
int channelCount() { return g_channels; }
int frameSize() { return g_buffer_frames; }
SDL_AudioFormat sampleFormat() { return g_format; }

// ── Active stream registry ────────────────────────────────────────────
static std::list<Stream*> g_active_streams;

static void register_stream(Stream* s) {
    g_active_streams.push_back(s);
}

static void unregister_stream(Stream* s) {
    g_active_streams.remove(s);
}

// ── init / quit ───────────────────────────────────────────────────────
bool init(int freq, SDL_AudioFormat format, int channels, int frameSize,
          const std::string& /*device*/) {
    if (g_initialized) return true;

    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    if (freq <= 0)    freq = 22050;
    if (frameSize <= 0) frameSize = 2048;

    g_sample_rate   = freq;
    g_channels      = channels;
    g_buffer_frames = frameSize;
    g_format        = format;

    if (sys_audio_open(static_cast<unsigned>(freq),
                       static_cast<unsigned>(channels),
                       /*format=*/0) != 0) {
        serial_printf("[Aulib] sys_audio_open(%d, %d) failed\n", freq, channels);
        return false;
    }

    serial_printf("[Aulib] init rate=%d ch=%d buf=%d\n",
                  freq, channels, frameSize);
    g_initialized = true;
    return true;
}

void quit() {
    if (!g_initialized) return;
    g_active_streams.clear();
    sys_audio_close();
    g_initialized = false;
    serial_printf("[Aulib] quit\n");
}

} // namespace Aulib (close early so the C hook is in ::)

// Strong override for the weak __sdl2_audio_tick() in sdl2.c. Keeps the
// mixer fed every time the game polls for events.
extern "C" void __sdl2_audio_tick(void) {
    Aulib::process();
}

namespace Aulib {

// ── Software mixer ────────────────────────────────────────────────────
void process() {
    if (!g_initialized) return;
    if (g_active_streams.empty()) return;

    // Cap chunk size to something small-ish so mixing latency stays low.
    int frames_per_chunk = g_buffer_frames;
    if (frames_per_chunk > 2048) frames_per_chunk = 2048;
    if (frames_per_chunk < 128)  frames_per_chunk = 128;

    // Two local scratch buffers per call; static to avoid a 16 KB stack frame.
    static int16_t decode_buf[2048 * 2]; // stereo frames * 2 channels
    static int32_t mix_accum[2048 * 2];
    static int16_t mix_out[2048 * 2];

    // Keep feeding the kernel until both DMA slots are full.
    // sys_audio_is_playing(): 1 = busy (no free slot), 0 = free slot available.
    while (sys_audio_is_playing() == 0) {
        bool any_active = false;

        // Zero the accumulator for this chunk.
        std::memset(mix_accum, 0,
                    static_cast<size_t>(frames_per_chunk) * 2 * sizeof(int32_t));

        // Snapshot the current active list so EOF callbacks can safely
        // mutate g_active_streams without invalidating our iterator.
        // (handle_eof() may call Stream::stop() or the finish callback
        //  which in turn may destroy SoundSample objects — all of which
        //  touch g_active_streams.)
        std::list<Stream*> snapshot = g_active_streams;
        for (Stream* s : snapshot) {
            if (!s->isPlaying()) continue;

            any_active = true;

            int got = s->mix_decode(decode_buf, frames_per_chunk);
            if (got > 0 && !s->muted()) {
                const float vl = s->vol_left();
                const float vr = s->vol_right();
                for (int f = 0; f < got; f++) {
                    int32_t l = static_cast<int32_t>(decode_buf[f * 2 + 0] * vl);
                    int32_t r = static_cast<int32_t>(decode_buf[f * 2 + 1] * vr);
                    mix_accum[f * 2 + 0] += l;
                    mix_accum[f * 2 + 1] += r;
                }
            }

            if (got < frames_per_chunk) {
                // Decoder exhausted — handle looping / fire callback.
                s->handle_eof();
            }
        }

        if (!any_active) break;

        // Clip to int16 range.
        const int total_samples = frames_per_chunk * 2;
        for (int i = 0; i < total_samples; i++) {
            int32_t v = mix_accum[i];
            if (v >  32767) v =  32767;
            if (v < -32768) v = -32768;
            mix_out[i] = static_cast<int16_t>(v);
        }

        // Push to kernel. rate arg is ignored post-open.
        long ret = sys_audio_write(mix_out,
                                   static_cast<unsigned long>(total_samples * 2),
                                   /*rate=*/0);
        if (ret <= 0) break; // both slots full again, or error
    }
}

// ── Stream ────────────────────────────────────────────────────────────
Stream::Stream(SDL_RWops* rwops, std::unique_ptr<Decoder> decoder,
               std::unique_ptr<Resampler> resampler, bool closeRwOnDestroy)
    : rwops_(rwops)
    , decoder_(std::move(decoder))
    , resampler_(std::move(resampler))
    , close_rw_(closeRwOnDestroy)
{
}

Stream::~Stream() {
    if (playing_) {
        unregister_stream(this);
        playing_ = false;
    }
    if (close_rw_ && rwops_) {
        SDL_RWclose(rwops_);
        rwops_ = nullptr;
    }
}

bool Stream::open() {
    if (opened_) return true;
    // Decoder was already opened in CreateStream() (SoundSample does it
    // to read rate), so there's nothing more to do here.
    opened_ = true;
    return true;
}

bool Stream::play(int iterations) {
    if (!opened_) return false;
    if (!decoder_) return false;

    // Rewind so repeated Play() calls work.
    decoder_->rewind();

    looping_         = (iterations == 0);
    iterations_left_ = iterations > 0 ? iterations : 0;

    if (!playing_) {
        playing_ = true;
        register_stream(this);
    }
    return true;
}

void Stream::stop() {
    if (!playing_) return;
    playing_ = false;
    unregister_stream(this);
    if (finish_cb_) {
        auto cb = finish_cb_;
        cb(*this);
    }
}

std::chrono::microseconds Stream::duration() const {
    if (!decoder_) return std::chrono::microseconds{0};
    return decoder_->duration();
}

int Stream::mix_decode(int16_t* dst, int frames) {
    if (!playing_ || !decoder_) return 0;
    return decoder_->decodeStereoS16(dst, frames);
}

void Stream::handle_eof() {
    // Either loop again or stop.
    if (looping_) {
        decoder_->rewind();
        return;
    }
    if (iterations_left_ > 1) {
        iterations_left_--;
        decoder_->rewind();
        return;
    }
    // stop() removes us from g_active_streams — safe here because the
    // mixer is iterating over a snapshot copy, not the live list.
    stop();
}

float Stream::vol_left() const {
    // Equal-power-ish pan: left channel attenuated as pan → +1.
    float v = volume_;
    if (pan_ > 0) v *= (1.0f - pan_);
    return v;
}

float Stream::vol_right() const {
    float v = volume_;
    if (pan_ < 0) v *= (1.0f + pan_);
    return v;
}

// ── DecoderDrwav ──────────────────────────────────────────────────────
// Minimal RIFF/WAVE parser: supports PCM format (fmt=1) with 8-bit unsigned
// or 16-bit signed samples, mono or stereo. That covers every Diablo SFX.

namespace {

inline uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

} // namespace

bool DecoderDrwav::open(SDL_RWops* rwops) {
    if (!rwops) return false;

    // Slurp the whole file.
    Sint64 size = SDL_RWsize(rwops);
    if (size <= 44) return false;
    std::vector<uint8_t> raw(static_cast<size_t>(size));
    SDL_RWseek(rwops, 0, RW_SEEK_SET);
    size_t read = SDL_RWread(rwops, raw.data(), 1, static_cast<size_t>(size));
    if (read != static_cast<size_t>(size)) return false;

    // RIFF header.
    if (std::memcmp(raw.data() + 0, "RIFF", 4) != 0) return false;
    if (std::memcmp(raw.data() + 8, "WAVE", 4) != 0) return false;

    // Walk chunks looking for "fmt " and "data".
    size_t pos = 12;
    bool   have_fmt = false;
    uint16_t audio_format = 0;
    uint16_t channels     = 0;
    uint32_t sample_rate  = 0;
    uint16_t bits_per_sample = 0;
    const uint8_t* data_ptr = nullptr;
    uint32_t      data_size = 0;

    while (pos + 8 <= raw.size()) {
        const char* cid = reinterpret_cast<const char*>(raw.data() + pos);
        uint32_t csz = rd32(raw.data() + pos + 4);
        size_t cbeg = pos + 8;
        if (cbeg + csz > raw.size()) break;

        if (std::memcmp(cid, "fmt ", 4) == 0 && csz >= 16) {
            audio_format    = rd16(raw.data() + cbeg + 0);
            channels        = rd16(raw.data() + cbeg + 2);
            sample_rate     = rd32(raw.data() + cbeg + 4);
            bits_per_sample = rd16(raw.data() + cbeg + 14);
            have_fmt = true;
        } else if (std::memcmp(cid, "data", 4) == 0) {
            data_ptr  = raw.data() + cbeg;
            data_size = csz;
            break;
        }

        pos = cbeg + csz + (csz & 1u); // chunks are word-aligned
    }

    if (!have_fmt || !data_ptr) return false;
    if (audio_format != 1) return false; // only raw PCM
    if (channels != 1 && channels != 2) return false;
    if (bits_per_sample != 8 && bits_per_sample != 16) return false;
    if (sample_rate == 0) return false;

    native_rate_     = static_cast<int>(sample_rate);
    native_channels_ = channels;

    const int bytes_per_sample = bits_per_sample / 8;
    const int frame_bytes      = bytes_per_sample * channels;
    const int in_frames        = static_cast<int>(data_size / frame_bytes);

    samples_.clear();
    samples_.resize(static_cast<size_t>(in_frames) * channels);

    if (bits_per_sample == 16) {
        // Signed 16-bit little endian — direct copy on x86.
        std::memcpy(samples_.data(), data_ptr,
                    static_cast<size_t>(in_frames) * frame_bytes);
    } else {
        // 8-bit unsigned → signed 16-bit.
        for (int i = 0; i < in_frames * channels; i++) {
            int s = static_cast<int>(data_ptr[i]) - 128;   // -128..+127
            samples_[i] = static_cast<int16_t>(s * 256);  // scale to int16
        }
    }

    total_frames_ = in_frames;
    out_phase_    = 0;
    return true;
}

int DecoderDrwav::decodeStereoS16(int16_t* dst, int frames) {
    if (total_frames_ == 0 || samples_.empty()) return 0;

    const int out_rate = g_sample_rate;
    const int produced_cap = frames;
    int produced = 0;

    // Nearest-neighbor resample: for each output frame, pick the input
    // frame at floor(out_phase_ * in_rate / out_rate). When the input
    // index walks past the end, we're done.
    while (produced < produced_cap) {
        int64_t in_idx = (static_cast<int64_t>(out_phase_) * native_rate_)
                         / out_rate;
        if (in_idx >= total_frames_) break;

        int16_t l, r;
        if (native_channels_ == 1) {
            l = r = samples_[static_cast<size_t>(in_idx)];
        } else {
            l = samples_[static_cast<size_t>(in_idx) * 2 + 0];
            r = samples_[static_cast<size_t>(in_idx) * 2 + 1];
        }
        dst[produced * 2 + 0] = l;
        dst[produced * 2 + 1] = r;

        produced++;
        out_phase_++;
    }

    return produced;
}

bool DecoderDrwav::rewind() {
    out_phase_ = 0;
    return true;
}

std::chrono::microseconds DecoderDrwav::duration() const {
    if (native_rate_ <= 0 || total_frames_ <= 0) {
        return std::chrono::microseconds{0};
    }
    int64_t us = (static_cast<int64_t>(total_frames_) * 1000000)
                 / native_rate_;
    return std::chrono::microseconds{us};
}

} // namespace Aulib
