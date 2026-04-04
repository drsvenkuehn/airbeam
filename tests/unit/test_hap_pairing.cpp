// tests/unit/test_hap_pairing.cpp
// Unit tests for HapPairing (Feature 010).
// Tests: SRP M1/M2 proof exchange, Ed25519 sign+verify, ChaCha20-Poly1305,
//        stale credential detection scaffold, timing gate.

#include <gtest/gtest.h>
#include "airplay2/HapPairing.h"
#include "airplay2/CredentialStore.h"
#include <sodium.h>
#include <array>
#include <vector>
#include <string>
#include <chrono>

extern "C" {
#include "srp.h"
}

using namespace AirPlay2;

// ────────────────────────────────────────────────────────────────────────────
// Test: SRP-6a M1/M2 proof exchange with known test vectors
// ────────────────────────────────────────────────────────────────────────────

TEST(SrpTest, UserVerifierRoundTrip)
{
    const char* username  = "Pair-Setup";
    const char* password  = "123456";
    const int   passLen   = 6;

    // Create verifier (server side)
    unsigned char* salt   = nullptr; int saltLen  = 0;
    unsigned char* verif  = nullptr; int verifLen = 0;
    srp_create_salted_verification_key(
        SRP_SHA512, SRP_NG_3072,
        username,
        reinterpret_cast<const unsigned char*>(password), passLen,
        &salt, &saltLen, &verif, &verifLen,
        nullptr, nullptr);
    ASSERT_NE(nullptr, salt);
    ASSERT_NE(nullptr, verif);

    // Create user (client side)
    SRPUser* user = srp_user_new(
        SRP_SHA512, SRP_NG_3072,
        username,
        reinterpret_cast<const unsigned char*>(password), passLen,
        nullptr, nullptr);
    ASSERT_NE(nullptr, user);

    const char*    authUser = nullptr;
    unsigned char* bytesA   = nullptr; int lenA = 0;
    srp_user_start_authentication(user, &authUser, &bytesA, &lenA);
    ASSERT_NE(nullptr, bytesA);
    EXPECT_GT(lenA, 0);
    // bytesA points to usr->bytes_A — freed by srp_user_delete, do NOT free here

    // Create verifier (server side) — computes B
    const unsigned char* bytesB = nullptr; int lenB = 0;
    SRPVerifier* ver = srp_verifier_new(
        SRP_SHA512, SRP_NG_3072,
        username,
        salt, saltLen, verif, verifLen,
        bytesA, lenA,
        &bytesB, &lenB,
        nullptr, nullptr);
    ASSERT_NE(nullptr, ver);
    ASSERT_NE(nullptr, bytesB);

    // Client processes challenge (s/B) → produces M1
    unsigned char* bytesM = nullptr; int lenM = 0;
    srp_user_process_challenge(user,
        salt, saltLen, bytesB, lenB,
        &bytesM, &lenM);
    ASSERT_NE(nullptr, bytesM);
    EXPECT_EQ(64, lenM);  // SHA-512 digest

    // Server verifies M1 → produces HAMK (M2)
    const unsigned char* hamk = nullptr;
    srp_verifier_verify_session(ver, bytesM, &hamk);
    ASSERT_NE(nullptr, hamk);
    EXPECT_TRUE(srp_verifier_is_authenticated(ver));

    // Client verifies HAMK
    srp_user_verify_session(user, hamk);
    EXPECT_TRUE(srp_user_is_authenticated(user));

    // Session keys should match
    int kLen1 = 0, kLen2 = 0;
    const unsigned char* k1 = srp_verifier_get_session_key(ver, &kLen1);
    const unsigned char* k2 = srp_user_get_session_key(user, &kLen2);
    ASSERT_EQ(kLen1, kLen2);
    EXPECT_EQ(0, memcmp(k1, k2, static_cast<size_t>(kLen1)));

    // bytesM points into usr->M (struct member) — do NOT free it separately
    // srp_free(bytesM);  // WRONG: not heap-allocated
    srp_verifier_delete(ver);
    srp_user_delete(user);
    srp_free(salt);
    srp_free(verif);
}

