// T025 — RTP framing and retransmit-buffer unit tests
#include <gtest/gtest.h>
#include "protocol/RtpPacket.h"
#include "protocol/RetransmitBuffer.h"

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(RtpPacket, RtpHeaderBytes01)
{
    RtpPacket pkt{};
    // Byte 0 and 1 are fixed constants defined by the spec.
    EXPECT_EQ(pkt.data[0], 0x80u);
    EXPECT_EQ(pkt.data[1], 0x60u);
}

TEST(RtpPacket, SeqWraps65535To0)
{
    RtpPacket pkt{};

    pkt.SetSeq(65534);
    EXPECT_EQ(pkt.data[2], 0xFF);
    EXPECT_EQ(pkt.data[3], 0xFE);

    pkt.SetSeq(65535);
    EXPECT_EQ(pkt.data[2], 0xFF);
    EXPECT_EQ(pkt.data[3], 0xFF);

    pkt.SetSeq(0);
    EXPECT_EQ(pkt.data[2], 0x00);
    EXPECT_EQ(pkt.data[3], 0x00);
}

TEST(RtpPacket, TimestampIncByN)
{
    RtpPacket pkt{};

    pkt.SetTimestamp(0);
    EXPECT_EQ(pkt.data[4], 0x00);
    EXPECT_EQ(pkt.data[5], 0x00);
    EXPECT_EQ(pkt.data[6], 0x00);
    EXPECT_EQ(pkt.data[7], 0x00);

    // 352 == 0x00000160
    pkt.SetTimestamp(352);
    EXPECT_EQ(pkt.data[4], 0x00);
    EXPECT_EQ(pkt.data[5], 0x00);
    EXPECT_EQ(pkt.data[6], 0x01);
    EXPECT_EQ(pkt.data[7], 0x60);
}

TEST(RtpPacket, SsrcConstant)
{
    RtpPacket pkt{};

    // 0xDEADBEEF big-endian → DE AD BE EF
    pkt.SetSsrc(0xDEADBEEFu);
    EXPECT_EQ(pkt.data[8],  0xDE);
    EXPECT_EQ(pkt.data[9],  0xAD);
    EXPECT_EQ(pkt.data[10], 0xBE);
    EXPECT_EQ(pkt.data[11], 0xEF);
}

TEST(RetransmitBuffer, RetransmitSlotO1)
{
    RetransmitBuffer rb;

    // Build distinguishable packets
    RtpPacket p0{}, p1{}, p511{};
    p0.SetSeq(0);   p0.payloadLen = 10;
    p1.SetSeq(1);   p1.payloadLen = 20;
    p511.SetSeq(511); p511.payloadLen = 30;

    rb.Store(p0,   0);
    rb.Store(p1,   1);
    rb.Store(p511, 511);

    const RtpPacket* r0   = rb.Retrieve(0);
    const RtpPacket* r511 = rb.Retrieve(511);

    ASSERT_NE(r0,   nullptr);
    ASSERT_NE(r511, nullptr);
    EXPECT_EQ(r0->payloadLen,   10);
    EXPECT_EQ(r511->payloadLen, 30);

    // seq=512 maps to slot 512 & 511 == 0 → overwrites seq=0
    RtpPacket p512{};
    p512.SetSeq(512); p512.payloadLen = 99;
    rb.Store(p512, 512);

    const RtpPacket* r512 = rb.Retrieve(512);
    ASSERT_NE(r512, nullptr);
    EXPECT_EQ(r512->payloadLen, 99);

    // The slot that held seq=0 is now occupied by seq=512
    EXPECT_EQ(rb.Retrieve(0), r512); // same pointer — circular overwrite
}
