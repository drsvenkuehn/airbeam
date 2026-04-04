# Research: WASAPI Loopback Audio Capture (Feature 007)

**Branch**: `007-wasapi-loopback-capture`  
**Phase**: 0 — Pre-design research  
**Status**: Complete — all unknowns resolved

---

## Decision 1 — Resampling Library: libspeexdsp (integer path)

**Decision**: Use `speex_resampler_process_interleaved_int()` from libspeexdsp, vendored as `third_party/speexdsp/`.

**Rationale**:
- libspeexdsp provides the only major resampler with a direct `int16_t` API. WASAPI loopback buffers are typically `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`, but the conversion `float32 → int16` must happen regardless (to produce S16LE output). With libspeexdsp, that single conversion feeds the resampler directly; there is no float-intermediate resampler buffer on the hot path.
- `speex_resampler_init(2, srcRate, 44100, SPEEX_RESAMPLER_QUALITY_7, &err)` pre-allocates all internal state once; `Process()` calls thereafter are allocation-free.
- BSD-3-Clause licence: compatible with AirBeam's MIT licence (constitution §VII satisfied).
- Source footprint: 5 files, ~1 200 lines of C. Vendoring is trivial; no autoconf/CMake build system to wrap.

**Quality level — SPEEX_RESAMPLER_QUALITY_7**:
- SNR for 48 000 → 44 100 Hz: ~97 dB at quality 7 vs ~86 dB at quality 5.
- CPU cost: ~0.3% on a single modern core for stereo 44 100 Hz output. Well within the 3% capture-thread budget (SC-003).
- SC-004 requires perceptually transparent conversion at normal listening volume; quality 7 comfortably exceeds typical audibility thresholds (~80 dB SNR).
- Quality 10 (maximum) adds ~0.2% CPU for ~2 dB additional SNR benefit; not justified.

**Vendoring approach**:
Files required from `libspeexdsp` source tree (Speex project, GitLab xiph.org):

| Vendored file | Source path in speexdsp repo |
|---------------|------------------------------|
| `third_party/speexdsp/include/speex_resampler.h` | `include/speex/speex_resampler.h` |
| `third_party/speexdsp/src/resample.c` | `libspeexdsp/resample.c` |
| `third_party/speexdsp/src/arch.h` | `libspeexdsp/arch.h` |
| `third_party/speexdsp/src/fixed_generic.h` | `libspeexdsp/fixed_generic.h` |
| `third_party/speexdsp/src/os_support.h` | `libspeexdsp/os_support.h` |

CMake target (`third_party/speexdsp/CMakeLists.txt`):
```cmake
add_library(speexdsp_resampler STATIC src/resample.c)
target_include_directories(speexdsp_resampler
    PUBLIC  include
    PRIVATE src)
target_compile_definitions(speexdsp_resampler PRIVATE
    OUTSIDE_SPEEX=1        # prevents symbol conflicts with any full speex build
    RANDOM_PREFIX=airbeam  # namespaces internal symbols
    FLOATING_POINT=0       # use fixed-point path (int16-friendly)
    FIXED_POINT=1)
if(MSVC)
    target_compile_options(speexdsp_resampler PRIVATE /W0)  # suppress C4244 in vendored code
endif()
```

**Alternatives considered**:

| Library | API | Why rejected |
|---------|-----|--------------|
| libsamplerate (SRC_SINC_BEST_QUALITY) | `float32` only | Requires `src_short_to_float_array` + `src_float_to_short_array` on every buffer — two extra conversion loops on the real-time thread; FP-denormal risk |
| r8brain-free-src | `double` only | Even heavier float conversion; C++ template-heavy; harder to vendor as a minimal STATIC target |
| Custom linear interpolation | int16, trivial | Prohibited by constitution §Toolchain; audible aliasing at 48 000→44 100 Hz |

---

## Decision 2 — WASAPI Event-Driven Capture Pattern

**Decision**: `AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK` with `IAudioClient::SetEventHandle(captureEvent_)`. Capture thread uses `WaitForMultipleObjects({captureEvent_, stopEvent_}, …)`.

