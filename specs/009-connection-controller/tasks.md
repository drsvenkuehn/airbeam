# Tasks: Full App Integration — ConnectionController and Live Audio Path

**Feature**: `009-connection-controller`  
**Input**: `specs/009-connection-controller/` (spec.md, plan.md, research.md, data-model.md, contracts/, quickstart.md)  
**Branch**: `009-connection-controller`

**Tests**: Included — SC-010 and `contracts/state-machine.md` explicitly require all state transitions to be exercisable in automated tests without a live AirPlay device. Test file: `tests/unit/test_connection_controller.cpp`.

**Organization**: Tasks are grouped by user story. US1 and US2 (P1) form the MVP. US3–US7 (P2/P3) extend it incrementally.

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (US1–US7)
- Each task includes exact file path(s)

---

## Phase 1: Setup — Independent Value Types

**Purpose**: Create the two standalone data types that have zero external dependencies. These unblock Phase 2 and the ConnectionController.h declaration.

- [x] T001 [P] Create `src/core/PipelineState.h` — define `enum class PipelineState : uint8_t { Idle, Connecting, Streaming, Disconnecting }` with doc comments per `data-model.md §1`; no includes required beyond `<cstdint>`
- [x] T002 [P] Create `src/core/ReconnectContext.h` — define `struct ReconnectContext` with `kMaxAttempts=3`, `kDelaysMs[3]={1000,2000,4000}`, `targetDevice`, `attempt`, `pending`, `Reset()`, `HasAttemptsLeft()`, `CurrentDelayMs()` per `data-model.md §2`; include `<windef.h>` for `UINT` and the existing `AirPlayReceiver` header

**Checkpoint**: T001 and T002 compile independently. No other source files need to change.

---

## Phase 2: Foundational — Core Infrastructure

**Purpose**: Update the message contract, update all existing callers of renamed messages, and create `StreamSession` and the `ConnectionController` class skeleton. **No user story can begin until this phase is complete.**

⚠️ **CRITICAL**: T003 must complete before T004 and T005. T006 must complete before T007. T007 and T001–T002 must complete before T008.

- [x] T003 Update `src/core/Messages.h` — (a) rename `WM_DEFAULT_DEVICE_CHANGED` → `WM_AUDIO_DEVICE_LOST` (keep value `WM_APP+6`); rename `WM_TEARDOWN_COMPLETE` → `WM_STREAM_STOPPED` (keep value `WM_APP+8`); (b) append four new constants: `WM_SPEAKER_LOST = WM_APP+10`, `WM_STREAM_STARTED = WM_APP+11`, `WM_CAPTURE_ERROR = WM_APP+12`, `WM_DEVICE_DISCOVERED = WM_APP+13`; add WPARAM/LPARAM ownership comments for each per `contracts/messages.md §Complete-Message-Table`
- [x] T004 [P] Update `src/audio/WasapiCapture.cpp` — replace `PostMessage(hwnd_, WM_DEFAULT_DEVICE_CHANGED, ...)` with `WM_AUDIO_DEVICE_LOST`; add `PostMessage(hwnd_, WM_CAPTURE_ERROR, errorCode, 0)` on all non-device-loss WASAPI error paths (e.g., `AUDCLNT_E_BUFFER_ERROR`) per `contracts/messages.md §Caller-Update-Checklist` and `§WM_CAPTURE_ERROR`
- [x] T005 [P] Update `src/core/AppController.cpp` — replace `case WM_DEFAULT_DEVICE_CHANGED:` with `case WM_AUDIO_DEVICE_LOST:`; replace `case WM_TEARDOWN_COMPLETE:` with `case WM_STREAM_STOPPED:`; replace `PostMessage(hwnd_, WM_TEARDOWN_COMPLETE, ...)` with `WM_STREAM_STOPPED` per `contracts/messages.md §Caller-Update-Checklist`; project must compile after this task
- [x] T006 [P] Create `src/core/StreamSession.h` — declare non-copyable, non-movable `StreamSession` class with `bool Init(const AirPlayReceiver& target, bool lowLatency, HWND hwnd)`; accessors `Capture()`, `Encoder()`, `Raop()`, `Target()`, `IsLowLatency()`, `RingCapacity()`; private fields: `target_`, `lowLatency_`, `ring_` (SpscRingBufferPtr), `cipher_`, `retransmit_`, `capture_`, `encoder_`, `raop_` per `data-model.md §3`
- [x] T007 Create `src/core/StreamSession.cpp` — implement `Init()`: allocate `SpscRingBuffer<AudioFrame, 128>` (normal) or `SpscRingBuffer<AudioFrame, 32>` (lowLatency) and store in `ring_`; construct `AesCbcCipher`, `RetransmitBuffer`, `WasapiCapture`, `AlacEncoderThread`, `RaopSession` by calling their respective constructors/Init methods with appropriate parameters; all allocation occurs here (before any `Start()` call) to satisfy FR-022 and TC-003; return `false` if any sub-init fails
- [x] T008 Create `src/core/ConnectionController.h` — declare full `ConnectionController` class per `data-model.md §4` and `contracts/connection-controller-api.md`: constructor taking `(HWND, Config&, ReceiverList&, TrayIcon&, BalloonNotify&, Logger&)`; all public command methods (`Connect`, `Disconnect`, `SetLowLatency`, `SetVolume`, `StartAutoConnectWindow`); all Win32 message handlers (`OnRaopConnected`, `OnRaopFailed`, `OnStreamStopped`, `OnAudioDeviceLost`, `OnSpeakerLost`, `OnCaptureError`, `OnDeviceDiscovered`, `OnTimer`); private helpers (`BeginConnect`, `BeginDisconnect`, `OnTeardownComplete`, `ScheduleReconnect`, `AttemptReconnect`, `CancelReconnect`, `TransitionTo`); timer constants `TIMER_RECONNECT_RETRY=10`, `TIMER_AUTOCONNECT=11`; private member fields (`hwnd_`, `config_`, `receivers_`, `tray_`, `balloon_`, `logger_`, `state_`, `reconnect_`, `inAutoConnectWindow_`, `pendingLowLatencyToggle_`, `pendingDisconnect_`, `session_`)

