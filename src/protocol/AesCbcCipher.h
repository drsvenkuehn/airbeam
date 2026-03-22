#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif
#include <cstdint>
#include <cstddef>

/// AES-128-CBC cipher for one RAOP session.
/// Constructed with the 16-byte session key; the session IV is constant per
/// AirPlay 1 spec. Each packet encryption copies the IV before calling BCrypt
/// so the original IV is never mutated.
class AesCbcCipher {
public:
    /// Initializes BCrypt algorithm provider and imports the session key.
    /// Throws std::runtime_error if BCrypt setup fails (only during session init,
    /// not on the hot path).
    explicit AesCbcCipher(const uint8_t key[16], const uint8_t iv[16]);
    ~AesCbcCipher();

    AesCbcCipher(const AesCbcCipher&)            = delete;
    AesCbcCipher& operator=(const AesCbcCipher&) = delete;

    /// Encrypts `paddedLen` bytes in-place (in == out is allowed).
    /// `paddedLen` MUST be a multiple of 16 (AES block size).
    /// Uses a copy of the stored IV so the session IV is never mutated.
    /// ZERO heap alloc on the hot path (IV copy is on the stack).
    bool Encrypt(const uint8_t* in, uint8_t* out, size_t paddedLen) noexcept;

private:
    BCRYPT_ALG_HANDLE  hAlg_        = nullptr;
    BCRYPT_KEY_HANDLE  hKey_        = nullptr;
    uint8_t            iv_[16]      = {};    // session IV (never mutated)
    uint8_t            keyObj_[1024] = {};   // BCrypt key object; AES-CBC needs ~656 bytes on Windows
};
