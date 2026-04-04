# Implementation Plan: AirPlay 2 Speaker Support

**Branch**: `010-airplay2-support` | **Date**: 2026-04-04 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `specs/010-airplay2-support/spec.md`

## Summary

Add AirPlay 2 sender support to AirBeam so users can stream system audio to HomePods, HomePod minis, and AirPlay 2-certified speakers. The implementation adds a one-time HAP (HomeKit Accessory Protocol) pairing ceremony, an AirPlay 2 session layer (HTTP/2 control + RTP/UDP audio with AES-128-GCM encryption and Apple PTP timing), and a multi-room coordinator for synchronized playback across up to 6 speakers simultaneously. AirPlay 1 (RAOP) code paths remain untouched; the protocol is selected per-receiver based on capabilities detected during mDNS discovery.

## Technical Context

**Language/Version**: C++17, MSVC 2022 (v143) — inherited from v1.0  
**Primary Dependencies**:
- libsodium (ISC / MIT-compatible) — Ed25519, X25519, ChaCha20-Poly1305
- csrp (BSD-2-Clause) — SRP-6a for HAP pairing PIN exchange
- OpenSSL 3.x (Apache-2.0) — implicit dependency of csrp for SRP-6a bignum operations (SHA-256, HMAC, BN arithmetic); Apache-2.0 is MIT-compatible
- Advapi32.lib (Windows built-in) — Windows Credential Manager (`CredWriteW` / `CredReadW`)
- WinHTTP (Windows built-in, 1903+) — HTTP/2 + TLS for AirPlay 2 control channel (replaces nghttp2; see research.md §3)
- Existing: Apple ALAC, Bonjour SDK, speexdsp, WinSparkle  

**Storage**: Windows Credential Manager (DPAPI-backed) for HAP pairing credentials; existing `config.json` for multi-room group preferences  
**Testing**: CTest + existing Google Test suite (`tests/unit/`, `tests/integration/`)  
**Target Platform**: Windows 10 (1903+), Windows 11, x86-64 (unchanged)  
**Project Type**: Native Win32 desktop tray application  
**Performance Goals**:
- ≤2 s end-to-end audio latency (capture → AirPlay 2 playback)
- ≤3 s time-to-audio for a previously paired speaker
- ≤30 s first-time pairing under normal network conditions
- <10 ms multi-room sync offset across all active speakers  

**Constraints**: Real-time safe on Thread 3 (capture) and Thread 4 (encoder/RTP). AirPlay 2 session management runs on Thread 5. HAP pairing runs on a temporary worker thread (or Thread 5). All new crypto dependencies must be MIT/ISC/Apache 2.0 compatible.  
**Scale/Scope**: Up to 6 simultaneous AirPlay 2 speakers in multi-room mode.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I — RT Audio Thread Safety | ✅ Pass | AirPlay 2 session/pairing runs on Thread 5 or worker; Threads 3 & 4 hot path unchanged. AesGcmCipher pre-allocated before streaming loop. |
| II — AirPlay 1 / RAOP Protocol Fidelity | ✅ Pass | AirPlay 1 code paths (`src/protocol/`) are untouched. Protocol routing is in `ConnectionController`; AirPlay 1 receivers use existing `RaopSession`. FR-019 & FR-020 enforce non-regression. The §II prohibition ("EXPLICITLY out of scope for v1.0") is scoped to v1.0 only; AirPlay 2 is a formally approved post-v1.0 scope expansion per the §Governance amendment completed 2026-04-04 (constitution.md bumped to v1.5.0). |
| III — Native Win32, No External UI Frameworks | ✅ Pass | Pairing flow uses tray balloon notifications + existing `CustomPopup` dialog extension for PIN input (Win32 `DialogBox`). No external UI frameworks added. |
| III-A — Visual Color Palette | ✅ Pass | Pairing-in-progress = blue (active), pairing error = red (error), paired checkbox = blue accent. No green. Must be enforced in implementation. |
| IV — Test-Verified Correctness | ⚠️ GATE | New crypto paths (HAP SRP-6a, Ed25519 sign/verify, AES-128-GCM) MUST have unit tests. HAP handshake must be integration-tested. All existing tests must pass (FR-019). |
| V — Observable Failures | ✅ Pass | Stale-credential re-pair, pairing failure, speaker drop, firewall port block all surface tray notifications per spec. |
| VI — Strict Scope Discipline | ✅ Pass | AirPlay 2 and multi-room were deferred in v1.0 constitution. Formal §Governance amendment completed 2026-04-04: feature spec created, 48-hour reflection observed, sole-maintainer self-approval, constitution.md updated to v1.5.0 with §VI scope expansion note. |
| VII — MIT-Compatible Licensing | ⚠️ GATE | libsodium (ISC ✅), csrp (BSD-2-Clause ✅), OpenSSL 3.x (Apache-2.0 ✅), WinHTTP (Windows built-in ✅) — all compatible. No GPL dependency permitted. Verified in research.md §1. |
| VIII — Localizable UI | ⚠️ GATE | All new user-facing strings (pairing prompts, error messages, "Forget device", PIN dialog) MUST go into locale resource files for all 7 locales (en, de, fr, es, ja, zh-Hans, ko) before merge. |

