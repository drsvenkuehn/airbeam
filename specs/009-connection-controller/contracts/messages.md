# Contract: Win32 Window Messages

**File**: `src/core/Messages.h`  
**Consumer**: `AppController::WndProc` (dispatches to `ConnectionController`)  
**Producers**: `RaopSession`, `WasapiCapture`, `MdnsDiscovery`, `ConnectionController`

All messages are in the `WM_APP` range (16-bit safe, no collision with system messages).
All messages are posted (not sent) from background threads to the main `HWND`; they are
processed on Thread 1 in the Win32 message loop.

---

## Complete Message Table

| Constant | Value | Producer | Consumer | WPARAM | LPARAM |
|----------|-------|----------|----------|--------|--------|
| `WM_TRAY_CALLBACK` | `WM_APP+1` | Shell | AppController | `NOTIFY_UID` | `LOWORD`: mouse/keyboard event |
| `WM_TRAY_POPUP_MENU` | `WM_APP+2` | AppController | AppController | 0 | 0 |
| `WM_RAOP_CONNECTED` | `WM_APP+3` | RaopSession (T5) | ConnectionController | 0 | 0 |
| `WM_RAOP_FAILED` | `WM_APP+4` | RaopSession (T5) | ConnectionController | error code | 0 |
| `WM_RECEIVERS_UPDATED` | `WM_APP+5` | MdnsDiscovery (T2) | AppController (TrayMenu refresh) | list version | 0 |
| `WM_AUDIO_DEVICE_LOST` | `WM_APP+6` | WasapiCapture (T3) | ConnectionController | 0 | 0 |
| `WM_BONJOUR_MISSING` | `WM_APP+7` | MdnsDiscovery (T2) | AppController | 0 | 0 |
| `WM_STREAM_STOPPED` | `WM_APP+8` | ConnectionController (T1) | AppController | 0 | 0 |
| `WM_UPDATE_REJECTED` | `WM_APP+9` | SparkleIntegration | AppController | 0 | 0 |
| `WM_SPEAKER_LOST` | `WM_APP+10` | MdnsDiscovery (T2) | ConnectionController | 0 | device ID ptr (see below) |
| `WM_STREAM_STARTED` | `WM_APP+11` | ConnectionController (T1) | AppController | 0 | 0 |
| `WM_CAPTURE_ERROR` | `WM_APP+12` | WasapiCapture (T3) | ConnectionController | error code | 0 |
| `WM_DEVICE_DISCOVERED` | `WM_APP+13` | MdnsDiscovery (T2) | ConnectionController | list version | receiver index |
| `WM_ENCODER_ERROR` | `WM_APP+14` | AlacEncoderThread (T4) | ConnectionController | HRESULT error code | 0 |

---

## Renamed Messages (Breaking Change — Source-Level Only)

Two existing constants are renamed in `Messages.h`. The integer value is unchanged, so no
binary or ABI break occurs. **All callers must be updated** before the build will compile.

| Old Name | New Name | Value | Rationale |
|----------|----------|-------|-----------|
| `WM_DEFAULT_DEVICE_CHANGED` | `WM_AUDIO_DEVICE_LOST` | `WM_APP+6` | Old name described a Windows system event; new name matches the spec contract and clarifies the handler path (capture-only restart). |
| `WM_TEARDOWN_COMPLETE` | `WM_STREAM_STOPPED` | `WM_APP+8` | Old name was internal jargon; new name is user-facing and matches `WM_STREAM_STARTED` symmetry. |

---

## Message Detail Specifications

### WM_AUDIO_DEVICE_LOST (WM_APP+6)

```
Producer : WasapiCapture (Thread 3)
Condition: IAudioClient session invalidated due to device removal, device change,
           or default device switch (AUDCLNT_E_DEVICE_INVALIDATED)
WPARAM   : 0
LPARAM   : 0
Handler  : ConnectionController::OnAudioDeviceLost()
Effect   : Capture-only restart (FR-009):
             1. WasapiCapture::Stop()        — join Thread 3 (≤30 ms timeout)
             2. WasapiCapture::Init()         — re-enumerate default device (on Thread 1)
             3. WasapiCapture::Start()        — restart Thread 3
             If step 2 fails: full BeginDisconnect() + balloon notification (FR-011)
State    : Streaming → Streaming (no state change on success)
           Streaming → Disconnecting (on re-init failure)
```

### WM_RAOP_FAILED (WM_APP+4)

```
Producer : RaopSession (Thread 5)
Condition: Fatal RTSP error (TCP disconnect, TEARDOWN from receiver, timeout)
WPARAM   : HRESULT or Win32 error code
LPARAM   : 0
Handler  : ConnectionController::OnRaopFailed()
Effect   : If state == Streaming: BeginDisconnect(forReconnect=true)
           If state == Connecting: BeginDisconnect(forReconnect=true)
           Otherwise: silent discard + TRACE (FR-021)
```

### WM_SPEAKER_LOST (WM_APP+10)

```
Producer : MdnsDiscovery (Thread 2)
Condition: DNSServiceBrowse kDNSServiceFlagsAdd=0 for a previously discovered service
WPARAM   : 0
LPARAM   : pointer to heap-allocated std::wstring containing the device service name.
           ConnectionController MUST delete this pointer after reading it.
Handler  : ConnectionController::OnSpeakerLost(devId)
Effect   : If devId == session_->Target().serviceName AND state == Streaming:
             BeginDisconnect(forReconnect=true)  [same path as WM_RAOP_FAILED]
           Otherwise: silent discard + TRACE (FR-021)
Lifetime : The std::wstring* is heap-allocated by MdnsDiscovery on Thread 2 and
           deleted by ConnectionController on Thread 1. This is the only safe
           ownership pattern for posted messages carrying strings.
```

