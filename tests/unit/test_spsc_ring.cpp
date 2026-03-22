// T022 — SPSC ring buffer unit tests
#include <gtest/gtest.h>
#include <memory>
#include "audio/SpscRingBuffer.h"
#include "audio/AudioFrame.h"

// ── Helper ────────────────────────────────────────────────────────────────────

static AudioFrame MakeFrame(uint32_t count)
{
    AudioFrame f{};
    f.frameCount = count;
    return f;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(SpscRingBuffer, FullBufferPushReturnsFalse)
{
    SpscRingBuffer<int, 4> rb;

    // An SPSC ring with N=4 holds at most N-1 = 3 items (one slot reserved).
    EXPECT_TRUE(rb.TryPush(1));
    EXPECT_TRUE(rb.TryPush(2));
    EXPECT_TRUE(rb.TryPush(3));
    // Buffer is now full; the next push must fail.
    EXPECT_FALSE(rb.TryPush(4));
}

TEST(SpscRingBuffer, EmptyBufferPopReturnsFalse)
{
    SpscRingBuffer<int, 4> rb;
    int val{};
    EXPECT_FALSE(rb.TryPop(val));
}

TEST(SpscRingBuffer, FifoOrderPreserved)
{
    // Heap-allocate: SpscRingBuffer<AudioFrame,1024> is ~1.4 MB — too large for stack.
    auto rb = std::make_unique<SpscRingBuffer<AudioFrame, 1024>>();
    constexpr int kCount = 1000;

    for (int i = 0; i < kCount; ++i) {
        ASSERT_TRUE(rb->TryPush(MakeFrame(static_cast<uint32_t>(i))));
    }

    for (int i = 0; i < kCount; ++i) {
        AudioFrame f{};
        ASSERT_TRUE(rb->TryPop(f));
        EXPECT_EQ(f.frameCount, static_cast<uint32_t>(i));
    }

    AudioFrame extra{};
    EXPECT_FALSE(rb->TryPop(extra));
}

TEST(SpscRingBuffer, WrapAroundBoundary)
{
    // N=8  →  capacity = 7 items
    SpscRingBuffer<int, 8> rb;
    constexpr int kCap = 7; // N-1

    // First batch
    for (int i = 0; i < kCap; ++i) {
        ASSERT_TRUE(rb.TryPush(i));
    }
    for (int i = 0; i < kCap; ++i) {
        int v{};
        ASSERT_TRUE(rb.TryPop(v));
        EXPECT_EQ(v, i);
    }

    // Second batch — head and tail have both wrapped past index 0
    for (int i = 0; i < kCap; ++i) {
        ASSERT_TRUE(rb.TryPush(100 + i));
    }
    for (int i = 0; i < kCap; ++i) {
        int v{};
        ASSERT_TRUE(rb.TryPop(v));
        EXPECT_EQ(v, 100 + i);
    }
}

TEST(SpscRingBuffer, NoDataLossAtCapacityMinusOne)
{
    // Heap-allocate: SpscRingBuffer<AudioFrame,1024> is ~1.4 MB — too large for stack.
    auto rb = std::make_unique<SpscRingBuffer<AudioFrame, 1024>>();
    constexpr int kCap = 1023; // N-1

    for (int i = 0; i < kCap; ++i) {
        ASSERT_TRUE(rb->TryPush(MakeFrame(static_cast<uint32_t>(i))));
    }
    EXPECT_TRUE(rb->IsFull());

    int popped = 0;
    AudioFrame f{};
    while (rb->TryPop(f)) {
        EXPECT_EQ(f.frameCount, static_cast<uint32_t>(popped));
        ++popped;
    }
    EXPECT_EQ(popped, kCap);
    EXPECT_TRUE(rb->IsEmpty());
}
