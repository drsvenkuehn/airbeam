# Contract: State Machine

**Class**: `ConnectionController`  
**States**: `Idle`, `Connecting`, `Streaming`, `Disconnecting`  
**Thread**: All transitions occur on Thread 1 (Win32 message loop) exclusively (TC-004)

---

## State Definitions

| State | Meaning | `session_` | Threads running |
|-------|---------|-----------|-----------------|
| `Idle` | No pipeline; may have pending reconnect timer | `nullptr` | None (T3/T4/T5 stopped) |
| `Connecting` | RTSP handshake in progress; capture running | allocated | T3, T5 |
| `Streaming` | All threads running; audio flowing | allocated | T3, T4, T5 |
| `Disconnecting` | Shutdown sequence in progress | allocated (draining) | Some (stopping) |

---

## Transition Table

Each row is a valid (State, Event) → (Next State, Action) triple.
Any (State, Event) combination not in this table results in **silent discard + TRACE** (FR-021).

| Current State | Event / Message | Next State | Action |
|---------------|----------------|------------|--------|
| **Idle** | `Connect(target)` | Connecting | `BeginConnect(target)` |
| **Idle** | `TIMER_RECONNECT_RETRY` | Connecting | `AttemptReconnect()` → check device → `BeginConnect()` |
| **Idle** | `TIMER_RECONNECT_RETRY` (device absent) | Idle | `CancelReconnect()` + balloon(IDS_SPEAKER_UNAVAILABLE) |
| **Idle** | `TIMER_AUTOCONNECT` | Idle | Clear `inAutoConnectWindow_`; no notification |
| **Idle** | `WM_DEVICE_DISCOVERED` (MAC matches, in window) | Connecting | `Connect(receiver)` |
| **Connecting** | `WM_RAOP_CONNECTED` | Streaming | `Encoder.Init()` + `Encoder.Start()` + post `WM_STREAM_STARTED` |
| **Connecting** | `WM_RAOP_FAILED` | Disconnecting | `BeginDisconnect(forReconnect=true)` |
| **Connecting** | `Disconnect()` | Disconnecting | `BeginDisconnect()` + `reconnect_.Reset()` |
| **Connecting** | `Connect(other target)` | Disconnecting | `BeginDisconnect(forReconnect=true)` with new target |
| **Connecting** | `WM_CAPTURE_ERROR` | Disconnecting | `BeginDisconnect()` + balloon(IDS_ERROR_CAPTURE_FAILED) |
| **Streaming** | `Disconnect()` | Disconnecting | `BeginDisconnect()` |
| **Streaming** | `Connect(other target)` | Disconnecting | `BeginDisconnect(forReconnect=true)` with new target |
| **Streaming** | `WM_RAOP_FAILED` | Disconnecting | `BeginDisconnect(forReconnect=true)` |
| **Streaming** | `WM_SPEAKER_LOST` (current device) | Disconnecting | `BeginDisconnect(forReconnect=true)` |
| **Streaming** | `WM_AUDIO_DEVICE_LOST` | Streaming *(no change)* | Capture-only restart (FR-009) |
| **Streaming** | `WM_CAPTURE_ERROR` | Disconnecting | `BeginDisconnect()` + balloon(IDS_ERROR_CAPTURE_FAILED) |
| **Streaming** | `SetLowLatency(toggle)` | Disconnecting | `BeginDisconnect(forReconnect=true)` with same target |
| **Streaming** | `SetVolume(v)` | Streaming *(no change)* | `Raop().SetVolume(v)` + persist config |
| **Disconnecting** | `WM_STREAM_STOPPED` (no reconnect pending) | Idle | `OnTeardownComplete()` + tray→Idle |
| **Disconnecting** | `WM_STREAM_STOPPED` (reconnect pending) | Idle → Connecting | `OnTeardownComplete()` + `ScheduleReconnect()` |

**Note**: Disconnecting → Connecting is NOT a direct arrow — the machine always passes through
Idle first (FR-005). When `WM_STREAM_STOPPED` fires with a reconnect pending, the controller
transitions to Idle then immediately starts a `SetTimer` for the backoff delay. When the timer
fires, it transitions to Connecting. The Idle state is observable (GetState() returns Idle)
during the timer wait window.

---

## BeginConnect Sequence

