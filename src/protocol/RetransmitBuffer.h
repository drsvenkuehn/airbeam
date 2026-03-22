#pragma once
#include <array>
#include <cstdint>
#include "protocol/RtpPacket.h"

/// Circular buffer of the last 512 RTP packets for retransmission.
/// Store and Retrieve are O(1) — index = seq & 511.
/// No synchronization needed per D-11 (single writer Thread 4, single reader Thread 5;
/// 512-packet window >> LAN RTT, so slot is never overwritten before Thread 5 reads it).
class RetransmitBuffer {
public:
    RetransmitBuffer() = default;

    /// Stores a packet at slot (seq & 511). Overwrites any previous occupant.
    void Store(const RtpPacket& pkt, uint16_t seq) noexcept;

    /// Retrieves the packet at slot (seq & 511).
    /// Returns nullptr if the slot has never been written.
    const RtpPacket* Retrieve(uint16_t seq) const noexcept;

private:
    static constexpr uint16_t kWindowSize = 512;
    std::array<RtpPacket, kWindowSize> slots_;
    bool written_[kWindowSize] = {};
};
