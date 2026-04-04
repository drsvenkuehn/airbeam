# Feature Specification: AirPlay 2 Speaker Support

**Feature Branch**: `010-airplay2-support`
**Created**: 2026-04-04
**Status**: Draft
**Input**: User description: "Support AirPlay 2.0 speakers"

## Overview

AirPlay 2 is Apple's current wireless audio protocol. Most speakers sold since 2018 — including every HomePod, HomePod mini, and many third-party AirPlay-certified speakers — support **only** AirPlay 2 and cannot be used with AirBeam v1.0. This feature removes that limitation by implementing the AirPlay 2 sender protocol, allowing AirBeam to stream to the modern speaker market.

AirPlay 2 differs from AirPlay 1 (RAOP) in three key ways:
1. **Device pairing** — a one-time cryptographic trust ceremony is required before the first stream. On Apple devices this happens via the Home app; AirBeam must provide its own pairing flow.
2. **Session protocol** — the control and audio transport use an updated protocol with different framing, timing (PTP-based), and encryption.
3. **Multi-room** — AirPlay 2 natively coordinates synchronized playback across multiple receivers simultaneously.

---

## Clarifications

### Session 2026-04-04

- Q: What identifier ties a stored pairing credential to a specific physical speaker across network changes? → A: HAP Device ID — the stable UUID assigned at manufacture and exchanged during the HAP pairing ceremony. Survives IP reassignment, mDNS name changes, and device renames.
- Q: When stored credentials are rejected by a factory-reset device, what should happen? → A: Automatically detect the rejection, delete the stale credential, and re-initiate the pairing flow — surfacing a tray notification explaining that the device was reset and re-pairing is in progress.
- Q: How should multi-speaker selection work in the tray menu? → A: AirPlay 2 speakers use independent checkboxes (multiple can be active simultaneously). AirPlay 1 speakers retain single-select radio behaviour. Selecting an AirPlay 1 speaker deselects all active AirPlay 2 speakers, and selecting any AirPlay 2 speaker deselects any active AirPlay 1 speaker.
- Q: Is there a maximum number of AirPlay 2 speakers active simultaneously in a multi-room group? → A: 6 speakers maximum (matches Apple's own AirPlay 2 group limit; keeps synchronisation complexity bounded).
- Q: What happens if two AirBeam instances simultaneously attempt to pair or stream to the same AirPlay 2 device? → A: Not applicable — AirBeam already enforces single-instance via a named OS mutex (`Global\AirBeam_SingleInstance`) inherited from v1.0. Two instances cannot run concurrently.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Stream to AirPlay 2-Only Speaker (Priority: P1)

A user has a HomePod, HomePod mini, or AirPlay 2-certified third-party speaker. Currently AirBeam lists these devices with an "(AirPlay 2)" label and they are greyed-out and unselectable. The user wants to click one and stream audio to it exactly as they would with an AirPlay 1 receiver.

**Why this priority**: This is the primary blocker for the majority of the modern speaker market. Without this, AirBeam is incompatible with Apple's entire current hardware lineup. Delivering P1 alone represents a significant expansion of AirBeam's usable device base.

**Independent Test**: Can be tested with a single HomePod or AirPlay 2-only speaker. Pair the device once, then select it from the tray menu and verify audio plays.

**Acceptance Scenarios**:

1. **Given** an AirPlay 2 speaker on the local network that has never been paired with AirBeam, **When** the user selects it from the tray menu, **Then** AirBeam initiates a pairing flow and displays clear guidance to complete it.
2. **Given** an AirPlay 2 speaker that has been previously paired, **When** the user selects it from the tray menu, **Then** audio begins streaming within 3 seconds with no pairing prompt.
3. **Given** streaming is active to an AirPlay 2 speaker, **When** the user adjusts volume in the AirBeam tray menu, **Then** the speaker volume changes within 500 ms.
4. **Given** streaming is active, **When** the user clicks Disconnect, **Then** the stream stops cleanly and the speaker is idle.
5. **Given** an AirPlay 2 speaker that was paired but is powered off, **When** it comes back online, **Then** AirBeam automatically reconnects (matching v1.0 AirPlay 1 reconnect behaviour).

---

### User Story 2 — One-Time Pairing Flow (Priority: P1)

Before an AirPlay 2 speaker can receive audio it must establish a cryptographic trust relationship with the sender. This is a one-time step per device. The user needs a clear, guided flow inside AirBeam to complete this without needing a separate Apple device.

**Why this priority**: Pairing is a prerequisite for P1. Without it no streaming is possible. It is listed separately because its UI and error paths are distinct and independently testable.

**Independent Test**: Reset an AirPlay 2 device to factory state, launch AirBeam, select the device, and verify the pairing flow completes successfully without any external tool.

**Acceptance Scenarios**:

1. **Given** an unpaired AirPlay 2 device is selected, **When** the pairing flow begins, **Then** the user sees a notification or dialog explaining what is happening and what action (if any) is required on their part.
2. **Given** the pairing flow is in progress, **When** the device requires a PIN (e.g., Apple TV), **Then** AirBeam prompts the user to enter the PIN displayed on the device.
3. **Given** pairing completes successfully, **When** the user opens the tray menu in a future session, **Then** the device appears as a normal selectable speaker with no "(AirPlay 2)" warning label.
4. **Given** pairing fails (device rejected, network error, timeout), **When** the error occurs, **Then** a tray notification explains the failure and suggests a resolution (retry, check network, factory-reset device).
5. **Given** a device was previously paired, **When** the user selects "Forget device" from the tray menu, **Then** the pairing credentials are removed and the device reverts to unpaired state.

---

### User Story 3 — Multi-Room Streaming (Priority: P2)

A user has two or more AirPlay 2 speakers (e.g., a HomePod in the living room and a HomePod mini in the kitchen). They want to stream the same audio to all of them simultaneously with synchronized playback — no audible echo between rooms.

**Why this priority**: Multi-room is a defining capability of AirPlay 2 that users frequently expect. However it builds on P1 (single-speaker streaming) and represents significant additional complexity. It is lower priority than getting single-speaker streaming right.

**Independent Test**: Two paired AirPlay 2 speakers on the same network. Enable both in the tray menu and verify audio plays in sync (< 10 ms audible difference) on both simultaneously.

**Acceptance Scenarios**:

1. **Given** two or more paired AirPlay 2 speakers, **When** the user enables multiple speakers in the tray menu, **Then** audio streams to all selected speakers simultaneously.
2. **Given** multi-room streaming is active, **When** the user adjusts volume in the tray menu, **Then** a global volume slider controls all active speakers together, and individual per-speaker sliders are also available.
3. **Given** multi-room streaming is active to speakers A and B, **When** speaker B becomes unavailable (power loss, network drop), **Then** streaming to speaker A continues uninterrupted and a tray notification reports the dropped speaker.
4. **Given** multi-room streaming is active, **When** audio is played at the same time on all speakers, **Then** the playback is synchronized within a perceptibly echo-free threshold (< 10 ms offset between speakers).

---

### User Story 4 — Backward Compatibility with AirPlay 1 Devices (Priority: P3)

Existing v1.0 users who stream to AirPlay 1 (RAOP) receivers must not be affected by the addition of AirPlay 2 support. Devices that support both protocols should continue to work via AirPlay 1.

**Why this priority**: Non-regression is mandatory but lower priority since AirPlay 1 behaviour is already validated and the risk is in new code paths only.

**Independent Test**: Run the full v1.0 integration test suite against an AirPlay 1 receiver (e.g., shairport-sync) after AirPlay 2 is implemented. All existing tests must continue to pass.

**Acceptance Scenarios**:

1. **Given** a receiver that supports only AirPlay 1, **When** AirBeam discovers it, **Then** it streams via AirPlay 1 exactly as in v1.0 with no pairing required.
2. **Given** a receiver that supports both AirPlay 1 and AirPlay 2, **When** AirBeam connects, **Then** it uses AirPlay 2 (preferred) and falls back to AirPlay 1 if AirPlay 2 pairing has not been completed.
3. **Given** the existing v1.0 unit and integration test suite, **When** run against the updated codebase, **Then** all tests pass without modification.

---

### Edge Cases

- What happens when the user's local network blocks the additional ports required by AirPlay 2 (mDNS, PTP, control/audio TCP/UDP ports)? → AirBeam MUST detect the unreachable port and surface a tray notification with firewall remediation guidance (FR-021). No silent failure.
- **Stale credentials (factory-reset device)**: If the connection attempt is rejected by the device because stored credentials are invalid, AirBeam MUST automatically delete the stale credential and re-initiate the pairing flow, surfacing a tray notification: "Device was reset — re-pairing required."
- **Concurrent AirBeam instances**: Not applicable — single-instance is enforced by a named OS mutex (`Global\AirBeam_SingleInstance`) from v1.0. Only one AirBeam process can run at a time.
- **HomePod switches Apple ID while streaming**: Treat as a stale-credential event — post `WM_AP2_PAIRING_STALE`, auto-delete the credential, surface the "Device was reset — re-pairing required" notification, and halt the stream. No automatic mid-session reconnect is attempted.
- **Multi-room group member disconnects and reconnects mid-stream**: Treat as a speaker drop (`WM_AP2_SPEAKER_DROPPED`) — remove the dropped session from the group, continue streaming to remaining speakers uninterrupted (FR-018). The reconnected speaker does NOT automatically re-join the active group; the user must re-select it.
- **Mixed-protocol selection**: Selecting an AirPlay 1 speaker while AirPlay 2 speakers are active (or vice versa) MUST automatically deselect the other protocol's speakers. The tray menu enforces protocol-exclusive selection; no mixed-protocol multi-room group can exist.

---

## Requirements *(mandatory)*

### Functional Requirements

**Discovery & Detection**

- **FR-001**: The system MUST discover AirPlay 2 speakers on the local network automatically and display them in the tray menu without requiring manual configuration.
- **FR-002**: The system MUST distinguish between AirPlay 1-only, AirPlay 2-only, and dual-protocol receivers and apply the correct streaming protocol for each.
- **FR-003**: Paired AirPlay 2 speakers MUST appear identically to AirPlay 1 speakers in the tray menu (no warning label, fully selectable).
- **FR-004**: Unpaired AirPlay 2 speakers MUST be visually identifiable in the tray menu, and selecting one MUST initiate the pairing flow rather than failing silently.

**Pairing**

- **FR-005**: The system MUST support a one-time cryptographic pairing ceremony with any AirPlay 2 device without requiring any Apple device or the Apple Home app.
- **FR-006**: Pairing credentials MUST be stored securely and persistently so that pairing does not need to be repeated across AirBeam sessions or reboots.
- **FR-007**: The system MUST support PIN-based pairing for devices that display a PIN on screen (e.g., Apple TV).
- **FR-008**: The system MUST provide a "Forget device" action per speaker that removes stored pairing credentials.
- **FR-009**: If pairing fails, the system MUST surface a descriptive notification explaining the failure and a suggested remediation.

**Streaming**

- **FR-010**: The system MUST stream ALAC-encoded audio to paired AirPlay 2 receivers with latency no greater than 2 seconds from capture to playback.
- **FR-011**: The system MUST support volume control for AirPlay 2 receivers via the existing tray menu volume slider.
- **FR-012**: The system MUST implement the AirPlay 2 timing protocol (Apple PTP clock synchronisation) to maintain synchronisation between audio sender and receiver.
- **FR-013**: The system MUST handle AirPlay 2 encryption requirements for all audio packets (AES-128-GCM per-packet encryption; 12-byte nonce constructed as `session_salt[4] ‖ rtp_seq[4] ‖ 0x00000000[4]` — zero-padded to the required 96-bit width per research.md §2).
- **FR-014**: Streaming MUST auto-reconnect to a previously paired AirPlay 2 speaker on startup if discovered within 5 seconds (matching v1.0 behaviour).

**Multi-Room (P2 scope)**

- **FR-015**: The system MUST allow the user to select two or more paired AirPlay 2 speakers simultaneously from the tray menu using independent checkboxes (multi-check toggle).
- **FR-022**: AirPlay 1 speakers MUST retain single-select (radio) behaviour. Selecting an AirPlay 1 speaker MUST automatically deselect all active AirPlay 2 speakers, and selecting any AirPlay 2 speaker MUST automatically deselect any active AirPlay 1 speaker.
- **FR-016**: When multiple speakers are selected, the system MUST deliver audio to all of them with synchronised timing (< 10 ms offset).
- **FR-017**: Volume control in multi-room mode MUST include both a global slider affecting all speakers and per-speaker controls.
- **FR-018**: If one speaker in a multi-room group drops, streaming to remaining speakers MUST continue uninterrupted.

**Backward Compatibility**

- **FR-019**: All AirPlay 1 (RAOP) functionality from v1.0 MUST remain fully operational and untouched for AirPlay 1-only devices.
- **FR-020**: For devices supporting both protocols, the system MUST prefer AirPlay 2 and fall back to AirPlay 1 only when AirPlay 2 pairing is not available.
- **FR-021**: If AirPlay 2 control port (default 7000), RTP audio port, or PTP timing port are unreachable after a connection attempt, the system MUST surface a tray notification: *"Cannot reach {DeviceName} — check firewall or router settings"* (red, §III-A) and abort the connection attempt without retry.

### Key Entities

- **AirPlay 2 Receiver**: A network-discoverable speaker or audio device that implements the AirPlay 2 protocol. Key attributes: name, network address, pairing state, protocol capabilities, volume level. **Identity**: uniquely identified by its HAP Device ID (stable UUID exchanged during pairing; survives IP changes and device renames).
- **Pairing Credential**: The cryptographic material established during the one-time pairing ceremony. Keyed by the device's HAP Device ID. Persisted per device. Required for every subsequent connection.
- **Multi-Room Group**: A user-defined set of two or more simultaneously active AirPlay 2 receivers. Attributes: member list (max 6 speakers), active state, group volume.
- **Stream Session (AirPlay 2)**: An active audio stream to one AirPlay 2 receiver. Distinct from the v1.0 RAOP session; manages timing, encryption, and control via the AirPlay 2 protocol.

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users with AirPlay 2-only speakers (HomePod, HomePod mini, or certified third-party) can stream audio from AirBeam without needing any Apple device or the Apple Home app.
- **SC-002**: First-time pairing with an AirPlay 2 speaker completes within 30 seconds under normal network conditions.
- **SC-003**: Audio begins playing on a previously paired AirPlay 2 speaker within 3 seconds of selecting it in the tray menu.
- **SC-004**: End-to-end audio latency (capture to playback) does not exceed 2 seconds for a single AirPlay 2 target.
- **SC-005**: In multi-room mode (up to 6 simultaneous AirPlay 2 speakers), audio across all active speakers is synchronised within a perceptibly echo-free threshold (< 10 ms measured offset).
- **SC-006**: Pairing credentials survive AirBeam restarts and Windows reboots — no re-pairing required after initial setup.
- **SC-007**: All existing v1.0 AirPlay 1 tests pass without modification after AirPlay 2 support is added.
- **SC-008**: AirBeam can stream continuously to an AirPlay 2 speaker for 24 hours without audio dropout, crash, or memory growth exceeding 10 MB above baseline.

---

## Assumptions

- AirPlay 2 pairing uses the HomeKit Accessory Protocol (HAP) specification; the cryptographic primitives (SRP-6a, Ed25519, Curve25519, ChaCha20-Poly1305) are standard and available via well-audited open-source libraries under permissive licences.
- Apple has not published an official AirPlay 2 specification; implementation will be based on publicly available reverse-engineering documentation (e.g., openairplay2 / shairport-sync research). This introduces a risk that undocumented protocol details may differ between device firmware versions.
- HomePod and HomePod mini require HAP pairing; Apple TV does NOT require HAP pairing for AirPlay 2 (uses PIN only). Both variants will be supported.
- Mixed-protocol multi-room (AirPlay 1 + AirPlay 2 speakers simultaneously) is **out of scope** for this feature due to incompatible timing protocols. Multi-room is limited to AirPlay 2 speakers only.
- The existing mDNS discovery infrastructure (Feature 008) will be extended, not replaced. AirPlay 2 receivers advertise on `_airplay._tcp` in addition to `_raop._tcp`.
- Pairing credentials will be stored in the Windows Credential Manager (DPAPI-protected) rather than plain JSON config, due to their sensitive cryptographic nature.
- AirPlay 2 support targets the same OS baseline as v1.0: Windows 10 (1903+) and Windows 11, x86-64.
- Multi-room streaming (FR-015–FR-018) is a P2 deliverable and may ship as a follow-on update after single-speaker AirPlay 2 support (P1) is validated.

