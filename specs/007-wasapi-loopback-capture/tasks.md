# Tasks: WASAPI Loopback Audio Capture (Feature 007)

**Feature Branch**: `007-wasapi-loopback-capture`
**Input**: Design documents from `specs/007-wasapi-loopback-capture/`
**Prerequisites**: plan.md ✅ | spec.md ✅ | research.md ✅ | data-model.md ✅ | contracts/ ✅ | quickstart.md ✅

**Tests**: Included — plan.md explicitly marks `test_resampler.cpp` and `test_wasapi_stop_start.cpp` as new
deliverables (← NEW); constitution §IV requires the WASAPI correlation integration test to compile and pass.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing.

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no in-flight dependencies on incomplete tasks)
- **[US#]**: Maps to user story (US1 = Continuous Capture, US2 = Format Compatibility,
  US3 = Device Recovery, US4 = Error Notification)
- Exact file paths are included in all task descriptions

---

## Phase 1: Setup — Build System & speexdsp Vendoring

**Purpose**: Vendor the five libspeexdsp resampler source files and wire them into the CMake build.
These tasks unblock the Resampler.cpp rewrite in Phase 4 (US2). All source files come from the public
Speex project (xiph.org, BSD-3-Clause licence — MIT-compatible per constitution §VII).

- [x] T001 [P] Create `third_party/speexdsp/include/` and vendor `speex_resampler.h` from the Speex project (source path in speexdsp repo: `include/speex/speex_resampler.h`)
- [x] T002 [P] Create `third_party/speexdsp/src/` and vendor `resample.c`, `arch.h`, `fixed_generic.h`, `os_support.h` from the Speex project (source path: `libspeexdsp/` subtree)
- [x] T003 [P] Create `third_party/speexdsp/CMakeLists.txt` — define `speexdsp_resampler` STATIC target from `src/resample.c`; set compile definitions `OUTSIDE_SPEEX=1`, `RANDOM_PREFIX=airbeam`, `FLOATING_POINT=0`, `FIXED_POINT=1`; expose `include/` as PUBLIC include directory and `src/` as PRIVATE; suppress MSVC C4244 with `target_compile_options(speexdsp_resampler PRIVATE /W0)` guarded by `if(MSVC)` (research.md Decision 1)
- [x] T004 Update root `CMakeLists.txt` — remove the `AIRBEAM_USE_RESAMPLER` / libsamplerate `FetchContent_Declare` + `FetchContent_MakeAvailable` block entirely; add `add_subdirectory(third_party/speexdsp)` before the main target definition; add `speexdsp_resampler` to `target_link_libraries` of the main airbeam target; keep the existing `avrt.lib` entry unchanged (plan.md Complexity Tracking, quickstart.md §1)

---

## Phase 2: Foundational — Constants, Message Identifiers & Type Extensions

**Purpose**: Add the compile-time constants, message IDs, and variant type arms that every user story
implementation depends on. These are pure header/declaration changes — no logic.

**⚠️ CRITICAL**: Phase 3 and later cannot begin until this phase is complete.

- [x] T005 [P] Add `constexpr UINT WM_CAPTURE_ERROR = WM_APP + 10;` to `src/core/Messages.h` on the line immediately after the existing `WM_UPDATE_REJECTED = WM_APP + 9` definition; preserve all existing constants; add a comment `// Feature 007: unrecoverable capture failure — WPARAM=HRESULT, LPARAM=0` (contracts/audio-message-protocol.md, research.md Decision 7)
- [x] T006 [P] Add `SpscRingBuffer<AudioFrame, 512>*` as the third arm to the `SpscRingBufferPtr` `std::variant` in `src/audio/SpscRingBuffer.h`; preserve the existing `SpscRingBuffer<AudioFrame, 128>*` and `SpscRingBuffer<AudioFrame, 32>*` arms unchanged; the `static_assert((N & (N-1)) == 0)` power-of-2 check already covers 512 (data-model.md, research.md Decision 5)
- [x] T007 Extend `src/audio/WasapiCapture.h` with three additions: **(1)** add `inline constexpr int kCaptureQueueFrames = 512;` at file scope before the class declaration; **(2)** add `std::atomic<ULONGLONG> deviceChangedAt_{0};` as a private member field alongside any existing atomics; **(3)** update the public API section to match the contract in `contracts/wasapi-capture-api.md` — `bool Start(SpscRingBufferPtr ring, HWND hwndMain)`, `void Stop()`, `bool IsRunning() const`, `uint64_t DroppedFrameCount() const`, `uint64_t UdpDropCount() const` — also add private member declarations for `std::atomic<bool> running_{false}`, `std::atomic<uint64_t> droppedFrameCount_{0}`, `HWND hwndMain_{nullptr}`, `SpscRingBufferPtr ring_`

