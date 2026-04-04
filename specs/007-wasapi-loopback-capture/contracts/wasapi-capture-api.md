# Contract: WasapiCapture Class Interface

**Subsystem**: Audio Capture (Feature 007)  
**File**: `src/audio/WasapiCapture.h`  
**Contract type**: C++ class public API  
**Stability**: Internal — consumed by `AppController` / `ConnectionController`

---

## Overview

`WasapiCapture` is the sole entry point for system audio capture. It owns the WASAPI COM session, the capture thread (Thread 3), the device-change monitor, and the optional format converter. All external interaction occurs through the five public methods below plus two Windows messages it posts to the application window.

---

## Compile-Time Constant

```cpp
// Declared at file scope in WasapiCapture.h (before class declaration)
inline constexpr int kCaptureQueueFrames = 512;
```

This constant specifies the required capacity of the SPSC ring buffer passed to `Start()`. The caller **must** create a `SpscRingBuffer<AudioFrame, kCaptureQueueFrames>` (i.e., the 512-slot arm of `SpscRingBufferPtr`). Any other capacity value is a misuse of the contract.

---

## Public Interface

```cpp
class WasapiCapture : public IMMNotificationClient {
public:
    WasapiCapture();
    ~WasapiCapture();                   // calls Stop() if running

    WasapiCapture(const WasapiCapture&)            = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /// Start capture from the default Windows render endpoint.
    ///
    /// Parameters:
    ///   ring      — The SPSC ring buffer shared with the ALAC encoder thread
    ///               (Thread 4). Must be a SpscRingBuffer<AudioFrame, 512>* arm
    ///               of SpscRingBufferPtr. Caller owns the buffer and must keep
    ///               it alive until Stop() returns.
    ///   hwndMain  — The application's main hidden message window. Receives:
    ///               • WM_CAPTURE_ERROR (WPARAM=HRESULT, LPARAM=0) on unrecoverable failure
    ///               • WM_DEFAULT_DEVICE_CHANGED (WPARAM=0, LPARAM=0) on device re-attach
    ///
    /// Called from: Thread 1 (Win32 message loop / AppController)
    /// Returns: true on success; false if initial WASAPI session could not be created
    ///          (WM_CAPTURE_ERROR is also posted in the false case)
    bool Start(SpscRingBufferPtr ring, HWND hwndMain);

    /// Stop capture and release all WASAPI resources.
    ///
    /// Thread-safe: may be called from ANY thread (Thread 1, Thread 5, test threads).
    /// Blocking: does not return until the capture thread (Thread 3) has fully exited.
    /// Idempotent: calling Stop() when already stopped is a no-op.
    ///
    /// Post-conditions:
    ///   • No WASAPI COM objects remain open.
    ///   • No capture event handles remain open.
    ///   • The ring buffer pointer is cleared (caller may free it after Stop() returns).
    void Stop();

    // ── Queries ──────────────────────────────────────────────────────────────

    /// Returns true if the capture thread is active and audio is flowing.
    /// Thread-safe: reads an atomic flag (memory_order_acquire).
    bool IsRunning() const;

    /// Returns the total number of AudioFrames dropped due to a full encoder queue
    /// since the last Start(). Thread-safe: reads an atomic counter (relaxed).
    uint64_t DroppedFrameCount() const;

    /// Returns UDP drop count (passthrough stat from encoder thread, if wired).
    /// [DEFERRED: wired in a future encoder-stats feature; returns 0 for v1.0]
    uint64_t UdpDropCount() const;
};
```

---

## Calling Sequence

### Normal start / stream / stop

