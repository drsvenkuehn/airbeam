#pragma once
#include <string>
#include <cstdint>

namespace SdpBuilder {
    /// Builds the SDP body for the RAOP ANNOUNCE request.
    /// Parameters:
    ///   clientIP      — dotted-decimal IPv4 of the sender (local)
    ///   receiverIP    — dotted-decimal IPv4 of the receiver
    ///   sessionId     — monotonically increasing 64-bit session ID (NTP timestamp)
    ///   rsaAesKey_b64 — base64-encoded RSA-wrapped AES key (from RsaKeyWrap::Wrap)
    ///   aesIv_b64     — base64-encoded AES IV (16 bytes, base64 encoded)
    std::string Build(
        const std::string& clientIP,
        const std::string& receiverIP,
        uint64_t           sessionId,
        const std::string& rsaAesKey_b64,
        const std::string& aesIv_b64
    );
}
