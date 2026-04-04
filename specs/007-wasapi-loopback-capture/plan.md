# Implementation Plan: WASAPI Loopback Audio Capture

**Branch**: `007-wasapi-loopback-capture` | **Date**: 2025-07-16 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `/specs/007-wasapi-loopback-capture/spec.md`

## Summary

Extend and harden the existing WASAPI loopback capture infrastructure to satisfy all 13 functional requirements of Feature 007. The capture subsystem skeleton already exists (`WasapiCapture.h/cpp`, `FrameAccumulator`, `SpscRingBuffer`, `Resampler`) but has four compliance gaps:

1. **Resampler library** — current `Resampler.cpp` uses libsamplerate (float API); spec FR-006 and its clarification session explicitly require **libspeexdsp** (integer API, BSD-3-Clause). The Resampler must be rewritten against `speex_resampler_process_interleaved_int()` and libspeexdsp vendored under `third_party/speexdsp/`.

2. **Device-change coalescing** — `WasapiCapture` already inherits `IMMNotificationClient` and sets `deviceChanged_` atomically, but no 20 ms fixed-deadline coalescing timer is implemented (FR-007).

3. **Ring-buffer capacity constant** — `SpscRingBufferPtr` is currently a `variant<128*, 32*>`. The spec requires a named compile-time constant `kCaptureQueueFrames = 512` declared in `WasapiCapture.h`, and a matching 512-slot variant arm in `SpscRingBuffer.h` (FR-008).

4. **Error notification message** — `WM_CAPTURE_ERROR` is not present in `Messages.h` (last defined constant is `WM_APP+9`). It must be added at `WM_APP+10` and routed through `AppController` to a tray balloon (FR-009).

Secondary hardening: verify MMCSS `AvSetMmThreadCharacteristics("Audio", …)` is called in `ThreadProc` (FR-003); change `Stop()`'s 200 ms join timeout to an unconditional `thread_.join()` (FR-012); add debug-level lifecycle logging (FR-013).

## Technical Context

**Language/Version**: C++17, MSVC 2022 (v143), `/permissive-`  
**Primary Dependencies**: Win32 WASAPI (`mmdeviceapi.h`, `audioclient.h`, `avrt.lib`, `ole32.lib`), libspeexdsp (vendored BSD-3-Clause, integer resampler), Google Test 1.14.0  
**Storage**: N/A — audio data flows in-memory through a lock-free SPSC ring buffer  
**Testing**: Google Test via CMake/CTest; `ctest --preset msvc-x64-debug-ci -L unit` and `-L integration`  
**Target Platform**: Windows 10 build 1903+, Windows 11, x86-64  
**Project Type**: Windows desktop application (background tray service, feature 007 is the audio capture subsystem)  
**Performance Goals**: Capture thread CPU < 3% on mid-range quad-core (SC-003); zero heap allocations on hot path (SC-007); device re-attach within 50 ms of change notification (SC-002)  
**Constraints**: Thread 3 (capture) MUST be real-time safe throughout steady-state streaming: no `new`/`malloc`, no mutex/critical-section, no blocking OS calls on the sample-processing path; all buffers pre-allocated before the capture loop begins; MMCSS-boosted for entire capture lifetime

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I — Real-Time Audio Thread Safety | ✅ PASS | FR-010 mandates zero alloc/lock/blocking on hot path; MMCSS required by FR-003; SPSC buffer mandated by FR-005; all pre-allocations happen before the capture loop |
| II — AirPlay 1 / RAOP Protocol Fidelity | ✅ PASS | Capture output feeds the existing ALAC encoder thread unchanged; no protocol modifications |
| III — Native Win32, No External UI Frameworks | ✅ PASS | Pure Win32 WASAPI/COM throughout; no UI framework introduced |
| III-A — Visual Color Palette | ✅ PASS | No new UI assets or icons created by this feature |
| IV — Test-Verified Correctness | ✅ PASS | Constitution mandates WASAPI correlation test (already scaffolded in `tests/integration/`); two new unit tests added: resampler pitch accuracy and Start/Stop resource leak (50 cycles) |
| V — Observable Failures — Never Silent | ✅ PASS | FR-009: `WM_CAPTURE_ERROR` posted to app HWND on unrecoverable failure; `AppController` surfaces tray balloon; FR-007: device change re-attach is automatic and within 50 ms |
| VI — Strict Scope Discipline | ✅ PASS | No deferred v1.0 capabilities (AirPlay 2, virtual audio driver, multi-room, per-app capture) touched |
| **VII — MIT-Compatible Licensing** | ⚠️ **VIOLATION** | libspeexdsp is BSD-3-Clause (MIT-compatible ✅) but **constitution §Toolchain** mandates libsamplerate or r8brain-free-src specifically — see Complexity Tracking below |
| VIII — Localizable UI | ✅ PASS | No new user-visible strings; error notification is routed through the existing `AppController` balloon path, which already externalises its strings |