### WM_STREAM_STARTED (WM_APP+11)

```
Producer : ConnectionController (Thread 1) — posted to itself
Condition: AlacEncoderThread::Start() succeeded after WM_RAOP_CONNECTED
WPARAM   : 0
LPARAM   : 0
Handler  : AppController::HandleStreamStarted()
Effect   : TrayIcon::SetState(TrayState::Streaming, targetDeviceName)
           Update tray menu: show "Disconnect" item, disable speaker list
```

### WM_STREAM_STOPPED (WM_APP+8)

```
Producer : ConnectionController (Thread 1) — posted to itself after teardown complete
WPARAM   : 0
LPARAM   : 0
Handler  : ConnectionController::OnStreamStopped() → AppController::HandleStreamStopped()
Effect   : TrayIcon::SetState(TrayState::Idle)
           Update tray menu: hide "Disconnect", enable speaker list
Note     : ConnectionController::OnTeardownComplete() handles reconnect scheduling
           before AppController sees the message. The controller posts WM_STREAM_STOPPED
           AFTER deciding whether to reconnect, so AppController always sees a clean
           Idle state when it processes this message.
```

### WM_DEVICE_DISCOVERED (WM_APP+13)

```
Producer : MdnsDiscovery (Thread 2)
Condition: New AirPlay receiver resolved and added to ReceiverList
WPARAM   : ReceiverList version tag (uint32_t) — for staleness detection
LPARAM   : index in ReceiverList (int) at the time of posting
Handler  : ConnectionController::OnDeviceDiscovered()
Effect   : If inAutoConnectWindow_ is true:
             Check if ReceiverList[LPARAM].mac == config_.lastDevice
             If match: Connect(receiver)  [FR-015]
           Posts WM_RECEIVERS_UPDATED to trigger TrayMenu refresh (via AppController)
Staleness: If WPARAM != ReceiverList::CurrentVersion(), the receiver may have moved;
           ConnectionController reads by MAC from ReceiverList to be safe.
```

### WM_CAPTURE_ERROR (WM_APP+12)

```
Producer : WasapiCapture (Thread 3)
Condition: Non-device-loss capture error (AUDCLNT_E_BUFFER_ERROR, overrun, etc.)
WPARAM   : HRESULT error code
LPARAM   : 0
Handler  : ConnectionController::OnCaptureError()
Effect   : BeginDisconnect()  [full teardown, no reconnect]
           balloon_.Show(IDS_ERROR_CAPTURE_FAILED, ...)
```

### WM_ENCODER_ERROR (WM_APP+14)

```
Producer : AlacEncoderThread (Thread 4)
Condition: Unexpected thread exit (not a clean shutdown initiated by Stop())
WPARAM   : HRESULT error code
LPARAM   : 0
Handler  : ConnectionController::OnEncoderError()
Effect   : BeginDisconnect()  [full teardown, no reconnect]
           balloon_.Show(IDS_ENCODER_ERROR)
```

---

## Message Handling — Wrong-State Discard (FR-021)

Any message received in a state where it does not constitute a valid transition input MUST be:
1. Ignored (no state change, no user notification, no side effect)
2. Traced with one `CC_TRACE(L"[CC] %s ignored in state %d", msgName, state_)` line

**Examples of wrong-state discards**:
- `WM_RAOP_CONNECTED` arriving in Disconnecting state (race: user disconnected before handshake completed)
- `WM_RAOP_FAILED` arriving in Idle state (stale post after controller already cleaned up)
- `WM_AUDIO_DEVICE_LOST` arriving in Connecting state (device changed before stream was established)
- Duplicate `WM_RAOP_FAILED` while reconnect timer is already pending

---

## Caller Update Checklist

When `Messages.h` is updated, the following callers must be updated:

| File | Old reference | New reference |
|------|--------------|---------------|
| `src/audio/WasapiCapture.cpp` | `PostMessage(hwnd_, WM_DEFAULT_DEVICE_CHANGED, ...)` | `PostMessage(hwnd_, WM_AUDIO_DEVICE_LOST, ...)` |
| `src/core/AppController.cpp` | `case WM_DEFAULT_DEVICE_CHANGED:` | `case WM_AUDIO_DEVICE_LOST:` |
| `src/core/AppController.cpp` | `PostMessage(hwnd_, WM_TEARDOWN_COMPLETE, ...)` | `PostMessage(hwnd_, WM_STREAM_STOPPED, ...)` |
| `src/core/AppController.cpp` | `case WM_TEARDOWN_COMPLETE:` | `case WM_STREAM_STOPPED:` |
| `src/discovery/MdnsDiscovery.cpp` | (new) | Add `PostMessage(hwnd_, WM_DEVICE_DISCOVERED, ...)` on add |
| `src/discovery/MdnsDiscovery.cpp` | (new) | Add `PostMessage(hwnd_, WM_SPEAKER_LOST, ...)` on remove |
| `src/audio/AlacEncoderThread.cpp` | (new) | Add `PostMessage(hwnd_, WM_ENCODER_ERROR, hr, 0)` on unexpected thread exit (not clean Stop()) |
