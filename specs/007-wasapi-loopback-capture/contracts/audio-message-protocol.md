# Contract: Audio Message Protocol

**Subsystem**: Audio Capture → Application (Feature 007)  
**Files**: `src/core/Messages.h`, `src/core/AppController.cpp`  
**Contract type**: Windows message protocol (PostMessage-based)  
**Stability**: Internal — posted by `WasapiCapture`, consumed by `AppController`

---

## Overview

`WasapiCapture` communicates asynchronous state changes to the application by posting Windows messages to the application's main hidden message window (`AppController::hwnd_`). This is the only cross-thread communication path from the audio capture subsystem to the UI layer.

All messages use `PostMessageW` (asynchronous, non-blocking) and are safe to call from any thread, including the COM notification thread and Thread 3.

---

## Message Definitions

Defined in `src/core/Messages.h`:

```cpp
// ── Existing messages (no changes) ──────────────────────────────────────────
constexpr UINT WM_TRAY_CALLBACK          = WM_APP + 1;
constexpr UINT WM_TRAY_POPUP_MENU        = WM_APP + 2;
constexpr UINT WM_RAOP_CONNECTED         = WM_APP + 3;
constexpr UINT WM_RAOP_FAILED            = WM_APP + 4;
constexpr UINT WM_RECEIVERS_UPDATED      = WM_APP + 5;
constexpr UINT WM_DEFAULT_DEVICE_CHANGED = WM_APP + 6;
constexpr UINT WM_BONJOUR_MISSING        = WM_APP + 7;
constexpr UINT WM_TEARDOWN_COMPLETE      = WM_APP + 8;
constexpr UINT WM_UPDATE_REJECTED        = WM_APP + 9;

// ── NEW: Feature 007 ─────────────────────────────────────────────────────────
constexpr UINT WM_CAPTURE_ERROR          = WM_APP + 10;  // unrecoverable capture failure
```

---

## Message Reference

### WM_CAPTURE_ERROR `(WM_APP + 10)` — NEW in Feature 007

| Field | Value | Description |
|-------|-------|-------------|
| `WPARAM` | `HRESULT` | The HRESULT error code that caused the failure, cast to `WPARAM` |
| `LPARAM` | `0` | Reserved; always zero in v1.0 |

**Sent by**: `WasapiCapture::ThreadProc()` (Thread 3) or `WasapiCapture::Start()` (Thread 1)  
**Sent when**: An unrecoverable capture failure occurs that cannot be resolved by device re-attachment  
**Frequency**: At most once per `Start()`/`Stop()` cycle  

**Receiver responsibilities** (`AppController`):
1. Call `wasapi_->Stop()` to ensure the capture thread is cleanly joined.
2. Update streaming state to "stopped" / "error".
3. Show a tray balloon notification to the user (localised error string).
4. Do **not** auto-restart capture — wait for the user to trigger reconnection.

**AppController handler skeleton**:
```cpp
case WM_CAPTURE_ERROR: {
    HRESULT hr = static_cast<HRESULT>(wParam);
    wasapi_->Stop();
    // Update tray icon to error state (red)
    // Show localised balloon: IDS_CAPTURE_ERROR_BALLOON
    // Log: "WasapiCapture posted WM_CAPTURE_ERROR hr=0x%08x"
    break;
}
```

**Common HRESULT values**:

| HRESULT | Symbolic name | Likely user-visible cause |
|---------|---------------|--------------------------|
| `0x88890004` | `AUDCLNT_E_DEVICE_INVALIDATED` | Audio device physically removed |
| `0x88890006` | `AUDCLNT_E_SERVICE_NOT_RUNNING` | Windows Audio service stopped / system suspend |
| `0x88890008` | `AUDCLNT_E_UNSUPPORTED_FORMAT` | Driver rejected mix format negotiation |
| `0x80070002` | `HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)` | No default render device exists |

---

### WM_DEFAULT_DEVICE_CHANGED `(WM_APP + 6)` — Existing, no change to payload

| Field | Value | Description |
|-------|-------|-------------|
| `WPARAM` | `0` | Unused |
| `LPARAM` | `0` | Unused |

**Sent by**: `WasapiCapture::ThreadProc()` (Thread 3) after a successful device re-attachment  
**Sent when**: The 20 ms coalescing deadline has elapsed and `Reinitialise()` succeeded  
**Frequency**: Once per completed device-change recovery event  

**Receiver responsibilities** (`AppController`):
- Optionally log the device change ("audio device changed, capture resumed").
- No tray balloon required for a successful re-attach (the stream continues uninterrupted from the user's perspective).
- If the UI showed a "device changed" indicator, clear it here.

---

## Message Ordering Guarantees

- `WM_DEFAULT_DEVICE_CHANGED` and `WM_CAPTURE_ERROR` are **mutually exclusive** for a given device-change event: either the re-attachment succeeded (→ `WM_DEFAULT_DEVICE_CHANGED`) or it failed (→ `WM_CAPTURE_ERROR`).
- `WM_CAPTURE_ERROR` is posted **at most once** between a `Start()` and `Stop()` call pair. After posting, the capture thread exits.
- Messages are delivered in posting order to the message queue; however, the Win32 message queue does not guarantee FIFO ordering between `PostMessage` calls from different threads. The handler must be idempotent.

---

## Invariants for Callers

- `hwndMain` must be a valid `HWND` for the entire duration of a capture session (between `Start()` and the point `Stop()` returns). `WasapiCapture` holds no reference count on the HWND.
- The message handler runs on Thread 1 (Win32 message loop). No audio objects should be accessed from Thread 1 without going through `WasapiCapture`'s public API.
- It is safe to call `wasapi_->Stop()` from inside a message handler; `Stop()` is designed to be called from any thread.
