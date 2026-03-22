#pragma once
#include <cstdint>
#include <memory>

struct SRC_STATE_tag;  // forward declare libsamplerate state

/// Wraps libsamplerate for audio format conversion.
/// If the capture format is already 44100 Hz stereo S16LE, this is a passthrough.
/// Allocated on Thread 1 before the streaming loop; Process() is allocation-free.
class Resampler {
public:
    /// If srcRate == 44100 && srcChannels == 2, no resampler is created (passthrough).
    /// Otherwise initializes libsamplerate SRC_SINC_BEST_QUALITY.
    Resampler(uint32_t srcRate, uint32_t srcChannels, uint32_t dstRate = 44100);
    ~Resampler();

    Resampler(const Resampler&)            = delete;
    Resampler& operator=(const Resampler&) = delete;

    bool IsPassthrough() const { return passthrough_; }

    /// Converts `inFrames` input audio frames to int16_t stereo 44100 Hz output.
    /// Returns number of output frames written to `out`.
    /// `in` is float32 interleaved (srcChannels channels).
    /// `out` is int16_t interleaved stereo (2 channels).
    int Process(const float* in, int16_t* out, int inFrames);

private:
    SRC_STATE_tag* srcState_    = nullptr;
    uint32_t       srcRate_     = 44100;
    uint32_t       srcChannels_ = 2;
    double         ratio_       = 1.0;
    bool           passthrough_ = true;
    // Pre-allocated float output buffer for SRC output (avoids per-call alloc)
    // Max output frames: ~2000 (for 352 input frames at max 6x upsampling)
    float          floatOutBuf_[4096 * 2] = {}; // 4096 frames * 2 channels
    // Pre-allocated buffer for mono→stereo upmix before resampling
    float          monoUpmixBuf_[4096 * 2] = {};
};
