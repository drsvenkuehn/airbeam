# Data Model: Feature 009 — ConnectionController

## Overview

This document defines all new types introduced by feature 009. Every type lives on Thread 1
(UI thread) except the `SpscRingBufferPtr` inside `StreamSession`, which is shared lock-free
between Thread 3 (producer) and Thread 4 (consumer).

---

## 1. PipelineState

**File**: `src/core/PipelineState.h`  
**Kind**: `enum class` (not a class; no methods)

```cpp
/// The four observable states of the audio pipeline.
/// All transitions occur on Thread 1 (Win32 message loop) only.
/// No state may be skipped; no concurrent states are permitted (FR-004).
enum class PipelineState : uint8_t {
    Idle,           ///< No active session. Pipeline threads not running.
    Connecting,     ///< Pipeline starting: RaopSession handshake in progress.
    Streaming,      ///< All three pipeline threads running; audio flowing.
    Disconnecting,  ///< Pipeline shutting down: stopping threads in order.
};
```

**Invariants**:
- Exactly one `PipelineState` value is active at any time.
- Only `ConnectionController` may change the state.
- Transitions are: Idle→Connecting, Connecting→Streaming, Connecting→Disconnecting,
  Streaming→Disconnecting, Disconnecting→Idle.
- Disconnecting→Connecting is NOT a direct transition; the controller always passes
  through Idle (FR-005). Reconnect pending is tracked separately in `ReconnectContext`.

---

## 2. ReconnectContext

**File**: `src/core/ReconnectContext.h`  
**Kind**: Plain value struct (no methods except `Reset()`)

```cpp
/// Tracks state for one in-progress exponential-backoff retry sequence.
/// Owned exclusively by ConnectionController; all fields written on Thread 1.
struct ReconnectContext {
    static constexpr int kMaxAttempts          = 3;
    static constexpr UINT kDelaysMs[kMaxAttempts] = { 1000, 2000, 4000 };

    AirPlayReceiver targetDevice;  ///< Device to reconnect to (copy; safe after original lost)
    int             attempt  = 0;  ///< Next attempt index (0, 1, 2); 3 means exhausted
    bool            pending  = false; ///< True while a reconnect sequence is active

    /// Reset to default (no pending reconnect).
    void Reset() noexcept { attempt = 0; pending = false; }

    /// Returns true if more attempts remain.
    bool HasAttemptsLeft() const noexcept { return attempt < kMaxAttempts; }

    /// Delay before the current attempt, or 0 if exhausted.
    UINT CurrentDelayMs() const noexcept {
        return (attempt < kMaxAttempts) ? kDelaysMs[attempt] : 0;
    }
};
```

**Lifecycle**:
- Created/populated when `WM_RAOP_FAILED` or `WM_SPEAKER_LOST` arrives in Streaming state.
- `attempt` incremented by `ConnectionController` each time `TIMER_RECONNECT_RETRY` fires.
- `Reset()` called when reconnect succeeds or is cancelled (FR-007/008).

---

## 3. StreamSession

**File**: `src/core/StreamSession.h` / `StreamSession.cpp`  
**Kind**: Non-copyable class owning all per-session heap resources

```cpp
/// Bundles all resources for one active audio streaming session.
/// Created at BeginConnect(); destroyed at BeginDisconnect() after all threads stop.
/// All Init() allocation occurs on Thread 1 (heap alloc allowed before hot loop).
class StreamSession {
public:
    StreamSession() = default;
    ~StreamSession() = default;

    // Non-copyable, non-movable (owns threads and sockets)
    StreamSession(const StreamSession&)            = delete;
    StreamSession& operator=(const StreamSession&) = delete;
    StreamSession(StreamSession&&)                 = delete;
    StreamSession& operator=(StreamSession&&)      = delete;

    /// Allocates all session resources on Thread 1.
    /// Must be called before any thread Start() call.
    /// @param target   The AirPlay receiver to connect to.
    /// @param lowLatency  If true, allocates 32-slot ring; else 128-slot.
    /// @param hwnd     Message window for posted notifications.
    /// @returns false if resource allocation fails (e.g. ALAC init error).
    bool Init(const AirPlayReceiver& target, bool lowLatency, HWND hwnd);

    // ── Pipeline thread access ────────────────────────────────────────────────

    WasapiCapture&      Capture()    { return *capture_; }
    AlacEncoderThread&  Encoder()    { return *encoder_; }
    RaopSession&        Raop()       { return *raop_; }

    // ── Read-only accessors ───────────────────────────────────────────────────

    const AirPlayReceiver& Target()       const { return target_; }
    bool                   IsLowLatency() const { return lowLatency_; }

    /// Ring buffer capacity (slots): 128 (normal) or 32 (low-latency).
    int RingCapacity() const { return lowLatency_ ? 32 : 128; }

private:
    AirPlayReceiver                    target_;
    bool                               lowLatency_  = false;
    SpscRingBufferPtr                  ring_;        ///< Lock-free SPSC; shared T3↔T4
    std::unique_ptr<AesCbcCipher>      cipher_;      ///< AES-128-CBC for RTP payloads
    std::unique_ptr<RetransmitBuffer>  retransmit_;  ///< 512-packet sliding window
    std::unique_ptr<WasapiCapture>     capture_;     ///< Thread 3
    std::unique_ptr<AlacEncoderThread> encoder_;     ///< Thread 4
    std::unique_ptr<RaopSession>       raop_;        ///< Thread 5
};
```

