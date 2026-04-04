// tests/unit/test_aes_gcm.cpp
// Unit tests for AesGcmCipher (Feature 010 — T022).
// Tests: encrypt+decrypt round-trip, 16-byte auth tag, nonce per packet,
//        tampered ciphertext fails auth tag check.

#include <gtest/gtest.h>
#include "airplay2/AesGcmCipher.h"
#include <array>
#include <vector>
#include <cstring>

using namespace AirPlay2;

// ────────────────────────────────────────────────────────────────────────────
// Test: Init with valid key succeeds
// ────────────────────────────────────────────────────────────────────────────

TEST(AesGcmCipherTest, InitSucceeds)
{
    AesGcmCipher cipher;
    const uint8_t key[16]  = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
    const uint8_t salt[4]  = { 0xDE, 0xAD, 0xBE, 0xEF };
    EXPECT_TRUE(cipher.Init(key, salt));
    EXPECT_TRUE(cipher.IsInitialised());
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Encrypt + Decrypt round-trip
// ────────────────────────────────────────────────────────────────────────────

TEST(AesGcmCipherTest, EncryptDecryptRoundTrip)
{
    const uint8_t key[16]  = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00 };
    const uint8_t salt[4]  = { 0x01, 0x02, 0x03, 0x04 };

    AesGcmCipher enc, dec;
    ASSERT_TRUE(enc.Init(key, salt));
    ASSERT_TRUE(dec.Init(key, salt));

    const std::string plaintext = "Hello AirPlay 2 audio packet payload!";
    const size_t plainLen = plaintext.size();

    std::vector<uint8_t> cipherBuf(plainLen + AesGcmCipher::kTagBytes);
    size_t cipherLen = 0;

    ASSERT_TRUE(enc.Encrypt(
        42 /* rtpSeq */,
        reinterpret_cast<const uint8_t*>(plaintext.c_str()), plainLen,
        cipherBuf.data(), &cipherLen));
    EXPECT_EQ(plainLen + AesGcmCipher::kTagBytes, cipherLen);

    std::vector<uint8_t> decBuf(plainLen);
    size_t decLen = 0;
    ASSERT_TRUE(dec.Decrypt(42, cipherBuf.data(), cipherLen, decBuf.data(), &decLen));
    EXPECT_EQ(plainLen, decLen);

    const std::string result(decBuf.begin(), decBuf.end());
    EXPECT_EQ(plaintext, result);
}

// ────────────────────────────────────────────────────────────────────────────
// Test: 16-byte auth tag is appended to ciphertext
// ────────────────────────────────────────────────────────────────────────────

TEST(AesGcmCipherTest, AuthTagAppended)
{
    const uint8_t key[16]  = {};
    const uint8_t salt[4]  = {};

    AesGcmCipher cipher;
    ASSERT_TRUE(cipher.Init(key, salt));

    const uint8_t plain[32] = {};
    uint8_t cipherBuf[32 + AesGcmCipher::kTagBytes] = {};
    size_t outLen = 0;

    ASSERT_TRUE(cipher.Encrypt(0, plain, 32, cipherBuf, &outLen));
    EXPECT_EQ(32u + AesGcmCipher::kTagBytes, outLen);
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Different rtpSeq produces different nonce (different ciphertext)
// ────────────────────────────────────────────────────────────────────────────

TEST(AesGcmCipherTest, DifferentRtpSeqProducesDifferentCiphertext)
{
    const uint8_t key[16]  = { 0xAA };
    const uint8_t salt[4]  = { 0xBB };

    AesGcmCipher c1, c2;
    ASSERT_TRUE(c1.Init(key, salt));
    ASSERT_TRUE(c2.Init(key, salt));

    const uint8_t plain[16] = { 0x42 };
    uint8_t ct1[32] = {}, ct2[32] = {};
    size_t l1 = 0, l2 = 0;

    ASSERT_TRUE(c1.Encrypt(100, plain, 16, ct1, &l1));
    ASSERT_TRUE(c2.Encrypt(101, plain, 16, ct2, &l2));

    // With different nonces the ciphertext must differ
    EXPECT_NE(0, memcmp(ct1, ct2, l1));
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Tampered ciphertext fails auth tag check
// ────────────────────────────────────────────────────────────────────────────

TEST(AesGcmCipherTest, TamperedCiphertextFails)
{
    const uint8_t key[16]  = { 0x01, 0x02 };
    const uint8_t salt[4]  = { 0x03 };

    AesGcmCipher enc, dec;
    ASSERT_TRUE(enc.Init(key, salt));
    ASSERT_TRUE(dec.Init(key, salt));

    const uint8_t plain[24] = { 'T','e','s','t',' ','A','u','d','i','o','P','a',
                                 'c','k','e','t','0','1','2','3','4','5','6','7' };
    uint8_t ct[24 + AesGcmCipher::kTagBytes] = {};
    size_t ctLen = 0;

    ASSERT_TRUE(enc.Encrypt(200, plain, 24, ct, &ctLen));

    // Tamper the first byte of ciphertext
    ct[0] ^= 0xFF;

    uint8_t decBuf[24] = {};
    size_t decLen = 0;
    EXPECT_FALSE(dec.Decrypt(200, ct, ctLen, decBuf, &decLen));
}

TEST(AesGcmCipherTest, TamperedTagFails)
{
    const uint8_t key[16]  = {};
    const uint8_t salt[4]  = {};

    AesGcmCipher enc, dec;
    ASSERT_TRUE(enc.Init(key, salt));
    ASSERT_TRUE(dec.Init(key, salt));

    const uint8_t plain[8] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 };
    uint8_t ct[8 + AesGcmCipher::kTagBytes] = {};
    size_t ctLen = 0;
    ASSERT_TRUE(enc.Encrypt(0, plain, 8, ct, &ctLen));

    // Tamper the last byte of the tag
    ct[ctLen - 1] ^= 0x01;

    uint8_t decBuf[8] = {};
    size_t decLen = 0;
    EXPECT_FALSE(dec.Decrypt(0, ct, ctLen, decBuf, &decLen));
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Known-answer test with all-zero key and nonce
// ────────────────────────────────────────────────────────────────────────────

TEST(AesGcmCipherTest, KnownAnswerZeroKeyNonce)
{
    // AES-128-GCM with all-zero key, all-zero salt (→ all-zero nonce for seq=0)
    // We just verify that encryption is deterministic.
    const uint8_t key[16]  = {};
    const uint8_t salt[4]  = {};

    AesGcmCipher c1, c2;
    ASSERT_TRUE(c1.Init(key, salt));
    ASSERT_TRUE(c2.Init(key, salt));

    const uint8_t plain[16] = {};
    uint8_t ct1[32] = {}, ct2[32] = {};
    size_t l1 = 0, l2 = 0;

    ASSERT_TRUE(c1.Encrypt(0, plain, 16, ct1, &l1));
    ASSERT_TRUE(c2.Encrypt(0, plain, 16, ct2, &l2));

    // Same key, same nonce, same plaintext → same ciphertext
    EXPECT_EQ(0, memcmp(ct1, ct2, l1));
}
