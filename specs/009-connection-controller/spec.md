# Feature Specification: Full App Integration — ConnectionController and Live Audio Path

**Feature Branch**: `009-connection-controller`  
**Created**: 2025-07-17  
**Status**: Draft  
**Input**: User description: "Implement the ConnectionController that coordinates the full live audio path from WASAPI capture through to the AirPlay receiver."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Connect to a Speaker and Stream Audio (Priority: P1)

A user opens the AirBeam tray icon, sees a list of discovered AirPlay speakers in the context menu, and clicks one. Within a few seconds audio from the Windows system is playing through the chosen speaker. The tray icon updates to indicate an active connection.

**Why this priority**: This is the core end-to-end value of the entire application. Nothing else matters if the happy path does not work. All other stories build on a working connection.

**Independent Test**: Can be fully tested by selecting a discovered speaker from the tray menu and verifying system audio plays through that speaker within 3 seconds. Delivers the primary product value independently.

**Acceptance Scenarios**:

1. **Given** the application is running and at least one AirPlay speaker has been discovered, **When** the user clicks a speaker name in the tray context menu, **Then** the full audio pipeline starts, audio plays through the selected speaker within 3 seconds, and the tray shows a "connected" indicator.
2. **Given** a connection is in progress (Connecting state), **When** the RTSP handshake and capture initialization both succeed, **Then** the controller transitions to Streaming state and posts a stream-started notification to the UI.
3. **Given** a connection is in progress, **When** the RTSP handshake fails before streaming begins, **Then** the pipeline is torn down cleanly, the controller returns to Idle, and the user receives a tray balloon notification explaining the failure.

---

### User Story 2 - Disconnect from a Speaker (Priority: P1)

A user who is actively streaming clicks "Disconnect" in the tray context menu. The audio stream stops cleanly — no leftover threads, no audio glitches on the receiver, and the tray returns to idle state.

**Why this priority**: Equal in priority to connecting: users must be able to stop streaming on demand without the application becoming unstable or requiring a restart.

**Independent Test**: Can be tested independently by establishing a connection (Story 1) and then selecting Disconnect, verifying audio stops, tray shows idle, and no background threads are left running.

**Acceptance Scenarios**:

1. **Given** the application is Streaming, **When** the user selects "Disconnect" from the tray menu, **Then** capture stops, the ring buffer drains, the encoder and RTSP session shut down in order, and the controller reaches Idle state within 2 seconds.
2. **Given** the application is Streaming, **When** the user selects a different speaker from the menu, **Then** the current pipeline tears down fully before the new pipeline starts (no concurrent sessions), and streaming resumes through the new speaker.
3. **Given** the application is in Connecting state, **When** the user selects "Disconnect", **Then** the in-progress connection attempt is cancelled, all partially-started components are stopped, and the controller returns to Idle.

---

### User Story 3 - Automatic Reconnect After Network Failure (Priority: P2)

While streaming, the network briefly drops or the receiver becomes temporarily unreachable. After the failure is detected, the application automatically attempts one reconnect. If the speaker is still visible on the network, streaming resumes without any user action.

**Why this priority**: Network hiccups are common in home environments. Without auto-reconnect, every Wi-Fi glitch requires manual user intervention, degrading the experience significantly.

**Independent Test**: Can be tested by simulating an RTSP failure notification while in Streaming state, verifying the exponential-backoff retry schedule (1 s → 2 s → 4 s), and confirming up to 3 reconnect attempts are made while the device remains discoverable.

**Acceptance Scenarios**:

1. **Given** the application is Streaming, **When** the RTSP session reports a failure and the device is still visible in the Bonjour browser, **Then** the controller attempts up to 3 full pipeline reconnects with exponential-backoff delays of 1 s, 2 s, and 4 s between each attempt (total retry window ≤ 7 s).
2. **Given** a reconnect attempt is pending, **When** the device disappears from the Bonjour browser before the reconnect fires, **Then** the reconnect is cancelled, the controller returns to Idle, and the user receives a tray notification that the speaker is no longer available.
3. **Given** a reconnect attempt fires, **When** the reconnect succeeds, **Then** the controller returns to Streaming state with no user interaction required and a brief "Reconnected" tray notification is shown.
4. **Given** all 3 reconnect attempts have been made, **When** every attempt fails, **Then** the controller returns to Idle and the user receives an actionable tray balloon notification (e.g., suggesting they check the speaker or their network).

