# Developer Quickstart: ConnectionController (Feature 009)

## What is it?

`ConnectionController` is the new central orchestrator for the AirBeam audio pipeline. It owns
the four-state machine (`Idle → Connecting → Streaming → Disconnecting`), manages the lifecycle
of all three pipeline threads (WasapiCapture/T3, AlacEncoderThread/T4, RaopSession/T5), and
handles every failure recovery path (reconnect, audio device substitution, error notifications).

It is a **Thread 1-only** class. All state transitions happen through the Win32 message loop.
No mutexes or atomics are used for state management.

---

## File Map

| File | Purpose |
|------|---------|
| `src/core/PipelineState.h` | `enum class PipelineState { Idle, Connecting, Streaming, Disconnecting }` |
| `src/core/ReconnectContext.h` | Value type: retry attempt counter + delays + target device |
| `src/core/StreamSession.h/.cpp` | Bundles ring buffer, capture, encoder, RAOP for one session |
| `src/core/ConnectionController.h/.cpp` | The state machine itself |
| `src/core/Messages.h` | All `WM_APP+N` constants (updated: 2 renamed, 4 new; plus WM_ENCODER_ERROR = WM_APP+14) |
| `tests/unit/test_connection_controller.cpp` | Full state machine tests with mocks |

---

## How to Start a Connection (Happy Path)

```cpp
// AppController::WndProc handles IDM_DEVICE_BASE + index menu selection:
void AppController::HandleCommand(UINT id) {
    if (id >= IDM_DEVICE_BASE && id < IDM_DEVICE_BASE + IDM_DEVICE_MAX_COUNT) {
        int idx = static_cast<int>(id - IDM_DEVICE_BASE);
        auto receiver = receiverList_.Get(idx);
        if (receiver) {
            controller_.Connect(*receiver);   // ← delegate to ConnectionController
        }
    }
}
```

Internally, `Connect()` calls `BeginConnect()`, which:
1. Creates a `StreamSession` and calls `Init()` (pre-allocates all buffers on Thread 1).
2. Starts WasapiCapture (Thread 3).
3. Starts RaopSession (Thread 5) — async RTSP handshake.
4. Transitions state to `Connecting`.

When `WM_RAOP_CONNECTED` arrives on Thread 1:
5. AlacEncoderThread is initialised with the RAOP audio socket and started (Thread 4).
6. `WM_STREAM_STARTED` is posted → AppController updates tray to Streaming state.
7. State transitions to `Streaming`.

---

## How to Disconnect

```cpp
// AppController::HandleCommand for IDM_QUIT or "Disconnect" menu item:
controller_.Disconnect();
```

`BeginDisconnect()` stops threads in strict order:
1. `WasapiCapture::Stop()` — no more ring writes.
2. Brief drain (Thread 4 flushes remaining frames).
3. `AlacEncoderThread::Stop()`.
4. `RaopSession::Stop()` — sends RTSP TEARDOWN.
5. Posts `WM_STREAM_STOPPED` → AppController updates tray to Idle.

---

## How to Wire Up the Message Loop

In `AppController::WndProc` (or equivalent), forward all `WM_APP+N` messages:

```cpp
LRESULT CALLBACK AppController::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<AppController*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    // ── ConnectionController messages ────────────────────────────────────────
    case WM_RAOP_CONNECTED:        self->controller_.OnRaopConnected(lParam);  break;
    case WM_RAOP_FAILED:           self->controller_.OnRaopFailed(lParam);     break;
    case WM_STREAM_STOPPED:        self->controller_.OnStreamStopped();
                                   self->HandleStreamStopped();                 break;
    case WM_AUDIO_DEVICE_LOST:     self->controller_.OnAudioDeviceLost();      break;
    case WM_SPEAKER_LOST:          self->controller_.OnSpeakerLost(
                                       reinterpret_cast<const wchar_t*>(lParam)); break;
    case WM_CAPTURE_ERROR:         self->controller_.OnCaptureError();         break;
    case WM_DEVICE_DISCOVERED:     self->controller_.OnDeviceDiscovered(lParam);break;
    case WM_STREAM_STARTED:        self->HandleStreamStarted();                break;
    case WM_TIMER:                 self->controller_.OnTimer(static_cast<UINT>(wParam));
                                   // AppController handles its own timers too
                                   break;
    // ── AppController-only messages ──────────────────────────────────────────
    case WM_TRAY_CALLBACK:         self->HandleTrayCallback(lParam);           break;
    case WM_RECEIVERS_UPDATED:     self->HandleReceiversUpdated();             break;
    case WM_BONJOUR_MISSING:       self->HandleBonjourMissing();               break;
    case WM_UPDATE_REJECTED:       self->HandleUpdateRejected();              break;
    // ...
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
```

---

## How to Query State

```cpp
PipelineState s = controller_.GetState();
bool streaming  = controller_.IsStreaming();
```

Use `GetState()` in `AppController::ShowTrayMenu()` to conditionally show/hide the
"Disconnect" menu item and disable the speaker list during Connecting/Disconnecting.

---

## How to Handle Audio Device Change

`WasapiCapture` posts `WM_AUDIO_DEVICE_LOST` when `IAudioClient` returns
`AUDCLNT_E_DEVICE_INVALIDATED`. The message routes to `ConnectionController::OnAudioDeviceLost()`.

If the controller is in `Streaming` state, it performs a **capture-only restart** without
touching the RTSP session:
1. Stops Thread 3.
2. Calls `WasapiCapture::Init()` on Thread 1 to re-enumerate the new default device.
3. Restarts Thread 3.