**Checkpoint**: All headers compile. `SpscRingBuffer<AudioFrame, kCaptureQueueFrames>` instantiation is
valid. User story implementation can now begin.

---

## Phase 3: User Story 1 — Continuous System Audio Capture (Priority: P1) 🎯 MVP

**Goal**: Implement the core WASAPI loopback capture loop — MMCSS-boosted Thread 3 reads the default
render endpoint's system audio mix, packages audio into 352-sample frames via `FrameAccumulator`, and
delivers frames to the encoder queue via `SpscRingBuffer::TryPush`. `Stop()` uses unconditional
`thread_.join()`.

**Independent Test**: Connect AirBeam to an AirPlay receiver (e.g., shairport-sync), play a 60-second
file in Windows Media Player, verify continuous audio on the receiver with no dropouts (US1 acceptance
scenario 1–2); run `.\build\msvc-x64-debug\AirBeamTests.exe --gtest_filter="WasapiStopStart*"` to
verify 50-cycle Start/Stop passes with no leaks (SC-006).

### Tests for User Story 1

- [x] T008 [US1] Create `tests/unit/test_wasapi_stop_start.cpp` — `WasapiStopStartTest` suite: construct a `SpscRingBuffer<AudioFrame, 512>` on the stack; call `wasapi.Start(SpscRingBufferPtr{&ring}, testHwnd)` followed by `wasapi.Stop()` in a loop 50 times; assert `wasapi.IsRunning() == false` after each `Stop()`; verify `DroppedFrameCount()` accessible; use a message-only HWND created with `CreateWindowEx(0, "STATIC", ...)` or a null HWND stub for the notification target; the test must compile and run without a real audio device (may call `Start()` and get `false` / `WM_CAPTURE_ERROR` in a no-device CI environment — that is acceptable, the leak check is the primary assertion)

### Implementation for User Story 1

