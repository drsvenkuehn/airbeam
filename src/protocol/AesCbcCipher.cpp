#pragma comment(lib, "bcrypt.lib")

#include "protocol/AesCbcCipher.h"
#include <stdexcept>
#include <cassert>
#include <cstring>

AesCbcCipher::AesCbcCipher(const uint8_t key[16], const uint8_t iv[16])
{
    NTSTATUS status;

    status = BCryptOpenAlgorithmProvider(&hAlg_, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(status))
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");

    // Set CBC chaining mode
    status = BCryptSetProperty(
        hAlg_,
        BCRYPT_CHAINING_MODE,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
        static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)),
        0);
    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg_, 0);
        throw std::runtime_error("BCryptSetProperty (chaining mode) failed");
    }

    // Assert key object buffer is large enough (debug builds only)
#ifndef NDEBUG
    ULONG cbKeyObj = 0, cbData = 0;
    BCryptGetProperty(hAlg_, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&cbKeyObj), sizeof(cbKeyObj), &cbData, 0);
    assert(cbKeyObj <= sizeof(keyObj_) && "AES key object exceeds 512-byte stack buffer");
#endif

    status = BCryptGenerateSymmetricKey(
        hAlg_, &hKey_,
        keyObj_, sizeof(keyObj_),
        const_cast<PUCHAR>(key), 16,
        0);
    if (!NT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg_, 0);
        throw std::runtime_error("BCryptGenerateSymmetricKey failed");
    }

    std::memcpy(iv_, iv, 16);
}

AesCbcCipher::~AesCbcCipher()
{
    if (hKey_)  BCryptDestroyKey(hKey_);
    if (hAlg_)  BCryptCloseAlgorithmProvider(hAlg_, 0);
}

bool AesCbcCipher::Encrypt(const uint8_t* in, uint8_t* out, size_t paddedLen) noexcept
{
    // Stack copy of IV so the session IV is never mutated
    uint8_t ivCopy[16];
    std::memcpy(ivCopy, iv_, 16);

    ULONG cbResult = 0;
    NTSTATUS status = BCryptEncrypt(
        hKey_,
        const_cast<PUCHAR>(in),
        static_cast<ULONG>(paddedLen),
        nullptr,
        ivCopy, 16,
        out,
        static_cast<ULONG>(paddedLen),
        &cbResult,
        0);

    return NT_SUCCESS(status);
}
