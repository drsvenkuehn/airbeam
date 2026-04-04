# Data Model: WASAPI Loopback Audio Capture (Feature 007)

**Branch**: `007-wasapi-loopback-capture`  
**Phase**: 1 — Design & Contracts  
**Status**: Complete

---

## Entity Overview

```
┌────────────────────────────────────────────────────────────────────────────┐
│  Thread 3 (capture)                  Thread 4 (ALAC encoder)               │
│                                                                             │
│  CaptureSession ──produces──► FrameAccumulator ──emits──► AudioFrame        │
│       │                                                       │             │
│       │  FormatConverter (optional, only if srcRate≠44100)    │             │
│       │  int16 samples ─────────────────────────────────────► │             │
│       │                                                       ▼             │
│  DeviceMonitor                            SpscRingBuffer<AudioFrame,512>    │
│  (IMMNotificationClient)                         │                          │
│       │ coalesce 20ms                            │ TryPop()                 │
│       ▼                                          ▼                          │
│  WasapiCapture (state machine)          AlacEncoderThread                   │
│       │                                                                     │
│       │ on error: PostMessage(WM_CAPTURE_ERROR, HRESULT, 0)                 │
│       ▼                                                                     │
│  AppController.hwnd_  ──► tray balloon + Stop()                             │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## Entities

### AudioFrame

The atomic unit of audio data exchanged between the capture subsystem and the ALAC encoder thread. One frame encodes exactly 8 ms of stereo audio at 44 100 Hz.

**Defined in**: `src/audio/AudioFrame.h` *(existing, no changes)*

| Field | Type | Description |
|-------|------|-------------|
| `samples` | `int16_t[704]` | 352 stereo PCM-16 samples, interleaved L/R: `[L₀, R₀, L₁, R₁, …, L₃₅₁, R₃₅₁]` |
| `frameCount` | `uint32_t` | Monotonically increasing sequence number assigned by WasapiCapture; wraps at `UINT32_MAX` |

**Invariants**:
- Exactly 704 `int16_t` values per frame (352 samples × 2 channels).
- Sample rate of contained audio is always **44 100 Hz** (after any format conversion).
- `sizeof(AudioFrame) == 1412` bytes (enforced by `static_assert`).
- `frameCount` is assigned by the capture thread and incremented per emitted frame; the encoder thread uses it for jitter/drop diagnostics only.

**Silence representation**: A frame produced during a silent capture window (`AUDCLNT_BUFFERFLAGS_SILENT`) has all `samples[]` elements equal to zero and a valid `frameCount`.

---

### FrameAccumulator

A stack-allocated accumulator that collects variable-length WASAPI capture buffers and emits complete 352-sample `AudioFrame` objects. It absorbs the mismatch between WASAPI's variable buffer sizes and the ALAC encoder's fixed 352-sample requirement.

**Defined in**: `src/audio/WasapiCapture.h` — private nested struct *(existing, no changes)*

| Field | Type | Description |
|-------|------|-------------|
| `buf` | `int16_t[704]` | In-progress frame accumulation buffer |
| `filled` | `int` | Count of `int16_t` values written into `buf` so far (range: 0–703) |

**Methods**:

| Method | Signature | Behaviour |
|--------|-----------|-----------|
| `Push` (float path) | `bool Push(const float* src, int frameCount, int channels, AudioFrame& out) noexcept` | Converts float32 → int16 (clamp + round), accumulates, emits when 704 elements filled |
| `Push` (int16 path) | `bool Push(const int16_t* src, int frameCount, int channels, AudioFrame& out) noexcept` | Accumulates directly (no conversion), emits when 704 elements filled |

**State transitions**:

```
EMPTY (filled==0) ──Push() with partial data──► FILLING (0 < filled < 704)
FILLING ──Push() completes 704 elements──► emits AudioFrame, resets to EMPTY
EMPTY / FILLING ──Push() with channels==1──► upmix: L copied to R before accumulation
```

**Allocation**: Entire struct lives on the stack (or as a class member). No heap allocation. No locking.

---

### SpscRingBuffer\<AudioFrame, kCaptureQueueFrames\>

The lock-free single-producer / single-consumer ring buffer connecting the capture thread (Thread 3) and the ALAC encoder thread (Thread 4). Capacity is fixed at compile time.

**Defined in**: `src/audio/SpscRingBuffer.h` *(extend: add 512-slot arm to variant)*

| Field | Type | Description |
|-------|------|-------------|
| `head_` | `alignas(64) std::atomic<uint32_t>` | Write index; owned by producer (Thread 3) |
| `tail_` | `alignas(64) std::atomic<uint32_t>` | Read index; owned by consumer (Thread 4) |
| `storage_` | `std::array<AudioFrame, N>` | Ring storage; pre-allocated as class member |

**Capacity constant** *(new in feature 007)*:
```cpp
// Declared in WasapiCapture.h
inline constexpr int kCaptureQueueFrames = 512;
```

**Memory**: `sizeof(SpscRingBuffer<AudioFrame, 512>)` ≈ **724 KB** (512 × 1412 bytes + two cache-line-aligned atomics). Allocated as a member of `ConnectionController` (or `AppController`) before streaming begins.

**Operations** (both allocation-free and lock-free):

| Method | Thread | Behaviour |
|--------|--------|-----------|
| `TryPush(const AudioFrame&)` | Thread 3 | Returns `true` on success; `false` if full (FR-011: caller drops the frame) |
| `TryPop(AudioFrame&)` | Thread 4 | Returns `true` on success; `false` if empty |

**`SpscRingBufferPtr` variant update** *(src/audio/SpscRingBuffer.h)*:
```cpp
// BEFORE (existing):
using SpscRingBufferPtr = std::variant<
    SpscRingBuffer<AudioFrame, 128>*,
    SpscRingBuffer<AudioFrame, 32>*>;