- [x] T009 [P] [US1] Implement `WasapiCapture::InitAudioClient()` (private helper) in `src/audio/WasapiCapture.cpp` — call `pEnumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice_)`; activate `IID_IAudioClient`; call `pAudioClient_->GetMixFormat(&pMixFormat_)`; call `pAudioClient_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0, 0, pMixFormat_, nullptr)`; create `captureEvent_` with `CreateEvent(nullptr, FALSE, FALSE, nullptr)` and call `pAudioClient_->SetEventHandle(captureEvent_)`; query `IID_IAudioCaptureClient` into `pCaptureClient_`; on any failing HRESULT call `ReleaseAudioClient()` and return `false`; on success log DEBUG "WasapiCapture: session open — format %.0f Hz %dch, resample=%s" (FR-013, research.md Decision 9)
- [x] T010 [P] [US1] Implement `WasapiCapture::ReleaseAudioClient()` (private helper) in `src/audio/WasapiCapture.cpp` — call `pAudioClient_->Stop()` if `pAudioClient_` non-null; release and null each pointer in order: `pCaptureClient_`, `pAudioClient_`, `pDevice_`; call `CoTaskMemFree(pMixFormat_)` and set to null; close `captureEvent_` with `CloseHandle` and set to null; call `resampler_.reset()` (no-op in US1 before US2 is integrated); each step guarded with null check to make the method safe to call after partial initialisation
- [x] T011 [US1] Implement `WasapiCapture::Start()` in `src/audio/WasapiCapture.cpp` — log DEBUG "WasapiCapture: starting capture on default render endpoint"; use `std::get_if<SpscRingBuffer<AudioFrame, 512>*>(&ring)` to verify the variant holds the correct arm — if null (wrong variant), post `WM_CAPTURE_ERROR(E_INVALIDARG, 0)` to `hwndMain_` and return `false` (consistent with all other Start() error paths and testable in CI without a real audio device); store `ring_` and `hwndMain_`; create `stopEvent_` with `CreateEvent(nullptr, TRUE, FALSE, nullptr)` (manual-reset); set `stopFlag_.store(false, memory_order_release)`; call `InitAudioClient()` — on failure post `WM_CAPTURE_ERROR` and return `false`; call `pAudioClient_->Start()`; launch `thread_` as `std::thread(&WasapiCapture::ThreadProc, this)`; set `running_.store(true, memory_order_release)`; return `true` (contracts/wasapi-capture-api.md calling sequence)
- [x] T012 [US1] Implement `WasapiCapture::Stop()` in `src/audio/WasapiCapture.cpp` — guard the entire method with `if (!thread_.joinable()) return;`; set `stopFlag_.store(true, memory_order_release)`; call `SetEvent(stopEvent_)`; call `thread_.join()` unconditionally — **remove any existing 200 ms `WaitForSingleObject` timeout** (FR-012, research.md Decision 6); after join set `running_.store(false)`; close `stopEvent_` and set to null; log DEBUG "WasapiCapture: capture thread exited cleanly"
- [x] T013 [US1] Implement `WasapiCapture::ThreadProc()` in `src/audio/WasapiCapture.cpp` — **(0) COM init**: call `CoInitializeEx(nullptr, COINIT_MULTITHREADED)` at ThreadProc start before any COM calls; **(1) MMCSS**: call `HANDLE mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &taskIndex)` before the capture loop (FR-003); **(2) wait loop**: `HANDLE handles[2] = { captureEvent_, stopEvent_ }; while(true) { DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 20); ... }` (FR-002); **(3) stop event branch** (`WAIT_OBJECT_0 + 1` or `stopFlag_`): break; **(4) audio event branch** (`WAIT_OBJECT_0`) — **HOT PATH BEGIN**: call `pCaptureClient_->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr)`; if `AUDCLNT_BUFFERFLAGS_SILENT`: use pre-allocated member `const int16_t* kSilenceBuf_` (initialized in `WasapiCapture` constructor to a static zero-filled array of size `kMaxFramesPerBuffer * 2` stereo) — call `accumulator_.Push(kSilenceBuf_, numFrames, 2)` (zero heap alloc, lock-free, FR-010 compliant); else call `accumulator_.Push(reinterpret_cast<float*>(pData), numFrames, pMixFormat_->nChannels, outFrame)` (float path, FR-004); if `Push` returns true: if `!ring_TryPush(outFrame)` then increment `droppedFrameCount_`, log TRACE "WasapiCapture: frame dropped — encoder queue full" (FR-011); `pCaptureClient_->ReleaseBuffer(numFrames)` **HOT PATH END** (FR-010: no alloc/lock/blocking between GetBuffer and ReleaseBuffer); **(5) post-loop**: call `AvRevertMmThreadCharacteristics(mmcssHandle)` if handle non-null; call `ReleaseAudioClient()`; set `running_.store(false)`; call `CoUninitialize()` after `AvRevertMmThreadCharacteristics()` and `ReleaseAudioClient()`
- [x] T014 [P] [US1] Implement `WasapiCapture::IsRunning()`, `DroppedFrameCount()`, and `UdpDropCount()` in `src/audio/WasapiCapture.cpp` — `IsRunning()` returns `running_.load(memory_order_acquire)`; `DroppedFrameCount()` returns `droppedFrameCount_.load(memory_order_relaxed)`; `UdpDropCount()` returns `0` (stub, passthrough stat not yet wired); ensure `droppedFrameCount_` is also incremented in `ThreadProc` (see T013) when `TryPush` returns false; reset `droppedFrameCount_` to 0 at the top of `Start()` each cycle

**Checkpoint**: US1 complete. `WasapiCapture::Start()` → `ThreadProc()` → `Stop()` lifecycle works
end-to-end. `WasapiStopStartTest` (50-cycle) passes. Core audio capture flows from WASAPI to
`SpscRingBuffer` at 44100 Hz (float→S16LE via `FrameAccumulator` float path).

---

## Phase 4: User Story 2 — Transparent Format Compatibility (Priority: P2)

**Goal**: Replace libsamplerate with libspeexdsp's `speex_resampler_process_interleaved_int()` so that
audio at 48000, 88200, or 96000 Hz is transparently resampled to 44100 Hz S16LE before entering the
encoder pipeline. Resampler state is pre-allocated in `InitAudioClient()` — zero runtime allocation on
the hot path.

**Independent Test**: Set Windows default audio device to 48000 Hz in Sound Settings, play a 1 kHz
reference sine wave, stream through AirBeam, verify correct pitch on the AirPlay receiver (SC-004);
run `.\build\msvc-x64-debug\AirBeamTests.exe --gtest_filter="Resampler*"`.

### Tests for User Story 2

- [x] T015 [US2] Create `tests/unit/test_resampler.cpp` — `ResamplerTest` suite with three test cases: **(a) PassthroughIdentity**: construct `Resampler(44100, 2)`, assert `IsPassthrough() == true`, call `Process()` and verify it returns without crashing and output count equals input count; **(b) Resample48kTo44k**: construct `Resampler(48000, 2)`, generate 480 frames (960 int16 samples) of a 1 kHz sine at 48000 Hz in a float input buffer, call `Process()`, verify output frame count is 441 ± 2 (ratio 44100/48000), verify zero-crossing rate of output matches a 1 kHz tone at 44100 Hz (SC-004); **(c) Resample96kTo44k**: same with 960 frames at 96000 Hz, expect ≈ 441 output frames (ratio 44100/96000) (plan.md §Project Structure, quickstart.md §2)

