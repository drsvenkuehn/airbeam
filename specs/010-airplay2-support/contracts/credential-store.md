# Contract: Credential Store — Windows Credential Manager Schema

**Feature**: 010-airplay2-support  
**Owner**: `src/airplay2/CredentialStore.h`

---

## Purpose

Defines the stable schema for all entries AirBeam writes to Windows Credential Manager. Any change to target name patterns or blob format is a breaking change requiring a migration path.

---

## Target Name Patterns

| Entry | Target Name (LPWSTR) | Type | Scope |
|-------|---------------------|------|-------|
| Controller identity | `AirBeam:ControllerIdentity` | `CRED_TYPE_GENERIC` | Current user |
| Per-device credential | `AirBeam:AirPlay2:<HAP_DEVICE_ID>` | `CRED_TYPE_GENERIC` | Current user |

`<HAP_DEVICE_ID>` = 12 uppercase hex characters representing the first 6 bytes of `SHA256(base64_decode(hapDevicePublicKey))`.

Example: `AirBeam:AirPlay2:A1B2C3D4E5F6`

---

## Blob Format

All blobs are UTF-8 encoded JSON, stored as `CRED_TYPE_GENERIC` credential data. DPAPI encryption is applied automatically by Windows.

### ControllerIdentity blob

```json
{
  "version": 1,
  "controller_id": "<UUID v4 string>",
  "ltpk": "<base64 standard Ed25519 public key, 32 bytes>",
  "ltsk": "<base64 standard Ed25519 secret key, 64 bytes>"
}
```

### PairingCredential blob

```json
{
  "version": 1,
  "controller_id": "<UUID v4 string — matches ControllerIdentity>",
  "controller_ltpk": "<base64 Ed25519 public key, 32 bytes>",
  "controller_ltsk": "<base64 Ed25519 secret key, 64 bytes>",
  "device_ltpk": "<base64 device Ed25519 public key, 32 bytes>",
  "device_name": "<human-readable device name at pairing time>"
}
```

---

## Versioning

- `"version"` field is mandatory. Currently `1`.
- On read, if `version` > known maximum, treat as corrupt and delete + re-pair.
- Future schema changes MUST increment version and include a migration path in `CredentialStore::Read()`.

---

## C++ Interface

```cpp
// src/airplay2/CredentialStore.h

struct ControllerIdentity {
    std::string controllerId;   // UUID v4
    std::array<uint8_t, 32> ltpk;
    std::array<uint8_t, 64> ltsk;
};

struct PairingCredential {
    std::string controllerId;
    std::array<uint8_t, 32> controllerLtpk;
    std::array<uint8_t, 64> controllerLtsk;
    std::array<uint8_t, 32> deviceLtpk;
    std::wstring deviceName;
};

class CredentialStore {
public:
    // Returns existing identity or generates + stores a new one.
    // Fails only if Credential Manager is inaccessible (catastrophic).
    static ControllerIdentity EnsureControllerIdentity();

    // Returns nullopt if no credential stored for this device ID.
    static std::optional<PairingCredential> Read(const std::string& hapDeviceId);

    // Overwrites any existing credential for this device ID.
    static bool Write(const std::string& hapDeviceId, const PairingCredential& cred);

    // Deletes stored credential. No-op if not found.
    static void Delete(const std::string& hapDeviceId);

    // Converts hapDevicePublicKey (base64, from TXT record) to hapDeviceId (12-char hex).
    static std::string DeviceIdFromPublicKey(const std::string& hapDevicePublicKeyBase64);
};
```