**Field notes**:
- `ring_` is a `SpscRingBufferPtr` (existing variant type: `std::variant<SpscRingBuffer<AudioFrame,128>*, SpscRingBuffer<AudioFrame,32>*>`).
  The actual `SpscRingBuffer` storage is heap-allocated once inside `Init()` as part of the
  variant and pre-sized before any thread starts. No resize at runtime (TC-003).
- `cipher_` and `retransmit_` are allocated in `Init()` and passed by raw pointer to
  `AlacEncoderThread::Init()` and `RaopSession::Start()`. `StreamSession` owns them and
  guarantees they outlive both threads.
- `WasapiCapture` is restarted in-place during audio device recovery (FR-009):
  `session_->Capture().Stop()` → `session_->Capture().Init(…)` → `session_->Capture().Start()`.
  No `StreamSession` reconstruction needed for capture-only restart.

---

## 4. ConnectionController

**File**: `src/core/ConnectionController.h` / `ConnectionController.cpp`  
**Kind**: Non-copyable, non-movable class; owns one `StreamSession` at a time

```cpp
class ConnectionController {
public:
    ConnectionController(HWND hwnd,
                         Config&        config,
                         ReceiverList&  receivers,
                         TrayIcon&      tray,
                         BalloonNotify& balloon,
                         Logger&        logger);

    ~ConnectionController();

    // Non-copyable, non-movable
    ConnectionController(const ConnectionController&)            = delete;
    ConnectionController& operator=(const ConnectionController&) = delete;

    // ── Invoked by AppController from Thread 1 ────────────────────────────────

    /// User selected a speaker (or auto-connect fired). Begins Idle→Connecting.
    /// If already Connecting/Streaming, tears down current session first (FR-005).
    void Connect(const AirPlayReceiver& target);

    /// User clicked "Disconnect". Begins Streaming/Connecting→Disconnecting→Idle.
    void Disconnect();

    /// User toggled Low Latency menu item.
    /// Idle: persists preference. Streaming: full pipeline restart (FR-012/013).
    /// Connecting/Disconnecting: deferred until stable state (edge-case §spec).
    void SetLowLatency(bool enabled);

    /// Volume slider moved. Propagates immediately if Streaming (FR-017).
    void SetVolume(float linear);

    /// Begin the 5-second auto-connect discovery window on startup (FR-015).
    void StartAutoConnectWindow();

    // ── Win32 message handlers (called by AppController::WndProc on Thread 1) ─

    void OnRaopConnected(LPARAM lParam);      ///< WM_RAOP_CONNECTED
    void OnRaopFailed(LPARAM lParam);         ///< WM_RAOP_FAILED
    void OnStreamStopped();                   ///< WM_STREAM_STOPPED (teardown complete)
    void OnAudioDeviceLost();                 ///< WM_AUDIO_DEVICE_LOST
    void OnSpeakerLost(const wchar_t* devId); ///< WM_SPEAKER_LOST
    void OnCaptureError();                    ///< WM_CAPTURE_ERROR
    void OnDeviceDiscovered(LPARAM lParam);   ///< WM_DEVICE_DISCOVERED (auto-connect)
    void OnTimer(UINT timerId);               ///< WM_TIMER dispatch

    // ── Queries ───────────────────────────────────────────────────────────────

    PipelineState GetState() const noexcept { return state_; }
    bool          IsStreaming() const noexcept { return state_ == PipelineState::Streaming; }

private:
    // ── Internal helpers ──────────────────────────────────────────────────────

    void BeginConnect(const AirPlayReceiver& target);
    void BeginDisconnect(bool forReconnect = false);
    void OnTeardownComplete();
    void ScheduleReconnect();
    void AttemptReconnect();
    void CancelReconnect();
    void TransitionTo(PipelineState next);

    // ── Win32 timer IDs (must not collide with AppController timer IDs) ───────
    static constexpr UINT TIMER_RECONNECT_RETRY = 10;  ///< Exponential-backoff reconnect
    static constexpr UINT TIMER_AUTOCONNECT     = 11;  ///< 5-second startup window

    // ── Members ───────────────────────────────────────────────────────────────
    HWND              hwnd_;
    Config&           config_;
    ReceiverList&     receivers_;
    TrayIcon&         tray_;
    BalloonNotify&    balloon_;
    Logger&           logger_;

    PipelineState     state_       { PipelineState::Idle };
    ReconnectContext  reconnect_;
    bool              inAutoConnectWindow_  { false };
    bool              pendingLowLatencyToggle_ { false }; ///< deferred during transitions
    bool              pendingDisconnect_       { false }; ///< user disconnect while Connecting

    std::unique_ptr<StreamSession> session_;  ///< null when Idle
};
```