### Implementation for User Story 2

- [x] T016 [P] [US2] Rewrite `src/audio/Resampler.h` — replace `SRC_STATE_tag*` (libsamplerate) with `SpeexResamplerState* speexState_{nullptr}`; add pre-allocated private member arrays `int16_t int16InBuf_[4096 * 2]` and `int16_t int16OutBuf_[4096 * 2]`; add `bool passthrough_{false}`; declare `Process()` with the locked-in signature: `const int16_t* Process(const float* in, int inFrames, int& outFrames) noexcept` — writes to internal `int16OutBuf_` and returns a pointer to it; add `bool IsPassthrough() const noexcept`; remove `#include <samplerate.h>`; add `#include <speex_resampler.h>`; constructor takes `(uint32_t srcRate, uint32_t srcChannels, uint32_t dstRate = 44100)` (data-model.md FormatConverter entity)
- [x] T017 [US2] Rewrite `src/audio/Resampler.cpp` — **constructor**: if `srcRate == 44100 && srcChannels == 2` set `passthrough_ = true` and return; else call `speex_resampler_init(2, srcRate, dstRate, SPEEX_RESAMPLER_QUALITY_7, &err)`; if `err != RESAMPLER_ERR_SUCCESS` store error state (caller maps to `E_OUTOFMEMORY`); **destructor**: call `speex_resampler_destroy(speexState_)` if non-null; **`Process(const float* in, int inFrames, int& outFrames) noexcept`**: (1) convert float32 → int16 into `int16InBuf_` (multiply by 32767.f, `std::clamp` to [-32768, 32767], round to nearest); (2) `spx_uint32_t inLen = (spx_uint32_t)inFrames, outLen = 4096;`; call `speex_resampler_process_interleaved_int(speexState_, int16InBuf_, &inLen, int16OutBuf_, &outLen);`; (3) set `outFrames = (int)outLen`; return `int16OutBuf_`; **`IsPassthrough()`**: return `passthrough_`; remove all `src_process()` / `src_short_to_float_array()` / `src_float_to_short_array()` calls (research.md Decision 1, data-model.md)
- [x] T018 [US2] Integrate `Resampler` into `WasapiCapture::InitAudioClient()` in `src/audio/WasapiCapture.cpp` — after `GetMixFormat()`, if `pMixFormat_->nSamplesPerSec != 44100` construct `resampler_ = std::make_unique<Resampler>(pMixFormat_->nSamplesPerSec, pMixFormat_->nChannels)`; else call `resampler_.reset()` (passthrough — no speexdsp state); update the InitAudioClient success log line to include `resample=%s` showing "passthrough" or "NNNHz→44100Hz"; add `std::unique_ptr<Resampler> resampler_` as a private member of `WasapiCapture` if not already present (research.md Decision 8, data-model.md §DeviceMonitor notes on Reinitialise)
- [x] T019 [US2] Integrate Resampler hot-path into `WasapiCapture::ThreadProc()` in `src/audio/WasapiCapture.cpp` — replace the existing unconditional `accumulator_.Push(float*...)` call in the `WAIT_OBJECT_0` audio-event branch: **if** `resampler_ && !resampler_->IsPassthrough()`: call `int outFrames = 0; const int16_t* int16Ptr = resampler_->Process(reinterpret_cast<const float*>(pData), numFrames, outFrames)`; feed `int16Ptr` to `accumulator_.Push(int16_t* path, outFrames, 2, outFrame)` (int16 path); **else**: feed `reinterpret_cast<float*>(pData)` directly to `accumulator_.Push(float* path, numFrames, channels, outFrame)` (existing passthrough path); the `Process()` signature is `const int16_t* Process(const float* in, int inFrames, int& outFrames) noexcept` — it writes to `Resampler`'s internal `int16OutBuf_` member and returns a pointer to it; zero heap allocation on the hot path (FR-010)

**Checkpoint**: US1 + US2 complete. Audio at any standard Windows sample rate (44100, 48000, 88200,
96000 Hz) is transparently converted to 44100 Hz S16LE. `ResamplerTest` suite (passthrough, 48k→44k,
96k→44k) passes.

---

## Phase 5: User Story 3 — Automatic Device Change Recovery (Priority: P3)