**Checkpoint**: Project compiles clean after Phase 2. All renamed message references resolve. ConnectionController.h and StreamSession.h are present; .cpp stubs (empty implementations) allow linking.

---

## Phase 3: User Story 1 — Connect to a Speaker and Stream Audio (Priority: P1) 🎯 MVP

**Goal**: User selects a speaker from the tray menu; audio begins streaming within 3 seconds (SC-001); tray updates to Streaming state.

**Independent Test**: Construct `ConnectionController` with mocks, call `Connect()`, simulate `WM_RAOP_CONNECTED`, assert state == Streaming and tray updated.

### Tests for User Story 1

- [x] T009 Create `tests/unit/test_connection_controller.cpp` — add to `CMakeLists.txt` test target; define mock infrastructure: `MockWasapiCapture` (`Init`, `Start`, `Stop`, `IsRunning`), `MockAlacEncoderThread` (`Init`, `Start`, `Stop`, `IsRunning`), `MockRaopSession` (`Start`, `Stop`, `SetVolume`, `IsRunning`, `AudioSocket()`), `MockReceiverList` (`Find(mac)`, `Get(index)`), `MockBalloonNotify` (`Show(resId,...)`), `FakeHwnd`; verify it compiles and links per `quickstart.md §mock-pattern` and `contracts/state-machine.md §Test-Coverage-Requirements`
- [x] T010 [US1] Write happy-path and wrong-state tests in `tests/unit/test_connection_controller.cpp`: (a) `HappyPath_Idle_to_Connecting_to_Streaming_to_Idle` — call `Connect()`, assert Connecting; call `OnRaopConnected()`, assert Streaming; call `Disconnect()` + `OnStreamStopped()`, assert Idle; (b) `WrongState_RaopConnected_InDisconnecting_IsDiscarded` — assert no state change; (c) `WrongState_RaopFailed_InIdle_IsDiscarded` — assert no state change; all per `contracts/state-machine.md §Test-Coverage-Requirements`

### Implementation for User Story 1