**Rationale**:
- `AUDCLNT_STREAMFLAGS_EVENTCALLBACK` is the WASAPI-native event signalling path (satisfies FR-002: event-signalling, not polling).
- The Windows audio engine signals `captureEvent_` when a buffer period worth of audio is available (~10 ms in shared mode), replacing any user-space sleep or timer.
- `WaitForMultipleObjects` with two handles (audio event + stop event) allows `Stop()` to unblock the capture thread immediately from any calling thread without additional flag checks in a tight loop.
- **Hot-path boundary**: the WaitForMultipleObjects call is the *event dispatcher*, not part of the sample-processing hot path. FR-010's "no blocking OS wait primitives" prohibition applies to the data processing path (GetBuffer → convert → accumulate → TryPush). The WFMO wait is structurally outside that boundary.
- Loopback requires `eRender` endpoint: `IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eConsole, &pDevice)`.
- Mix format retrieved via `IAudioClient::GetMixFormat()`. Typical result: `WAVEFORMATEXTENSIBLE`, subtype `KSDATAFORMAT_SUBTYPE_IEEE_FLOAT`, 32-bit float, 2 channels, 48 000 Hz.

**Capture thread skeleton** (pseudocode):
```cpp
// Pre-loop: MMCSS boost, initialize audio client, SetEventHandle
HANDLE handles[2] = { captureEvent_, stopEvent_ };
while (true) {
    DWORD wait = WaitForMultipleObjects(2, handles, FALSE, deviceCoalesceMs);
    if (wait == WAIT_OBJECT_0 + 1 || stopFlag_) break;   // stop event
    if (wait == WAIT_TIMEOUT) {
        // Check device-change coalescing deadline (Decision 4)
        if (deviceChangePending_ && TimeSinceFirstChange() >= 20ms) Reinitialise();
        continue;
    }
    // HOT PATH BEGINS
    BYTE* pData; UINT32 numFrames; DWORD flags;
    pCaptureClient_->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr);
    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
        // convert + accumulate + TryPush (FR-010 zone: no alloc, no lock, no blocking)
    }
    pCaptureClient_->ReleaseBuffer(numFrames);
    // HOT PATH ENDS
}
// Post-loop: AvRevertMmThreadCharacteristics, ReleaseAudioClient, log
```

---

## Decision 3 — MMCSS Thread Priority Boost

**Decision**: Call `AvSetMmThreadCharacteristics(L"Audio", &taskIndex)` at the top of `WasapiCapture::ThreadProc()`, before `IAudioClient::Start()`. Store the returned handle; call `AvRevertMmThreadCharacteristics(mmcssHandle)` before the thread exits.

**Rationale**:
- MMCSS "Audio" task raises the thread's scheduling priority above normal and reduces timer resolution to 1 ms, directly supporting FR-003.
- `avrt.lib` is already linked in `CMakeLists.txt` and `<avrt.h>` is already included in `WasapiCapture.h` — no build changes needed for MMCSS.
- Must be called from Thread 3 itself (not from Thread 1 calling `Start()`), as MMCSS boosts are per-thread.
- Must be reverted before the thread exits to avoid leaking the MMCSS registration.

**API**:
```cpp
DWORD taskIndex = 0;
HANDLE mmcssHandle = AvSetMmThreadCharacteristics(L"Audio", &taskIndex);
// ... capture loop ...
if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
```

---

## Decision 4 — Device-Change Coalescing (20 ms Fixed-Deadline Timer)

**Decision**: Track the time of the first pending device-change notification via `std::atomic<ULONGLONG> deviceChangedAt_`. The capture thread's `WaitForMultipleObjects` uses a short timeout (20 ms) so it periodically checks whether the 20 ms deadline has elapsed and reinitialises exactly once.

**Rationale**:
- `IMMNotificationClient::OnDefaultDeviceChanged` is called on an arbitrary COM thread (not Thread 3). It must not reinitialise WASAPI resources directly; it must signal Thread 3.
- Using an atomic timestamp (`GetTickCount64()`) instead of a separate Win32 timer avoids:
  - Thread proliferation (no timer thread)
  - `SetTimer` / `KillTimer` COM/UI thread coupling
  - Additional event handle management