---

### User Story 4 - Seamless Audio Device Substitution (Priority: P2)

The user's default Windows audio output device changes while AirBeam is streaming — for example, a USB headset is unplugged or a different audio device is selected in Windows Sound Settings. AirBeam detects the change, seamlessly re-initialises capture on the new default device, and continues streaming with a minimal audio gap, all without dropping the RTSP session.

**Why this priority**: Windows audio device changes are common and would otherwise silently break the stream. Keeping the RTSP session alive avoids the costly full reconnect handshake.

**Independent Test**: Can be tested by triggering a capture-error notification while in Streaming state, verifying only the capture side restarts, and confirming the RTSP session ID does not change.

**Acceptance Scenarios**:

1. **Given** the application is Streaming, **When** the capture subsystem reports the audio device was lost, **Then** capture stops, re-initialises against the new default audio device, and resumes feeding audio into the pipeline — all while the RTSP session remains connected.
2. **Given** a capture restart is in progress, **When** re-initialisation succeeds, **Then** audio resumes through the receiver and the total audio gap is no longer than 50 milliseconds.
3. **Given** a capture restart is in progress, **When** re-initialisation fails (e.g., no audio device available), **Then** the full pipeline is torn down, the controller reaches Idle, and the user sees an actionable tray balloon notification.

---

### User Story 5 - Low-Latency Mode Toggle (Priority: P3)

A user who is sensitive to audio/video sync — for example when watching video — opens the tray menu and toggles "Low Latency" on. AirBeam restarts the pipeline with a smaller buffer, reducing end-to-end latency. Toggling it off restores the larger, more robust buffer.

**Why this priority**: Useful for a subset of users who need tighter sync, but the feature's absence does not prevent core streaming. Placed at P3 because both buffer sizes produce working audio.

**Independent Test**: Can be tested by toggling the Low Latency menu item while Streaming, verifying the pipeline restarts, and confirming the ring buffer capacity changes to the expected size.

**Acceptance Scenarios**:

1. **Given** the application is Streaming with Low Latency off (128-slot buffer), **When** the user toggles "Low Latency" on, **Then** the pipeline restarts with a 32-slot buffer and streaming resumes within 3 seconds.
2. **Given** the application is Streaming with Low Latency on, **When** the user toggles "Low Latency" off, **Then** the pipeline restarts with a 128-slot buffer and streaming resumes.
3. **Given** the application is Idle, **When** the user toggles the Low Latency setting, **Then** the preference is saved and will be applied the next time a connection is started — no pipeline action occurs immediately.
4. **Given** the Low Latency toggle changes, **Then** the new preference is persisted to config so it survives application restarts.

---

### User Story 6 - Startup Auto-Connect to Last-Used Speaker (Priority: P3)

A user restarts their PC. AirBeam starts with the system tray. Within a few seconds, Bonjour discovers the same speaker the user was last connected to, and AirBeam automatically begins streaming — no manual selection required.

**Why this priority**: Quality-of-life improvement for users with a single, always-on speaker setup. The 5-second discovery window is a deliberate trade-off — long enough to catch slow Bonjour announcements, short enough to fail fast on a missing device.

**Independent Test**: Can be tested by populating config with a known device MAC, starting the application, and confirming auto-connect fires when that device appears in Bonjour within the 5-second window.

**Acceptance Scenarios**:

1. **Given** config contains a last-used device MAC, **When** that device appears in the Bonjour browser within 5 seconds of startup, **Then** the full connection pipeline starts automatically without any user action.
2. **Given** config contains a last-used device MAC, **When** 5 seconds elapse after startup without that device being discovered, **Then** the application remains Idle and no auto-connect attempt is made.
3. **Given** an auto-connect attempt is in progress, **When** it fails, **Then** the controller falls back to Idle with the same error notification behaviour as a manual connection failure.

---

### User Story 7 - Volume Adjustment Without Pipeline Restart (Priority: P3)

While streaming, the user adjusts the AirBeam volume slider in the tray menu. The volume change takes effect on the receiver immediately, with no interruption to the audio stream.

**Why this priority**: Users expect volume to be adjustable without reconnecting. This is a convenience feature with no impact on streaming reliability.