- [x] T011 [US1] Create `src/core/ConnectionController.cpp` — implement constructor (member initialisation, no threads started); destructor (call `Disconnect()` if not Idle); `TransitionTo(PipelineState next)`: log `CC_TRACE(L"[CC] state %d → %d", old, next)` using `Logger::Trace` per `research.md Decision 5`, update `state_`, update `tray_.SetState()`; `GetState()` and `IsStreaming()` accessors; define `CC_TRACE` macro with `#ifdef _DEBUG` guard per `research.md Decision 5 §Macro-Definition`
- [x] T012 [US1] Implement `ConnectionController::BeginConnect()` in `src/core/ConnectionController.cpp` — assert `state_ == Idle`; construct `session_ = make_unique<StreamSession>()`; call `session_->Init(target, config_.lowLatency, hwnd_)` — return on failure with `balloon_.Show(IDS_ERROR_CAPTURE_FAILED)`; call `session_->Capture().Start()` (Thread 3); call `session_->Raop().Start(cfg)` (Thread 5, async); `TransitionTo(Connecting)`; log CC_TRACE per `contracts/state-machine.md §BeginConnect-Sequence` steps 1–6 and `research.md §thread-startup-ordering`
- [x] T013 [US1] Implement `ConnectionController::Connect()` in `src/core/ConnectionController.cpp` — if Idle: `BeginConnect(target)`; if Streaming (same target): no-op; if Streaming/Connecting (different target): `BeginDisconnect(forReconnect=true)` and store target as pending reconnect target; if Disconnecting: store as pending; per `contracts/connection-controller-api.md §Connect`
- [x] T014 [US1] Implement `ConnectionController::OnRaopConnected()` in `src/core/ConnectionController.cpp` — wrong-state guard: if state != Connecting, `CC_TRACE` + return (FR-021); `session_->Encoder().Init(session_->Raop().AudioSocket(), ...)` (heap alloc OK pre-loop, FR-022); `session_->Encoder().Start()` (Thread 4); `PostMessage(hwnd_, WM_STREAM_STARTED, 0, 0)`; `config_.Save()` (persist lastDevice); `TransitionTo(Streaming)`; per `contracts/state-machine.md §BeginConnect-Sequence` steps 9–13
- [x] T015 [US1] Update `src/core/AppController.h` and `src/core/AppController.cpp` — (a) add `ConnectionController controller_` member to AppController; (b) in `Start()`: construct `controller_` with all injected dependencies; (c) in `WndProc`: add `case WM_RAOP_CONNECTED: controller_.OnRaopConnected(lParam)` and `case WM_STREAM_STARTED: HandleStreamStarted()`; (d) implement `HandleStreamStarted()`: `TrayIcon::SetState(TrayState::Streaming, targetName)`, enable "Disconnect" menu item, disable speaker list; (e) delegate `IDM_DEVICE_BASE + N` menu commands to `controller_.Connect(*receiver)` per `quickstart.md §wndproc-wiring` and `§connect-happy-path`; note: `Config::Save()` is called in `OnRaopConnected()` (T014), not here — do not add a second call

**Checkpoint**: US1 complete. User selects a speaker → tray shows Streaming → audio flows (SC-001). Verify with T010 tests passing.

---

## Phase 4: User Story 2 — Disconnect from a Speaker (Priority: P1)

**Goal**: User clicks "Disconnect"; pipeline tears down in strict order within 2 seconds (SC-002); tray returns to Idle. Switching speakers serialises sessions with no overlap (FR-005).

**Independent Test**: Construct in Streaming state, call `Disconnect()`, simulate `WM_STREAM_STOPPED`, assert Idle and session_ == nullptr.

### Tests for User Story 2

- [x] T016 [US2] Write disconnect tests in `tests/unit/test_connection_controller.cpp`: (a) `UserDisconnect_WhileConnecting_CancelsAndGoesIdle` — Connect(), then Disconnect() before OnRaopConnected, assert Disconnecting then Idle; (b) `SpeakerSwitch_Streaming_TeardownThenReconnect` — stream to A, then Connect(B), assert Disconnecting then Connecting with target B per `contracts/state-machine.md §Test-Coverage-Requirements`

### Implementation for User Story 2

- [x] T017 [US2] Implement `ConnectionController::Disconnect()` in `src/core/ConnectionController.cpp` — Idle: no-op; Connecting/Streaming: `reconnect_.Reset()`, `BeginDisconnect(forReconnect=false)`; Disconnecting: set `pendingDisconnect_ = true` per `contracts/connection-controller-api.md §Disconnect`
- [x] T018 [US2] Implement `ConnectionController::BeginDisconnect()` in `src/core/ConnectionController.cpp` — assert state is Connecting or Streaming; `TransitionTo(Disconnecting)`; `session_->Capture().Stop()` (Thread 3, ≤30ms); brief ring drain ≤50ms (Thread 4 flushes remaining frames); `session_->Encoder().Stop()` (Thread 4); `session_->Raop().Stop()` (RTSP TEARDOWN, Thread 5); `PostMessage(hwnd_, WM_STREAM_STOPPED, 0, 0)`; store `forReconnect` flag; emit CC_TRACE per `contracts/state-machine.md §BeginDisconnect-Sequence`; teardown order enforces FR-002; total ≤330ms (SC-002)
- [x] T019 [US2] Implement `ConnectionController::OnStreamStopped()` and `OnTeardownComplete()` in `src/core/ConnectionController.cpp` — wrong-state guard (not Disconnecting → discard + CC_TRACE); call `OnTeardownComplete()`: `session_.reset()` (all threads already stopped); if `forReconnect` and reconnect pending: `ScheduleReconnect()`; else: `TransitionTo(Idle)`; handle `pendingDisconnect_` and `pendingLowLatencyToggle_` deferred flags per `contracts/state-machine.md §BeginDisconnect-Sequence` steps 8–9
- [x] T020 [US2] Update `src/core/AppController.cpp` — add `case WM_STREAM_STOPPED: controller_.OnStreamStopped(); HandleStreamStopped();`; implement `HandleStreamStopped()`: `TrayIcon::SetState(TrayState::Idle)`, hide "Disconnect" menu item, re-enable speaker list; wire `IDM_DISCONNECT` → `controller_.Disconnect()`; add `case WM_TIMER: controller_.OnTimer(static_cast<UINT>(wParam));` per `quickstart.md §wndproc-wiring §disconnect`

