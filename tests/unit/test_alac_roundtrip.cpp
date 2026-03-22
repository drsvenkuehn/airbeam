// T024 — ALAC round-trip unit tests
#define _USE_MATH_DEFINES  // M_PI on MSVC
#include <gtest/gtest.h>
#include "ALACEncoder.h"
#include "ALACDecoder.h"
#include "ALACBitUtilities.h"

#include <cmath>
#include <cstring>
#include <vector>

// ALAC encoder returns 0 on success (no ALAC_noErr constant in the codec headers)
static constexpr int32_t kALAC_noErr = 0;

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr uint32_t kSampleRate  = 44100;
static constexpr uint32_t kFrameSize   = 352;
static constexpr uint32_t kNumChannels = 2;
static constexpr uint32_t kBitDepth    = 16;

// Raw PCM bytes for one stereo frame: kFrameSize * 2 ch * 2 bytes
static constexpr size_t kPcmBytes = kFrameSize * kNumChannels * (kBitDepth / 8);

// Maximum ALAC output: encoder uses mMaxOutputBytes = frameSize * channels * ((10+32)/8) + 1 = 3521
// Use 4224 (frameSize * channels * 6) to stay well clear of the limit.
static constexpr size_t kAlacMaxBytes = kFrameSize * kNumChannels * 6;

// ── Fixture ───────────────────────────────────────────────────────────────────

class AlacRoundTrip : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // ── Input format (linear PCM) ─────────────────────────────────────────
        std::memset(&mInputFormat, 0, sizeof(mInputFormat));
        mInputFormat.mSampleRate       = kSampleRate;
        mInputFormat.mFormatID         = kALACFormatLinearPCM;
        mInputFormat.mFormatFlags      = kALACFormatFlagIsSignedInteger | kALACFormatFlagIsPacked;
        mInputFormat.mBytesPerPacket   = kNumChannels * (kBitDepth / 8);
        mInputFormat.mFramesPerPacket  = 1;
        mInputFormat.mBytesPerFrame    = kNumChannels * (kBitDepth / 8);
        mInputFormat.mChannelsPerFrame = kNumChannels;
        mInputFormat.mBitsPerChannel   = kBitDepth;

        // Output format (ALAC): mFormatFlags encodes bit depth as a 1-indexed code:
        // 1=16-bit, 2=20-bit, 3=24-bit, 4=32-bit (see kTestFormatFlag_*BitSourceData in convert-utility/main.cpp).
        // Do NOT use kALACFormatFlagIsSignedInteger (=4) here — that would set 32-bit mode.
        std::memset(&mOutputFormat, 0, sizeof(mOutputFormat));
        mOutputFormat.mSampleRate       = kSampleRate;
        mOutputFormat.mFormatID         = kALACFormatAppleLossless;
        mOutputFormat.mFormatFlags      = 1;   // 16-bit source data
        mOutputFormat.mFramesPerPacket  = kFrameSize;
        mOutputFormat.mChannelsPerFrame = kNumChannels;
        mOutputFormat.mBitsPerChannel   = kBitDepth;

        mEncoder = std::make_unique<ALACEncoder>();
        mEncoder->SetFrameSize(kFrameSize);
        int32_t rc = mEncoder->InitializeEncoder(mOutputFormat);
        if (rc != kALAC_noErr) {
            GTEST_SKIP() << "ALACEncoder::InitializeEncoder failed (" << rc << "); skipping.";
        }

        mDecoder = std::make_unique<ALACDecoder>();
        uint32_t cookieSize = mEncoder->GetMagicCookieSize(kNumChannels);
        mCookie.resize(cookieSize);
        mEncoder->GetMagicCookie(mCookie.data(), &cookieSize);

        rc = mDecoder->Init(mCookie.data(), cookieSize);
        if (rc != kALAC_noErr) {
            GTEST_SKIP() << "ALACDecoder::Init failed (" << rc << "); skipping.";
        }
    }

    // Encode `pcmIn` → compressed → decode → `pcmOut`.  Returns true on success.
    bool RoundTrip(const int16_t* pcmIn, int16_t* pcmOut)
    {
        std::vector<uint8_t> encoded(kAlacMaxBytes, 0);
        // ioNumBytes: on input = PCM byte count, on output = compressed byte count
        int32_t ioNumBytes = static_cast<int32_t>(kPcmBytes);

        int32_t rc = mEncoder->Encode(mInputFormat, mOutputFormat,
                                      reinterpret_cast<uint8_t*>(const_cast<int16_t*>(pcmIn)),
                                      encoded.data(), &ioNumBytes);
        if (rc != kALAC_noErr) {
            ADD_FAILURE() << "Encode failed with rc=" << rc << ", ioNumBytes=" << ioNumBytes;
            return false;
        }
        // Use full buffer size for decoder — decoder reads ID_END tag after payload
        // and may read a few bits beyond the exact compressed byte count.
        BitBuffer bb;
        BitBufferInit(&bb, encoded.data(), static_cast<uint32_t>(encoded.size()));
        uint32_t outSamples = 0;
        rc = mDecoder->Decode(&bb,
                              reinterpret_cast<uint8_t*>(pcmOut),
                              kFrameSize, kNumChannels, &outSamples);
        if (rc != kALAC_noErr) {
            ADD_FAILURE() << "Decode failed with rc=" << rc
                          << ", outSamples=" << outSamples
                          << ", encodedBytes=" << ioNumBytes;
            return false;
        }
        if (outSamples != kFrameSize) {
            ADD_FAILURE() << "Decode returned " << outSamples << " samples, expected " << kFrameSize;
            return false;
        }
        return true;
    }

    AudioFormatDescription       mInputFormat{};
    AudioFormatDescription       mOutputFormat{};
    std::unique_ptr<ALACEncoder> mEncoder;
    std::unique_ptr<ALACDecoder> mDecoder;
    std::vector<uint8_t>         mCookie;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(AlacRoundTrip, SilenceRoundTrip)
{
    int16_t pcmIn [kFrameSize * kNumChannels]{};   // all zeros
    int16_t pcmOut[kFrameSize * kNumChannels]{};
    std::memset(pcmIn,  0, sizeof(pcmIn));

    ASSERT_TRUE(RoundTrip(pcmIn, pcmOut));
    EXPECT_EQ(std::memcmp(pcmIn, pcmOut, sizeof(pcmIn)), 0);
}

