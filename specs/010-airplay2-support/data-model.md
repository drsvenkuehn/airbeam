# Data Model: AirPlay 2 Speaker Support

**Phase 1 output for feature 010** | **Date**: 2026-04-04

---

## Entities

### 1. AirPlayReceiver (extended)

Extends the existing `struct AirPlayReceiver` in `src/discovery/AirPlayReceiver.h`.

| Field | Type | Source | Notes |
|-------|------|--------|-------|
| `instanceName` | `std::wstring` | existing | mDNS service instance name |
| `displayName` | `std::wstring` | existing | human-readable label |
| `hostName` | `std::wstring` | existing | e.g. `"HomePod.local"` |
| `ipAddress` | `std::string` | existing | dotted-decimal or IPv6 |
| `port` | `uint16_t` | existing | AirPlay control port |
| `isAirPlay1Compatible` | `bool` | existing | TXT `et` contains `'1'` |
| `isAirPlay2Only` | `bool` | existing | `pk` present + no RSA |
| `supportsAes` | `bool` | existing | RSA-AES capable |
| `macAddress` | `std::string` | existing | from TXT or instance name |
| `deviceModel` | `std::string` | existing | e.g. `"AudioAccessory5,1"` |
| `stableId` | `std::wstring` | existing | MAC-based stable ID (AirPlay 1 key) |
| `lastSeenTick` | `ULONGLONG` | existing | stale-entry detection |
| **`supportsAirPlay2`** | `bool` | **NEW** | `pk` TXT field present |
| **`hapDevicePublicKey`** | `std::string` | **NEW** | base64 Ed25519 pubkey from TXT `pk` |
| **`pairingState`** | `PairingState` | **NEW** | see enum below |
| **`airPlay2Port`** | `uint16_t` | **NEW** | AirPlay 2 control port (from TXT, may differ from port 5000) |

**PairingState enum** (new, `src/discovery/AirPlayReceiver.h`):
```cpp
enum class PairingState {
    NotApplicable,   // AirPlay 1-only device
    Unpaired,        // AirPlay 2, no stored credential
    Pairing,         // HAP ceremony in progress
    Paired,          // Credential in Windows Credential Manager
    Error            // Last pairing attempt failed
};
```

**Identity**: For AirPlay 2 receivers, identity is the **HAP Device ID** (derived as `SHA256(hapDevicePublicKey)[0:6]` → 6-byte hex string). Used as the Credential Manager target key. Stable across IP changes and device renames.

**State transitions**:
```
NotApplicable   (AirPlay 1-only devices — no transitions)

Unpaired   ──[user selects device]──▶   Pairing
Pairing    ──[ceremony success]───▶   Paired
Pairing    ──[ceremony failure]───▶   Error
Paired     ──[stale credential]───▶   Pairing  (auto, no user action)
Paired     ──["Forget device"]────▶   Unpaired
Error      ──[user retries]────────▶   Pairing
```

---

### 2. PairingCredential (new)

Stored in Windows Credential Manager (DPAPI-backed). Not in `config.json`.

| Field | Type | Notes |
|-------|------|-------|
| `controllerId` | `string (UUID)` | AirBeam controller identity UUID |
| `controllerLtpk` | `bytes[32]` | AirBeam Ed25519 public key |
| `controllerLtsk` | `bytes[64]` | AirBeam Ed25519 secret key (SENSITIVE) |
| `deviceLtpk` | `bytes[32]` | Device Ed25519 public key (for verify) |
| `deviceName` | `string` | Human-readable name at time of pairing |

**Storage key**: `L"AirBeam:AirPlay2:<HAP_DEVICE_ID>"` where `HAP_DEVICE_ID` = 12-char hex (6 bytes from `SHA256(hapDevicePublicKey)`).

**Lifecycle**:
- Created: on successful HAP pairing ceremony completion
- Read: on every AirPlay 2 connection attempt
- Deleted: on "Forget device" user action OR on stale-credential auto-repairing

---

### 3. ControllerIdentity (new, singleton)

AirBeam's own long-term identity, generated once on first launch.

| Field | Type | Notes |
|-------|------|-------|
| `controllerId` | `string (UUID)` | Stable UUID for this installation |
| `ltpk` | `bytes[32]` | Ed25519 public key |
| `ltsk` | `bytes[64]` | Ed25519 secret key (SENSITIVE) |

**Storage key**: `L"AirBeam:ControllerIdentity"` in Windows Credential Manager.

**Lifecycle**: Generated once by `CredentialStore::EnsureControllerIdentity()` at startup; never deleted (would invalidate all paired devices).

---

### 4. AirPlay2Session (new)

Active streaming session to one AirPlay 2 receiver. Subclass of `StreamSession`.

| Field | Type | Notes |
|-------|------|-------|
| `receiverHapDeviceId` | `string` | Links to PairingCredential |
| `controlSocket` | `HINTERNET` | WinHTTP HTTP/2 session handle |
| `audioSocket` | `SOCKET` | UDP socket for RTP |
| `timingSocket` | `SOCKET` | UDP socket for PTP timing |
| `sessionKey` | `bytes[16]` | HKDF-derived AES-128-GCM key |
| `sessionSalt` | `bytes[4]` | Per-session random (part of GCM nonce) |
| `rtpSequence` | `uint16_t` | Monotonic RTP sequence counter |
| `rtpTimestamp` | `uint32_t` | Monotonic RTP timestamp |
| `ptpClockOffset` | `int64_t` | Offset from receiver clock (nanoseconds) |
| `activeState` | `enum` | Connecting / Streaming / Teardown |

**Relationship**: One `AirPlay2Session` per active AirPlay 2 receiver. In multi-room, `MultiRoomCoordinator` holds 2–6 sessions.

---

### 5. MultiRoomGroup (new, P2)

Logical group of simultaneously active AirPlay 2 sessions.

| Field | Type | Notes |
|-------|------|-------|
| `members` | `vector<AirPlay2Session*>` | 2–6 sessions |
| `isActive` | `bool` | Group streaming state |
| `groupVolume` | `float` | 0.0–1.0, applied to all members |
| `referenceClockId` | `uint32_t` | PTP reference clock (AirBeam's clock) |

**Constraints**:
- `members.size()` ∈ [2, 6]
- All members must be in `PairingState::Paired`
- All members must be AirPlay 2 (no AirPlay 1 mixing — enforced by tray menu UX)

---

## Relationships

```
ControllerIdentity (1)
    └── used in ──▶ PairingCredential (0..*) [one per device]
                        └── maps to ──▶ AirPlayReceiver.hapDeviceId
                                            └── active session ──▶ AirPlay2Session (0..1)
                                                                        └── group ──▶ MultiRoomGroup (0..1)
```

---

## Validation Rules

| Rule | Description |
|------|-------------|
| `hapDevicePublicKey` non-empty | Required before any pairing attempt |
| `PairingCredential.controllerLtsk` 64 bytes | Ed25519 secret key is 64 bytes (seed + public) |
| `MultiRoomGroup.members` ≥ 2 | Cannot have a single-member group |
| `MultiRoomGroup.members` ≤ 6 | Maximum 6 speakers (spec SC-005) |
| AirPlay 1 exclusive selection | Selecting an AirPlay 1 speaker clears all AirPlay 2 active sessions, and vice versa |
| Credential Manager target unique | Each device has at most one `PairingCredential` (keyed by HAP Device ID) |
