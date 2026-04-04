// src/airplay2/AesGcmCipher.cpp
// AES-128-GCM using Windows BCrypt BCRYPT_CHAIN_MODE_GCM.
// Pre-allocated handles — RT-safe on Thread 4.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <cstring>

#include "airplay2/AesGcmCipher.h"
#include "core/Logger.h"

#pragma comment(lib, "Bcrypt.lib")

namespace AirPlay2 {

AesGcmCipher::AesGcmCipher() = default;

AesGcmCipher::~AesGcmCipher()
{
    if (hKey_) { BCryptDestroyKey(hKey_); hKey_ = nullptr; }
    if (hAlg_) { BCryptCloseAlgorithmProvider(hAlg_, 0); hAlg_ = nullptr; }
}

bool AesGcmCipher::Init(const uint8_t* key16, const uint8_t* sessionSalt4)
{
    if (hKey_) { BCryptDestroyKey(hKey_); hKey_ = nullptr; }
    if (hAlg_) { BCryptCloseAlgorithmProvider(hAlg_, 0); hAlg_ = nullptr; }

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &hAlg_, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        LOG_WARN("AesGcmCipher: BCryptOpenAlgorithmProvider failed (0x%08X)", status);
        return false;
    }

    // Set GCM chain mode
    status = BCryptSetProperty(
        hAlg_,
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
        static_cast<ULONG>((wcslen(BCRYPT_CHAIN_MODE_GCM) + 1) * sizeof(wchar_t)),
        0);
    if (!BCRYPT_SUCCESS(status)) {
        LOG_WARN("AesGcmCipher: BCryptSetProperty(GCM) failed (0x%08X)", status);
        BCryptCloseAlgorithmProvider(hAlg_, 0); hAlg_ = nullptr;
        return false;
    }

    // Query key object size
    DWORD keyObjSize = 0, cbData = 0;
    status = BCryptGetProperty(
        hAlg_, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&keyObjSize), sizeof(keyObjSize),
        &cbData, 0);
    if (!BCRYPT_SUCCESS(status) || keyObjSize > sizeof(keyObj_)) {
        BCryptCloseAlgorithmProvider(hAlg_, 0); hAlg_ = nullptr;
        return false;
    }

    // Import key (AES-128 = 16 bytes)
    status = BCryptGenerateSymmetricKey(
        hAlg_, &hKey_,
        keyObj_, keyObjSize,
        const_cast<PUCHAR>(key16), kKeyBytes,
        0);
    if (!BCRYPT_SUCCESS(status)) {
        LOG_WARN("AesGcmCipher: BCryptGenerateSymmetricKey failed (0x%08X)", status);
        BCryptCloseAlgorithmProvider(hAlg_, 0); hAlg_ = nullptr;
        return false;
    }

    std::memcpy(sessionSalt_, sessionSalt4, 4);
    return true;
}

// ── Nonce construction ─────────────────────────────────────────────────────
// Per FR-013: nonce = sessionSalt[4] ‖ rtp_seq_be32[4] ‖ 0x00000000[4]

void AesGcmCipher::BuildNonce(uint32_t rtpSeq, uint8_t* nonce12) const
{
    std::memcpy(nonce12, sessionSalt_, 4);
    // rtpSeq as big-endian 32-bit
    nonce12[4] = static_cast<uint8_t>(rtpSeq >> 24);
    nonce12[5] = static_cast<uint8_t>(rtpSeq >> 16);
    nonce12[6] = static_cast<uint8_t>(rtpSeq >>  8);
    nonce12[7] = static_cast<uint8_t>(rtpSeq);
    std::memset(nonce12 + 8, 0, 4);
}

// ── Encrypt ────────────────────────────────────────────────────────────────

bool AesGcmCipher::Encrypt(uint32_t   rtpSeq,
                            const uint8_t* plaintext,  size_t   plainLen,
                            uint8_t*       ciphertext, size_t*  outLen)
{
    if (!hKey_) return false;

    uint8_t nonce[kNonceBytes];
    BuildNonce(rtpSeq, nonce);

    uint8_t tag[kTagBytes] = {};

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo{};
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce   = nonce;
    authInfo.cbNonce   = kNonceBytes;
    authInfo.pbTag     = tag;
    authInfo.cbTag     = kTagBytes;

    ULONG bytesEncrypted = 0;
    const NTSTATUS status = BCryptEncrypt(
        hKey_,
        const_cast<PUCHAR>(plaintext), static_cast<ULONG>(plainLen),
        &authInfo,
        nullptr, 0,  // no separate IV — nonce is in authInfo
        ciphertext,  static_cast<ULONG>(plainLen),
        &bytesEncrypted, 0);

    if (!BCRYPT_SUCCESS(status)) {
        LOG_WARN("AesGcmCipher::Encrypt failed (0x%08X)", status);
        return false;
    }

    // Append 16-byte auth tag
    std::memcpy(ciphertext + bytesEncrypted, tag, kTagBytes);
    *outLen = bytesEncrypted + kTagBytes;
    return true;
}

// ── Decrypt ────────────────────────────────────────────────────────────────

bool AesGcmCipher::Decrypt(uint32_t   rtpSeq,
                            const uint8_t* ciphertext, size_t   ciphertextLen,
                            uint8_t*       plaintext,  size_t*  outLen)
{
    if (!hKey_) return false;
    if (ciphertextLen < kTagBytes) return false;

    const size_t payloadLen = ciphertextLen - kTagBytes;
    uint8_t nonce[kNonceBytes];
    BuildNonce(rtpSeq, nonce);

    // Auth tag is the last 16 bytes
    uint8_t tag[kTagBytes];
    std::memcpy(tag, ciphertext + payloadLen, kTagBytes);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo{};
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce   = nonce;
    authInfo.cbNonce   = kNonceBytes;
    authInfo.pbTag     = tag;
    authInfo.cbTag     = kTagBytes;

    ULONG bytesDecrypted = 0;
    const NTSTATUS status = BCryptDecrypt(
        hKey_,
        const_cast<PUCHAR>(ciphertext), static_cast<ULONG>(payloadLen),
        &authInfo,
        nullptr, 0,
        plaintext, static_cast<ULONG>(payloadLen),
        &bytesDecrypted, 0);

    if (!BCRYPT_SUCCESS(status)) {
        // GCM auth tag check failure — tampered or stale packet
        return false;
    }

    *outLen = bytesDecrypted;
    return true;
}

} // namespace AirPlay2
