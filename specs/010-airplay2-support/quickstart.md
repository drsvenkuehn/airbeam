# Quickstart: Implementing AirPlay 2 Speaker Support

**Feature**: 010-airplay2-support  
**Audience**: Developer picking up this feature

---

## What This Feature Does

Adds AirPlay 2 sender support so AirBeam can stream to HomePods, HomePod minis, and AirPlay 2-certified speakers — devices that v1.0 discovers but cannot stream to.

**AirPlay 1 code is untouched.** All new code lives in `src/airplay2/`. Protocol selection happens in `ConnectionController` based on `AirPlayReceiver.supportsAirPlay2`.

---

## Before You Start

1. **Read first**:
   - `specs/010-airplay2-support/research.md` — crypto library decisions, protocol wire details
   - `specs/010-airplay2-support/data-model.md` — entity structure and state transitions
   - `specs/010-airplay2-support/contracts/` — `CredentialStore` schema and `AirPlay2Session` interface

2. **Key prior art** (read-only protocol reference; do NOT copy GPL code):
   - shairport-sync `openairplay2` branch — HAP pairing implementation in C
   - pyatv docs — AirPlay 2 wire-level documentation (Apache 2.0 / MIT)

3. **Build environment check**:
   ```powershell
   # Verify both builds clean before starting
   ninja -C build\msvc-x64-debug AirBeam && ninja -C build\msvc-x64-release AirBeam
   ```

---

## Delivery Phases

### P1: Single AirPlay 2 Speaker (Blocking — do first)

| Step | File(s) | Description |
|------|---------|-------------|
| 1 | `src/discovery/AirPlayReceiver.h` | Add `supportsAirPlay2`, `hapDevicePublicKey`, `pairingState`, `airPlay2Port` fields |
| 2 | `src/discovery/TxtRecord.h/.cpp` | Parse `pk`, `vv` TXT fields; set `supportsAirPlay2` and `isAirPlay2Only` |
| 3 | `third_party/libsodium/` | Vendor libsodium; add CMake `find_package(sodium)` |
| 4 | `third_party/csrp/` | Vendor csrp single-file; link against OpenSSL |
| 5 | `src/airplay2/CredentialStore.h/.cpp` | Windows Credential Manager wrapper; see `contracts/credential-store.md` |
| 6 | `src/airplay2/HapPairing.h/.cpp` | SRP-6a + Ed25519 HAP ceremony; posts `WM_AP2_PAIRING_REQUIRED` |
| 7 | `src/airplay2/AesGcmCipher.h/.cpp` | AES-128-GCM via Windows BCrypt (replaces CBC for AP2 packets) |
| 8 | `src/airplay2/PtpClock.h/.cpp` | PTP timing (adapt from `src/protocol/NtpClock.h`) |
| 9 | `src/airplay2/AirPlay2Session.h/.cpp` | Subclass `StreamSession`; implement `Init`, `StartRaop` (HTTP/2), volume |
| 10 | `src/core/ConnectionController.h/.cpp` | Select `AirPlay2Session` vs `StreamSession` based on receiver flags |
| 11 | `src/core/Messages.h` | Add `WM_AP2_*` message constants |
| 12 | `src/core/AppController.h/.cpp` | Handle `WM_AP2_PAIRING_REQUIRED` → launch pairing flow |
| 13 | `src/ui/CustomPopup.h/.cpp` | Show pairing state visually (spinner/checkmark per speaker) |
| 14 | `src/localization/` | Add strings for all new notifications, 7 locales |
| 15 | `tests/unit/test_hap_pairing.cpp` | SRP-6a vectors, Ed25519 sign/verify, full ceremony test |
| 16 | `tests/unit/test_aes_gcm.cpp` | AES-128-GCM known-vector test |
| 17 | `tests/unit/test_credential_store.cpp` | Write/read/delete round-trip |

### P2: Multi-Room (after P1 is validated)

| Step | File(s) | Description |
|------|---------|-------------|
| 18 | `src/ui/CustomPopup.h/.cpp` | Multi-select checkboxes for AirPlay 2; radio behaviour for AirPlay 1 |
| 19 | `src/ui/TrayMenu.h/.cpp` | Multi-select build logic, protocol-exclusive switching |
| 20 | `src/airplay2/MultiRoomCoordinator.h/.cpp` | Manage 2–6 `AirPlay2Session` instances; PTP sync |
| 21 | `src/core/AppController.h/.cpp` | Activate `MultiRoomCoordinator` when >1 AP2 speaker selected |
| 22 | `tests/unit/test_ptp_clock.cpp` | Clock offset calculation |
| 23 | `tests/unit/test_multi_select_menu.cpp` | AP1/AP2 mutual exclusion tray logic |

---

## Critical Rules (Constitution gates)

- **RT safety (§I)**: `AesGcmCipher` pre-allocates all buffers in `Init()` before Thread 4 starts. **Zero heap on Thread 4 hot path.**
- **No GPL code (§VII)**: shairport-sync is GPL — read for protocol understanding only. All code must be original.
- **All strings localised (§VIII)**: Every user-visible string goes in `localization/` before merge. No hardcoded English.
- **Color palette (§III-A)**: Pairing-in-progress = blue. Error = red. No green anywhere.
- **Tests before merge (§IV)**: `test_hap_pairing`, `test_aes_gcm`, `test_credential_store` must pass.

---

## Build Commands

```powershell
# Always kill AirBeam before building (linker cannot overwrite running exe)
Stop-Process -Name AirBeam -ErrorAction SilentlyContinue

# Build both configurations
ninja -C build\msvc-x64-debug AirBeam
ninja -C build\msvc-x64-release AirBeam

# Run unit tests
ctest --preset msvc-x64-debug-ci -L unit

# Run AirBeam (debug build, day-to-day)
.\build\msvc-x64-debug\AirBeam.exe
```