**Post-Phase-1 re-check**: Re-validate III-A (color use in multi-select UI) and VIII (all strings externalized) after contracts are defined.

## Project Structure

### Documentation (this feature)

```text
specs/010-airplay2-support/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/
│   ├── credential-store.md      # Credential Manager storage schema
│   └── airplay2-session-api.md  # AirPlay2Session C++ interface contract
└── tasks.md             # Phase 2 output (/speckit.tasks — not yet created)
```

### Source Code Layout

```text
src/
├── airplay2/                        ← NEW module
│   ├── HapPairing.h/.cpp            ← HAP SRP-6a + Ed25519 handshake
│   ├── CredentialStore.h/.cpp       ← Windows Credential Manager wrapper
│   ├── AirPlay2Session.h/.cpp       ← AirPlay 2 control + audio session (extends StreamSession)
│   ├── AesGcmCipher.h/.cpp          ← AES-128-GCM packet encryption (replaces CBC for AP2)
│   ├── PtpClock.h/.cpp              ← Apple PTP clock sync protocol
│   └── MultiRoomCoordinator.h/.cpp  ← Multi-room group leader negotiation + timing (P2)
├── audio/                           ← UNCHANGED (hot path preserved)
├── core/
│   ├── AppController.h/.cpp         ← MODIFIED: multi-select speaker management, protocol routing
│   ├── ConnectionController.h/.cpp  ← MODIFIED: protocol selection (AP1 vs AP2), pairing trigger
│   └── StreamSession.h              ← MODIFIED: virtual base extended for AP2 subclass
├── discovery/
│   ├── AirPlayReceiver.h            ← MODIFIED: add hapDeviceId, pairingState enum
│   ├── MdnsDiscovery.h/.cpp         ← MODIFIED: browse _airplay._tcp in addition to _raop._tcp
│   └── ReceiverList.h/.cpp          ← MODIFIED: merge _airplay._tcp + _raop._tcp records
├── protocol/                        ← UNCHANGED (AirPlay 1 only)
├── ui/
│   ├── CustomPopup.h/.cpp           ← MODIFIED: checkbox multi-select for AP2, radio for AP1
│   └── TrayMenu.h/.cpp              ← MODIFIED: multi-select build logic, "Forget device" item
└── localization/                    ← MODIFIED: add new string keys for all 7 locales

tests/
├── unit/
│   ├── test_hap_pairing.cpp         ← NEW: SRP-6a vectors, Ed25519 sign/verify, full ceremony
│   ├── test_aes_gcm.cpp             ← NEW: AES-128-GCM known-vector tests
│   ├── test_credential_store.cpp    ← NEW: write/read/delete credential round-trip
│   ├── test_ptp_clock.cpp           ← NEW: clock offset calculation
│   └── test_multi_select_menu.cpp   ← NEW: AP1/AP2 mutual exclusion tray logic
├── integration/
│   └── test_airplay2_session.cpp    ← NEW: full AP2 session against shairport-sync (AP2 mode)
└── [existing tests — all must continue to pass]

third_party/
├── speexdsp/                        ← existing
├── libsodium/                       ← NEW: vendored (ISC license)
└── csrp/                            ← NEW: vendored (BSD-2-Clause license)
```

**Structure Decision**: Single-project layout extending the existing `src/` tree. New `src/airplay2/` module isolates all AirPlay 2 code from the untouched AirPlay 1 `src/protocol/` module. `StreamSession` remains the virtual base; `AirPlay2Session` is a new subclass. This allows `ConnectionController` to select the correct concrete session type at connect time without forking existing logic.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| §VI deferred capability | AirPlay 2 and multi-room were v1.0 non-goals | This IS the formal post-v1.0 feature; constitution amendment procedure satisfied by this spec |
| New vendored dependencies (libsodium, csrp) | HAP pairing requires SRP-6a + Ed25519 not available in Windows stdlib | wolfSSL (GPL), OpenSSL (large footprint), mbedTLS (heavier) all rejected; libsodium+csrp is the minimal MIT-compatible combination |