```
Thread 1:
  1. ASSERT state_ == Idle
  2. session_ = make_unique<StreamSession>()
  3. session_->Init(target, config_.lowLatency, hwnd_)   ← heap alloc OK (pre-loop)
  4. WasapiCapture::Start()                              ← Thread 3 starts (T3)
  5. RaopSession::Start(cfg)                             ← Thread 5 starts RTSP (T5) async
  6. TransitionTo(Connecting)
  ← returns to Win32 message loop

Thread 5 (async):
  7. RTSP OPTIONS → ANNOUNCE → SETUP → RECORD
  8. PostMessage(hwnd_, WM_RAOP_CONNECTED, 0, 0)

Thread 1 (OnRaopConnected):
  9. AlacEncoderThread::Init(Raop().AudioSocket(), ...)  ← heap alloc OK (pre-loop)
 10. AlacEncoderThread::Start()                          ← Thread 4 starts (T4)
 11. PostMessage(hwnd_, WM_STREAM_STARTED, 0, 0)
 12. Config::Save()                                      ← persist lastDevice
 13. TransitionTo(Streaming)
```

---

## BeginDisconnect Sequence

```
Thread 1:
  1. ASSERT state_ == Connecting || Streaming
  2. TransitionTo(Disconnecting)
  3. WasapiCapture::Stop()          ← signals Thread 3 to exit and blocks until joined (≤30 ms)
  4. AlacEncoderThread drains remaining ring frames in its own run loop before exiting.
     No explicit wait on Thread 1. The 50 ms bound is the measured thread-join time on AlacEncoderThread::Stop().
  5. AlacEncoderThread::Stop()      ← signals Thread 4 to exit and blocks until joined (≤50 ms)
  6. RaopSession::Stop()            ← sends RTSP TEARDOWN; signals Thread 5 and blocks until joined (≤200 ms)
  7. PostMessage(hwnd_, WM_STREAM_STOPPED, 0, 0)
  ← returns to Win32 message loop

Thread 1 (OnStreamStopped):
  8. session_.reset()               ← destroy StreamSession (all threads already stopped)
  9. if forReconnect: ScheduleReconnect() → SetTimer(TIMER_RECONNECT_RETRY, delay)
     else: TransitionTo(Idle)
```

**Teardown ordering enforces FR-002**:
- Stop capture first so ring stops receiving new frames.
- Drain ring (brief wait) so Thread 4 can flush pending frames before stopping.
- Stop encoder before stopping RTSP so no partial RTP packets are sent after TEARDOWN.

**Duration guarantee**: SC-002 requires full disconnect within 2 s. Stop() signals each thread to exit and blocks until the thread has exited (join). Total budget: WasapiCapture ≤30 ms, AlacEncoderThread ≤50 ms, RaopSession ≤200 ms.
- WasapiCapture::Stop() join: ≤30 ms
- Ring drain: **time-capped at 50 ms** — AlacEncoderThread drains remaining frames in its own run loop before its Stop() join returns. The drain stops after 50 ms regardless of ring contents, then disconnect proceeds.
- AlacEncoderThread::Stop() join: ≤50 ms (includes ring drain)
- RaopSession::Stop() join (TEARDOWN): ≤200 ms
- Total: ~330 ms well within 2 s

---

## Reconnect Sub-Machine (within Idle state)

While `state_ == Idle` and `reconnect_.pending == true`, a timer is active:

```
Idle + TIMER_RECONNECT_RETRY fires:
  attempt = reconnect_.attempt (0, 1, or 2)
  Check ReceiverList for reconnect_.targetDevice

  if device present:
    reconnect_.attempt++
    BeginConnect(reconnect_.targetDevice)
    if reconnect attempt SUCCEEDS → WM_RAOP_CONNECTED → Streaming → reconnect_.Reset()
    if reconnect attempt FAILS → WM_RAOP_FAILED → Disconnecting → WM_STREAM_STOPPED
       → if reconnect_.HasAttemptsLeft(): ScheduleReconnect()
       → else: reconnect_.Reset() + TransitionTo(Idle) + balloon(IDS_RECONNECT_FAILED)

  if device absent (FR-007):
    KillTimer(TIMER_RECONNECT_RETRY)
    reconnect_.Reset()
    TransitionTo(Idle)
    balloon_.Show(IDS_SPEAKER_UNAVAILABLE)
```