- The 20 ms window is enforced naturally: the capture thread wakes on `WAIT_TIMEOUT` after 20 ms, sees `deviceChangedAt_` is set and elapsed, and calls `Reinitialise()`. Any notifications arriving within that 20 ms window simply find `deviceChangedAt_` already set (no overwrite of an earlier timestamp).
- `Reinitialise()` runs on Thread 3 in the already-established COM apartment, so `IAudioClient` teardown/recreation is safe.

**Coalescing protocol**:
```cpp
// Called on COM notification thread (IMMNotificationClient):
HRESULT OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) {
    if (flow != eRender || role != eConsole) return S_OK;
    ULONGLONG expected = 0;
    // Only record the first notification; subsequent ones within the window are no-ops.
    deviceChangedAt_.compare_exchange_strong(expected, GetTickCount64(),
        std::memory_order_release, std::memory_order_relaxed);
    return S_OK;
}

// In capture thread's WAIT_TIMEOUT branch:
ULONGLONG t = deviceChangedAt_.load(std::memory_order_acquire);
if (t != 0 && (GetTickCount64() - t) >= 20) {
    deviceChangedAt_.store(0, std::memory_order_release);
    Reinitialise();  // tears down old session, creates new one on Thread 3
}
```

**Alternatives considered**:

| Approach | Why rejected |
|----------|--------------|
| `PostMessage(WM_DEFAULT_DEVICE_CHANGED)` to Thread 1, reinit from AppController | Reinit must happen on Thread 3 (which owns the COM apartment holding WASAPI objects); crossing to Thread 1 and back adds latency and complexity |
| Win32 `SetTimer` on a helper thread | Requires a message loop; adds a 6th thread; timer resolution is coarser than needed |
| Fixed-sleep in OnDefaultDeviceChanged | Blocking the COM notification thread; prohibited real-time safety violation |

---

## Decision 5 — `kCaptureQueueFrames = 512` and Ring Buffer Sizing

**Decision**: Declare `inline constexpr int kCaptureQueueFrames = 512;` in `WasapiCapture.h`. Add `SpscRingBuffer<AudioFrame, 512>*` as a third arm to `SpscRingBufferPtr` variant in `SpscRingBuffer.h`. The caller (ConnectionController / AppController) must create a `SpscRingBuffer<AudioFrame, 512>` for the capture→encoder path.

**Rationale**:
- 512 frames × 352 samples/frame ÷ 44 100 Hz ≈ **4.1 seconds** of buffering. This provides substantial headroom for encoder bursts without unbounded memory.
- Memory cost: 512 × `sizeof(AudioFrame)` = 512 × (704 × 2 + 4) = 512 × 1 412 bytes ≈ **706 KB** — easily within budget for a desktop application.
- `SpscRingBuffer<T,N>` requires N to be a power of 2 (static_assert enforced). 512 = 2⁹ ✅.
- The existing 128-slot and 32-slot arms remain in the variant for other queues; the 512-slot arm is used exclusively for the capture→encoder path.

---

## Decision 6 — `Stop()` Thread-Safety and Join Behaviour

**Decision**: `Stop()` sets `stopFlag_` atomically, signals `stopEvent_` with `SetEvent`, then calls `thread_.join()` unconditionally (no timeout).

**Rationale**:
- FR-012 requires Stop() to "return only after the capture thread has fully exited". An unconditional `join()` is the only implementation that satisfies this guarantee.
- The existing 200 ms join timeout must be removed. The capture thread will exit promptly (within one WASAPI buffer period, ~10 ms) once `stopEvent_` is signalled, because the `WaitForMultipleObjects` call will return `WAIT_OBJECT_0 + 1`.
- `SetEvent(stopEvent_)` is safe from any thread (it is a kernel event handle). `thread_.join()` blocks the caller but does not run on Thread 3.
- If `Stop()` is called before `Start()` has been called (or after the thread has already exited), the `thread_.joinable()` guard prevents a double-join crash.

