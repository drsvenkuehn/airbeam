# Contract: ConnectionController Public API

**File**: `src/core/ConnectionController.h`  
**Owner**: `AppController` (creates one instance; holds for application lifetime)  
**Thread**: All methods called on Thread 1 (Win32 message loop) only  
**Dependencies**: `Config&`, `ReceiverList&`, `TrayIcon&`, `BalloonNotify&`, `Logger&`

---

## Constructor

```cpp
ConnectionController(
    HWND              hwnd,       // Main message window; used for SetTimer/KillTimer/PostMessage
    Config&           config,     // Read/written for lastDevice, lowLatency, volume
    ReceiverList&     receivers,  // Queried for device presence checks during reconnect
    TrayIcon&         tray,       // SetState() called on connect/disconnect/error
    BalloonNotify&    balloon,    // Show() called on every unrecoverable error (FR-019)
    Logger&           logger      // Trace() called at state transitions and events (FR-023)
);
```

**Precondition**: All references must outlive `ConnectionController`.  
**Effect**: Initialises all members to defaults; does not start any timers or threads.

---

## Destructor

```cpp
~ConnectionController();
```

**Effect**: Calls `Disconnect()` if state is not Idle, then joins all threads. Safe to
call from Thread 1 during `WM_DESTROY` handling.

---

## Command Methods (called by AppController)

### Connect

```cpp
void Connect(const AirPlayReceiver& target);
```

**Called from**: AppController when user selects a speaker or auto-connect fires.  
**Precondition**: Thread 1.  
**Behaviour**:
- If state == **Idle**: calls `BeginConnect(target)`.
- If state == **Streaming** or **Connecting** (different target): calls `BeginDisconnect(forReconnect=true)`, stores `target` as the pending reconnect target. After teardown completes, `BeginConnect(target)` is called automatically.
- If state == **Streaming** (same target): no-op (already connected).
- If state == **Disconnecting**: stores `target` as pending; `BeginConnect` fires after teardown.

### Disconnect

```cpp
void Disconnect();
```

**Called from**: AppController when user clicks "Disconnect" or application shuts down.  
**Precondition**: Thread 1.  
**Behaviour**:
- If state == **Idle**: no-op.
- If state == **Connecting** or **Streaming**: calls `BeginDisconnect()`.
  `reconnect_.Reset()` is called to cancel any pending reconnect (FR-008 not applicable here — this is user-initiated).
- If state == **Disconnecting**: sets `pendingDisconnect_ = true`; teardown already in progress.

### SetLowLatency

```cpp
void SetLowLatency(bool enabled);
```

**Called from**: AppController when user toggles "Low Latency" menu item.  
**Precondition**: Thread 1.  
**Behaviour**:
- If `config_.lowLatency == enabled`: no-op.
- Persists `config_.lowLatency = enabled` + `config_.Save()` (FR-014).
- If state == **Idle**: preference saved, no pipeline action (FR-013).
- If state == **Streaming**: calls `BeginDisconnect(forReconnect=true)` with same target + new latency setting. Pipeline restarts after teardown (FR-012).
- If state == **Connecting** or **Disconnecting**: defers toggle; `pendingLowLatencyToggle_ = true`. Applied when next stable state is reached.

### SetVolume

```cpp
void SetVolume(float linear);
```

**Called from**: AppController when volume slider changes.  
**Precondition**: Thread 1. `linear` is in [0.0f, 1.0f].  
**Behaviour**:
- Persists `config_.volume = linear` + `config_.Save()` (FR-018).
- If state == **Streaming**: calls `session_->Raop().SetVolume(linear)` immediately (FR-017).
- Otherwise: saved preference applied at next `BeginConnect()`.

### StartAutoConnectWindow

```cpp
void StartAutoConnectWindow();
```

**Called from**: AppController::Start() on application startup.  
**Precondition**: Thread 1. Should be called only once per app lifetime.  
**Behaviour**:
- If `config_.lastDevice` is empty: no-op (nothing to auto-connect to).
- Otherwise: sets `inAutoConnectWindow_ = true` and calls `SetTimer(hwnd_, TIMER_AUTOCONNECT, 5000, nullptr)`.
- When `WM_DEVICE_DISCOVERED` fires during the window and the device MAC matches `config_.lastDevice`, `Connect()` is called automatically (FR-015).
- When `TIMER_AUTOCONNECT` fires: clears `inAutoConnectWindow_`; no notification (FR-016).

