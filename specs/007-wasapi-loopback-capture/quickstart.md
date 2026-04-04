# Developer Quickstart: WASAPI Loopback Audio Capture (Feature 007)

**Branch**: `007-wasapi-loopback-capture`  
**Audience**: Developer implementing or reviewing the capture subsystem changes  
**Prereqs**: Windows 10 1903+ machine, VS 2022 Build Tools installed, CMake 3.20+, Ninja

---

## 1 — Check Out and Build

```powershell
git clone https://github.com/<user>/airbeam.git
cd airbeam
git checkout 007-wasapi-loopback-capture

# Configure (debug, unit tests included)
cmake --preset msvc-x64-debug

# Build
cmake --build build/msvc-x64-debug --parallel
```

Expected output: `AirBeam.exe` and `AirBeamTests.exe` appear in `build/msvc-x64-debug/`.

> **Note on libspeexdsp**: The resampler source is vendored under `third_party/speexdsp/` and
> compiled as part of the CMake build — no internet access or extra install steps required.
> The `AIRBEAM_USE_RESAMPLER` / libsamplerate FetchContent block has been removed from
> `CMakeLists.txt` and replaced with `add_subdirectory(third_party/speexdsp)`.

---

## 2 — Run the Unit Tests

```powershell
# Run all unit tests (fast, no audio device required)
ctest --preset msvc-x64-debug -L unit --output-on-failure

# Or run the test binary directly for a specific test
.\build\msvc-x64-debug\AirBeamTests.exe --gtest_filter="Resampler*"
.\build\msvc-x64-debug\AirBeamTests.exe --gtest_filter="WasapiStopStart*"
```

**Key unit tests for feature 007**:

| Test suite | File | What it verifies |
|------------|------|-----------------|
| `ResamplerTest.*` | `tests/unit/test_resampler.cpp` | libspeexdsp 48 000→44 100 and 96 000→44 100 Hz pitch accuracy; passthrough identity at 44 100 Hz |
| `WasapiStopStartTest.*` | `tests/unit/test_wasapi_stop_start.cpp` | 50 consecutive Start/Stop cycles with no handle/memory leaks (SC-006) |
| `FrameAccumulatorTest.*` | `tests/unit/test_frame_accumulator.cpp` | Existing; verifies 352-sample fixed-frame emission |
| `SpscRingTest.*` | `tests/unit/test_spsc_ring.cpp` | Existing; verifies TryPush/TryPop correctness and wrap-around |

---

## 3 — Run the Integration Test (requires audio device)

The WASAPI correlation test verifies that a known WAV file captured through the loopback path produces output with cross-correlation > 0.99 at the encoder input (constitution §IV mandatory test).

```powershell
# Requires a real audio output device; run interactively (not in headless CI)
ctest --preset msvc-x64-debug -L integration --output-on-failure
```

If your machine has no audio output device, skip this step and verify manually using step 5.

---

## 4 — Verify Hot-Path Real-Time Safety (SC-007)

To confirm zero heap allocations on the capture hot path, attach a memory profiler during a live capture session:

```powershell
# Option A: Visual Studio Diagnostic Tools (if full VS 2022 installed)
# Start AirBeam.exe under the VS Diagnostic Tools > Memory Usage snapshot
# Trigger two snapshots: one before streaming, one during 30s of streaming
# Diff: heap delta on Thread 3 during steady-state must be 0 bytes

# Option B: Application Verifier (Windows SDK tool — no full VS needed)
appverif.exe /enable Heaps /for AirBeam.exe
# Run AirBeam.exe and stream for 60 seconds
# Application Verifier logs to the debugger; attach WinDbg or DebugView
# Expected: no heap allocations logged for Thread 3 during steady-state
```

---

## 5 — End-to-End Smoke Test (manual)

1. Start a shairport-sync receiver (Linux or WSL2 with Docker):
   ```bash
   docker run -d --network host --name shairport mikebrady/shairport-sync:latest
   ```
