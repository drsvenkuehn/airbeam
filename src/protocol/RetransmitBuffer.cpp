#include "protocol/RetransmitBuffer.h"

void RetransmitBuffer::Store(const RtpPacket& pkt, uint16_t seq) noexcept
{
    const uint16_t idx    = seq & (kWindowSize - 1);
    slots_[idx]           = pkt;
    written_[idx]         = true;
}

const RtpPacket* RetransmitBuffer::Retrieve(uint16_t seq) const noexcept
{
    const uint16_t idx = seq & (kWindowSize - 1);
    return written_[idx] ? &slots_[idx] : nullptr;
}