**Goal**: Implement `IMMNotificationClient::OnDefaultDeviceChanged()` with the 20 ms fixed-deadline
coalescing mechanism so that changing the Windows default output device triggers exactly one
re-initialisation of the capture session within 50 ms, regardless of how many rapid notifications
arrive. Device re-attach runs on Thread 3 in the established COM apartment.

**Independent Test**: Start streaming, change the default audio output in Windows Sound Settings, verify
audio resumes on the AirPlay receiver within ~50 ms with no user interaction (SC-002); multiple rapid
device changes within 2 seconds trigger exactly one re-initialisation, not multiple; run
`ctest --preset msvc-x64-debug -L integration` if an audio device is available.

### Tests for User Story 3

- [x] T020 [US3] Update `tests/integration/test_wasapi_correlation.cpp` — adjust the `wasapi.Start(ring, hwnd)` call to pass a `SpscRingBuffer<AudioFrame, kCaptureQueueFrames>` (512-slot variant arm) if the scaffolded version uses an older signature or smaller capacity; confirm the cross-correlation > 0.99 assertion is still in place; verify the test compiles and passes the build step (constitution §IV mandatory test — must not be broken by the T011/T012 signature changes)

### Implementation for User Story 3

- [x] T021 [P] [US3] Implement `WasapiCapture::OnDefaultDeviceChanged()` in `src/audio/WasapiCapture.cpp` — filter: `if (flow != eRender || role != eConsole) return S_OK;`; record only the first notification in the window using `ULONGLONG expected = 0; deviceChangedAt_.compare_exchange_strong(expected, GetTickCount64(), std::memory_order_release, std::memory_order_relaxed);` (if `expected` was non-zero the CAS fails and this notification is a no-op, which is correct); log DEBUG "WasapiCapture: device change notification received (coalescing)"; return `S_OK` (FR-007, research.md Decision 4)
- [x] T022 [P] [US3] Wire up `IMMNotificationClient` lifecycle in `WasapiCapture` constructor and destructor in `src/audio/WasapiCapture.cpp` — **constructor**: call `CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, IID_IMMDeviceEnumerator, &pEnumerator_)` to create the long-lived enumerator; call `pEnumerator_->RegisterEndpointNotificationCallback(this)` to start receiving device-change events; **destructor**: call `pEnumerator_->UnregisterEndpointNotificationCallback(this)` before releasing; call `pEnumerator_->Release()`; note: the stub `OnDeviceAdded`, `OnDeviceRemoved`, `OnDeviceStateChanged`, `OnPropertyValueChanged` methods should already exist returning `S_OK` — only `OnDefaultDeviceChanged` gets a real implementation in T021 (contracts/wasapi-capture-api.md §Preconditions)
- [x] T023 [US3] Implement the device-change coalescing `WAIT_TIMEOUT` branch in `WasapiCapture::ThreadProc()` in `src/audio/WasapiCapture.cpp` — in the `WaitForMultipleObjects` result switch, add: `case WAIT_TIMEOUT: { ULONGLONG t = deviceChangedAt_.load(std::memory_order_acquire); if (t != 0 && (GetTickCount64() - t) >= 20) { deviceChangedAt_.store(0, std::memory_order_release); Reinitialise(); } break; }` — the 20 ms `WaitForMultipleObjects` timeout already set in T013 drives this check (FR-007, research.md Decision 4 pseudocode)
- [x] T024 [US3] Implement `WasapiCapture::Reinitialise()` in `src/audio/WasapiCapture.cpp` — log DEBUG "WasapiCapture: reinitialising on new default device"; call `ReleaseAudioClient()`; call `InitAudioClient()` — which will `GetDefaultAudioEndpoint` for the newly selected device and reconstruct the `Resampler` if the new format differs; on success: call `pAudioClient_->Start()`; `PostMessageW(hwndMain_, WM_DEFAULT_DEVICE_CHANGED, 0, 0)`; on failure: retry once if `hr == AUDCLNT_E_DEVICE_IN_USE`; if still failing: `PostMessageW(hwndMain_, WM_CAPTURE_ERROR, (WPARAM)hr, 0)`; break from the ThreadProc loop to exit cleanly (contracts/wasapi-capture-api.md §Device change recovery calling sequence)

**Checkpoint**: US1 + US2 + US3 complete. A device change triggers `OnDefaultDeviceChanged`, the
capture thread coalesces within 20 ms, calls `Reinitialise()` exactly once, and the stream resumes on
the new device within the 50 ms SC-002 budget. Rapid consecutive notifications do not cause double
re-initialisation or crashes.

---

## Phase 6: User Story 4 — Graceful Error Notification (Priority: P4)

**Goal**: Post `WM_CAPTURE_ERROR` to the application's main window on any unrecoverable WASAPI failure,
and handle it in `AppController` by stopping the stream and showing a tray balloon. The application
must never silently hang or continue showing a "streaming" indicator when capture has failed.