**Checkpoint**: US1 + US2 complete. Full connect/disconnect lifecycle works. Speaker-switching serialises correctly (FR-005, SC-002). Verify with T010 + T016.

---

## Phase 5: User Story 3 — Automatic Reconnect After Network Failure (Priority: P2)

**Goal**: RTSP failure or speaker disappearance triggers up to 3 reconnect attempts with 1s/2s/4s backoff (SC-004, FR-006). Device absence before retry cancels cleanly (FR-007). All 3 failures → balloon notification (FR-008).

**Independent Test**: Simulate `WM_RAOP_FAILED` in Streaming, verify timer fires with correct delays, reconnect succeeds or fails with appropriate balloon.

### Tests for User Story 3

- [x] T021 [US3] Write reconnect tests in `tests/unit/test_connection_controller.cpp`: (a) `Reconnect_SucceedsOnSecondAttempt` — OnRaopFailed() in Streaming→BeginDisconnect→OnStreamStopped; fire timer twice (first BeginConnect+OnRaopFailed, second BeginConnect+OnRaopConnected), assert Streaming, reconnect_.Reset(), and MockBalloonNotify.Show(IDS_RECONNECTED) was called; (b) `Reconnect_AllThreeAttemptsFail_ShowsBalloon` — three consecutive BeginConnect+OnRaopFailed cycles, assert Idle and MockBalloonNotify.Show(IDS_RECONNECT_FAILED); (c) `Reconnect_DeviceDisappearsBeforeTimer_CancelledWithBalloon` — set up pending reconnect, remove device from MockReceiverList, fire timer, assert Idle and IDS_SPEAKER_UNAVAILABLE balloon per `contracts/state-machine.md §Test-Coverage-Requirements` items 3–5

### Implementation for User Story 3

- [x] T022 [US3] Implement `ConnectionController::OnRaopFailed()` in `src/core/ConnectionController.cpp` — valid in Connecting or Streaming: `BeginDisconnect(forReconnect=true)`; wrong-state: `CC_TRACE` + return (FR-021); per `contracts/connection-controller-api.md §OnRaopFailed` and `contracts/messages.md §WM_RAOP_FAILED`
- [x] T023 [US3] Implement `ConnectionController::ScheduleReconnect()`, `AttemptReconnect()`, and `CancelReconnect()` in `src/core/ConnectionController.cpp` — `ScheduleReconnect()`: set `reconnect_.pending=true`, `SetTimer(hwnd_, TIMER_RECONNECT_RETRY, kDelaysMs[reconnect_.attempt], nullptr)`, emit CC_TRACE with attempt# and delay; `AttemptReconnect()`: `reconnect_.attempt++`, check `receivers_.Find(reconnect_.targetDevice.mac)` — if present: `BeginConnect(reconnect_.targetDevice)`, else: `CancelReconnect()` + `balloon_.Show(IDS_SPEAKER_UNAVAILABLE)`; on successful reconnect (OnRaopConnected called from reconnect context): call `balloon_.Show(IDS_RECONNECTED, session_->Target().name)` (US3 Acceptance Scenario 3); after last failed attempt: `reconnect_.Reset()` + `TransitionTo(Idle)` + `balloon_.Show(IDS_RECONNECT_FAILED)`; `CancelReconnect()`: `KillTimer(hwnd_, TIMER_RECONNECT_RETRY)`, `reconnect_.Reset()` per `contracts/state-machine.md §Reconnect-Sub-Machine` and `research.md Decision 1`
- [x] T024 [US3] Implement `ConnectionController::OnSpeakerLost()` in `src/core/ConnectionController.cpp` — delete `devId` pointer after reading (LPARAM ownership per `contracts/messages.md §WM_SPEAKER_LOST`); valid in Streaming only: if `devId` matches `session_->Target().serviceName` → `BeginDisconnect(forReconnect=true)`; wrong-state or non-matching device: `CC_TRACE` + return
- [x] T025 [US3] Update `src/discovery/MdnsDiscovery.cpp` — on `DNSServiceBrowse` remove event: `PostMessage(hwnd_, WM_SPEAKER_LOST, 0, (LPARAM)new std::wstring(serviceId))`; on new receiver resolved and added: `PostMessage(hwnd_, WM_DEVICE_DISCOVERED, receiverList.CurrentVersion(), index)` alongside existing `WM_RECEIVERS_UPDATED`; per `contracts/messages.md §Caller-Update-Checklist` and `§WM_DEVICE_DISCOVERED`
- [x] T026 [US3] Update `src/core/AppController.cpp` — add `case WM_RAOP_FAILED: controller_.OnRaopFailed(lParam);` and `case WM_SPEAKER_LOST: controller_.OnSpeakerLost(reinterpret_cast<const wchar_t*>(lParam));` to WndProc per `quickstart.md §wndproc-wiring`

