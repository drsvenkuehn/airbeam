// tests/unit/test_credential_store.cpp
// Unit tests for CredentialStore (Feature 010).
// Tests: write/read/delete round-trip, controller identity stability,
//        DeviceIdFromPublicKey produces 12-char hex.

#include <gtest/gtest.h>
#include "airplay2/CredentialStore.h"
#include <sodium.h>
#include <array>
#include <string>
#include <optional>

using namespace AirPlay2;

// Test device ID — use a unique prefix to avoid polluting real credentials
static const std::string kTestDeviceId = "TESTAB1234EF";

// ────────────────────────────────────────────────────────────────────────────
// Helper: create a dummy PairingCredential
// ────────────────────────────────────────────────────────────────────────────

static PairingCredential MakeDummyCred()
{
    if (sodium_init() < 0)
        throw std::runtime_error("sodium_init failed");

    PairingCredential cred;
    cred.controllerId = "test-controller-uuid-1234";
    cred.deviceName   = L"Test HomePod";

    // Generate random keys
    crypto_sign_keypair(cred.controllerLtpk.data(), cred.controllerLtsk.data());

    // Generate a fake device key
    std::array<uint8_t, 32> dpk{};
    std::array<uint8_t, 64> dsk{};
    crypto_sign_keypair(dpk.data(), dsk.data());
    cred.deviceLtpk = dpk;

    return cred;
}

// ────────────────────────────────────────────────────────────────────────────
// Fixture: clean up test credential before/after each test
// ────────────────────────────────────────────────────────────────────────────

class CredentialStoreTest : public ::testing::Test {
protected:
    void SetUp() override    { CredentialStore::Delete(kTestDeviceId); }
    void TearDown() override { CredentialStore::Delete(kTestDeviceId); }
};

// ────────────────────────────────────────────────────────────────────────────
// Test: Read returns nullopt when no credential stored
// ────────────────────────────────────────────────────────────────────────────