---

## Decision 7 — `WM_CAPTURE_ERROR` Message Assignment

**Decision**: Add `constexpr UINT WM_CAPTURE_ERROR = WM_APP + 10;` to `src/core/Messages.h`. (The current highest defined constant is `WM_UPDATE_REJECTED = WM_APP + 9`.)

**Rationale**:
- All existing `WM_APP+1` through `WM_APP+9` slots are taken; `WM_APP+10` is the next available slot.
- `WPARAM` = HRESULT error code (as specified in the clarification session).
- `LPARAM` = 0 (reserved/unused, as specified).
- `AppController` handles this message by calling `wasapi_->Stop()` and then displaying a tray balloon notification with a localised error string.

---

## Decision 8 — Resampler Instantiation Guard

**Decision**: Construct the `Resampler` in `WasapiCapture::InitAudioClient()` (which runs on Thread 3) only when `pMixFormat_->nSamplesPerSec != 44100`. Store as `std::unique_ptr<Resampler> resampler_`.

**Rationale**:
- FR-006: "Resampler only instantiated when device native rate ≠ 44100 Hz."
- Constructing in `InitAudioClient()` means re-initialisation after a device change will recreate the resampler with the new device's mix format automatically.
- `Resampler::IsPassthrough()` returns `true` when `srcRate == 44100 && srcChannels == 2`, allowing the hot path to skip the conversion step entirely without a branch on `resampler_ != nullptr`.
- Pre-allocated internal buffers in the new speexdsp `Resampler` hold at most `4096 * 2` int16 values (≈16 KB) on the stack equivalent — declared as members, not heap. Actual `SpeexResamplerState` is heap-allocated by `speex_resampler_init()` during `InitAudioClient()`, before the capture loop begins (compliant with FR-010).

---

## Decision 9 — Debug Logging Sink

**Decision**: Use the existing AirBeam log sink (whatever macro/function is currently used in `WasapiCapture.cpp`) at `DEBUG`/`TRACE` severity for lifecycle events. No new log file, no new logging infrastructure.

**Log events to emit** (FR-013):
| Event | Severity | Message |
|-------|----------|---------|
| `Start()` called | DEBUG | "WasapiCapture: starting capture on default render endpoint" |
| `InitAudioClient()` succeeds | DEBUG | "WasapiCapture: session open — format %.0f Hz %dch, resample=%s" |
| Device-change notification received | DEBUG | "WasapiCapture: device change notification received (coalescing)" |
| Reinitialise triggered | DEBUG | "WasapiCapture: reinitialising on new default device" |
| Buffer dropped (queue full) | TRACE | "WasapiCapture: frame dropped — encoder queue full" |
| Unrecoverable error | DEBUG | "WasapiCapture: unrecoverable error hr=0x%08x — posting WM_CAPTURE_ERROR" |
| `Stop()` returns | DEBUG | "WasapiCapture: capture thread exited cleanly" |

---

## Resolved Unknowns Summary

| Unknown | Resolution |
|---------|-----------|
| Which resampler library? | libspeexdsp (integer API), vendored BSD-3-Clause |
| WASAPI trigger mechanism? | Event-driven: `AUDCLNT_STREAMFLAGS_EVENTCALLBACK` + `SetEventHandle` |
| MMCSS API usage? | `AvSetMmThreadCharacteristics(L"Audio", &taskIndex)` in ThreadProc |
| Device-change coalescing mechanism? | Atomic timestamp + 20 ms WFMO timeout on capture thread |
| Ring buffer capacity constant? | `kCaptureQueueFrames = 512`, declared in `WasapiCapture.h` |
| `WM_CAPTURE_ERROR` slot? | `WM_APP + 10` (next free after `WM_UPDATE_REJECTED = WM_APP + 9`) |
| `Stop()` join behaviour? | Unconditional `thread_.join()` — remove 200 ms timeout |
| Resampler quality level? | `SPEEX_RESAMPLER_QUALITY_7` (SNR ~97 dB, CPU ~0.3%) |
