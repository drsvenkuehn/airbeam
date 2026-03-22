#include "audio/Resampler.h"
#include <samplerate.h>
#include <cstring>
#include <cmath>

Resampler::Resampler(uint32_t srcRate, uint32_t srcChannels, uint32_t dstRate)
    : srcRate_(srcRate), srcChannels_(srcChannels)
{
    if (srcRate == 44100 && srcChannels == 2) {
        passthrough_ = true;
        return;
    }

    passthrough_ = false;
    ratio_       = static_cast<double>(dstRate) / static_cast<double>(srcRate);

    int error = 0;
    // Always output stereo (2 channels)
    srcState_ = src_new(SRC_SINC_BEST_QUALITY, 2, &error);
    // srcState_ may be null on failure; Process() will handle gracefully
}

Resampler::~Resampler()
{
    if (srcState_)
        src_delete(srcState_);
}

int Resampler::Process(const float* in, int16_t* out, int inFrames)
{
    if (passthrough_) {
        // Direct float-to-int16 conversion (input is already stereo 44100)
        const int samples = inFrames * 2; // stereo
        for (int i = 0; i < samples; ++i) {
            float s = in[i];
            if      (s >  1.0f) s =  1.0f;
            else if (s < -1.0f) s = -1.0f;
            out[i] = static_cast<int16_t>(lrintf(s * 32767.0f));
        }
        return inFrames;
    }

    if (!srcState_)
        return 0;

    const int maxOutFrames = static_cast<int>(sizeof(floatOutBuf_) / (2 * sizeof(float)));

    SRC_DATA data{};
    data.data_in       = in;
    data.input_frames  = inFrames;
    data.data_out      = floatOutBuf_;
    data.output_frames = maxOutFrames;
    data.src_ratio     = ratio_;
    data.end_of_input  = 0;

    // Handle mono-to-stereo upmix using the pre-allocated buffer (no heap alloc).
    if (srcChannels_ == 1) {
        const int upmixSamples = inFrames * 2;
        for (int i = 0; i < inFrames; ++i) {
            monoUpmixBuf_[i * 2]     = in[i];
            monoUpmixBuf_[i * 2 + 1] = in[i];
        }
        data.data_in      = monoUpmixBuf_;
        data.input_frames = inFrames; // still inFrames stereo frames
        (void)upmixSamples;
    }

    if (src_process(srcState_, &data) != 0)
        return 0;

    const int outFrames   = static_cast<int>(data.output_frames_gen);
    const int outSamples  = outFrames * 2;
    for (int i = 0; i < outSamples; ++i) {
        float s = floatOutBuf_[i];
        if      (s >  1.0f) s =  1.0f;
        else if (s < -1.0f) s = -1.0f;
        out[i] = static_cast<int16_t>(lrintf(s * 32767.0f));
    }

    return outFrames;
}
