// T026 — AES-128-CBC NIST Known-Answer-Test vectors
#include <gtest/gtest.h>
#include "protocol/AesCbcCipher.h"
#include <windows.h>

#include <array>
#include <cstring>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Convert a hex string (no spaces, even length) into a byte array.
template<size_t N>
static std::array<uint8_t, N> FromHex(const char* hex)
{
    std::array<uint8_t, N> out{};
    for (size_t i = 0; i < N; ++i) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            return 0;
        };
        out[i] = static_cast<uint8_t>((nibble(hex[i * 2]) << 4) | nibble(hex[i * 2 + 1]));
    }
    return out;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

struct KatVector {
    const char* keyHex;
    const char* ivHex;
    const char* ptHex;
    const char* ctHex;
};

// NIST AES-128-CBC encrypt vectors (CAVS)
static constexpr KatVector kVectors[3] = {
    {
        "2b7e151628aed2a6abf7158809cf4f3c",
        "000102030405060708090a0b0c0d0e0f",   // NIST SP 800-38A F.2.1 IV
        "6bc1bee22e409f96e93d7e117393172a",
        "7649abac8119b246cee98e9b12e9197d"
    },
    {
        "2b7e151628aed2a6abf7158809cf4f3c",
        "7649abac8119b246cee98e9b12e9197d",
        "ae2d8a571e03ac9c9eb76fac45af8e51",
        "5086cb9b507219ee95db113a917678b2"
    },
    {
        "2b7e151628aed2a6abf7158809cf4f3c",
        "5086cb9b507219ee95db113a917678b2",
        "30c81c46a35ce411e5fbc1191a0a52ef",
        "73bed6b8e3c1743b7116e69e22229516"
    }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(AesCbcCipher, Vector1_Encrypt)
{
    const auto& v  = kVectors[0];
    auto key = FromHex<16>(v.keyHex);
    auto iv  = FromHex<16>(v.ivHex);
    auto pt  = FromHex<16>(v.ptHex);
    auto expected = FromHex<16>(v.ctHex);

    AesCbcCipher cipher(key.data(), iv.data());
    std::array<uint8_t, 16> ct{};
    cipher.Encrypt(pt.data(), ct.data(), 16);

    EXPECT_EQ(ct, expected);
}

TEST(AesCbcCipher, Vector2_Encrypt)
{
    const auto& v  = kVectors[1];
    auto key = FromHex<16>(v.keyHex);
    auto iv  = FromHex<16>(v.ivHex);
    auto pt  = FromHex<16>(v.ptHex);
    auto expected = FromHex<16>(v.ctHex);

    AesCbcCipher cipher(key.data(), iv.data());
    std::array<uint8_t, 16> ct{};
    cipher.Encrypt(pt.data(), ct.data(), 16);

    EXPECT_EQ(ct, expected);
}

TEST(AesCbcCipher, Vector3_Encrypt)
{
    const auto& v  = kVectors[2];
    auto key = FromHex<16>(v.keyHex);
    auto iv  = FromHex<16>(v.ivHex);
    auto pt  = FromHex<16>(v.ptHex);
    auto expected = FromHex<16>(v.ctHex);

    AesCbcCipher cipher(key.data(), iv.data());
    std::array<uint8_t, 16> ct{};
    cipher.Encrypt(pt.data(), ct.data(), 16);

    EXPECT_EQ(ct, expected);
}

// Verify that the stored IV is not mutated between calls (two independent
// encryptions with separate cipher objects each produce the correct result).
TEST(AesCbcCipher, IvIsStateless)
{
    const auto& v0 = kVectors[0];
    const auto& v1 = kVectors[1];

    auto key  = FromHex<16>(v0.keyHex);
    auto iv0  = FromHex<16>(v0.ivHex);
    auto iv1  = FromHex<16>(v1.ivHex);
    auto pt0  = FromHex<16>(v0.ptHex);
    auto pt1  = FromHex<16>(v1.ptHex);
    auto exp0 = FromHex<16>(v0.ctHex);
    auto exp1 = FromHex<16>(v1.ctHex);

    // Each cipher owns its IV; encrypt in reverse order to verify no leakage.
    AesCbcCipher cipher0(key.data(), iv0.data());
    AesCbcCipher cipher1(key.data(), iv1.data());

    std::array<uint8_t, 16> ct1{}, ct0{};
    cipher1.Encrypt(pt1.data(), ct1.data(), 16);
    cipher0.Encrypt(pt0.data(), ct0.data(), 16);

    EXPECT_EQ(ct0, exp0);
    EXPECT_EQ(ct1, exp1);
}