TEST_F(CredentialStoreTest, ReadReturnsNulloptWhenNotFound)
{
    const auto result = CredentialStore::Read(kTestDeviceId);
    EXPECT_FALSE(result.has_value());
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Write then Read round-trip
// ────────────────────────────────────────────────────────────────────────────

TEST_F(CredentialStoreTest, WriteReadRoundTrip)
{
    const PairingCredential orig = MakeDummyCred();
    ASSERT_TRUE(CredentialStore::Write(kTestDeviceId, orig));

    const auto result = CredentialStore::Read(kTestDeviceId);
    ASSERT_TRUE(result.has_value());

    const PairingCredential& loaded = *result;
    EXPECT_EQ(loaded.controllerId,    orig.controllerId);
    EXPECT_EQ(loaded.deviceName,      orig.deviceName);
    EXPECT_EQ(loaded.controllerLtpk,  orig.controllerLtpk);
    EXPECT_EQ(loaded.controllerLtsk,  orig.controllerLtsk);
    EXPECT_EQ(loaded.deviceLtpk,      orig.deviceLtpk);
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Delete removes credential
// ────────────────────────────────────────────────────────────────────────────

TEST_F(CredentialStoreTest, DeleteRemovesCredential)
{
    ASSERT_TRUE(CredentialStore::Write(kTestDeviceId, MakeDummyCred()));
    ASSERT_TRUE(CredentialStore::Read(kTestDeviceId).has_value());

    CredentialStore::Delete(kTestDeviceId);

    EXPECT_FALSE(CredentialStore::Read(kTestDeviceId).has_value());
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Delete is idempotent (no-op if not found)
// ────────────────────────────────────────────────────────────────────────────

TEST_F(CredentialStoreTest, DeleteIdempotent)
{
    CredentialStore::Delete(kTestDeviceId);  // no credential written — should not crash
    CredentialStore::Delete(kTestDeviceId);  // second call also safe
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Overwrite credential
// ────────────────────────────────────────────────────────────────────────────

TEST_F(CredentialStoreTest, OverwriteCredential)
{
    const PairingCredential first = MakeDummyCred();
    ASSERT_TRUE(CredentialStore::Write(kTestDeviceId, first));

    PairingCredential second = MakeDummyCred();
    second.deviceName = L"Updated HomePod";
    ASSERT_TRUE(CredentialStore::Write(kTestDeviceId, second));

    const auto result = CredentialStore::Read(kTestDeviceId);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->deviceName, L"Updated HomePod");
}

// ────────────────────────────────────────────────────────────────────────────
// Test: EnsureControllerIdentity is stable across multiple calls
// ────────────────────────────────────────────────────────────────────────────

TEST(ControllerIdentityTest, StableAcrossMultipleCalls)
{
    const ControllerIdentity id1 = CredentialStore::EnsureControllerIdentity();
    const ControllerIdentity id2 = CredentialStore::EnsureControllerIdentity();
    const ControllerIdentity id3 = CredentialStore::EnsureControllerIdentity();

    EXPECT_EQ(id1.controllerId, id2.controllerId);
    EXPECT_EQ(id2.controllerId, id3.controllerId);
    EXPECT_EQ(id1.ltpk, id2.ltpk);
    EXPECT_EQ(id2.ltpk, id3.ltpk);
    EXPECT_EQ(id1.ltsk, id2.ltsk);
    EXPECT_EQ(id2.ltsk, id3.ltsk);
}

// ────────────────────────────────────────────────────────────────────────────
// Test: EnsureControllerIdentity generates valid Ed25519 keypair
// ────────────────────────────────────────────────────────────────────────────

TEST(ControllerIdentityTest, ValidEd25519Keypair)
{
    if (sodium_init() < 0) GTEST_SKIP() << "sodium_init failed";

    const ControllerIdentity id = CredentialStore::EnsureControllerIdentity();

    // Sign a test message and verify with the public key
    const std::string msg = "AirBeam controller identity test";
    std::array<uint8_t, 64 + 32> sig{};
    unsigned long long sigLen = 0;
    ASSERT_EQ(0, crypto_sign(sig.data(), &sigLen,
                             reinterpret_cast<const uint8_t*>(msg.c_str()), msg.size(),
                             id.ltsk.data()));

    std::vector<uint8_t> opened(msg.size());
    unsigned long long openedLen = 0;
    EXPECT_EQ(0, crypto_sign_open(opened.data(), &openedLen,
                                   sig.data(), sigLen,
                                   id.ltpk.data()));
}

// ────────────────────────────────────────────────────────────────────────────
// Test: DeviceIdFromPublicKey produces 12-char uppercase hex
// ────────────────────────────────────────────────────────────────────────────

TEST(DeviceIdFromPublicKeyTest, Produces12CharHex)
{
    if (sodium_init() < 0) GTEST_SKIP() << "sodium_init failed";

    // Generate a random Ed25519 public key and base64-encode it
    std::array<uint8_t, 32> pk{};
    std::array<uint8_t, 64> sk{};
    crypto_sign_keypair(pk.data(), sk.data());

    // Base64 encode (reuse the same logic as CredentialStore)
    // For testing, just use a known test vector
    // base64("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=") — 32 zero bytes
    const std::string b64 = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
    const std::string deviceId = CredentialStore::DeviceIdFromPublicKey(b64);

    ASSERT_EQ(12u, deviceId.size());
    for (char c : deviceId) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))
            << "Non-hex character: " << c;
    }
}

TEST(DeviceIdFromPublicKeyTest, EmptyInputReturnsEmpty)
{
    const std::string result = CredentialStore::DeviceIdFromPublicKey("");
    EXPECT_TRUE(result.empty());
}

TEST(DeviceIdFromPublicKeyTest, DifferentKeysProduceDifferentIds)
{
    if (sodium_init() < 0) GTEST_SKIP() << "sodium_init failed";

    std::array<uint8_t, 32> pk1{}, pk2{};
    std::array<uint8_t, 64> sk1{}, sk2{};
    crypto_sign_keypair(pk1.data(), sk1.data());
    crypto_sign_keypair(pk2.data(), sk2.data());

    // Simple base64 encode for test
    auto b64enc = [](const uint8_t* d, size_t n) {
        static const char* T =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string r;
        for (size_t i = 0; i < n; i += 3) {
            uint32_t v = (uint32_t)d[i] << 16;
            if (i+1<n) v |= (uint32_t)d[i+1] << 8;
            if (i+2<n) v |= d[i+2];
            r += T[(v>>18)&63]; r += T[(v>>12)&63];
            r += (i+1<n)?T[(v>>6)&63]:'=';
            r += (i+2<n)?T[v&63]:'=';
        }
        return r;
    };

    const std::string id1 = CredentialStore::DeviceIdFromPublicKey(b64enc(pk1.data(), 32));
    const std::string id2 = CredentialStore::DeviceIdFromPublicKey(b64enc(pk2.data(), 32));
    EXPECT_NE(id1, id2);
}