**Post-design re-check** (after Phase 1): All §I real-time constraints verified against the data model — `FrameAccumulator` accumulates on-stack, `SpscRingBuffer::TryPush` is allocation-free, libspeexdsp integer hot path holds no locks. ✅

## Project Structure

### Documentation (this feature)

```text
specs/007-wasapi-loopback-capture/
├── plan.md              # This file
├── research.md          # Phase 0 — library selection, API patterns, coalescing strategy
├── data-model.md        # Phase 1 — entities, fields, state transitions
├── quickstart.md        # Phase 1 — build, test, verify end-to-end
├── contracts/
│   ├── wasapi-capture-api.md       # WasapiCapture class interface contract
│   ├── audio-message-protocol.md   # WM_* message protocol
│   └── audioframe-format.md        # AudioFrame wire/data format
└── tasks.md             # Phase 2 output (/speckit.tasks — NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
src/audio/
├── WasapiCapture.h        ← EXTEND: add kCaptureQueueFrames constant; document
│                             20ms coalescing via deviceChangedAt_ timestamp;
│                             add IsRunning()
├── WasapiCapture.cpp      ← EXTEND: verify/add AvSetMmThreadCharacteristics("Audio");
│                             implement 20ms coalescing (WaitForMultipleObjects +
│                             deviceChangedAt_ atomic); dispatch WM_CAPTURE_ERROR;
│                             change Stop() join from 200ms timeout → unconditional join();
│                             add debug-level lifecycle log calls (FR-013)
├── SpscRingBuffer.h       ← EXTEND: add SpscRingBuffer<AudioFrame,512>* arm to
│                             SpscRingBufferPtr variant; add RingCapacity() helper
├── AudioFrame.h           ← no change
├── Resampler.h            ← REPLACE: remove SRC_STATE_tag; forward-declare
│                             SpeexResamplerState; update Process() to int16 paths
├── Resampler.cpp          ← REPLACE: remove libsamplerate; implement using
│                             speex_resampler_process_interleaved_int(); pre-allocate
│                             SpeexResamplerState at quality level 7
├── AlacEncoderThread.h    ← no change
└── AlacEncoderThread.cpp  ← no change

src/core/
├── Messages.h             ← ADD: WM_CAPTURE_ERROR = WM_APP + 10
├── AppController.h        ← ADD: OnCaptureError() declaration
└── AppController.cpp      ← ADD: WM_CAPTURE_ERROR case → Stop() + tray balloon

third_party/speexdsp/      ← NEW: vendored resampler source
├── CMakeLists.txt         # speexdsp_resampler STATIC target; /W0 on MSVC
├── include/
│   └── speex_resampler.h  # vendored (Speex project BSD-3-Clause)
└── src/
    ├── resample.c
    ├── arch.h
    ├── fixed_generic.h
    └── os_support.h

CMakeLists.txt             ← MODIFY: remove AIRBEAM_USE_RESAMPLER / libsamplerate
│                             FetchContent block; add add_subdirectory(third_party/speexdsp);
│                             link speexdsp_resampler; keep avrt.lib

tests/unit/
├── test_frame_accumulator.cpp     ← existing, no change
├── test_spsc_ring.cpp             ← existing, no change
├── test_resampler.cpp             ← NEW: speexdsp 48000→44100 and 96000→44100
│                                     pitch accuracy; passthrough identity
└── test_wasapi_stop_start.cpp     ← NEW: 50-cycle Start/Stop; handle/memory
│                                     leak detection (SC-006)
tests/integration/
└── test_wasapi_correlation.cpp    ← existing; update Start() call if HRESULT
                                      return type or SpscRingBufferPtr arm changed
```

**Structure Decision**: Single-project layout — all changes within the existing `src/audio/` and `src/core/` directories. `third_party/speexdsp/` vendors only the resampler portion of libspeexdsp (~1 200 lines of C) as a minimal CMake STATIC target, consistent with the spec's "vendored" language and the reality that speexdsp has no native CMake build system. No new top-level directories, no new threads, no new processes.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| **§Toolchain (Platform Constraints)** — libspeexdsp used instead of mandated libsamplerate or r8brain-free-src | Spec FR-006 and its explicit clarification session (2026-03-26) require libspeexdsp. Its `speex_resampler_process_interleaved_int()` API processes int16 samples natively — no float intermediate buffer required on the capture hot path. BSD-3-Clause licence is MIT-compatible (§VII is satisfied). State is fully pre-allocated via `speex_resampler_init()` before the capture loop; zero runtime allocation. | libsamplerate's `src_process()` takes and produces float32; using it on the capture hot path requires `src_short_to_float_array` + `src_float_to_short_array` per buffer — additional computation and potential FP-denormal stalls on the real-time thread. r8brain-free-src is float-only. Neither provides a direct int16 API. The spec clarification was explicit and unambiguous: libspeexdsp is the correct choice for this integer pipeline. |