2. Launch `AirBeam.exe`. The tray icon should appear in grey (idle).
3. Right-click the tray icon → select the shairport-sync receiver.
4. Play any audio in Windows (e.g., open a browser and play a YouTube video).
5. Verify audio is audible through shairport-sync output.
6. Change the default audio device in Windows Sound Settings (Win + I → Sound → Output).
7. Verify audio resumes automatically within ~50 ms (brief gap acceptable per SC-002).
8. Right-click tray → Disconnect. The tray icon returns to grey.

---

## 6 — Verify WM_CAPTURE_ERROR Notification

To test the unrecoverable error path (FR-009):

1. Start streaming (step 5 above).
2. Open **Device Manager** → Sound, video and game controllers → disable the default audio output device.
3. Within 1 second (SC-005), a tray balloon should appear indicating a capture failure.
4. The tray icon should change to red (error state).
5. Re-enable the device in Device Manager; manually reconnect from the tray menu.

---

## 7 — Key Files to Understand

| File | What changed for feature 007 |
|------|------------------------------|
| `src/audio/WasapiCapture.h` | `kCaptureQueueFrames = 512` constant; `deviceChangedAt_` atomic for coalescing |
| `src/audio/WasapiCapture.cpp` | MMCSS boost; 20 ms coalescing; `WM_CAPTURE_ERROR` dispatch; unconditional `join()` |
| `src/audio/Resampler.h` | libsamplerate → libspeexdsp; integer `Process()` path |
| `src/audio/Resampler.cpp` | `speex_resampler_process_interleaved_int()` replacing `src_process()` |
| `src/audio/SpscRingBuffer.h` | Added `SpscRingBuffer<AudioFrame, 512>*` arm to `SpscRingBufferPtr` variant |
| `src/core/Messages.h` | Added `WM_CAPTURE_ERROR = WM_APP + 10` |
| `src/core/AppController.cpp` | Handles `WM_CAPTURE_ERROR` → Stop + tray balloon |
| `third_party/speexdsp/` | Vendored resampler source (5 files, BSD-3-Clause) |
| `CMakeLists.txt` | Removed libsamplerate FetchContent; added `add_subdirectory(third_party/speexdsp)` |

---

## 8 — Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Build fails: `speex_resampler.h not found` | `third_party/speexdsp/` not checked in or `add_subdirectory` missing | Verify `third_party/speexdsp/` exists and `CMakeLists.txt` has `add_subdirectory(third_party/speexdsp)` |
| `AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED` | Another app holds the device in exclusive mode | Set device to "Allow applications to take exclusive control" = OFF in Windows Sound Properties |
| Integration test times out | No audio device or shairport-sync not running | Run unit tests only; use `-L unit` filter |
| `WM_CAPTURE_ERROR` fires immediately on `Start()` | No default render device set | Set a default audio output in Windows Sound Settings |
| Audio pitch is wrong after device change | Resampler not recreated with new device format | Verify `Reinitialise()` calls `resampler_.reset()` then recreates with new `pMixFormat_` |
| `WasapiStopStart` test fails on leak check | `ReleaseAudioClient()` not releasing all COM objects | Check `pCaptureClient_`, `pAudioClient_`, `pDevice_` all released; `captureEvent_` closed |

**SC-003 CPU verification**: Run a 60-second capture session and observe the AirBeam process CPU in Task Manager or Windows Performance Monitor. The capture thread should remain below 3% on a mid-range quad-core CPU at normal clock speed. If higher, check that MMCSS boost is active (FR-003) and that no unexpected allocations occur on the hot path.

---

## 9 — Spec and Design References

| Artifact | Path |
|----------|------|
| Feature specification | `specs/007-wasapi-loopback-capture/spec.md` |
| Implementation plan (this feature) | `specs/007-wasapi-loopback-capture/plan.md` |
| Phase 0 research decisions | `specs/007-wasapi-loopback-capture/research.md` |
| Data model & entity definitions | `specs/007-wasapi-loopback-capture/data-model.md` |
| WasapiCapture class contract | `specs/007-wasapi-loopback-capture/contracts/wasapi-capture-api.md` |
| Message protocol contract | `specs/007-wasapi-loopback-capture/contracts/audio-message-protocol.md` |
| AudioFrame format contract | `specs/007-wasapi-loopback-capture/contracts/audioframe-format.md` |
| Project constitution | `.specify/memory/constitution.md` |