**Checkpoint**: US3 complete. App reconnects after transient RTSP failure. 1s/2s/4s backoff fires correctly. All 3 fail → balloon. Device gone before retry → cancelled cleanly (SC-004, FR-006/007/008).

---

## Phase 6: User Story 4 — Seamless Audio Device Substitution (Priority: P2)

**Goal**: Windows default audio device changes while streaming; capture restarts within ≤50ms without dropping the RTSP session (SC-003, FR-009/010). Failed reinit triggers full teardown with balloon (FR-011).

**Independent Test**: Simulate `WM_AUDIO_DEVICE_LOST` in Streaming, verify MockWasapiCapture.Stop()+Init()+Start() called, state remains Streaming.

### Tests for User Story 4

- [x] T027 [US4] Write capture-restart tests in `tests/unit/test_connection_controller.cpp`: (a) `AudioDeviceLost_ReinitSucceeds_StateRemainsStreaming` — OnAudioDeviceLost() in Streaming, assert Capture().Stop()+Init()+Start() called, state == Streaming, Encoder still running; (b) `AudioDeviceLost_ReinitFails_TriggersFullDisconnect` — MockWasapiCapture.Init() returns false, assert Disconnecting then Idle and IDS_ERROR_NO_AUDIO_DEVICE balloon per `contracts/state-machine.md §Test-Coverage-Requirements` items 6–7

### Implementation for User Story 4

- [x] T028 [US4] Implement `ConnectionController::OnAudioDeviceLost()` in `src/core/ConnectionController.cpp` — wrong-state guard (not Streaming → discard + CC_TRACE); `CC_TRACE(L"[CC] Capture restart: audio device lost")`; `session_->Capture().Stop()` (join ≤30ms); `ok = session_->Capture().Init(...)` (re-enumerate default device on Thread 1, heap alloc OK); if ok: `session_->Capture().Start()` (Thread 3 resumes), `CC_TRACE(L"[CC] Capture restart: complete")`; else: `BeginDisconnect()` + `balloon_.Show(IDS_ERROR_NO_AUDIO_DEVICE)`; Thread 4 NOT stopped during restart; per `contracts/state-machine.md §Capture-Only-Restart` and FR-009/FR-010/SC-003; 50ms gap guarantee documented in `research.md §capture-restart-timing`
- [x] T029 [US4] Implement `ConnectionController::OnCaptureError()` in `src/core/ConnectionController.cpp` — valid in Streaming or Connecting: `BeginDisconnect(forReconnect=false)` + `balloon_.Show(IDS_ERROR_CAPTURE_FAILED)`; wrong-state: CC_TRACE + return per `contracts/connection-controller-api.md §OnCaptureError`
- [x] T030 [US4] Update `src/core/AppController.cpp` — add `case WM_AUDIO_DEVICE_LOST: controller_.OnAudioDeviceLost();` and `case WM_CAPTURE_ERROR: controller_.OnCaptureError();` to WndProc per `quickstart.md §wndproc-wiring`

**Checkpoint**: US4 complete. Capture restarts seamlessly on device change (≤50ms, SC-003). Failed reinit teardown works. Verify with T027.

---

## Phase 7: User Story 5 — Low-Latency Mode Toggle (Priority: P3)

**Goal**: Toggling "Low Latency" while Streaming restarts pipeline with 32-slot buffer; while Idle saves preference only (FR-012/013/014). Toggle during transitions is deferred (FR-005).

**Independent Test**: Toggle while Streaming → full pipeline restart with new ring capacity; toggle while Idle → config updated, no pipeline action.

### Tests for User Story 5

- [x] T031 [US5] Write low-latency tests in `tests/unit/test_connection_controller.cpp`: (a) `SetLowLatency_WhileStreaming_TriggersPipelineRestart` — in Streaming, SetLowLatency(true), assert BeginDisconnect called then reconnect with lowLatency=true ring; (b) `SetLowLatency_WhileIdle_PersistsPreference_NoPipelineAction` — SetLowLatency(true) in Idle, assert config_.lowLatency=true, state remains Idle per `contracts/state-machine.md §Test-Coverage-Requirements` items 11–12

### Implementation for User Story 5