```
[Thread 1]                    [Thread 3 — created by Start()]
    │
    ├─ wasapi.Start(ring, hwnd) ──creates Thread 3──►
    │                                    │
    │                              InitAudioClient()
    │                              AvSetMmThreadCharacteristics("Audio")
    │                              IAudioClient::Start()
    │                              LOOP:
    │                                WaitForMultipleObjects(...)
    │                                GetBuffer() → convert → accumulate → TryPush()
    │                                ReleaseBuffer()
    │                              END LOOP
    │
    ├─ wasapi.Stop()
    │       │──SetEvent(stopEvent_)──────────────────►
    │       │                               loop exits
    │       │                          AvRevertMmThreadCharacteristics()
    │       │                          ReleaseAudioClient()
    │       │◄──────────────────────────────Thread 3 exits
    │       │  thread_.join() returns
    │   Stop() returns
```

### Device change recovery

```
[COM notification thread]     [Thread 3]
    │
    OnDefaultDeviceChanged()
    deviceChangedAt_ = GetTickCount64()  (atomic CAS, no-op if already set)
    │
    │ (20 ms elapses)
                                    WAIT_TIMEOUT fires
                                    deviceChangedAt_ elapsed?  YES
                                    ReleaseAudioClient()
                                    InitAudioClient() on new device
                                    IAudioClient::Start()
                                    PostMessage(WM_DEFAULT_DEVICE_CHANGED)
                                    continue capture loop
```

### Unrecoverable error

```
[Thread 3]
    │
    HRESULT hr = GetBuffer(...)  → AUDCLNT_E_DEVICE_INVALIDATED (or similar)
    ReleaseAudioClient()
    PostMessage(hwndMain_, WM_CAPTURE_ERROR, (WPARAM)hr, 0)
    thread exits
```

---

## Preconditions and Invariants

| Condition | Rule |
|-----------|------|
| COM initialisation | The calling thread (Thread 1) must have called `CoInitializeEx` before `Start()`. Thread 3 calls `CoInitializeEx(COINIT_MULTITHREADED)` internally for its own COM usage. |
| Ring buffer capacity | The `SpscRingBufferPtr` passed to `Start()` must hold a `SpscRingBuffer<AudioFrame, 512>*` pointer (the kCaptureQueueFrames arm). Passing a 128- or 32-slot arm is a logic error; behaviour is undefined. |
| HWND lifetime | `hwndMain` must remain valid for the entire duration between `Start()` and `Stop()`. The capture thread posts to it asynchronously. |
| Start/Stop symmetry | `Start()` must not be called while `IsRunning() == true`. `Stop()` is safe to call at any time, including before `Start()`. |
| Thread 3 hot-path | The following operations are **prohibited** inside `ThreadProc` between `GetBuffer` and `ReleaseBuffer`: `new`, `malloc`, `delete`, `std::mutex::lock`, `WaitForSingleObject`, `Sleep`, `PostMessage`, file I/O. |

---

## Error Conditions

| Error | HRESULT | WasapiCapture behaviour |
|-------|---------|------------------------|
| No default render device | `HRESULT_FROM_WIN32(ERROR_NOT_FOUND)` or `E_NOTFOUND` | `Start()` returns `false`; posts `WM_CAPTURE_ERROR` |
| Format negotiation failure | `AUDCLNT_E_UNSUPPORTED_FORMAT` | `InitAudioClient()` returns `false`; posts `WM_CAPTURE_ERROR` |
| Device invalidated (removed) | `AUDCLNT_E_DEVICE_INVALIDATED` | Treated as unrecoverable (not a re-attach); posts `WM_CAPTURE_ERROR` |
| Device change (re-enumerate) | `AUDCLNT_E_DEVICE_IN_USE` (rare) | `Reinitialise()` retried once; if still failing, posts `WM_CAPTURE_ERROR` |
| System suspend/resume | `AUDCLNT_E_SERVICE_NOT_RUNNING` | Treated as unrecoverable; posts `WM_CAPTURE_ERROR` |
| `speex_resampler_init` failure | N/A (returns int error) | Mapped to `E_OUTOFMEMORY`; posts `WM_CAPTURE_ERROR` |