**Independent Test**: Start streaming, disable the default audio output device via Device Manager,
verify a tray balloon appears within 1 second (SC-005) and `IsRunning()` returns false; verify
`WasapiCapture::Stop()` is safe to call from the `WM_CAPTURE_ERROR` handler on Thread 1.

### Implementation for User Story 4

- [x] T025 [P] [US4] Add `WM_CAPTURE_ERROR` `PostMessageW` dispatch across all three error sites in `src/audio/WasapiCapture.cpp` — **(1) `InitAudioClient()` failure**: after the failing HRESULT is identified (no default device, format negotiation failure, `speex_resampler_init` failure), call `PostMessageW(hwndMain_, WM_CAPTURE_ERROR, (WPARAM)hr, 0)` and return false; **(2) `ThreadProc()` hot-path**: when `GetBuffer()` or a subsequent WASAPI call returns an unrecoverable HRESULT (`AUDCLNT_E_DEVICE_INVALIDATED`, `AUDCLNT_E_SERVICE_NOT_RUNNING`, `AUDCLNT_E_UNSUPPORTED_FORMAT`, or any non-`S_OK` excluding retriable errors), call `PostMessageW(hwndMain_, WM_CAPTURE_ERROR, (WPARAM)hr, 0)` then break from the capture loop; **(3) `Reinitialise()` final failure** (covered by T024 but ensure the PostMessage call is present); log DEBUG "WasapiCapture: unrecoverable error hr=0x%08x — posting WM_CAPTURE_ERROR" at each site (FR-009, research.md Decision 7)
- [x] T026 [P] [US4] Add `void OnCaptureError(HRESULT hr)` declaration to `src/core/AppController.h` and include `<winerror.h>` if not already included; the method is called from the `WM_CAPTURE_ERROR` message handler in `WndProc` (contracts/audio-message-protocol.md §Receiver responsibilities)
- [x] T027 [US4] Implement the `WM_CAPTURE_ERROR` case in `AppController::WndProc()` (or equivalent message handler) in `src/core/AppController.cpp` — `case WM_CAPTURE_ERROR: { HRESULT hr = static_cast<HRESULT>(wParam); wasapi_->Stop(); /* update tray icon to error/red state */ /* show localised tray balloon: IDS_CAPTURE_ERROR_BALLOON via the existing balloon infrastructure */ /* log: "WasapiCapture posted WM_CAPTURE_ERROR hr=0x%08x", hr */ break; }` — do **not** auto-restart capture; the handler must be idempotent (calling `Stop()` when already stopped is a no-op per FR-012); route through `OnCaptureError()` helper if preferred (contracts/audio-message-protocol.md §AppController handler skeleton)

**Checkpoint**: All 13 FRs implemented. All 4 user stories complete. Full feature functional.
`WM_CAPTURE_ERROR` posted within 1 second of device failure (SC-005).

---

## Phase 7: Polish & Validation

**Purpose**: Verify the build, unit test suite, compile-time invariants, and the end-to-end smoke test
described in quickstart.md.