- [x] T032 [US5] Implement `ConnectionController::SetLowLatency()` in `src/core/ConnectionController.cpp` — no-op if `config_.lowLatency == enabled`; persist `config_.lowLatency = enabled` + `config_.Save()` (FR-014); Idle: no pipeline action (FR-013); Streaming: `BeginDisconnect(forReconnect=true)` using same target and new latency setting (pipeline restarts via OnTeardownComplete path); Connecting or Disconnecting: set `pendingLowLatencyToggle_ = true` (deferred, FR-005); per `contracts/connection-controller-api.md §SetLowLatency`
- [x] T033 [US5] Update `src/core/AppController.cpp` — wire `IDM_LOW_LATENCY_TOGGLE` → `controller_.SetLowLatency(!config_.lowLatency)`; update tray menu checkmark state to reflect current `config_.lowLatency`

**Checkpoint**: US5 complete. Low Latency toggle restarts with correct ring size within 3s (SC-006). Deferred toggle during transitions works. Verify with T031.

---

## Phase 8: User Story 6 — Startup Auto-Connect to Last-Used Speaker (Priority: P3)

**Goal**: On startup, if `config_.lastDevice` is set and that device appears in Bonjour within 5 seconds, connect automatically (FR-015). Window expiry is silent (FR-016). Completes within 8s from launch (SC-007).

**Independent Test**: Populate config with lastDevice MAC, call StartAutoConnectWindow(), fire OnDeviceDiscovered() with matching MAC, assert Connect() called.

### Tests for User Story 6

- [x] T034 [US6] Write auto-connect tests in `tests/unit/test_connection_controller.cpp`: (a) `AutoConnect_DeviceDiscoveredWithin5s_ConnectsAutomatically` — set config_.lastDevice, StartAutoConnectWindow(), OnDeviceDiscovered(matchingMAC), assert state == Connecting; (b) `AutoConnect_TimerExpires_NoMatchNoNotification` — StartAutoConnectWindow(), fire TIMER_AUTOCONNECT without discovering matching device, assert Idle and MockBalloonNotify.Show never called per `contracts/state-machine.md §Test-Coverage-Requirements` items 13–14

### Implementation for User Story 6

- [x] T035 [US6] Implement `ConnectionController::StartAutoConnectWindow()` and `ConnectionController::OnDeviceDiscovered()` in `src/core/ConnectionController.cpp` — `StartAutoConnectWindow()`: if `config_.lastDevice` empty → no-op; else `inAutoConnectWindow_ = true`, `SetTimer(hwnd_, TIMER_AUTOCONNECT, 5000, nullptr)`; `OnDeviceDiscovered(lParam)`: always processed (no wrong-state discard per api contract); if `inAutoConnectWindow_` and `receivers_.Get(lParam).mac == config_.lastDevice` → `Connect(receiver)` (FR-015); `TIMER_AUTOCONNECT` handler (in `OnTimer()`): `inAutoConnectWindow_ = false`, `KillTimer(hwnd_, TIMER_AUTOCONNECT)` — no notification (FR-016); per `contracts/connection-controller-api.md §StartAutoConnectWindow` and `§OnDeviceDiscovered`
- [x] T036 [US6] Update `src/core/AppController.cpp` — call `controller_.StartAutoConnectWindow()` in `AppController::Start()` after constructing ConnectionController; add `case WM_DEVICE_DISCOVERED: controller_.OnDeviceDiscovered(lParam);` to WndProc per `quickstart.md §wndproc-wiring`

**Checkpoint**: US6 complete. Auto-connect fires within 5s discovery window (SC-007). Window expiry is silent (FR-016). Verify with T034.

---

## Phase 9: User Story 7 — Volume Adjustment Without Pipeline Restart (Priority: P3)

**Goal**: Volume changes propagate to active RTSP session immediately without stream interruption (FR-017, SC-008 ≤500ms). Persisted to config (FR-018).

**Independent Test**: SetVolume() while Streaming → MockRaopSession.SetVolume() called; while Idle → config persisted, not propagated.

### Tests for User Story 7

- [x] T037 [US7] Write volume tests in `tests/unit/test_connection_controller.cpp`: (a) `SetVolume_WhileStreaming_PropagatedImmediately` — in Streaming, SetVolume(0.5f), assert MockRaopSession.SetVolume(0.5f) called, state unchanged; (b) `SetVolume_WhileIdle_PersistedNotPropagated` — SetVolume(0.7f) in Idle, assert config_.volume==0.7f, MockRaopSession.SetVolume never called per `contracts/state-machine.md §Test-Coverage-Requirements` items 15–16

### Implementation for User Story 7