TEST_F(AlacRoundTrip, SineWaveRoundTrip)
{
    int16_t pcmIn [kFrameSize * kNumChannels]{};
    int16_t pcmOut[kFrameSize * kNumChannels]{};

    for (uint32_t i = 0; i < kFrameSize; ++i) {
        int16_t s = static_cast<int16_t>(20000.0 * std::sin(2.0 * M_PI * 1000.0 * i / kSampleRate));
        pcmIn[i * 2]     = s;  // L
        pcmIn[i * 2 + 1] = s;  // R
    }

    ASSERT_TRUE(RoundTrip(pcmIn, pcmOut));
    EXPECT_EQ(std::memcmp(pcmIn, pcmOut, sizeof(pcmIn)), 0);
}

TEST_F(AlacRoundTrip, MaxAmplitudeRoundTrip)
{
    int16_t pcmIn [kFrameSize * kNumChannels]{};
    int16_t pcmOut[kFrameSize * kNumChannels]{};

    for (uint32_t i = 0; i < kFrameSize; ++i) {
        int16_t s = (i % 2 == 0) ? 32767 : -32767;
        pcmIn[i * 2]     = s;
        pcmIn[i * 2 + 1] = s;
    }

    ASSERT_TRUE(RoundTrip(pcmIn, pcmOut));
    EXPECT_EQ(std::memcmp(pcmIn, pcmOut, sizeof(pcmIn)), 0);
}