**Delay sequence**:
```
Attempt 0: SetTimer(TIMER_RECONNECT_RETRY, 1000)   → fires after 1 s
Attempt 1: SetTimer(TIMER_RECONNECT_RETRY, 2000)   → fires after 2 s
Attempt 2: SetTimer(TIMER_RECONNECT_RETRY, 4000)   → fires after 4 s
Attempt 3: exhausted → give up
```

---

## Capture-Only Restart Sub-Sequence (within Streaming state)

Triggered by `WM_AUDIO_DEVICE_LOST`. State remains `Streaming` throughout.

```
Thread 1 (OnAudioDeviceLost):
  1. TRACE("[CC] Capture restart: audio device lost")
  2. session_->Capture().Stop()          ← join Thread 3 (≤30 ms)
  3. ok = session_->Capture().Init(...)  ← re-enumerate default device; heap alloc OK
  4. if ok:
       session_->Capture().Start()       ← Thread 3 resumes
       TRACE("[CC] Capture restart: complete")
     else:
       BeginDisconnect()                 ← full teardown (FR-011)
       balloon_.Show(IDS_ERROR_NO_AUDIO_DEVICE)
```

**50 ms gap guarantee** (SC-003, FR-010):
- Thread 3 Stop() + Init() + Start() completes in ≤50 ms under normal conditions.
- The RTP stream's sequence numbers and timestamps are not reset; the receiver typically
  fills the ~50 ms gap with silence or concealment.
- Thread 4 (AlacEncoderThread) is NOT stopped. It continues draining the ring buffer during
  the restart window. If the ring empties before Thread 3 resumes, Thread 4's `TryPop`
  returns `false` — this is safe and produces a brief silence gap at the encoder output.

---

## State Invariants

1. `state_ == Idle` iff `session_ == nullptr`.
2. `state_ == Streaming` iff Threads 3, 4, and 5 are all in their running state.
3. `state_ == Connecting` iff Threads 3 and 5 are running; Thread 4 is not yet started.
4. `reconnect_.pending == true` only while `state_ == Idle` and `TIMER_RECONNECT_RETRY` is set.
5. No two `StreamSession` instances exist simultaneously.
6. `TransitionTo()` is the only method that writes `state_`; it always emits a TRACE line.

---

## Test Coverage Requirements (SC-010)

All transitions in the table above MUST be exercisable in `test_connection_controller.cpp`
without a live AirPlay device. Mock implementations required:

| Mock | Replaces | Minimal interface |
|------|---------|-------------------|
| `MockWasapiCapture` | `WasapiCapture` | `Init()`, `Start()`, `Stop()`, `IsRunning()` |
| `MockAlacEncoderThread` | `AlacEncoderThread` | `Init()`, `Start()`, `Stop()`, `IsRunning()` |
| `MockRaopSession` | `RaopSession` | `Start()`, `Stop()`, `SetVolume()`, `IsRunning()`, `AudioSocket()` |
| `MockReceiverList` | `ReceiverList` | `Find(mac)`, `Get(index)` |
| `MockBalloonNotify` | `BalloonNotify` | `Show(resId, ...)` — records calls for assertion |

Tests MUST cover:
- [ ] Happy path: Idle → Connecting → Streaming → Disconnecting → Idle
- [ ] User disconnect while Connecting
- [ ] Reconnect: all 3 attempts succeed on attempt 2
- [ ] Reconnect: all 3 attempts fail → Idle + balloon
- [ ] Reconnect cancelled: device disappears before timer fires (FR-007)
- [ ] Audio device lost → capture restart → continue Streaming
- [ ] Audio device lost → reinit fails → Disconnecting → Idle + balloon
- [ ] Wrong-state discard: WM_RAOP_FAILED in Idle
- [ ] Wrong-state discard: WM_RAOP_CONNECTED in Disconnecting
- [ ] Speaker switch: Streaming → Disconnecting → Connecting (same HWND, new target)
- [ ] Low latency toggle while Streaming
- [ ] Low latency toggle while Idle (no pipeline action)
- [ ] Auto-connect: device discovered within 5s window → Connect()
- [ ] Auto-connect: 5s window expires with no match → Idle, no notification
- [ ] Volume change while Streaming → SetVolume propagated
- [ ] Volume change while Idle → persisted, not propagated