- [x] T038 [US7] Implement `ConnectionController::SetVolume()` in `src/core/ConnectionController.cpp` — persist `config_.volume = linear` + `config_.Save()` (FR-018); if state == Streaming: `session_->Raop().SetVolume(linear)` immediately (FR-017); otherwise: volume saved and applied at next `BeginConnect()` via `RaopSession::Start(cfg)` config parameter; per `contracts/connection-controller-api.md §SetVolume`
- [x] T039 [US7] Update `src/core/AppController.cpp` — wire volume slider or menu action → `controller_.SetVolume(linear)` where `linear` is in [0.0f, 1.0f]

**Checkpoint**: US7 complete. Volume propagates within 500ms (SC-008). Persisted correctly (FR-018). Verify with T037.

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Localisation, observability completeness, real-time safety audit, and final test gate.

- [x] T040 [P] Add all 8 string resource IDs to the English `.rc` resource file and all 7 locale resource files: `IDS_CONNECTING_TO` ("Connecting to %s…"), `IDS_STREAMING_TO` ("Streaming to %s"), `IDS_RECONNECTING` ("Reconnecting to %s (attempt %d of 3)…"), `IDS_RECONNECT_FAILED` ("Could not reconnect to %s. Check that your speaker is on and connected to the same network."), `IDS_SPEAKER_UNAVAILABLE` ("%s is no longer available."), `IDS_ERROR_CAPTURE_FAILED` ("Audio capture failed unexpectedly. Please reconnect."), `IDS_ERROR_NO_AUDIO_DEVICE` ("No audio device found. Please connect an audio device and try again."), `IDS_RECONNECTED` ("Reconnected to %s."); per `quickstart.md §string-resource-ids` and Constitution §VIII (Localizable UI)
- [x] T041 [P] Audit and complete `CC_TRACE` / `Logger::Trace` call sites in `src/core/ConnectionController.cpp` — verify every state transition in `TransitionTo()` emits `[CC] state %d → %d`; every Thread 3/4/5 start/stop emits `[CC] Thread %s started/stopped`; each reconnect attempt emits `[CC] Reconnect attempt %d, delay %dms`; each capture restart emits `[CC] Capture restart: ...`; each wrong-state discard emits `[CC] %s ignored in state %d`; per FR-023 and `research.md Decision 5`; confirm NO trace calls in `WasapiCapture::CaptureLoop` or `AlacEncoderThread::EncodeLoop` hot paths (FR-022)
- [x] T042 Perform real-time safety audit per `quickstart.md §real-time-safety-checklist` — verify `WasapiCapture::CaptureLoop` (Thread 3) contains no `new`/`malloc`/`realloc`; `AlacEncoderThread::EncodeLoop` (Thread 4) contains no heap allocation; neither loop acquires any mutex or `CRITICAL_SECTION`; `SpscRingBuffer` uses only `std::atomic` with acquire/release ordering; `ConnectionController` never calls T3/T4 methods from within their own running threads; all buffers pre-allocated in `Init()` before any `Start()` call; TC-001/TC-002/TC-003 satisfied
- [x] T043 Run full test suite — execute `ctest --preset msvc-x64-debug-ci` from repo root; all test cases in `tests/unit/test_connection_controller.cpp` pass (16 state-machine scenarios covering all transitions in `contracts/state-machine.md`); no regressions in existing unit, integration, or E2E tests; SC-010 verified: all 4 states and all transitions exercisable without a live AirPlay device
- [x] T044 [US1] Add `WM_ENCODER_ERROR = WM_APP+14` to `src/core/Messages.h`; post from `AlacEncoderThread` on unexpected thread exit (not a clean `Stop()`-initiated shutdown) in `src/audio/AlacEncoderThread.cpp`; handle in `ConnectionController::OnEncoderError()` in `src/core/ConnectionController.cpp` with `BeginDisconnect()` + `balloon_.Show(IDS_ENCODER_ERROR)`; add `IDS_ENCODER_ERROR` to all locale resource files; add `case WM_ENCODER_ERROR: controller_.OnEncoderError();` to `AppController::WndProc` per `contracts/messages.md §WM_ENCODER_ERROR` and `spec.md §FR-019`
- [x] T045 [US2] Remove old `AppController` timer-based reconnect/retry logic (`KillTimer` IDs 1 and 2) now superseded by `ConnectionController` in `src/core/AppController.cpp`

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)        ──────────────────────────────────────────────┐
  T001 [P], T002 [P]   (independent; no deps)                        │
                                                                      ↓
Phase 2 (Foundational)  T003 → T004 [P], T005 [P]                   │
  T006 [P] (parallel with T003; no dep)                              │
  T007 (after T006)                                                   │
  T008 (after T001, T002, T006)                                      │
                                ↓                                     │
Phase 3 (US1)  T009 → T010, T011–T015 (sequential in CC.cpp)        │
                                ↓                                     │