---

## Win32 Message Handlers

All message handlers are called from `AppController::WndProc` on Thread 1.

```cpp
void OnRaopConnected(LPARAM lParam);
```
- Valid in: **Connecting**. Ignored (+ TRACE) in any other state (FR-021).
- Effect: Calls `session_->Encoder().Init(session_->Raop().AudioSocket(), ...)`, then `session_->Encoder().Start()`. Posts `WM_STREAM_STARTED`. Transitions to **Streaming**.

```cpp
void OnRaopFailed(LPARAM lParam);
```
- Valid in: **Connecting**, **Streaming**. Ignored (+ TRACE) otherwise.
- Effect: `BeginDisconnect(forReconnect=true)`.

```cpp
void OnStreamStopped();
```
- Valid in: **Disconnecting**. Ignored (+ TRACE) otherwise.
- Effect: Calls `OnTeardownComplete()`. If reconnect pending: `ScheduleReconnect()`. Otherwise: transitions to **Idle**, updates tray.

```cpp
void OnAudioDeviceLost();
```
- Valid in: **Streaming**. Ignored (+ TRACE) otherwise.
- Effect: Capture-only restart sequence (FR-009). State remains **Streaming** on success.

```cpp
void OnSpeakerLost(const wchar_t* devId);
```
- Valid in: **Streaming**. Ignored (+ TRACE) otherwise.
- Effect: If `devId` matches `session_->Target().serviceName`: `BeginDisconnect(forReconnect=true)`.
- Memory: `devId` points to a `std::wstring*` heap-allocated by MdnsDiscovery; `OnSpeakerLost` deletes it after reading.

```cpp
void OnCaptureError();
```
- Valid in: **Streaming**, **Connecting**. Ignored (+ TRACE) otherwise.
- Effect: `BeginDisconnect()` (no reconnect), `balloon_.Show(IDS_ERROR_CAPTURE_FAILED)`.

```cpp
void OnDeviceDiscovered(LPARAM lParam);
```
- Always processed (no wrong-state discard for this message — it's informational).
- Effect: If `inAutoConnectWindow_` and receiver at `lParam` index matches `config_.lastDevice` MAC: calls `Connect()`.

```cpp
void OnTimer(UINT timerId);
```
- Dispatches on `timerId`:
  - `TIMER_RECONNECT_RETRY`: calls `AttemptReconnect()`.
  - `TIMER_AUTOCONNECT`: clears `inAutoConnectWindow_`, kills timer.

---

## Query Methods

```cpp
PipelineState GetState() const noexcept;
bool          IsStreaming() const noexcept;  // shorthand: GetState() == Streaming
```

---

## Timer IDs

| Constant | Value | Purpose |
|----------|-------|---------|
| `TIMER_RECONNECT_RETRY` | 10 | Fires after backoff delay; triggers `AttemptReconnect()` |
| `TIMER_AUTOCONNECT` | 11 | Fires after 5-second startup window; clears auto-connect flag |

**Note**: Timer IDs must not collide with `AppController` timer IDs (1 = reconnect window,
2 = RAOP retry in old code — these will be removed when ConnectionController takes over).

---

## Invariants

1. `session_ != nullptr` iff `state_ != Idle`.
2. Only one timer (`TIMER_RECONNECT_RETRY` or `TIMER_AUTOCONNECT`) active at a time for each ID.
3. `reconnect_.pending == true` only while `state_ == Idle` AND a reconnect timer is active.
4. `state_` is modified only by `TransitionTo()`, which emits `CC_TRACE` (FR-023).
5. Every method that returns early on wrong state calls `CC_TRACE` first (FR-021).
6. Every `BeginDisconnect()` eventually produces a `WM_STREAM_STOPPED` on Thread 1.

---

## Dependency on AppController

`AppController` is responsible for:
1. Creating `ConnectionController` in its `Start()` method.
2. Forwarding all relevant `WM_*` messages to `ConnectionController`'s handlers.
3. Handling `WM_STREAM_STARTED` and `WM_STREAM_STOPPED` to update the tray menu state.
4. Calling `Connect(receiver)` when the user selects a speaker from the tray menu.
5. Calling `Disconnect()` when the user clicks "Disconnect" or the app shuts down.

`ConnectionController` does NOT call back into `AppController` directly — all communication
is via posted `WM_*` messages to `hwnd_`.
