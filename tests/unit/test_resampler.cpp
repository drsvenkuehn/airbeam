#include <gtest/gtest.h>
#include "audio/Resampler.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace {
static const double kPi = 3.14159265358979323846;

// Generate mono float sine at freq/sampleRate for numFrames frames
// Returns float array (mono, 1 channel per frame)
std::vector<float> GenSineMono(double freq, uint32_t sampleRate, int numFrames) {
    std::vector<float> buf(numFrames);
    for (int i = 0; i < numFrames; i++) {
        buf[i] = 0.5f * (float)std::sin(2.0 * kPi * freq * i / sampleRate);
    }
    return buf;
}

// Count zero crossings in int16 stereo (use left channel only)
int CountZeroCrossings(const int16_t* data, int frames) {
    int count = 0;
    for (int i = 1; i < frames; i++) {
        int16_t prev = data[(i-1)*2];  // left channel
        int16_t curr = data[i*2];
        if ((prev < 0 && curr >= 0) || (prev >= 0 && curr < 0))
            count++;
    }
    return count;
}

} // namespace

TEST(ResamplerTest, PassthroughIdentity) {
    Resampler r(44100, 2);
    EXPECT_TRUE(r.IsPassthrough());

    // Generate some float stereo audio
    std::vector<float> floatBuf(100 * 2);  // 100 frames, stereo
    for (int i = 0; i < 100; i++) {
        floatBuf[i*2]   = 0.5f * (float)std::sin(2.0 * kPi * 1000.0 * i / 44100.0);
        floatBuf[i*2+1] = floatBuf[i*2];
    }

    int outFrames = 0;
    const int16_t* out = r.Process(floatBuf.data(), 100, outFrames);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(outFrames, 100);
    // Verify samples are nonzero (not all silence)
    bool anyNonzero = false;
    for (int i = 0; i < 100*2; i++) if (out[i] != 0) { anyNonzero = true; break; }
    EXPECT_TRUE(anyNonzero);
}

TEST(ResamplerTest, Resample48kTo44k) {
    Resampler r(48000, 2);
    EXPECT_FALSE(r.IsPassthrough());

    // 480 frames of 1kHz stereo sine at 48000Hz
    const int inFrames = 480;
    std::vector<float> floatBuf(inFrames * 2);
    for (int i = 0; i < inFrames; i++) {
        float s = 0.5f * (float)std::sin(2.0 * kPi * 1000.0 * i / 48000.0);
        floatBuf[i*2]   = s;
        floatBuf[i*2+1] = s;
    }

    int outFrames = 0;
    const int16_t* out = r.Process(floatBuf.data(), inFrames, outFrames);
    ASSERT_NE(out, nullptr);

    // Expected: 480 * 44100/48000 = 441 frames
    EXPECT_GE(outFrames, 439);
    EXPECT_LE(outFrames, 443);

    // Zero-crossing rate: 1kHz at 44100Hz = 2000 ZC/sec
    // In 441 frames: 441/44100 = 0.01 sec → ~20 ZC expected
    int zc = CountZeroCrossings(out, outFrames);
    // 1kHz sine has ~2 ZC per ms; 441 frames ≈ 10ms → ~20 ZC ±5
    EXPECT_GE(zc, 15) << "Too few zero crossings (pitch too low)";
    EXPECT_LE(zc, 25) << "Too many zero crossings (pitch too high)";
}

TEST(ResamplerTest, Resample96kTo44k) {
    Resampler r(96000, 2);
    EXPECT_FALSE(r.IsPassthrough());

    // 960 frames of 1kHz stereo sine at 96000Hz
    const int inFrames = 960;
    std::vector<float> floatBuf(inFrames * 2);
    for (int i = 0; i < inFrames; i++) {
        float s = 0.5f * (float)std::sin(2.0 * kPi * 1000.0 * i / 96000.0);
        floatBuf[i*2]   = s;
        floatBuf[i*2+1] = s;
    }

    int outFrames = 0;
    const int16_t* out = r.Process(floatBuf.data(), inFrames, outFrames);
    ASSERT_NE(out, nullptr);

    // Expected: 960 * 44100/96000 = 441 frames
    EXPECT_GE(outFrames, 439);
    EXPECT_LE(outFrames, 443);

    int zc = CountZeroCrossings(out, outFrames);
    EXPECT_GE(zc, 15);
    EXPECT_LE(zc, 25);
}