---

## 5. Messages.h Changes

**File**: `src/core/Messages.h` (modified)

**Renamed constants** (same integer value, new name):

| Old Name | New Name | Value |
|----------|----------|-------|
| `WM_DEFAULT_DEVICE_CHANGED` | `WM_AUDIO_DEVICE_LOST` | `WM_APP + 6` |
| `WM_TEARDOWN_COMPLETE` | `WM_STREAM_STOPPED` | `WM_APP + 8` |

**New constants appended**:

```cpp
/// Posted by MdnsDiscovery when a specific AirPlay speaker is no longer visible.
/// LPARAM: pointer to a heap-allocated std::wstring device ID (caller frees).
/// If the affected device is the currently-connected one, triggers full reconnect.
constexpr UINT WM_SPEAKER_LOST      = WM_APP + 10;

/// Posted by ConnectionController after all three pipeline threads are confirmed running.
/// LPARAM: unused (0). Causes AppController to update tray to Streaming state.
constexpr UINT WM_STREAM_STARTED    = WM_APP + 11;

/// Posted by WasapiCapture on a non-device-loss capture error (e.g. buffer overrun).
/// Treated as unrecoverable; controller performs full teardown.
constexpr UINT WM_CAPTURE_ERROR     = WM_APP + 12;

/// Posted by MdnsDiscovery when a specific new AirPlay speaker is discovered.
/// LPARAM: index into ReceiverList (WPARAM: receiver list version tag).
/// Used for auto-connect check (FR-015) alongside WM_RECEIVERS_UPDATED.
constexpr UINT WM_DEVICE_DISCOVERED = WM_APP + 13;
```

---

## 6. Validation Rules

| Rule | Enforcement point |
|------|-------------------|
| No two sessions active simultaneously | `StreamSession` is `unique_ptr`; `BeginConnect` asserts `session_ == nullptr` or calls `BeginDisconnect` first |
| Ring buffer pre-allocated before any thread Start() | `StreamSession::Init()` must return `true` before any `Start()` call in `BeginConnect()` |
| Thread 3/4 hot loop: no heap alloc | Static analysis (ASAN + heap tracking in debug), unit tests with mock hooks |
| State transitions on Thread 1 only | All `TransitionTo()` calls traceable to Thread 1 message handlers; no other callers |
| All error paths notify user | Every `return` from an error branch in ConnectionController must call `balloon_.Show(...)` |
| Localizable strings | All `balloon_.Show()` and `tray_.SetTooltip()` calls use `StringLoader::Get(IDS_*)` resource IDs |

---

## 7. State Transitions (Summary)

> **Summary only** — see `contracts/state-machine.md` for the authoritative full transition table including guard conditions.

| From | Event | To | Action |
|------|-------|----|--------|
| Idle | Connect(target) | Connecting | BeginConnect(); tray→Connecting |
| Connecting | WM_RAOP_CONNECTED | Streaming | AlacEncoderThread::Start(); post WM_STREAM_STARTED; tray→Streaming |
| Connecting | WM_RAOP_FAILED | Disconnecting | BeginDisconnect(forReconnect=true) |
| Connecting | Disconnect() | Disconnecting | BeginDisconnect(); pendingDisconnect_=true |
| Streaming | Disconnect() | Disconnecting | BeginDisconnect() |
| Streaming | Connect(other) | Disconnecting | BeginDisconnect(); store new target for reconnect |
| Streaming | WM_RAOP_FAILED | Disconnecting | BeginDisconnect(forReconnect=true) |
| Streaming | WM_SPEAKER_LOST (current) | Disconnecting | BeginDisconnect(forReconnect=true) |
| Streaming | WM_AUDIO_DEVICE_LOST | Streaming | Capture-only restart; no state change |
| Streaming | WM_CAPTURE_ERROR | Disconnecting | BeginDisconnect(); notify user |
| Streaming | SetLowLatency(toggle) | Disconnecting | BeginDisconnect(forReconnect=true with same target) |
| Disconnecting | WM_STREAM_STOPPED | Idle (or Connecting) | OnTeardownComplete(); if reconnect pending → ScheduleReconnect(); else Idle |
| Idle | TIMER_RECONNECT_RETRY fires | Connecting | AttemptReconnect() → BeginConnect() if device present |
| Idle | TIMER_AUTOCONNECT fires | Idle | CancelAutoConnect(); no notification (FR-016) |
