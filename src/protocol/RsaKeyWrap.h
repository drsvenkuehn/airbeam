#pragma once
#include <cstdint>
#include <string>

namespace RsaKeyWrap {
    /// Encrypts the 16-byte AES session key with the AirPlay 1 RSA-2048
    /// public key (PKCS#1 v1.5 padding) and returns the result as standard
    /// base64 (with +/= characters) as required by the AirPlay 1 ANNOUNCE SDP.
    /// Called once per RAOP session on Thread 5 (never on the RT audio path).
    /// Returns empty string on error.
    std::string Wrap(const uint8_t key[16]);
}
