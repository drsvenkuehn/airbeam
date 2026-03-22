#pragma once
#pragma warning(disable: 4324) // structure padded due to alignas — intentional for cache-line isolation
#include <array>
#include <atomic>
#include <cstdint>
#include <variant>
#include "audio/AudioFrame.h"

template<typename T, uint32_t N>
class SpscRingBuffer {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
public:
    SpscRingBuffer() noexcept : head_(0), tail_(0) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    bool TryPush(const T& item) noexcept {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t next = head + 1;
        if ((next - tail_.load(std::memory_order_acquire)) >= N)
            return false;   // full: N items stored (one slot reserved to distinguish full/empty)
        storage_[head & (N - 1)] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool TryPop(T& item) noexcept {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire))
            return false;   // empty
        item = storage_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool IsEmpty() const noexcept {
        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }

    bool IsFull() const noexcept {
        return (head_.load(std::memory_order_acquire) -
                tail_.load(std::memory_order_acquire)) >= (N - 1);
    }

    uint32_t Size() const noexcept {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_acquire);
    }

private:
    alignas(64) std::atomic<uint32_t> head_;
    alignas(64) std::atomic<uint32_t> tail_;
    std::array<T, N> storage_;
};

// ── T100: SpscRingBufferPtr type-erasure ──────────────────────────────────────
// Forward declaration ensures AudioFrame.h only needs to be included once.

/// Type-erased pointer to either a 128-slot or 32-slot (low-latency) ring buffer.
/// ConnectionController stores this so Threads 3 and 4 can be passed the same
/// pointer regardless of latency mode.
using SpscRingBufferPtr = std::variant<
    SpscRingBuffer<AudioFrame, 128>*,
    SpscRingBuffer<AudioFrame, 32>*
>;

inline bool RingTryPush(SpscRingBufferPtr& ring, const AudioFrame& frame) noexcept {
    return std::visit([&](auto* buf) noexcept { return buf->TryPush(frame); }, ring);
}

inline bool RingTryPop(SpscRingBufferPtr& ring, AudioFrame& frame) noexcept {
    return std::visit([&](auto* buf) noexcept { return buf->TryPop(frame); }, ring);
}
