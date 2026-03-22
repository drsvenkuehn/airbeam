/// T101: Unit tests for WasapiCapture::FrameAccumulator behaviour.
/// The struct is mirrored here for testing since it is private inside WasapiCapture.
#include <gtest/gtest.h>
#include "audio/AudioFrame.h"
#include <cstring>
#include <vector>

// ── Mirror of WasapiCapture::FrameAccumulator ─────────────────────────────────

struct FrameAccumulator {
    int16_t buf[704] = {};
    int     filled   = 0;

    // Appends up to frameCount frames from src into buf.
    // channels=2: copy L+R directly; channels=1: upmix mono → stereo (L=R).
    // Returns true (and clears buf) once exactly 352 stereo frames have accumulated.
    bool Push(const int16_t* src, int frameCount, int channels, AudioFrame& out) noexcept {
        for (int i = 0; i < frameCount && filled < 704; ++i) {
            buf[filled++] = src[i * channels];                                      // L
            buf[filled++] = (channels > 1) ? src[i * channels + 1] : src[i * channels]; // R
        }
        if (filled >= 704) {
            memcpy(out.samples, buf, 704 * sizeof(int16_t));
            filled = 0;
            return true;
        }
        return false;
    }
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::vector<int16_t> MakeStereo(int frameCount, int16_t value = 100)
{
    std::vector<int16_t> v(static_cast<size_t>(frameCount) * 2, value);
    return v;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(FrameAccumulator, EmptyInputNoFrame)
{
    FrameAccumulator acc;
    AudioFrame out{};
    bool emitted = acc.Push(nullptr, 0, 2, out);
    EXPECT_FALSE(emitted);
    EXPECT_EQ(acc.filled, 0);
}

TEST(FrameAccumulator, Exactly352SamplesOneFrame)
{
    FrameAccumulator acc;
    auto src = MakeStereo(352);
    AudioFrame out{};
    bool emitted = acc.Push(src.data(), 352, 2, out);
    EXPECT_TRUE(emitted);
    EXPECT_EQ(acc.filled, 0) << "No carry after exactly one frame";
}

TEST(FrameAccumulator, SplitAcrossTwoCalls)
{
    FrameAccumulator acc;
    AudioFrame out{};

    auto src300 = MakeStereo(300);
    bool first = acc.Push(src300.data(), 300, 2, out);
    EXPECT_FALSE(first) << "300 frames must not yet emit";
    EXPECT_EQ(acc.filled, 600);

    auto src52 = MakeStereo(52);
    bool second = acc.Push(src52.data(), 52, 2, out);
    EXPECT_TRUE(second) << "300+52=352 frames must emit on second call";
    EXPECT_EQ(acc.filled, 0) << "No carry after exactly one frame";
}

TEST(FrameAccumulator, DoubleFrameFrom704Samples)
{
    // Push 704 stereo frames. Push only consumes until the buffer fills (352),
    // so we call it in a loop to drain all 704 source frames.
    FrameAccumulator acc;
    auto src = MakeStereo(704);
    AudioFrame out{};

    int emitCount = 0;
    int processed = 0;
    while (processed < 704) {
        int remaining = 704 - processed;
        bool emitted = acc.Push(src.data() + processed * 2, remaining, 2, out);
        if (emitted) {
            ++emitCount;
            // Each successful emit consumed exactly (704 - acc.filled_before) / 2 frames.
            // After emit, filled resets to 0, so we consumed 352 frames this call.
            processed += 352;
        } else {
            processed += remaining;
        }
    }
    EXPECT_EQ(emitCount, 2);
    EXPECT_EQ(acc.filled, 0);
}

TEST(FrameAccumulator, PartialCarryAfter1058Samples)
{
    // 1058 stereo frames = 3×352 + 2 remainder.
    FrameAccumulator acc;
    auto src = MakeStereo(1058);
    AudioFrame out{};

    int emitCount = 0;
    int processed = 0;
    while (processed < 1058) {
        int remaining = 1058 - processed;
        bool emitted = acc.Push(src.data() + processed * 2, remaining, 2, out);
        if (emitted) {
            ++emitCount;
            processed += 352;
        } else {
            processed += remaining;
        }
    }
    EXPECT_EQ(emitCount, 3);
    EXPECT_EQ(acc.filled, 4) << "2 stereo samples (4 int16_t) should remain";
}
