#pragma once
// src/airplay2/CredentialStore.h — Windows Credential Manager schema for AirPlay 2 pairing.
// Contract: specs/010-airplay2-support/contracts/credential-store.md v1.0
//
// All blob storage uses CredWriteW / CredReadW (advapi32.lib).
// Blobs are UTF-8 JSON; Windows applies DPAPI encryption automatically.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <array>
#include <optional>
#include <cstdint>

namespace AirPlay2 {

// ────────────────────────────────────────────────────────────────────────────
// Data structures
// ────────────────────────────────────────────────────────────────────────────

/// Controller identity stored at `AirBeam:ControllerIdentity`.
/// Generated once and reused for all pairings.
struct ControllerIdentity {
    std::string             controllerId;   ///< UUID v4 string
    std::array<uint8_t, 32> ltpk;           ///< Ed25519 long-term public key (32 bytes)
    std::array<uint8_t, 64> ltsk;           ///< Ed25519 long-term secret key (64 bytes)
};

/// Per-device pairing credential stored at `AirBeam:AirPlay2:<HAP_DEVICE_ID>`.
struct PairingCredential {
    std::string             controllerId;       ///< UUID v4 (matches ControllerIdentity)
    std::array<uint8_t, 32> controllerLtpk;     ///< Ed25519 controller public key
    std::array<uint8_t, 64> controllerLtsk;     ///< Ed25519 controller secret key
    std::array<uint8_t, 32> deviceLtpk;         ///< Ed25519 device public key
    std::wstring            deviceName;         ///< Human-readable device name at pairing time
};

// ────────────────────────────────────────────────────────────────────────────
// CredentialStore — static interface
// ────────────────────────────────────────────────────────────────────────────

class CredentialStore {
public:
    /// Returns existing controller identity or generates and stores a new one.
    /// Fails (throws std::runtime_error) only if Credential Manager is inaccessible.
    static ControllerIdentity EnsureControllerIdentity();

    /// Returns stored pairing credential for hapDeviceId, or nullopt if absent.
    static std::optional<PairingCredential> Read(const std::string& hapDeviceId);

    /// Stores (overwrites) pairing credential for hapDeviceId.
    /// Returns true on success.
    static bool Write(const std::string& hapDeviceId, const PairingCredential& cred);

    /// Deletes stored credential. No-op if not found.
    static void Delete(const std::string& hapDeviceId);

    /// Converts hapDevicePublicKey (base64 from TXT record "pk" field) to
    /// hapDeviceId: 12 uppercase hex chars = first 6 bytes of SHA-256(base64_decode(pk)).
    /// Returns empty string on failure.
    static std::string DeviceIdFromPublicKey(const std::string& hapDevicePublicKeyBase64);

private:
    static constexpr wchar_t kIdentityTarget[] = L"AirBeam:ControllerIdentity";
    static std::wstring DeviceTarget(const std::string& hapDeviceId);
};

} // namespace AirPlay2
