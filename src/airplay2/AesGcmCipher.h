#pragma once
// src/airplay2/AesGcmCipher.h — AES-128-GCM encrypt/decrypt using Windows BCrypt.
// Per FR-013: per-packet nonce = session_salt[4] ‖ rtp_seq[4] ‖ 0x00000000[4] (12 bytes).
// Pre-allocated BCrypt handles in ctor for RT-safe use on Thread 4 (§I).
// No heap allocation on hot path.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <cstddef>

#pragma comment(lib, "Bcrypt.lib")

namespace AirPlay2 {

/// AES-128-GCM cipher wrapper using Windows BCrypt API.
/// Designed for RT-safe use on Thread 4 — no heap allocations on the hot path.
/// The 16-byte auth tag is appended to the ciphertext buffer.
class AesGcmCipher {
public:
    static constexpr size_t kKeyBytes  = 16;   ///< AES-128
    static constexpr size_t kTagBytes  = 16;   ///< GCM auth tag
    static constexpr size_t kNonceBytes = 12;  ///< GCM nonce (96-bit)

    AesGcmCipher();
    ~AesGcmCipher();

    AesGcmCipher(const AesGcmCipher&)            = delete;
    AesGcmCipher& operator=(const AesGcmCipher&) = delete;

    /// Initialise with a 16-byte session key and 4-byte session salt.
    /// @returns true on success (BCrypt handle acquired).
    bool Init(const uint8_t* key16, const uint8_t* sessionSalt4);

    /// Encrypt in-place.  Output buffer must have room for plainLen + kTagBytes bytes.
    /// Per-packet nonce = sessionSalt_[0..3] ‖ rtpSeq32 ‖ 0x00000000
    /// @returns true on success.
    bool Encrypt(uint32_t   rtpSeq,
                 const uint8_t* plaintext,  size_t   plainLen,
                 uint8_t*       ciphertext, size_t*  outLen);

    /// Decrypt ciphertext + kTagBytes auth tag.
    /// @param ciphertextLen includes the 16-byte tag (ciphertextLen = payload + kTagBytes).
    bool Decrypt(uint32_t   rtpSeq,
                 const uint8_t* ciphertext, size_t   ciphertextLen,
                 uint8_t*       plaintext,  size_t*  outLen);

    bool IsInitialised() const { return hKey_ != nullptr; }

private:
    void BuildNonce(uint32_t rtpSeq, uint8_t* nonce12) const;

    BCRYPT_ALG_HANDLE hAlg_  = nullptr;
    BCRYPT_KEY_HANDLE hKey_  = nullptr;
    // Key object storage (avoids heap alloc; BCrypt requires a persistent buffer)
    // BCrypt AES-128-GCM key object requires up to ~3262 bytes in debug builds on x64.
    // Use 4096 to be safe across all Windows versions.
    alignas(16) uint8_t keyObj_[4096] = {};
    uint8_t sessionSalt_[4] = {};
};

} // namespace AirPlay2