// AFTER (feature 007 adds 512-slot arm):
using SpscRingBufferPtr = std::variant<
    SpscRingBuffer<AudioFrame, 128>*,
    SpscRingBuffer<AudioFrame, 32>*,
    SpscRingBuffer<AudioFrame, 512>*>;   // ← new arm for kCaptureQueueFrames
```

---

### FormatConverter (Resampler)

Converts captured PCM from the device's native sample rate and bit depth to the AirPlay-required 44 100 Hz stereo S16LE format. Backed by libspeexdsp's `SpeexResamplerState`.

**Defined in**: `src/audio/Resampler.h` and `Resampler.cpp` *(replace libsamplerate with libspeexdsp)*

| Field | Type | Description |
|-------|------|-------------|
| `speexState_` | `SpeexResamplerState*` | Pre-allocated speexdsp resampler state; heap-allocated by `speex_resampler_init()` in constructor; freed in destructor |
| `srcRate_` | `uint32_t` | Native device sample rate (44 100, 48 000, 88 200, or 96 000 Hz) |
| `srcChannels_` | `uint32_t` | Native device channel count (1 or 2) |
| `passthrough_` | `bool` | `true` when `srcRate_ == 44100 && srcChannels_ == 2` — no conversion needed |
| `int16InBuf_` | `int16_t[4096*2]` | Pre-allocated int16 input staging buffer for float→int16 conversion |
| `int16OutBuf_` | `int16_t[4096*2]` | Pre-allocated int16 output buffer from speexdsp |

**Construction**:
```
Resampler(srcRate, srcChannels, dstRate=44100)
  if srcRate == 44100 && srcChannels == 2:  passthrough_ = true  (no speexdsp state)
  else: speex_resampler_init(2, srcRate, dstRate, SPEEX_RESAMPLER_QUALITY_7, &err)