**Independent Test**: Can be tested while Streaming by changing the volume control and confirming the volume level propagates to the receiver with no stream interruption.

**Acceptance Scenarios**:

1. **Given** the application is Streaming, **When** the user adjusts the volume control, **Then** the new volume level is delivered to the receiver and the audio stream continues without interruption.
2. **Given** the application is Idle, **When** the user adjusts the volume control, **Then** the preference is saved and will be applied when streaming next begins.

---

### Edge Cases

- What happens when the user rapidly clicks different speakers in the menu? The controller must serialize connection requests: finish tearing down the current session before starting the next.
- What happens if the Bonjour browser loses a device while the controller is in Connecting state for that device? The connection attempt must be aborted and the controller must return to Idle with a notification.
- What happens if the ring buffer overflows (encoder can't keep up)? The controller must not crash; the oldest frames may be dropped and the situation surfaced only if it becomes persistent.
- What happens when the user triggers Low Latency toggle during a Connecting or Disconnecting transition? The toggle must be deferred until the controller reaches a stable state (Idle or Streaming).
- What happens if config.json is missing or corrupt on startup? Auto-connect must be skipped silently and the application starts in Idle.
- What happens if `WM_SPEAKER_LOST` arrives for the currently connected speaker while streaming? This triggers the same full-reconnect path as `WM_RAOP_FAILED`: attempt up to 3 full pipeline reconnects with 1 s / 2 s / 4 s exponential-backoff delays (FR-006), honouring FR-007 cancellation if the device remains absent, then notify and go Idle if all attempts fail (see FR-008).
- What happens if `WM_AUDIO_DEVICE_LOST` arrives while streaming? This triggers a capture-only restart (FR-009): Thread 3 (WasapiCapture) is stopped, WASAPI capture is re-initialised against the new default device, and Thread 3 resumes — the RTSP session (Thread 2 / RaopSession) is not disturbed. The audio gap MUST NOT exceed 50 ms (FR-010). If re-initialisation fails, the full pipeline is torn down (FR-011).
- What happens if multiple WM_RAOP_FAILED messages are posted in rapid succession? The controller must honour only the first and ignore duplicates while a reconnect is already pending.
- **Stale or out-of-order message arrival**: A background thread may post a message (e.g., WM_RAOP_FAILED, WM_STREAM_STARTED) after the controller has already moved past the state in which that message would be meaningful (e.g., due to a user-initiated disconnect racing a failure notification). This is by design: the controller silently discards any such message, emits a debug trace log, and leaves state unchanged. No retry, no notification, no side effect (see FR-021).

## Requirements *(mandatory)*

### Functional Requirements

#### Pipeline Lifecycle

- **FR-001**: When a user selects a speaker, the controller MUST allocate a ring buffer at the configured capacity (128 slots in normal mode, 32 slots in low-latency mode), then start WasapiCapture, RaopSession, and AlacEncoderThread in that order, posting a stream-started notification to the UI only after all three have started successfully.
- **FR-002**: When the user selects "Disconnect" or selects a different speaker, the controller MUST tear down the pipeline in strict order: stop capture → drain ring buffer → stop encoder → stop RTSP session, then post a stream-stopped notification to the UI.
- **FR-003**: All state transitions MUST occur on the UI thread (Thread 1). Background threads MUST communicate with the controller exclusively via posted window messages — no direct state mutation from background threads.
- **FR-004**: The controller MUST maintain exactly four externally-observable states: Idle, Connecting, Streaming, and Disconnecting. No state may be skipped and no concurrent states are permitted.
- **FR-005**: A pipeline restart (e.g., triggered by Low Latency toggle or speaker switch) MUST fully complete the Disconnecting → Idle transition before entering Connecting. No partially-active sessions may overlap.

#### Auto-Reconnect

- **FR-006**: When the controller receives `WM_RAOP_FAILED` or `WM_SPEAKER_LOST` while in Streaming state, the controller MUST attempt up to 3 full pipeline reconnects using exponential-backoff delays of 1 s, 2 s, and 4 s before each successive attempt (total retry window ≤ 7 s). Each individual retry attempt MUST be abandoned immediately if the device is not present in the Bonjour browser at the moment the attempt fires (see FR-007). `WM_RAOP_FAILED` indicates the RTSP session failed while the device may still be visible in Bonjour; `WM_SPEAKER_LOST` indicates the Bonjour entry disappeared — both trigger identical retry logic, with FR-007 governing cancellation.
- **FR-007**: If the failed device disappears from the Bonjour browser before the reconnect fires, the controller MUST cancel the pending reconnect, transition to Idle, and notify the user.
- **FR-008**: If all 3 reconnect attempts fail, the controller MUST transition to Idle and notify the user with an actionable message. No further automatic reconnects are attempted after the retry sequence is exhausted.

#### Default Audio Device Recovery

- **FR-009**: When the capture subsystem posts `WM_AUDIO_DEVICE_LOST` while the controller is in Streaming state, the controller MUST stop and re-initialise only Thread 3 (WasapiCapture) against the current default audio device, without tearing down or restarting the RTSP session (RaopSession / Thread 5).
- **FR-010**: The audio gap during a capture-side restart MUST not exceed 50 milliseconds under normal operating conditions.
- **FR-011**: If capture re-initialisation fails, the controller MUST perform a full pipeline teardown, transition to Idle, and notify the user.

#### Low-Latency Mode

- **FR-012**: When Low Latency mode is toggled while Streaming, the controller MUST perform a full pipeline restart using the new ring buffer capacity (32 slots when enabled, 128 slots when disabled).
- **FR-013**: When Low Latency mode is toggled while Idle, the controller MUST persist the preference without any pipeline action.
- **FR-014**: Low Latency preference MUST be persisted to the application configuration so the setting survives restarts.

#### Startup Auto-Connect

- **FR-015**: On startup, if a last-used device MAC address is present in configuration, the controller MUST start a 5-second discovery window. If that device is announced by the Bonjour browser within the window, the controller MUST automatically initiate a connection.
- **FR-016**: If the 5-second window expires without the device being discovered, the controller MUST remain in Idle state with no notification to the user.

#### Volume Control

- **FR-017**: Volume changes MUST be propagated to the active RTSP session immediately, without requiring a pipeline restart, regardless of the current Streaming state.
- **FR-018**: Volume preference MUST be persisted to configuration.

#### Error Surfaces

- **FR-019**: Every unrecoverable error (RTSP failure after reconnect, capture re-init failure, encoder unexpected stop) MUST produce a tray balloon notification with a specific, actionable message. Silent failures are not permitted. `AlacEncoderThread` unexpected termination posts `WM_ENCODER_ERROR` (`WM_APP+14`); `ConnectionController::OnEncoderError()` handles this by calling `BeginDisconnect()` and showing `IDS_ENCODER_ERROR` balloon.
- **FR-020**: Tray balloon notifications for errors MUST describe what went wrong and, where applicable, suggest a user action (e.g., "Check that your speaker is on and connected to the same network").
- **FR-021**: The tray context menu MUST visually distinguish the currently-connected speaker from others (e.g. bold text, checkmark, or equivalent platform indicator).

#### Message Handling

- **FR-021**: Any window message received in a state where it does not constitute a valid transition input MUST be silently discarded with no state change and no user-visible effect. A single debug-level trace log entry MUST be emitted identifying the message and the current state. No other side effect is permitted.

#### Real-Time Audio Path Constraints

- **FR-022**: The audio capture thread (Thread 3 / WasapiCapture) and the encoder thread (Thread 4 / AlacEncoderThread) MUST NOT perform any heap allocation (`new`, `malloc`, `realloc`, or equivalent) or acquire any mutex or lock during their steady-state hot-loop execution. All codec buffers, capture staging buffers, and any other per-loop working memory MUST be pre-allocated during thread initialisation before the hot loop begins. The ring buffer is the only inter-thread data structure shared between Thread 3 and Thread 4; it MUST be a lock-free single-producer / single-consumer (SPSC) structure.

#### Observability & Debug Tracing

- **FR-023**: The ConnectionController MUST emit a named debug trace (`OutputDebugString` or equivalent `TRACE()` macro) at every state transition (e.g., Idle → Connecting, Connecting → Streaming, Streaming → Disconnecting, Disconnecting → Idle), at every thread start and stop (Threads 3, 4, and 5), at each reconnect attempt (including the attempt number and the backoff delay applied), and at each capture restart. Per-frame hot-loop activity on Threads 3 and 4 MUST NOT be traced.

### Key Entities

- **ConnectionController**: The central coordinator. Owns references to all pipeline components, tracks current state, and dispatches all start/stop/reconnect logic. Owned by AppController for the application lifetime.
- **PipelineState**: The four-valued enum representing the controller's current state (Idle, Connecting, Streaming, Disconnecting). Transitions are the sole responsibility of the ConnectionController.
- **StreamSession**: Represents one active combination of a target speaker, a ring buffer, a capture instance, an encoder instance, and an RTSP session. Destroyed on disconnect; a new one is created on each connect.
- **AppConfig**: The persisted configuration record containing last-used device MAC, low-latency flag, and volume level. Read at startup; updated on every relevant user action.
- **Window Message Contract**: The set of Win32 messages posted between background threads and the UI thread:
  - `WM_DEVICE_DISCOVERED` — BonjourBrowser announces a new AirPlay speaker.
  - `WM_AUDIO_DEVICE_LOST` — WasapiCapture (Thread 3) reports the Windows audio capture device was removed or changed. Triggers a **capture-only restart**: stop Thread 3, wait ≤ 50 ms, reinitialize WASAPI capture against the new default device; RTSP session is not disturbed.
  - `WM_SPEAKER_LOST` — BonjourBrowser reports that a previously-discovered AirPlay speaker is no longer visible. If the affected speaker is the currently-connected one, triggers a **full RAOP disconnect + 3-retry reconnect** with 1 s / 2 s / 4 s exponential-backoff (same path as `WM_RAOP_FAILED`).
  - `WM_STREAM_STARTED` — Signals that all pipeline components have started successfully.
  - `WM_STREAM_STOPPED` — Signals that the pipeline has fully stopped.
  - `WM_RAOP_FAILED` — RaopSession reports a fatal session failure. Triggers the **full RAOP disconnect + 3-retry reconnect** path (identical to `WM_SPEAKER_LOST`).
  - `WM_CAPTURE_ERROR` — WasapiCapture reports a non-device-loss capture error (e.g., buffer overrun). Treated as an unrecoverable pipeline error; controller performs full teardown and notifies user.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: From the moment the user clicks a speaker in the tray menu, audio is audible through that speaker within 3 seconds under normal network conditions.
- **SC-002**: The "Disconnect" action completes (all pipeline components stopped, controller in Idle) within 2 seconds of the user selecting it.
- **SC-003**: When the Windows default audio device changes while streaming, audio resumes through the receiver within 50 milliseconds, with the AirPlay session remaining uninterrupted. This bound is enforced by the combination of: (a) zero heap allocation during the hot loop on Threads 3 and 4 (FR-022), and (b) the lock-free SPSC ring buffer eliminating mutex contention on the capture → encoder path.
- **SC-004**: After a transient RTSP failure with the device still present, the application reconnects and resumes streaming without any user action within the 7-second retry window (3 attempts at 1 s / 2 s / 4 s backoff); the user experiences at most a brief audio gap and a reconnecting notification.
- **SC-005**: 100% of unrecoverable errors produce a visible tray balloon notification; zero silent failures occur in any error scenario covered by the requirements.
- **SC-006**: Low Latency mode toggle takes effect (pipeline restarts and streaming resumes) within 3 seconds of the user selecting it.
- **SC-007**: Startup auto-connect, when the target device is present, completes streaming initiation within 8 seconds of application launch (5-second discovery window + up to 3 seconds for connection).
- **SC-008**: Volume changes are reflected on the receiver within 500 milliseconds of the user adjusting the control, with no interruption to the audio stream.
- **SC-009**: The controller correctly serializes all state transitions — no test scenario produces a state where two pipeline instances are running concurrently.
- **SC-010**: All four pipeline states are reachable and all defined state transitions are exercisable in an automated integration test without a live AirPlay device (using stubs/mocks for WasapiCapture, RaopSession, and AlacEncoderThread).

## Technical Constraints

- **TC-001 — Hard real-time audio threads**: Threads 3 (WasapiCapture) and 4 (AlacEncoderThread) operate under a hard real-time constraint during steady-state streaming. No heap allocation and no mutex/lock acquisition are permitted inside the hot loop of either thread. Violation of this constraint risks audio glitches, priority inversion, and breach of the ≤50 ms capture-restart gap guarantee (SC-003).
- **TC-002 — Lock-free SPSC ring buffer**: The shared ring buffer between Thread 3 and Thread 4 MUST be implemented as a lock-free SPSC queue. No other synchronisation primitive may be introduced on the capture → encoder data path.
- **TC-003 — Pre-allocated buffers**: All codec working memory (ALAC frame scratch buffers) and WASAPI capture staging buffers MUST be allocated once during thread initialisation. Buffer sizes are derived from the configured ring-buffer slot capacity (128 slots in normal mode, 32 slots in low-latency mode); they MUST NOT be resized at runtime.
- **TC-004 — UI-thread state management**: All ConnectionController state transitions occur exclusively on Thread 1 (the Win32 message-loop / UI thread). No additional synchronisation primitives (mutexes, condition variables, atomics) are permitted for state management; Win32 message serialisation is the sole coordination mechanism.

## Assumptions

- All five existing components (WasapiCapture, AlacEncoderThread, RaopSession, BonjourBrowser, TrayIcon/AppController) are fully functional and pass their existing unit/E2E tests. The ConnectionController does not modify them — it only calls their public start/stop APIs and receives their posted messages.
- The Win32 message loop on Thread 1 is the sole orchestration surface. The controller does not introduce any additional synchronisation primitives (mutexes, condition variables) for state management; Win32 message serialisation is sufficient.
- The 5-second auto-connect discovery window is fixed. Making it user-configurable is out of scope for this feature.
- The reconnect policy after an RTSP failure is 3 attempts with exponential-backoff delays of 1 s, 2 s, and 4 s. Policies beyond this (e.g., unlimited retries or user-configurable back-off) are out of scope.
- The "volume control" referred to in the requirements is a logical volume level (e.g., 0–100) communicated to the RTSP session; actual Windows system volume is not affected by AirBeam.
- Concurrent streaming to multiple speakers simultaneously is out of scope for this feature.
- The tray balloon notification system provided by the existing TrayIcon component is sufficient for all error surfaces; no separate notification framework is needed.
- Configuration persistence (reading and writing AppConfig / config.json) is already implemented. This feature reads and writes config values through the existing AppConfig API.
- The ring buffer implementations for both capacities (128-slot and 32-slot) already exist and are tested. ConnectionController selects between them at construction time based on the current config flag.

## Clarifications

### Session 2026-03-30

- Q: Which reconnect policy should the spec reflect — single attempt with 5-second flat delay, or 3 retries with exponential backoff? → A: 3 retries with exponential backoff: 1 s → 2 s → 4 s (total max retry window: 7 s before giving up)

- Q: When a message arrives in a state where it isn't valid (wrong-state arrival), what should happen? → A: Silently discard; no side effects; emit a debug trace log only. State is unchanged.

- Q: Does `WM_DEVICE_LOST` carry a single message with a payload distinguishing audio device vs. speaker, or should it be two distinct messages with distinct names? → A: Two distinct messages: `WM_AUDIO_DEVICE_LOST` (posted by WasapiCapture when the Windows capture device is removed/changed; triggers capture-only restart: stop Thread 3, wait ≤ 50 ms, reinitialize WASAPI against new default device) and `WM_SPEAKER_LOST` (posted by BonjourBrowser when the AirPlay speaker disappears; triggers full RAOP disconnect + 3-retry reconnect with 1 s / 2 s / 4 s backoff — same path as `WM_RAOP_FAILED`).

- Q: What are the real-time audio path safety constraints on Threads 3 and 4? → A: Hard real-time constraint: zero heap allocation (`new`/`malloc`/`realloc`) and zero mutex/lock acquisition during the steady-state hot loop on BOTH Thread 3 (WasapiCapture) and Thread 4 (AlacEncoderThread). All codec and capture buffers are pre-allocated during thread initialisation. The ring buffer is the only inter-thread data structure and is a lock-free SPSC queue.

- Q: What should be logged for normal pipeline lifecycle events? → A: Named `OutputDebugString`/`TRACE()` calls at every state transition (Idle→Connecting, Connecting→Streaming, Streaming→Disconnecting, Disconnecting→Idle), thread start/stop, each reconnect attempt (including attempt number and delay), and each capture restart. Hot-loop internals (per-frame activity) are NOT logged.