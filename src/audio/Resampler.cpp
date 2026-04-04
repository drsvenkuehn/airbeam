#include "audio/Resampler.h"
#include <speex_resampler.h>
#include <cstring>
#include <cmath>

Resampler::Resampler(uint32_t srcRate, uint32_t srcChannels, uint32_t dstRate)
    : srcRate_(srcRate), srcChannels_(srcChannels)
{
    if (srcRate == dstRate) {
        passthrough_ = true;
        return;
    }

    passthrough_ = false;

    int error = 0;
    // Always output stereo (2 channels)
    spxState_ = speex_resampler_init(2, srcRate, dstRate,
                                     SPEEX_RESAMPLER_QUALITY_DEFAULT, &error);
    // spxState_ may be null on failure; Process() handles gracefully
}

Resampler::~Resampler()
{
    if (spxState_)
        speex_resampler_destroy(spxState_);
}

const int16_t* Resampler::Process(const float* in, int inFrames, int& outFrames) noexcept
{
    if (passthrough_) {
        // Float-to-int16 conversion (input may be mono or stereo at 44100)
        const int outCh = 2;
        int limit = inFrames;
        if (limit > 4096) limit = 4096;
        for (int i = 0; i < limit; ++i) {
            float l = in[i * (int)srcChannels_];
            float r = (srcChannels_ >= 2) ? in[i * (int)srcChannels_ + 1] : l;
            if (l >  1.0f) l =  1.0f; else if (l < -1.0f) l = -1.0f;
            if (r >  1.0f) r =  1.0f; else if (r < -1.0f) r = -1.0f;
            passBuf_[i * outCh]     = static_cast<int16_t>(lrintf(l * 32767.0f));
            passBuf_[i * outCh + 1] = static_cast<int16_t>(lrintf(r * 32767.0f));
        }
        outFrames = limit;
        return passBuf_;
    }

    if (!spxState_) {
        outFrames = 0;
        return int16OutBuf_;
    }

    int limit = inFrames;
    if (limit > 4096) limit = 4096;

    // Convert float input to int16 stereo in int16InBuf_
    for (int i = 0; i < limit; ++i) {
        float l = in[i * (int)srcChannels_];
        float r = (srcChannels_ >= 2) ? in[i * (int)srcChannels_ + 1] : l;
        if (l >  1.0f) l =  1.0f; else if (l < -1.0f) l = -1.0f;
        if (r >  1.0f) r =  1.0f; else if (r < -1.0f) r = -1.0f;
        int16InBuf_[i * 2]     = static_cast<int16_t>(lrintf(l * 32767.0f));
        int16InBuf_[i * 2 + 1] = static_cast<int16_t>(lrintf(r * 32767.0f));
    }

    spx_uint32_t in_len  = static_cast<spx_uint32_t>(limit);
    spx_uint32_t out_len = 4096;

    speex_resampler_process_interleaved_int(
        spxState_,
        int16InBuf_,  &in_len,
        int16OutBuf_, &out_len);

    outFrames = static_cast<int>(out_len);
    return int16OutBuf_;
}