```

**Hot-path method**:
```
int Process(const float* in, int16_t* out, int inFrames) noexcept
  1. Convert float32 → int16 into int16InBuf_  (clamp + round, no alloc)
  2. speex_resampler_process_interleaved_int(speexState_,
         int16InBuf_, &inLen, out, &outLen)
  3. Return outLen (output frame count)
```

**Invariants**:
- Constructor runs on Thread 3 inside `InitAudioClient()` — heap allocation here is outside the hot path (FR-010 compliant).
- `Process()` performs zero heap allocation; all buffers are pre-allocated members.
- `Process()` is never called if `passthrough_ == true`.
- Destroyed and recreated in `Reinitialise()` if device change produces a different mix format.

---

### CaptureSession (logical, not a class)

The set of COM objects representing an active WASAPI loopback session. Owned by `WasapiCapture`; created in `InitAudioClient()`, destroyed in `ReleaseAudioClient()`.

| Field in WasapiCapture | Type | Description |
|------------------------|------|-------------|
| `pEnumerator_` | `IMMDeviceEnumerator*` | Created once in constructor; lives for object lifetime |
| `pDevice_` | `IMMDevice*` | Default render endpoint for the current session |
| `pAudioClient_` | `IAudioClient*` | Session handle; `Initialize()` called with `AUDCLNT_SHAREMODE_SHARED \| AUDCLNT_STREAMFLAGS_LOOPBACK \| AUDCLNT_STREAMFLAGS_EVENTCALLBACK` |
| `pCaptureClient_` | `IAudioCaptureClient*` | Buffer access interface; `GetBuffer()` / `ReleaseBuffer()` on the hot path |
| `captureEvent_` | `HANDLE` | Kernel event signalled by the audio engine when a buffer period is ready |
| `pMixFormat_` | `WAVEFORMATEX*` | Device mix format; freed with `CoTaskMemFree` on session release |

**Session lifecycle**:
```
[Thread 3]: InitAudioClient()
  → GetDefaultAudioEndpoint(eRender, eConsole)
  → IAudioClient::GetMixFormat() → pMixFormat_
  → IAudioClient::Initialize(SHARED, LOOPBACK | EVENTCALLBACK, ...)
  → IAudioClient::SetEventHandle(captureEvent_)
  → Construct Resampler if pMixFormat_->nSamplesPerSec != 44100
  → IAudioClient::Start()

[Thread 3]: ReleaseAudioClient()
  → IAudioClient::Stop()
  → pCaptureClient_->Release(), pAudioClient_->Release(), pDevice_->Release()
  → CoTaskMemFree(pMixFormat_)
  → resampler_.reset()
```

---

### DeviceMonitor (IMMNotificationClient, inline in WasapiCapture)

The `WasapiCapture` class directly implements `IMMNotificationClient`. Only `OnDefaultDeviceChanged` has meaningful behaviour.

**New fields for coalescing** *(added in feature 007)*:

| Field | Type | Description |
|-------|------|-------------|
| `deviceChangedAt_` | `std::atomic<ULONGLONG>` | `GetTickCount64()` at the time of the first pending notification; 0 = no pending change |

**Coalescing invariant**: `deviceChangedAt_` transitions `0 → t` on the first notification; subsequent notifications within the 20 ms window are no-ops (compare-exchange fails). Thread 3 resets to 0 after triggering reinitialisation.

---

### WasapiCapture (state machine)

Top-level capture subsystem class. Manages the capture thread lifetime, all WASAPI COM objects, the device monitor, and the resampler.

**Defined in**: `src/audio/WasapiCapture.h` *(extend)*

**New constant** *(feature 007)*:
```cpp
// In WasapiCapture.h (global scope, before class declaration)
inline constexpr int kCaptureQueueFrames = 512;
```

**State machine**:

```
         Start()
