# Implementation Plan: Full App Integration — ConnectionController and Live Audio Path

**Branch**: `009-connection-controller` | **Date**: 2025-07-17 | **Spec**: [spec.md](spec.md)  
**Input**: `specs/009-connection-controller/spec.md`

## Summary

Introduce `ConnectionController` — a new class owned by `AppController` that centralises all
pipeline lifecycle logic (connect, disconnect, reconnect, audio-device recovery, low-latency toggle,
auto-connect, volume). It lives entirely on Thread 1 (Win32 message loop), drives a strict
four-state machine (Idle → Connecting → Streaming → Disconnecting), and coordinates the three
pipeline threads (WasapiCapture/T3, AlacEncoderThread/T4, RaopSession/T5) exclusively through
Win32 posted messages and timers — no mutexes, no atomics on the hot path.

New supporting artefacts: `StreamSession` (bundles all per-session resources), `PipelineState`
enum, extended `Messages.h` with four new/renamed window messages (4 new WM_ constants, 2 renames), and a `ReconnectContext`
value type for exponential-backoff retry state. All 23 FRs and 10 SCs from the spec are
addressed; Thread 3/4 real-time safety constraints (FR-022, TC-001–003) are preserved as
a hard gate throughout.

## Technical Context

**Language/Version**: C++17 — MSVC 2022 (v143), `/permissive-`  
**Primary Dependencies**: Win32 API (SetTimer/KillTimer, PostMessage, OutputDebugString),
WASAPI (via existing `WasapiCapture`), Bonjour SDK (via existing `MdnsDiscovery`/`ReceiverList`),
ALAC encoder (via existing `AlacEncoderThread`), RTSP/RAOP (via existing `RaopSession`),
BCrypt AES (via existing `AesCbcCipher`), nlohmann/json (via existing `Config`)  
**Storage**: `%APPDATA%\AirBeam\config.json` — read/written via existing `Config` class  
**Testing**: GoogleTest / CTest (`msvc-x64-debug-ci` preset); unit tests with stub mocks for
Thread 3/4/5; existing integration + E2E suites  
**Target Platform**: Windows 10 (build 1903+) and Windows 11, x86-64  
**Project Type**: Native Win32 desktop tray application  
**Performance Goals**: ≤50 ms capture-restart gap (SC-003), ≤3 s connect (SC-001),
≤2 s disconnect (SC-002), ≤7 s reconnect window (SC-004), ≤500 ms volume propagation (SC-008)  
**Constraints**: Zero heap alloc + zero mutex/lock on Thread 3/4 hot loops (TC-001–003);
all state transitions serialised on Thread 1 (TC-004); no new synchronisation primitives beyond
Win32 message serialisation  
**Scale/Scope**: Single active session; 5-thread fixed architecture; ≤100 discovered speakers;
one `ConnectionController` instance per app lifetime

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-checked after Phase 1 design — see ✅ marks below.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Real-Time Thread Safety | ✅ PASS | FR-022 + TC-001–003 mandate zero heap alloc + zero mutex on Threads 3 & 4. `StreamSession` pre-allocates all buffers during `Init()` before the hot loop. ConnectionController never touches Threads 3/4 state directly. |
| II. AirPlay 1 Protocol Fidelity | ✅ PASS | Reuses existing `RaopSession` (RTSP, AES, NTP, retransmit). ConnectionController only calls `Start()`/`Stop()`. No protocol code is added or modified. |
| III. Native Win32 / No External UI Frameworks | ✅ PASS | All coordination via `SetTimer`, `PostMessage`, `OutputDebugString`. No new dependencies. |
| III-A. Visual Color Palette | ✅ PASS | TrayState values (Idle=Gray, Connecting=Blue, Streaming=Blue, Error=Red) are already defined and used; ConnectionController calls `TrayIcon::SetState()` only. No new colors introduced. |
| IV. Test-Verified Correctness | ✅ PASS | SC-010 explicitly requires all 4 states and all transitions to be exercisable via mocks in automated integration tests. New unit test file `test_connection_controller.cpp` required before merge. |
| V. Observable Failures | ✅ PASS | FR-019/020 + FR-007/008: every unrecoverable error produces a balloon notification. Silent failure is prohibited. All error paths call `BalloonNotify`. |
| VI. Strict Scope Discipline | ✅ PASS | Single speaker only. No multi-room. No AirPlay 2. |
| VII. MIT-Compatible Licensing | ✅ PASS | No new dependencies. All referenced components are already in-tree. |
| VIII. Localizable UI | ✅ PASS | All balloon notification text and tray tooltip strings MUST pass through `StringLoader`. No hardcoded user-visible strings in `ConnectionController`. |

**Post-design re-check**: Phase 1 design introduces no new external dependencies, no new threads,
no new synchronisation primitives, and no hardcoded strings. Constitution compliance is maintained.

## Project Structure

### Documentation (this feature)

```text
specs/009-connection-controller/
├── plan.md              ← this file
├── research.md          ← Phase 0 output
├── data-model.md        ← Phase 1 output
├── quickstart.md        ← Phase 1 output
├── contracts/
│   ├── messages.md                   ← Win32 window message contract (new + renamed)
│   ├── connection-controller-api.md  ← ConnectionController public API
│   └── state-machine.md              ← State transition table + invariants
└── tasks.md             ← Phase 2 output (/speckit.tasks — NOT created here)
```

### Source Code (repository root)

```text
src/
├── core/
│   ├── AppController.h / .cpp        (modified — delegate lifecycle to ConnectionController)
│   ├── Commands.h                    (unchanged)
│   ├── Config.h / .cpp               (unchanged)
│   ├── ConnectionController.h        (NEW)
│   ├── ConnectionController.cpp      (NEW)
│   ├── Messages.h                    (modified — add 4 new WM_ constants, rename 2)
│   ├── PipelineState.h               (NEW — enum class PipelineState)
│   ├── ReconnectContext.h            (NEW — value type for retry state)
│   ├── StreamSession.h               (NEW — per-session resource bundle)
│   ├── StreamSession.cpp             (NEW)
│   └── Logger.h / .cpp               (unchanged)
├── audio/
│   ├── WasapiCapture.cpp             (modified — use renamed WM_AUDIO_DEVICE_LOST, post WM_CAPTURE_ERROR)
│   └── ...AlacEncoderThread, SpscRingBuffer unchanged
├── protocol/                         (unchanged — RaopSession, AesCbcCipher, RetransmitBuffer)
├── discovery/
│   ├── MdnsDiscovery.h / .cpp        (modified — posts WM_DEVICE_DISCOVERED + WM_SPEAKER_LOST)
│   └── ...rest unchanged
└── ui/                               (unchanged)

tests/
├── unit/
│   ├── test_connection_controller.cpp  (NEW)
│   └── ...existing tests...
├── integration/
│   └── ...existing tests unchanged...
└── e2e/
    └── ...existing tests unchanged...
```

**Structure Decision**: Single-project layout. `ConnectionController` lives in `src/core/`
alongside `AppController`. Supporting value types (`PipelineState`, `ReconnectContext`,
`StreamSession`) are split into their own headers for readability and testability.
`Messages.h` is updated in-place — no new file needed for message constants.

## Complexity Tracking

> No constitution violations. Table not required.
