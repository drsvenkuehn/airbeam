#pragma once
#include <cstdint>
#include <memory>

struct SpeexResamplerState_;

/// Wraps speexdsp rational polyphase resampler for audio format conversion.
/// If the capture format is already 44100 Hz, this is a passthrough.
/// Allocated before the streaming loop; Process() is allocation-free.
class Resampler {
public:
    /// If srcRate == 44100, no resampler is created (passthrough).
    /// Otherwise initializes speexdsp with QUALITY_DEFAULT.
    Resampler(uint32_t srcRate, uint32_t srcChannels, uint32_t dstRate = 44100);
    ~Resampler();

    Resampler(const Resampler&)            = delete;
    Resampler& operator=(const Resampler&) = delete;

    bool IsPassthrough() const { return passthrough_; }

    /// Converts `inFrames` input audio frames to int16_t stereo 44100 Hz output.
    /// Returns a pointer to the internal int16 output buffer; caller must not free it.
    /// outFrames is set to the number of output frames produced.
    /// `in` is float32 interleaved (srcChannels channels).
    const int16_t* Process(const float* in, int inFrames, int& outFrames) noexcept;

private:
    SpeexResamplerState_* spxState_    = nullptr;
    uint32_t              srcRate_     = 44100;
    uint32_t              srcChannels_ = 2;
    bool                  passthrough_ = true;

    // Pre-allocated int16 stereo input buffer (for float→int16 conversion before resampling)
    int16_t               int16InBuf_[4096 * 2]  = {};
    // Pre-allocated int16 stereo output buffer (returned by Process())
    int16_t               int16OutBuf_[4096 * 2] = {};
    // Pre-allocated passthrough int16 buffer (for passthrough float→int16)
    int16_t               passBuf_[4096 * 2]     = {};
};