- [x] T028 Build verification — run `cmake --preset msvc-x64-debug && cmake --build build/msvc-x64-debug --parallel`; confirm `AirBeam.exe` and `AirBeamTests.exe` build cleanly; no warnings from `src/` (vendored `third_party/speexdsp/` C4244 warnings suppressed by `/W0`); confirm `speexdsp_resampler.lib` appears in the build output (quickstart.md §1)
- [x] T029 [P] Run unit test suite against `tests/unit/test_resampler.cpp`, `tests/unit/test_wasapi_stop_start.cpp`, `tests/unit/test_frame_accumulator.cpp`, `tests/unit/test_spsc_ring.cpp` — `ctest --preset msvc-x64-debug -L unit --output-on-failure`; verify all four suites pass: `ResamplerTest` (PassthroughIdentity, Resample48kTo44k, Resample96kTo44k), `WasapiStopStartTest` (50-cycle leak check), `FrameAccumulatorTest` (existing), `SpscRingTest` (existing) (quickstart.md §2)
- [x] T030 [P] Verify compile-time invariants in `src/audio/AudioFrame.h`, `src/audio/SpscRingBuffer.h`, and `src/audio/WasapiCapture.h` — confirm `static_assert(sizeof(AudioFrame) == 1412)` compiles; confirm `static_assert((kCaptureQueueFrames & (kCaptureQueueFrames - 1)) == 0)` compiles (512 is power-of-2); confirm `SpscRingBuffer<AudioFrame, kCaptureQueueFrames>` instantiates using the new 512-slot arm of `SpscRingBufferPtr` (data-model.md validation rules)
- [x] T031 End-to-end smoke test per `specs/007-wasapi-loopback-capture/quickstart.md` §5–6 — launch `build/msvc-x64-debug/AirBeam.exe`, connect to shairport-sync receiver, play audio in Windows, change default output device mid-stream (verify ~50 ms recovery per SC-002), reconnect, disconnect; then disable the default audio device in Device Manager and verify a tray balloon fires within 1 second (SC-005)
- [x] T032 [P] Add CRT debug-heap allocation probe in `tests/unit/test_wasapi_stop_start.cpp`: call `_CrtSetAllocHook()` before the capture loop and assert zero allocations occur between `IAudioCaptureClient::GetBuffer()` and `ReleaseBuffer()` during a 5-second capture run; verifies SC-007 (no hot-path heap allocs)
- [x] T033 Add `IDS_CAPTURE_ERROR_BALLOON` string resource to `resources/en.rc` and add placeholder entry to all 6 other locale files (de, fr, es, ja, zh-Hans, ko) in `resources/` — required by §VIII locale compliance before any release tag
- [ ] T034 [RELEASE BLOCKER] Run 24-hour continuous AirBeam stream to shairport-sync (Docker); verify no memory growth via Task Manager/VLD and no audible drift — satisfies §IV 24-hour stress test requirement
  - Monitoring script: `tests/stress/monitor_stress_test.ps1` (pass: slope < 7 KB/min, total growth < 10 MB, process alive, container alive)
  - Setup: `docker compose up -d` → build + launch AirBeam → connect to AirBeam-Test → run monitor script

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately; T001, T002, T003 can be started in parallel
- **Foundational (Phase 2)**: Depends on Phase 1 completion (T004 must be done before headers referencing `speex_resampler.h` compile); T005, T006, T007 can proceed in parallel once T004 is done
- **US1 (Phase 3)**: Depends on Phase 2 completion — T008 (test) can be written in parallel with T009/T010; T011/T012/T014 can follow T009/T010; T013 (ThreadProc) last
- **US2 (Phase 4)**: Depends on Phase 3 completion — T015 (test) can be written in parallel with T016; T017 follows T016; T018/T019 extend Phase 3 implementations in the same .cpp file
- **US3 (Phase 5)**: Depends on Phase 3 + Phase 4 completion — T021/T022 can be parallel; T023 extends the ThreadProc `WAIT_TIMEOUT` branch from T013; T024 is a new method
- **US4 (Phase 6)**: Depends on Phase 2 (WM_CAPTURE_ERROR in Messages.h), Phase 3 (hwndMain_ in WasapiCapture), and Phase 5 (Reinitialise error path in T024) — T025/T026 can be parallel; T027 follows T026
- **Polish (Phase 7)**: Depends on all previous phases complete

### User Story Dependencies

- **US1 (P1)**: Depends only on Phase 2 — no dependency on US2/US3/US4; this is the MVP
- **US2 (P2)**: Depends on US1 — extends `WasapiCapture.cpp` methods already established in Phase 3
- **US3 (P3)**: Depends on US1 — extends `ThreadProc` with the `WAIT_TIMEOUT` branch; requires `InitAudioClient`/`ReleaseAudioClient` from Phase 3
- **US4 (P4)**: Depends on Phase 2 (for `WM_CAPTURE_ERROR` constant) and US1 (for `hwndMain_` in WasapiCapture); `AppController` changes can be written independently of US2/US3 after Phase 2

### Within Each Phase (Ordering Within a User Story)

- Test tasks first (TDD: write the failing test before the implementation)
- Private helper methods before the public API that calls them (`InitAudioClient`/`ReleaseAudioClient` before `Start`/`Stop`)
- Header declarations before `.cpp` implementations
- `ThreadProc` extensions (for US2 resampler path, US3 coalescing) after the base methods they extend are complete

### Parallel Opportunities

| Phase | Parallel tasks |
|-------|---------------|
| Phase 1 | T001, T002, T003 — different directories |
| Phase 2 | T005, T006, T007 — different files |
| Phase 3 | T009, T010 — different private methods; T014 — different public methods parallel with T013 |
| Phase 4 | T015, T016 — different files (test vs header) |
| Phase 5 | T021, T022 — different methods |
| Phase 6 | T025, T026 — different files (WasapiCapture.cpp vs AppController.h) |
| Phase 7 | T029, T030 — different verification steps |

---

## Parallel Example: User Story 1

