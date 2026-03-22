# Feature Specification: AirBeam — Windows AirPlay Audio Sender

**Feature Branch**: `001-airplay-audio-sender`
**Created**: 2026-03-21
**Status**: Draft
**Input**: Windows system tray application that streams system audio to AirPlay (RAOP) receivers on the local network

## Clarifications

### Session 2026-03-21

- Q: When the user selects a second speaker while already streaming to one, what should happen? → A: Automatically disconnect from the current speaker and connect to the newly selected one (seamless swap).
- Q: How should AirPlay 2-only receivers (those that don't support AirPlay 1) be handled in the tray speaker list? → A: Show them grayed out with a label (e.g., "AirPlay 2 — not supported in v1.0").
- Q: Should AirBeam write a persistent log file the user can access for troubleshooting? → A: Yes — rolling log file in `%APPDATA%\AirBeam\logs\`, capped at 7 days, accessible via "Open log folder" in the tray menu.
- Q: When audio dropouts occur in low-latency mode, how should AirBeam respond? → A: Log the dropout silently; no notification unless streaming disconnects entirely.
- Q: What tray icon state should be shown during the startup auto-reconnect window? → A: Show a distinct "connecting" state (e.g., pulsing/animated icon) during the 5-second attempt, then switch to streaming or idle based on outcome.
- **Direct input**: Localization for major languages is required (not deferred).
- Q: How does AirBeam determine which language to display? → A: Follow the Windows system locale automatically; fall back to English if the detected locale is not among the 7 shipped languages. No user-selectable locale preference; no additional `config.json` key required.
- Q: What should happen if `config.json` is malformed or corrupted on startup? → A: Show a tray notification ("Preferences could not be loaded — reset to defaults"), overwrite the corrupt file with defaults, and continue normally.
- Q: What should happen if a second instance of AirBeam is launched while it is already running? → A: Enforce single-instance: the second launch silently exits and brings the existing tray icon to focus (pops up the context menu).
- Q: What should happen when the user selects "Quit" from the tray menu while actively streaming? → A: Tear down the RTSP session cleanly (send TEARDOWN), stop audio capture, then exit — no confirmation prompt required.
- Q: What should happen when Windows shuts down or the user logs off while AirBeam is streaming? → A: Handle WM_ENDSESSION: attempt RTSP TEARDOWN with a 1-second time budget, then exit — the OS may kill the process if the budget is exceeded.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Stream System Audio to an AirPlay Speaker (Priority: P1)

A user has an AirPlay-compatible speaker (HomePod, AirPort Express, Apple TV, or similar) on their
local Wi-Fi network. They want all audio playing on their Windows PC — music, video, notifications —
to come out of that speaker instead of, or in addition to, their PC speakers.

They install AirBeam, which appears as a tray icon. The application automatically discovers
available AirPlay speakers. The user right-clicks the tray icon, sees their speaker listed, clicks
it, and audio immediately begins playing from that speaker. The tray icon changes to indicate an
active stream.

**Why this priority**: This is the entire purpose of the application. Without this story there is
no product. All other stories are enhancements to this core capability.

**Independent Test**: Can be fully tested by installing the application, having at least one AirPlay
receiver on the same Wi-Fi network, clicking the receiver in the tray menu, and confirming audio
plays from the receiver. Delivers the core value independently.

**Acceptance Scenarios**:

1. **Given** AirBeam is installed and running with at least one AirPlay receiver on the local
   network, **When** the user right-clicks the tray icon, **Then** the receiver appears by name
   in the speaker list within 10 seconds of application start.
2. **Given** the user clicks a discovered speaker in the menu, **When** audio is playing on the
   PC, **Then** that audio plays from the selected AirPlay receiver within 3 seconds.
3. **Given** audio is streaming successfully, **When** the user checks the tray icon,
   **Then** the icon indicates an active streaming state.
4. **Given** the user clicks the active speaker again to disconnect, **When** the menu closes,
   **Then** audio stops at the receiver and the tray icon returns to its idle state.
5. **Given** audio is streaming to Speaker A, **When** the user clicks a different Speaker B in
   the tray menu, **Then** AirBeam disconnects from Speaker A and begins streaming to Speaker B
   automatically, with no additional confirmation required (a brief audio gap is acceptable).

---

### User Story 2 - Automatic Reconnection to Last Speaker (Priority: P2)

A user who uses AirBeam daily does not want to manually re-select their speaker every time they
start their PC. On application start, AirBeam should silently reconnect to the last used speaker
if it is available, without any user action.

**Why this priority**: Reconnection is essential for daily-driver usability. Without it, the
user must repeat the speaker-selection step every session, which undermines the "zero-config"
value proposition.

**Independent Test**: Can be tested by connecting to a speaker, quitting and restarting AirBeam,
and confirming audio resumes without user interaction if the speaker is reachable.

**Acceptance Scenarios**:

1. **Given** the user connected to a speaker in a previous session, **When** AirBeam starts and
   the speaker is discovered within 5 seconds, **Then** the connection is established
   automatically with no user action required. During the 5-second attempt window the tray icon
   MUST show the "connecting" (pulsing) state; it transitions to "streaming" on success.
2. **Given** AirBeam is set to launch at Windows startup, **When** the user logs in with their
   speaker powered on, **Then** audio streams to the speaker without the user opening the tray
   menu.
3. **Given** AirBeam starts and the last-used speaker is not found within 5 seconds,
   **When** the discovery period expires, **Then** AirBeam remains idle and does not show an
   error — the user can manually connect later.

---

### User Story 3 - Control Volume and Application Settings (Priority: P3)

A user wants to control the volume level sent to the AirPlay speaker independently of their PC
volume. They also want options for low-latency mode (for watching video) and automatic launch at
Windows startup, all accessible from the tray context menu, along with a shortcut to open
the diagnostic log folder for troubleshooting and a "Check for Updates" item to manually
trigger an update check at any time.

**Why this priority**: Volume control and settings are expected quality-of-life features for any
audio application. They do not block the core use case but are required for a complete v1.0.

**Independent Test**: Can be tested independently by verifying the volume slider changes audio
level at the receiver, the low-latency toggle changes buffering behavior, the startup toggle
persists the launch preference across reboots, and the "Check for Updates" item triggers
WinSparkle's update dialog.

**Acceptance Scenarios**:

1. **Given** audio is streaming, **When** the user moves the volume slider in the tray menu,
   **Then** the volume at the AirPlay receiver changes accordingly within 1 second.
2. **Given** the user enables "Low-latency mode", **When** a new streaming session starts,
   **Then** the end-to-end audio latency is measurably reduced (target: under 500 ms
   sender-to-receiver).
3. **Given** the user enables "Launch at startup", **When** Windows restarts,
   **Then** AirBeam starts automatically with the Windows user session.
4. **Given** the user disables "Launch at startup", **When** Windows restarts,
   **Then** AirBeam does not start automatically.
5. **Given** the user sets a volume and closes AirBeam, **When** AirBeam is restarted,
   **Then** the previous volume setting is restored.
6. **Given** the user clicks "Check for Updates" in the tray menu, **When** the menu item is
   invoked in any icon state (idle, connecting, streaming, or error), **Then** an update check
   is triggered and feedback is presented to the user (update available, already up-to-date,
   or network unavailable).

---

### User Story 4 - Resilient Streaming Under Real-World Conditions (Priority: P4)

A user who streams audio for long periods expects AirBeam to handle real-world disruptions
gracefully: changing the Windows audio output device, brief Wi-Fi drops, or the AirPlay receiver
becoming temporarily unavailable. The application should recover silently where possible and notify
the user clearly when it cannot.

**Why this priority**: Resilience is essential for a background audio application. A single
unhandled disruption that kills the stream silently would undermine user trust.

**Independent Test**: Can be tested by streaming audio, switching the Windows default audio device,
and confirming the stream resumes on the new device within 1 second; and by simulating a brief
network interruption and confirming reconnect is attempted automatically.

**Acceptance Scenarios**:

1. **Given** audio is streaming, **When** the user changes the Windows default audio output device,
   **Then** AirBeam reattaches to the new device and streaming resumes within 1 second (a brief
   audio gap is acceptable).
2. **Given** the AirPlay receiver becomes unreachable, **When** the connection is lost,
   **Then** AirBeam attempts to reconnect automatically (at least 3 attempts) before notifying
   the user of the failure via a tray notification.
3. **Given** a connection failure notification is shown, **When** the user reads it,
   **Then** the notification clearly states which device was lost and that reconnection failed.
4. **Given** Bonjour is not installed on the system, **When** AirBeam starts,
   **Then** the user sees a tray notification explaining the missing dependency and providing
   guidance on how to install it.
5. **Given** any error condition occurs, **When** the error cannot be automatically recovered,
   **Then** a tray notification is shown — silent failures are never permitted.

---

### User Story 5 - Receive and Install Software Updates Automatically (Priority: P3)

A user wants to be notified when a new version of AirBeam is available and install it with one
click, without manually visiting a download page or checking for releases. AirBeam silently
checks for updates in the background once per day. When an update is found, the user sees a
native update dialog with release notes, a download progress indicator, and a one-click install
prompt. Users who prefer to manage updates manually can disable the background check in settings
while retaining the ability to trigger a manual check at any time.

**Why this priority**: Automatic updates keep users on secure, up-to-date builds and reduce
support burden. This story does not block core streaming functionality but is required for a
production-quality v1.0.

**Independent Test**: Can be tested by pointing the application at a test appcast URL containing
a newer version entry and confirming the update dialog appears. Background check suppression can
be tested by setting `autoUpdate: false` and verifying no automatic dialog appears after 24 hours.

**Acceptance Scenarios**:

1. **Given** AirBeam is running and connected to the internet, **When** 24 hours elapse since
   the last update check, **Then** a background check is performed automatically and the user
   sees a dialog if a newer version is available.
2. **Given** the user clicks "Check for Updates" in the tray menu, **When** a newer version is
   available on the server, **Then** an update dialog appears immediately showing version
   information and offering a one-click install.
3. **Given** the user clicks "Check for Updates", **When** the installed version is already the
   latest, **Then** the user sees feedback indicating the application is up-to-date.
4. **Given** `autoUpdate` is set to `false` in config, **When** 24 hours elapse,
   **Then** no automatic background check is performed and no dialog appears unprompted; the
   "Check for Updates" tray item still triggers an immediate check when clicked.
5. **Given** the system is offline when an update check is triggered (either automatic or manual),
   **Then** the update mechanism shows an appropriate network-error feedback without crashing or
   showing a silent failure.

---

- What happens when a discovered receiver supports only AirPlay 2 (no AirPlay 1 fallback)?
  → It appears in the speaker list grayed out and labeled "AirPlay 2 — not supported in v1.0";
  clicking it has no effect.
- What happens when the user clicks a different speaker while already streaming?
  → AirBeam disconnects from the current speaker and connects to the new one automatically
  (seamless swap); a brief audio gap is acceptable; no confirmation is required.
- What does the user see during the 5-second startup auto-reconnect attempt?
  → The tray icon shows the "connecting" (pulsing) state throughout the window. On success it
  transitions to "streaming"; on expiry with no result it returns to "idle" silently.
- What happens when no AirPlay receivers are discoverable on the network?
  → Tray menu shows an empty speaker list; no error is shown unless a reconnect was attempted.
- What happens when the previously saved speaker's MAC address is no longer on the network?
  → AirBeam waits 5 seconds on start, then enters idle state with no notification.
- What happens when two speakers advertise the same display name?
  → Both appear in the list; distinguishable by their underlying network address.
- What happens when Bonjour/DNS-SD is unavailable at runtime?
  → Discovery is disabled; a tray notification is shown with installation instructions.
- What happens when the network drops entirely mid-stream?
  → RTSP connection error is detected; reconnect is attempted; failure notification is shown after
  3 attempts.
- What happens when the system has no audio playing (silence)?
  → The stream continues with silence frames to maintain AirPlay receiver sync; no dropout occurs.
- What happens when audio dropouts occur in low-latency mode (momentary glitches, not a disconnect)?
  → Dropouts are logged silently to the diagnostic log; no tray notification is shown. Streaming
  continues uninterrupted. A notification is only raised if the connection drops entirely.
- What happens on a 24-hour continuous stream?
  → No memory growth, no audio drift, and no manual intervention required.
- What happens when an automatic background update check runs and a newer version is available?
  → The update mechanism presents its native update dialog to the user.
- What happens when "Check for Updates" is clicked while the system is offline?
  → The update mechanism shows a standard network-error UI; no crash or silent failure occurs.
- What happens when an update package has an invalid or missing cryptographic signature?
  → The update is silently rejected and the installation does not proceed; a tray notification
  informs the user that the update could not be verified.
- What happens when `autoUpdate` is `false` and 24 hours elapse?
  → No automatic check is performed; the "Check for Updates" tray item still triggers an
  on-demand check when clicked.
- What happens if `config.json` is malformed or corrupted on startup?
  → A tray notification is shown ("Preferences could not be loaded — reset to defaults"); the
  file is overwritten with defaults and the application continues normally.
- What happens if the user launches AirBeam while it is already running?
  → The second instance exits immediately; the existing tray icon is brought to focus by
  opening the context menu. No duplicate stream or error dialog appears.
- What happens when the user selects "Quit" from the tray menu while actively streaming?
  → AirBeam sends RTSP TEARDOWN to the receiver, stops audio capture, and exits cleanly
  within 2 seconds — no confirmation prompt.
- What happens when Windows shuts down or the user logs off while AirBeam is streaming?
  → AirBeam handles WM_ENDSESSION: attempts RTSP TEARDOWN within a 1-second budget, then
  exits. The OS may kill the process if the budget is exceeded.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST continuously discover AirPlay receivers on the local network
  automatically, requiring no manual IP entry or configuration from the user. Discovered receivers
  that are AirPlay 1-compatible MUST appear as selectable entries. Receivers that are AirPlay
  2-only (no AirPlay 1 fallback) MUST appear as grayed-out, non-selectable entries labeled
  "AirPlay 2 — not supported in v1.0"; clicking them MUST have no effect.
- **FR-002**: System MUST capture all audio currently playing on the Windows default audio output
  device, regardless of which application produced it.
- **FR-003**: System MUST stream captured audio to a user-selected AirPlay receiver in real time,
  with audio quality preserved losslessly.
- **FR-004**: Users MUST be able to select, connect to, and disconnect from a discovered AirPlay
  receiver via the system tray context menu. Selecting a different speaker while one is already
  active MUST automatically disconnect the current speaker and connect to the new one (seamless
  swap); no confirmation prompt is shown.
- **FR-005**: System MUST display its current state via a distinguishable system tray icon using
  four distinct visual states: **idle** (not connected), **connecting** (attempting to establish
  or re-establish a connection, shown as a pulsing/animated icon), **streaming** (active audio
  stream), and **error** (unrecoverable failure).
- **FR-006**: Users MUST be able to adjust the volume level at the AirPlay receiver independently
  of the Windows system volume, via a slider in the tray context menu.
- **FR-007**: System MUST persist user preferences (last device, volume level, low-latency mode,
  startup preference, automatic update check enabled/disabled) and restore them on next launch.
  The automatic update check preference defaults to enabled (`true`) if not explicitly set.
  If `config.json` is missing, the system MUST silently create it with all defaults. If
  `config.json` is present but malformed or unreadable, the system MUST show a tray notification
  ("Preferences could not be loaded — reset to defaults"), overwrite the file with defaults, and
  continue normally.
- **FR-008**: System MUST automatically reconnect to the last-used speaker on startup if it is
  discoverable within 5 seconds, without requiring user interaction.
- **FR-009**: System MUST notify the user via a tray balloon or toast notification when a
  connection is established, when a connection is unexpectedly lost, or when an error cannot be
  automatically resolved.
- **FR-010**: System MUST silently re-attach audio capture when the Windows default audio output
  device changes, with no user action required.
- **FR-011**: System MUST retry a lost AirPlay connection at least 3 times before surfacing a
  failure notification to the user.
- **FR-012**: Users MUST be able to toggle a low-latency mode that reduces sender-side audio
  buffering for improved responsiveness at the cost of increased dropout risk. When audio
  dropouts occur in this mode, the application MUST log them silently and continue streaming;
  a tray notification MUST NOT be shown unless the stream disconnects entirely.
- **FR-013**: Users MUST be able to configure AirBeam to launch automatically when Windows starts,
  via a toggle in the tray context menu.
- **FR-014**: System MUST provide an installer that detects and optionally installs any missing
  system dependencies (specifically the Bonjour/DNS-SD runtime) without requiring manual steps
  from the user. The installer MUST bundle `WinSparkle.dll` alongside `AirBeam.exe`.
- **FR-015**: System MUST support a portable mode where configuration is stored next to the
  executable rather than in the user profile, enabling use from a USB drive. The portable
  distribution MUST include `dnssd.dll`, `WinSparkle.dll`, and all required resource files
  alongside `AirBeam.exe`.
- **FR-016**: System MUST write timestamped diagnostic log entries to a rolling log file at
  `%APPDATA%\AirBeam\logs\`. Log files MUST be automatically deleted after 7 days. Users MUST
  be able to open the log folder directly via an "Open log folder" entry in the tray context menu.
- **FR-017**: All user-visible text (tray menu labels, tray icon tooltips, balloon/toast
  notification messages, error descriptions, and installer UI) MUST be localizable. The
  application MUST ship with localized strings for the following major languages: English,
  German, French, Spanish, Japanese, Simplified Chinese, and Korean. Additional languages may
  be added without code changes by supplying a new resource file.
- **FR-018**: System MUST provide automatic over-the-air software updates via a native update
  mechanism. Specifically:
  - Background update checks MUST run automatically at a 24-hour interval; this MUST be enabled
    by default.
  - Users MUST be able to suppress automatic background checks via the `autoUpdate` preference
    in `config.json` (`false` disables background checks); the manual "Check for Updates" tray
    menu item MUST remain fully functional regardless of this setting.
  - The tray context menu MUST include a **"Check for Updates"** item that triggers an
    immediate on-demand check with user-visible feedback at all times.
  - The update mechanism owns the complete update UI (download progress, install prompt,
    restart); AirBeam MUST NOT implement any supplemental update overlay.
  - All distributed update packages MUST be cryptographically signed; the application MUST
    verify signatures before allowing an update to proceed.
- **FR-019**: System MUST enforce single-instance execution. If AirBeam is already running and
  a second instance is launched, the second instance MUST exit immediately and bring the
  existing tray icon to focus by opening its context menu. No second streaming session,
  configuration window, or error dialog MUST appear from the duplicate launch.
- **FR-020**: When the user selects "Quit" from the tray context menu, the system MUST tear
  down any active AirPlay session cleanly (send RTSP TEARDOWN to the receiver), stop audio
  capture, and exit the process. No confirmation prompt is shown. The quit action MUST complete
  within 2 seconds.
- **FR-021**: The system MUST handle Windows shutdown and user logoff events (`WM_ENDSESSION`).
  On receiving this event, AirBeam MUST attempt RTSP TEARDOWN within a 1-second time budget,
  then exit. If the time budget is exceeded, the OS may terminate the process; this is
  acceptable.

### Key Entities

- **AirPlay Receiver**: A discovered speaker or AirPlay-capable device on the local network.
  Identified by a human-readable name and a unique hardware address. Carries capability metadata
  (supported audio formats, encryption requirements, AirPlay protocol version). AirPlay 1-compatible
  receivers are selectable; AirPlay 2-only receivers are displayed but non-selectable in v1.0.
- **Audio Stream**: An active real-time session delivering system audio to a selected AirPlay
  receiver. Transitions through four states: **idle** → **connecting** → **streaming** → **error**
  (or back to idle on disconnect). Carries current volume level.
- **Configuration**: Persisted user preferences — last connected receiver, volume, low-latency
  preference, startup behavior, and automatic update check preference (`autoUpdate`, default `true`).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can begin streaming to a discovered speaker within 3 interactions (right-click
  tray → see speaker → click speaker) from a cold start.
- **SC-002**: Audio begins playing at the AirPlay receiver within 3 seconds of the user selecting
  the speaker.
- **SC-003**: End-to-end latency (audio produced on PC to audio heard at receiver) does not exceed
  3 seconds in standard buffered mode.
- **SC-004**: End-to-end latency does not exceed 500 ms in low-latency mode under normal Wi-Fi
  conditions.
- **SC-005**: Audio delivered to the receiver is losslessly identical to the source material
  (verified by cross-correlation > 0.99 in automated testing).
- **SC-006**: The application streams continuously for 24 hours without user-observable audio
  dropouts caused by the sender, memory growth, or audio drift.
- **SC-007**: The application recovers from a Windows default audio device change within 1 second
  of the change occurring, with no user action required.
- **SC-008**: Zero silent failure states — every unrecoverable error produces a visible tray
  notification describing the problem. *(Cross-reference: measurability criterion for FR-009.)*
- **SC-009**: The application installs and operates correctly on Windows 10 (build 1903+) and
  Windows 11 without requiring the user to install audio drivers or kernel-mode software.
- **SC-010**: Automatic reconnection at startup succeeds for the previously used speaker in at
  least 95% of sessions where the speaker is powered on and reachable.
- **SC-011**: The "Check for Updates" tray menu item is present and triggerable in all four icon
  states (idle, connecting, streaming, error); invoking it always produces user-visible feedback
  within 5 seconds.

## Assumptions

- The target AirPlay receivers are AirPlay 1 (RAOP) compatible. AirPlay 2 / HomeKit pairing is
  out of scope for v1.0.
- Only one AirPlay target is supported simultaneously in v1.0; multi-room is deferred to v2.0.
- The user's local network supports mDNS/multicast (standard home Wi-Fi networks do).
- Per-application audio capture is not in scope; only system-wide loopback capture is provided.
- The Bonjour/DNS-SD runtime may not be present and must be handled gracefully.
- All user-visible strings are externalized and localizable. Shipped languages: English, German,
  French, Spanish, Japanese, Simplified Chinese, Korean.
- Automatic update checking is enabled by default (`autoUpdate: true`); users may opt out via
  config but manual update checks remain available at all times.

## Constitution Check

Validation of compliance with AirBeam Constitution v1.3.1 (all eight core principles + Auto-Update section).

| Principle | Status | Notes |
|-----------|:------:|-------|
| I. Real-Time Audio Thread Safety | ✅ | FR-002, FR-003, FR-010 require WASAPI capture and real-time streaming; implementation plan must enforce zero-allocation / lock-free on capture and encoder threads |
| II. AirPlay 1 / RAOP Protocol Fidelity | ✅ | FR-003 (lossless stream), FR-004 (session control), FR-011 (retransmit / reconnect); Assumptions scope to AirPlay 1 only |
| III. Native Win32, No External UI Frameworks | ✅ | FR-004 (tray menu), FR-005 (tray icon states), FR-006 (volume slider), FR-013 (startup toggle), FR-016 (log folder item), FR-018 (Check for Updates item) — all tray UI, no external framework |
| IV. Test-Verified Correctness | ✅ | SC-005 (cross-correlation > 0.99), SC-006 (24 h stress); plan/tasks must add unit tests for ALAC, RTP, AES, and integration test against shairport-sync |
| V. Observable Failures — Never Silent | ✅ | FR-009 (notifications), FR-011 (retry before notify), FR-007 (corrupt config notify), SC-008 (zero silent failures), edge cases for all error states |
| VI. Strict Scope Discipline (v1.0) | ✅ | Assumptions explicitly defer AirPlay 2 pairing, multi-room, per-app capture, video/screen |
| VII. MIT-Compatible Licensing | ✅ | No GPL dependencies referenced; ALAC (Apache 2.0), Bonjour (redistribution terms), WinSparkle (BSD-2-Clause) all compatible |
| VIII. Localizable User Interface | ✅ | FR-017 mandates 7 languages (EN, DE, FR, ES, JA, ZH-HANS, KO); all user-visible text externalized |
| Auto-Update | ✅ | FR-018 covers all six Auto-Update MUSTs: 24 h background check, opt-out config, "Check for Updates" tray item always present, update mechanism owns UI, signed packages required |