IDLE ──────────────────────────────► STARTING
                                         │
                                    InitAudioClient() OK?
                                    ┌────┴────┐
                                    │         │
                                   YES        NO
                                    │         │
                                    ▼         ▼
                                RUNNING   post WM_CAPTURE_ERROR
                                    │     → IDLE
                        ┌───────────┼───────────────────┐
                        │           │                   │
                  Stop() called  deviceChangedAt_  HRESULT error
                        │        deadline elapsed       │
                        ▼           │                   ▼
                   STOPPING         ▼            post WM_CAPTURE_ERROR
                        │    REINITIALISING       → STOPPING → IDLE
                        │           │
                   thread_.join()  ReleaseAudioClient()
                        │         InitAudioClient() OK?
                        ▼         ┌────┴────┐
                       IDLE       │         │
                                 YES        NO
                                  │         │
                                  ▼         ▼
                               RUNNING  post WM_CAPTURE_ERROR
                                         → STOPPING → IDLE
```

**Thread ownership**:
- `Start()` / `Stop()` — called from Thread 1 (or any non-capture thread)
- `InitAudioClient()`, `ReleaseAudioClient()`, `Reinitialise()`, `ThreadProc()` — Thread 3 only
- `OnDefaultDeviceChanged()` — arbitrary COM notification thread (sets `deviceChangedAt_` atomically)

---

## Field Summary: Changes for Feature 007

| File | Change | Reason |
|------|--------|--------|
| `WasapiCapture.h` | Add `inline constexpr int kCaptureQueueFrames = 512;` | FR-008: named constant |
| `WasapiCapture.h` | Add `std::atomic<ULONGLONG> deviceChangedAt_{0};` | FR-007: 20 ms coalescing |
| `WasapiCapture.cpp` | Add `AvSetMmThreadCharacteristics` / `AvRevertMmThreadCharacteristics` | FR-003: MMCSS |
| `WasapiCapture.cpp` | Implement 20 ms coalescing via `deviceChangedAt_` + `WAIT_TIMEOUT` | FR-007 |
| `WasapiCapture.cpp` | Post `WM_CAPTURE_ERROR` on unrecoverable HRESULT | FR-009 |
| `WasapiCapture.cpp` | Replace 200 ms join timeout with `thread_.join()` | FR-012 |
| `WasapiCapture.cpp` | Add debug-level log calls at lifecycle events | FR-013 |
| `SpscRingBuffer.h` | Add `SpscRingBuffer<AudioFrame, 512>*` arm to `SpscRingBufferPtr` | FR-008: 512-slot queue |
| `Resampler.h` | Replace `SRC_STATE_tag*` with `SpeexResamplerState*`; update `Process()` signature | FR-006 |
| `Resampler.cpp` | Replace libsamplerate with libspeexdsp integer path | FR-006 |
| `Messages.h` | Add `WM_CAPTURE_ERROR = WM_APP + 10` | FR-009 |
| `AppController.h/cpp` | Add `OnCaptureError()` handler: stop stream + tray balloon | FR-009 |
| `CMakeLists.txt` | Remove libsamplerate FetchContent; add `add_subdirectory(third_party/speexdsp)` | FR-006 |

---

## Validation Rules

| Entity | Invariant | Enforcement |
|--------|-----------|-------------|
| `AudioFrame.samples` | Length always 704 int16_t | `static_assert(sizeof(AudioFrame) == 1412)` |
| `SpscRingBuffer<T,N>` | N is power of 2 | `static_assert((N & (N-1)) == 0)` |
| `kCaptureQueueFrames` | 512 — must equal the N used for the capture→encoder queue | Compile-time: caller constructs `SpscRingBuffer<AudioFrame, kCaptureQueueFrames>` |
| `Resampler` hot-path | No heap allocation in `Process()` | All buffers are pre-allocated class members; verified by SC-007 (memory profiler) |
| `FrameAccumulator` | `filled` never exceeds 703 | Loop logic + emit-and-reset on `filled >= 704` |
| `WasapiCapture` | `pCaptureClient_` non-null only in RUNNING / REINITIALISING | RAII via `ReleaseAudioClient()` |