Phase 4 (US2)  T016–T020                                             │
                                ↓                                     │
Phase 5 (US3)  T021–T026                                             │
                                ↓                                     │
Phase 6 (US4)  T027–T030                                             │
                                ↓                                     │
Phase 7 (US5)  T031–T033       ─┐                                    │
Phase 8 (US6)  T034–T036       ─┤ can run in parallel once US4 done │
Phase 9 (US7)  T037–T039       ─┘                                    │
                                ↓                                     │
Phase 10 (Polish) T040–T043                              ────────────┘
```

### User Story Dependencies

- **US1 (P1)**: Requires Phase 2 complete. No other story dependency.
- **US2 (P1)**: Requires US1 complete (uses BeginConnect, session_ teardown).
- **US3 (P2)**: Requires US2 complete (uses BeginDisconnect, OnStreamStopped reconnect path).
- **US4 (P2)**: Requires US1 complete. Independent of US2/US3 (separate message handler).
- **US5 (P3)**: Requires US2 complete (uses BeginDisconnect forReconnect path).
- **US6 (P3)**: Requires US1 complete (calls Connect()). Independent of US2–US5.
- **US7 (P3)**: Requires US1 complete (uses Raop().SetVolume()). Fully independent.

### Within Each Phase

- Tests MUST be written before implementation tasks and initially fail (red/compile-error phase).
- `ConnectionController.cpp` tasks within a story build on each other sequentially.
- `AppController.cpp` wiring tasks within a story are append-only (additive WndProc cases) and won't conflict with prior stories.

### Parallel Opportunities by Phase

- **Phase 1**: T001 ‖ T002
- **Phase 2**: T004 ‖ T005 ‖ T006 (all after T003; T006 can start independently)
- **Phase 3**: T009 starts as soon as Phase 2 is done; T011–T014 are sequential in CC.cpp
- **Phase 7/8/9**: All three can be parallelised once Phase 6 is complete (different CC.cpp methods, different stories)
- **Phase 10**: T040 ‖ T041 (different files)

---

## Parallel Example: User Story 1

```
# After Phase 2 completes, launch in parallel:
Task A: Write tests (T009, T010) in tests/unit/test_connection_controller.cpp
Task B: Implement ConnectionController constructor/TransitionTo (T011)

# Once T011 done, continue sequentially:
Task: Implement BeginConnect() (T012)
Task: Implement Connect() (T013)
Task: Implement OnRaopConnected() (T014)
Task: Wire AppController (T015)
```

---

## Implementation Strategy

### MVP First (US1 + US2 Only — Phases 1–4)

1. Complete Phase 1: Create PipelineState.h and ReconnectContext.h
2. Complete Phase 2: Messages.h, callers, StreamSession, ConnectionController.h (builds clean)
3. Complete Phase 3: Connect + Streaming happy path ← **first working audio**
4. Complete Phase 4: Disconnect ← **first usable app build**
5. **STOP and VALIDATE**: Manual tray test + T010/T016 pass
6. Demo / internal release

### Incremental Delivery

1. MVP (Phases 1–4) → working connect/disconnect
2. Add US3 (Phase 5) → auto-reconnect after network hiccup
3. Add US4 (Phase 6) → seamless audio device substitution
4. Add US5/US6/US7 (Phases 7–9) in parallel → polish features
5. Phase 10 → string resources, trace audit, safety gate, full test pass

### Parallel Team Strategy

With 2+ developers after Phase 2 completes:
- **Dev A**: US1 → US2 → US3 (sequential pipeline lifecycle)
- **Dev B**: US4 (independent capture-restart path), then US5/US6/US7

---

## Notes

- `[P]` tasks touch different files or independent functions — safe to run in parallel
- `[US#]` labels trace each task to a specific user story for independent delivery validation
- Real-time constraint (TC-001/002/003) is a **hard gate** — T042 must pass before merge
- `CC_TRACE` macro is debug-only (`#ifdef _DEBUG`); no overhead in release builds
- All `balloon_.Show()` calls must use `StringLoader::Get(IDS_*)` — never hardcoded strings (Constitution §VIII)
- Timer IDs 10 (`TIMER_RECONNECT_RETRY`) and 11 (`TIMER_AUTOCONNECT`) are reserved for `ConnectionController`; AppController must not use these IDs
- The `WM_SPEAKER_LOST` LPARAM `std::wstring*` is heap-allocated by MdnsDiscovery on Thread 2 and **must be deleted** by `OnSpeakerLost()` on Thread 1 — this is the only safe cross-thread string ownership pattern for posted messages
- `Disconnecting → Connecting` is NOT a direct arrow — always passes through Idle (FR-005); the reconnect timer window provides the observable Idle pause