```bash
# Step 1 — parallel (different private methods, same .cpp):
Task T009: Implement WasapiCapture::InitAudioClient()
Task T010: Implement WasapiCapture::ReleaseAudioClient()

# Step 2 — parallel after T009/T010 (different public methods):
Task T011: Implement WasapiCapture::Start()
Task T012: Implement WasapiCapture::Stop()
Task T014: Implement IsRunning()/DroppedFrameCount()/UdpDropCount()

# Step 3 — sequential (ThreadProc calls Init/Release and uses all the above):
Task T013: Implement WasapiCapture::ThreadProc()

# In parallel throughout:
Task T008: Create tests/unit/test_wasapi_stop_start.cpp
```

## Parallel Example: User Story 2

```bash
# Step 1 — parallel:
Task T015: Create tests/unit/test_resampler.cpp
Task T016: Rewrite src/audio/Resampler.h

# Step 2 — after T016:
Task T017: Rewrite src/audio/Resampler.cpp

# Step 3 — sequential (same file, extend Phase 3 methods):
Task T018: Integrate Resampler into InitAudioClient()
Task T019: Integrate Resampler hot-path into ThreadProc()
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (vendor speexdsp, update CMakeLists.txt)
2. Complete Phase 2: Foundational (Messages.h, SpscRingBuffer variant, WasapiCapture.h declarations)
3. Complete Phase 3: User Story 1 (core capture loop, MMCSS, unconditional Stop join)
4. **STOP and VALIDATE**: Run `WasapiStopStartTest` (50-cycle), build end-to-end, smoke test at 44100 Hz
5. Audio capture at 44100 Hz passthrough is working — MVP delivered

### Incremental Delivery

1. Setup + Foundational → project builds with speexdsp; libsamplerate completely removed
2. US1 → Core capture loop; 50-cycle Start/Stop test passes; smoke test passes at 44100 Hz
3. US2 → Resampler replaced; 48000/96000 Hz devices work transparently; ResamplerTest passes
4. US3 → Device change recovery; SC-002 (50 ms re-attach) verified
5. US4 → Error notification; tray balloon on device failure; SC-005 verified
6. Polish → Full test suite green; integration test passing; smoke test complete

### Parallel Team Strategy

With multiple developers after Phase 2 (Foundational) is complete:

- **Developer A**: User Story 1 — `WasapiCapture.cpp` core lifecycle + `test_wasapi_stop_start.cpp`
- **Developer B**: User Story 2 — `Resampler.h/cpp` rewrite + `test_resampler.cpp`
- **Developer C**: User Story 4 — `AppController.h/cpp` `WM_CAPTURE_ERROR` handler (only needs Phase 2)

US3 (device recovery) extends ThreadProc from US1 and must wait for Developer A to complete Phase 3.

---

## Notes

- **[P] tasks** touch different files or non-overlapping methods — no edit conflicts
- **[US#] labels** map each task to a specific user story for traceability and independent validation
- **Real-time hot-path constraint (FR-010)**: between `GetBuffer()` and `ReleaseBuffer()` — **NO** `new`, `malloc`, `delete`, `std::mutex::lock`, `WaitForSingleObject`, `Sleep`, `PostMessage`, or file I/O; all buffers pre-allocated before the loop
- **`AUDCLNT_BUFFERFLAGS_SILENT`**: the silence path still pushes frames with zero samples and a valid incrementing `frameCount` (audioframe-format.md §Silence Representation)
- **`PostMessageW` is always async-safe**: calling it from Thread 3 or the COM notification thread is correct and non-blocking
- **`WasapiCapture` IMMNotificationClient COM ref-counting**: `QueryInterface`/`AddRef`/`Release` should delegate to the containing object's lifetime; the existing implementation handles this — do not introduce a separate ref count in T022
- **`speex_resampler_init` error → `E_OUTOFMEMORY`**: if the speexdsp constructor fails (returns `RESAMPLER_ERR_ALLOC_FAILED`), map to `E_OUTOFMEMORY` and let `InitAudioClient()` post `WM_CAPTURE_ERROR` (contracts/wasapi-capture-api.md §Error Conditions)
- **`IMMNotificationClient` stub methods**: `OnDeviceAdded`, `OnDeviceRemoved`, `OnDeviceStateChanged`, `OnPropertyValueChanged` — if not already present, add them returning `S_OK` with no body; only `OnDefaultDeviceChanged` (T021) has real logic
- **Commit hygiene**: commit after each phase checkpoint at minimum; prefer per-task commits where the description carries the task ID (e.g., `T009: Implement WasapiCapture::InitAudioClient()`)
- **BSD-3-Clause source files** in `third_party/speexdsp/` are MIT-compatible (constitution §VII); include the Speex project copyright header verbatim in each vendored file as it appears in the original source