The whole sequence completes within ≤50 ms under normal conditions. If reinit fails, the full
pipeline is torn down and the user receives a balloon notification.

---

## How Reconnect Works

When `WM_RAOP_FAILED` or `WM_SPEAKER_LOST` arrives in `Streaming` state:
1. The controller calls `BeginDisconnect(forReconnect=true)`.
2. After teardown (`WM_STREAM_STOPPED`), `ScheduleReconnect()` sets a timer for 1 s.
3. When the timer fires on Thread 1, `AttemptReconnect()` checks whether the device is
   still in `ReceiverList`. If yes → `BeginConnect()`. If no → balloon + Idle.
4. If the connect attempt fails again → backoff to 2 s, then 4 s.
5. After 3 failed attempts → balloon(IDS_RECONNECT_FAILED) + stay Idle.

**User cancellation during reconnect**: Calling `Disconnect()` while a reconnect timer is
pending cancels the timer and clears `reconnect_`.

---

## How Auto-Connect Works

On application startup, `AppController::Start()` calls `controller_.StartAutoConnectWindow()`.

If `config_.lastDevice` is non-empty:
- A 5-second timer is set.
- Every `WM_DEVICE_DISCOVERED` message is checked against `config_.lastDevice` (by MAC).
- If a match is found within 5 seconds, `Connect()` is called automatically.
- If the timer expires with no match, the controller stays Idle with no notification.

---

## How to Toggle Low Latency

```cpp
// User clicks IDM_LOW_LATENCY_TOGGLE:
controller_.SetLowLatency(!config_.lowLatency);
```

- **While Idle**: preference is saved to config; no pipeline action.
- **While Streaming**: the pipeline restarts with the new ring buffer size (32 or 128 slots).
  The restart uses the same target device and goes through the full Disconnecting → Idle →
  Connecting → Streaming cycle.
- **While Connecting/Disconnecting**: the toggle is deferred and applied once a stable state
  is reached.

---

## How to Write Tests (Mock Pattern)

`ConnectionController` depends on its injected references. For unit tests, use thin stub classes:

```cpp
// Example: test that WM_RAOP_FAILED in Streaming triggers BeginDisconnect
TEST(ConnectionControllerTest, RaopFailedWhileStreaming_TriggersDisconnect) {
    FakeHwnd          hwnd;
    MockConfig        config;
    MockReceiverList  receivers;
    MockTrayIcon      tray;
    MockBalloonNotify balloon;
    MockLogger        logger;

    ConnectionController cc(hwnd.Get(), config, receivers, tray, balloon, logger);

    // Drive to Streaming state via simulated messages
    cc.Connect(MakeReceiver("AA:BB:CC:DD:EE:FF", "Living Room"));
    EXPECT_EQ(cc.GetState(), PipelineState::Connecting);

    cc.OnRaopConnected(0);
    EXPECT_EQ(cc.GetState(), PipelineState::Streaming);

    // Simulate RAOP failure
    cc.OnRaopFailed(0);
    EXPECT_EQ(cc.GetState(), PipelineState::Disconnecting);
}
```

All 16 test cases listed in `contracts/state-machine.md` must pass before merge.

---

## Real-Time Safety Checklist

Before shipping, verify these invariants hold in every code path:

- [ ] `ConnectionController` never calls any method of `WasapiCapture` or `AlacEncoderThread`
      from inside their own running threads (only from Thread 1).
- [ ] `StreamSession::Init()` is always called before any `Start()`.
- [ ] `AlacEncoderThread::Init()` and `WasapiCapture::Init()` perform all heap allocation.
- [ ] The hot loop of Thread 3 (`WasapiCapture::CaptureLoop`) contains no `new`/`malloc`.
- [ ] The hot loop of Thread 4 (`AlacEncoderThread::EncodeLoop`) contains no `new`/`malloc`.
- [ ] No mutex or `CRITICAL_SECTION` is acquired in either hot loop.
- [ ] The ring buffer (`SpscRingBuffer`) uses only `std::atomic` with acquire/release ordering.

---

## String Resource IDs (new additions for this feature)

All user-visible notification strings must be added to the English resource file and all
seven locale files before merge (Constitution §VIII):

| Resource ID | Usage | Example English text |
|-------------|-------|---------------------|
| `IDS_CONNECTING_TO` | Tray tooltip during Connecting | `"Connecting to %s…"` |
| `IDS_STREAMING_TO` | Tray tooltip during Streaming | `"Streaming to %s"` |
| `IDS_RECONNECTING` | Balloon: reconnect attempt in progress | `"Reconnecting to %s (attempt %d of 3)…"` |
| `IDS_RECONNECT_FAILED` | Balloon: all retries exhausted | `"Could not reconnect to %s. Check that your speaker is on and connected to the same network."` |
| `IDS_SPEAKER_UNAVAILABLE` | Balloon: speaker lost before reconnect | `"%s is no longer available."` |
| `IDS_ERROR_CAPTURE_FAILED` | Balloon: WM_CAPTURE_ERROR | `"Audio capture failed unexpectedly. Please reconnect."` |
| `IDS_ERROR_NO_AUDIO_DEVICE` | Balloon: capture reinit failed | `"No audio device found. Please connect an audio device and try again."` |
| `IDS_RECONNECTED` | Balloon: reconnect success | `"Reconnected to %s."` |
