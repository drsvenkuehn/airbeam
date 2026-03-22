#pragma once
#include <cstdint>
#include <cstring>

/// RTP packet buffer for one ALAC frame over RAOP.
/// Header layout (12 bytes, RFC 3550):
///   [0]   = 0x80  (V=2, P=0, X=0, CC=0)
///   [1]   = 0x60  (M=0, PT=96)
///   [2-3] = sequence number (big-endian)
///   [4-7] = timestamp (big-endian)
///   [8-11]= SSRC (big-endian)
///   [12+] = ALAC payload (AES-encrypted)
struct RtpPacket {
    uint8_t  data[1500];     ///< full wire bytes including 12-byte header
    uint16_t payloadLen;     ///< number of payload bytes after the 12-byte header

    RtpPacket() noexcept : data{}, payloadLen{} {
        data[0] = 0x80;  // V=2, P=0, X=0, CC=0
        data[1] = 0x60;  // M=0, PT=96
    }

    /// Resets fixed header bytes (use after memset or placement-new).
    void InitHeader() noexcept {
        data[0] = 0x80;
        data[1] = 0x60;
    }

    void SetSeq(uint16_t seq) noexcept {
        data[2] = static_cast<uint8_t>(seq >> 8);
        data[3] = static_cast<uint8_t>(seq);
    }

    void SetTimestamp(uint32_t ts) noexcept {
        data[4] = static_cast<uint8_t>(ts >> 24);
        data[5] = static_cast<uint8_t>(ts >> 16);
        data[6] = static_cast<uint8_t>(ts >>  8);
        data[7] = static_cast<uint8_t>(ts);
    }

    void SetSsrc(uint32_t ssrc) noexcept {
        data[8]  = static_cast<uint8_t>(ssrc >> 24);
        data[9]  = static_cast<uint8_t>(ssrc >> 16);
        data[10] = static_cast<uint8_t>(ssrc >>  8);
        data[11] = static_cast<uint8_t>(ssrc);
    }

    uint16_t GetSeq() const noexcept {
        return static_cast<uint16_t>((data[2] << 8) | data[3]);
    }
};