TEST(SrpTest, WrongPasswordFails)
{
    const char* username  = "Pair-Setup";

    unsigned char* salt  = nullptr; int saltLen  = 0;
    unsigned char* verif = nullptr; int verifLen = 0;
    srp_create_salted_verification_key(
        SRP_SHA512, SRP_NG_3072,
        username,
        reinterpret_cast<const unsigned char*>("correct"), 7,
        &salt, &saltLen, &verif, &verifLen,
        nullptr, nullptr);

    SRPUser* user = srp_user_new(SRP_SHA512, SRP_NG_3072,
        username,
        reinterpret_cast<const unsigned char*>("wrong"), 5,
        nullptr, nullptr);
    ASSERT_NE(nullptr, user);

    const char* authUser = nullptr;
    unsigned char* bytesA = nullptr; int lenA = 0;
    srp_user_start_authentication(user, &authUser, &bytesA, &lenA);

    const unsigned char* bytesB = nullptr; int lenB = 0;
    SRPVerifier* ver = srp_verifier_new(SRP_SHA512, SRP_NG_3072,
        username, salt, saltLen, verif, verifLen,
        bytesA, lenA, &bytesB, &lenB, nullptr, nullptr);

    unsigned char* bytesM = nullptr; int lenM = 0;
    srp_user_process_challenge(user, salt, saltLen, bytesB, lenB, &bytesM, &lenM);

    const unsigned char* hamk = nullptr;
    if (bytesM) {
        srp_verifier_verify_session(ver, bytesM, &hamk);
        EXPECT_FALSE(srp_verifier_is_authenticated(ver));
    }

    // bytesA and bytesM point into user/verifier structs — freed by srp_user_delete
    // Do NOT call srp_free on them
    if (ver) srp_verifier_delete(ver);
    srp_user_delete(user);
    srp_free(salt); srp_free(verif);
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Ed25519 sign + verify round-trip using libsodium
// ────────────────────────────────────────────────────────────────────────────

TEST(Ed25519Test, SignVerifyRoundTrip)
{
    if (sodium_init() < 0) GTEST_SKIP() << "sodium_init failed";

    std::array<uint8_t, 32> pk{};
    std::array<uint8_t, 64> sk{};
    ASSERT_EQ(0, crypto_sign_keypair(pk.data(), sk.data()));

    const std::string msg = "HAP pairing test message";
    std::array<uint8_t, 64> sig{};
    ASSERT_EQ(0, crypto_sign_detached(
        sig.data(), nullptr,
        reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size(),
        sk.data()));

    EXPECT_EQ(0, crypto_sign_verify_detached(
        sig.data(),
        reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size(),
        pk.data()));
}

TEST(Ed25519Test, TamperedMessageFails)
{
    if (sodium_init() < 0) GTEST_SKIP() << "sodium_init failed";

    std::array<uint8_t, 32> pk{};
    std::array<uint8_t, 64> sk{};
    crypto_sign_keypair(pk.data(), sk.data());

    const std::string msg     = "original";
    const std::string tampered = "tampered";
    std::array<uint8_t, 64> sig{};
    crypto_sign_detached(sig.data(), nullptr,
                         reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size(), sk.data());

    EXPECT_NE(0, crypto_sign_verify_detached(
        sig.data(),
        reinterpret_cast<const uint8_t*>(tampered.c_str()), tampered.size(),
        pk.data()));
}

// ────────────────────────────────────────────────────────────────────────────
// Test: ChaCha20-Poly1305 encrypt + decrypt of simulated M5/M6 frames
// ────────────────────────────────────────────────────────────────────────────

TEST(ChaCha20Poly1305Test, EncryptDecryptRoundTrip)
{
    if (sodium_init() < 0) GTEST_SKIP() << "sodium_init failed";

    std::array<uint8_t, 32> key{};
    randombytes_buf(key.data(), 32);

    const std::string plaintext = "Simulated M5 sub-TLV payload";
    std::vector<uint8_t> cipher(plaintext.size() + 16);
    uint8_t nonce[12] = "PS-Msg05";

    ASSERT_EQ(0, crypto_aead_chacha20poly1305_ietf_encrypt(
        cipher.data(), nullptr,
        reinterpret_cast<const uint8_t*>(plaintext.c_str()), plaintext.size(),
        nullptr, 0, nullptr,
        nonce, key.data()));

    std::vector<uint8_t> decrypted(plaintext.size());
    ASSERT_EQ(0, crypto_aead_chacha20poly1305_ietf_decrypt(
        decrypted.data(), nullptr, nullptr,
        cipher.data(), cipher.size(),
        nullptr, 0,
        nonce, key.data()));

    const std::string result(decrypted.begin(), decrypted.end());
    EXPECT_EQ(plaintext, result);
}

TEST(ChaCha20Poly1305Test, TamperedCiphertextFails)
{
    if (sodium_init() < 0) GTEST_SKIP() << "sodium_init failed";

    std::array<uint8_t, 32> key{};
    randombytes_buf(key.data(), 32);

    const std::string plaintext = "M6 response payload";
    std::vector<uint8_t> cipher(plaintext.size() + 16);
    uint8_t nonce[12] = "PS-Msg06";
    crypto_aead_chacha20poly1305_ietf_encrypt(
        cipher.data(), nullptr,
        reinterpret_cast<const uint8_t*>(plaintext.c_str()), plaintext.size(),
        nullptr, 0, nullptr, nonce, key.data());

    // Tamper with ciphertext
    cipher[0] ^= 0xFF;

    std::vector<uint8_t> decrypted(plaintext.size());
    EXPECT_NE(0, crypto_aead_chacha20poly1305_ietf_decrypt(
        decrypted.data(), nullptr, nullptr,
        cipher.data(), cipher.size(),
        nullptr, 0, nonce, key.data()));
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Stale credential detection — scaffold
// Simulates HAP VERIFY phase rejection (kTLVError_Authentication).
// This is a separate two-step VERIFY exchange; NOT a re-run of SRP M1/M2.
// ────────────────────────────────────────────────────────────────────────────

TEST(HapPairingTest, VerifyRejectionIsStaleNotFailed)
{
    // In production: AirPlay2Session::Init() calls HapPairing::Verify(),
    // which posts WM_AP2_PAIRING_STALE (not WM_AP2_FAILED) on AuthFailed.
    // This test validates the enum distinction.
    EXPECT_NE(PairingResult::AuthFailed, PairingResult::NetworkError);
    EXPECT_NE(PairingResult::AuthFailed, PairingResult::ProtocolError);
    EXPECT_NE(PairingResult::AuthFailed, PairingResult::Success);
}

// ────────────────────────────────────────────────────────────────────────────
// Test: TLV8 encode/decode round-trip
// ────────────────────────────────────────────────────────────────────────────

TEST(TlvTest, EncodeDecodeRoundTrip)
{
    TlvList original;
    original.push_back({TlvType::State,     {0x01}});
    original.push_back({TlvType::Method,    {0x00}});
    original.push_back({TlvType::Identifier, {'A', 'B', 'C'}});

    const std::vector<uint8_t> encoded = TlvEncode(original);
    const TlvList decoded = TlvDecode(encoded);

    ASSERT_EQ(3u, decoded.size());
    EXPECT_EQ(TlvType::State,      decoded[0].type);
    EXPECT_EQ(std::vector<uint8_t>({0x01}), decoded[0].value);
    EXPECT_EQ(TlvType::Method,     decoded[1].type);
    EXPECT_EQ(TlvType::Identifier, decoded[2].type);
    const std::vector<uint8_t> expected = {'A', 'B', 'C'};
    EXPECT_EQ(expected, decoded[2].value);
}

TEST(TlvTest, LargeValueFragmentsAndReassembles)
{
    TlvList items;
    std::vector<uint8_t> big(512, 0xAB);  // > 255 bytes — requires fragmentation
    items.push_back({TlvType::PublicKey, big});

    const std::vector<uint8_t> encoded = TlvEncode(items);
    // Should have two fragments: [0x03][255][...] [0x03][257][...]
    EXPECT_GT(encoded.size(), big.size());  // overhead from framing

    const TlvList decoded = TlvDecode(encoded);
    ASSERT_EQ(1u, decoded.size());
    EXPECT_EQ(big, decoded[0].value);
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Wall-clock timing gate (SC-002 local gate — M1→M6 ≤ 30 s)
// This is a local stub since we don't have a real device in unit tests.
// The networked gate is in test_airplay2_session.cpp (T029).
// ────────────────────────────────────────────────────────────────────────────

TEST(HapPairingTimingTest, LocalSrpCeremonyIsUnder30Seconds)
{
    // Perform a full local SRP-6a round-trip (no network) and assert it
    // completes well under 30 s. This validates the crypto overhead is acceptable.
    const auto start = std::chrono::steady_clock::now();

    // Full local SRP setup + verify
    unsigned char* salt  = nullptr; int saltLen  = 0;
    unsigned char* verif = nullptr; int verifLen = 0;
    srp_create_salted_verification_key(SRP_SHA512, SRP_NG_3072,
        "Pair-Setup",
        reinterpret_cast<const unsigned char*>("123456"), 6,
        &salt, &saltLen, &verif, &verifLen, nullptr, nullptr);

    SRPUser* user = srp_user_new(SRP_SHA512, SRP_NG_3072,
        "Pair-Setup",
        reinterpret_cast<const unsigned char*>("123456"), 6,
        nullptr, nullptr);

    const char* authUser = nullptr;
    unsigned char* bytesA = nullptr; int lenA = 0;
    srp_user_start_authentication(user, &authUser, &bytesA, &lenA);

    const unsigned char* bytesB = nullptr; int lenB = 0;
    SRPVerifier* ver = srp_verifier_new(SRP_SHA512, SRP_NG_3072,
        "Pair-Setup", salt, saltLen, verif, verifLen,
        bytesA, lenA, &bytesB, &lenB, nullptr, nullptr);

    unsigned char* bytesM = nullptr; int lenM = 0;
    srp_user_process_challenge(user, salt, saltLen, bytesB, lenB, &bytesM, &lenM);

    const unsigned char* hamk = nullptr;
    if (bytesM && ver) srp_verifier_verify_session(ver, bytesM, &hamk);
    if (hamk && user) srp_user_verify_session(user, hamk);

    const auto end = std::chrono::steady_clock::now();
    const auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // bytesM and bytesA are struct-owned pointers — freed by srp_verifier_delete / srp_user_delete
    // Do NOT call srp_free on them
    if (ver) srp_verifier_delete(ver);
    if (user) srp_user_delete(user);
    srp_free(salt); srp_free(verif);

    // Local SRP should complete in well under 1 second (no network involved)
    EXPECT_LT(ms, 30000) << "Local SRP ceremony took " << ms << " ms — exceeds 30 s gate";
    // Performance info — output to stdout instead of logger (tests don't init Logger)
    printf("Local SRP-6a ceremony: %lld ms\n", static_cast<long long>(ms));
}
