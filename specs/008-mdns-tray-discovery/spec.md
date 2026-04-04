# Feature Specification: mDNS Discovery and Tray Speaker Menu

**Feature Branch**: `008-mdns-tray-discovery`  
**Created**: 2025-07-17  
**Status**: Ready for Implementation  

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Automatic Speaker Discovery on Startup (Priority: P1)

A user launches AirBeam on a network that has one or more AirPlay 1 receivers (e.g., an Apple TV, a HomePod mini, or an AirPort Express). Without any manual configuration, the tray icon's context menu populates with the names of those speakers within a few seconds. The user immediately knows what is available to stream to.

**Why this priority**: This is the foundational capability of the feature. All other stories depend on discovery working correctly. Without it the menu stays permanently empty and the feature delivers zero value.

**Independent Test**: Launch AirBeam on a network with at least one AirPlay 1 receiver and right-click the tray icon within 10 seconds. The speaker menu must contain at least one device entry. Delivers direct user value without any further interaction.

**Acceptance Scenarios**:

1. **Given** AirBeam has just started and Bonjour is installed, **When** one or more AirPlay 1 receivers are advertising on the local network, **Then** their friendly names appear as selectable items in the tray context menu within 10 seconds of launch.
2. **Given** the tray menu is open and a new receiver comes online mid-session, **When** the device finishes advertising, **Then** its name is added to the menu without requiring an application restart.
3. **Given** AirBeam is running with no receivers present, **When** the user opens the tray menu, **Then** a grayed-out "Searching for speakers…" placeholder is shown instead of an empty submenu.
4. **Given** Bonjour runtime is not installed, **When** the user opens the tray menu, **Then** a grayed-out "Install Bonjour to discover speakers" item is shown (no crash, no empty menu).

---

### User Story 2 — Select a Speaker to Stream To (Priority: P1)

A user has multiple speakers discovered and visible in the tray menu. They click on one speaker name, and AirBeam switches its audio output target to that device. The selected speaker is visually marked with a checkmark so the user always knows which device is active.

**Why this priority**: Discovery without selection is useless; together stories 1 and 2 form the minimum viable end-to-end flow.

**Independent Test**: With two or more AirPlay 1 receivers on the network, click a speaker name in the tray menu. The streaming target changes to that device and the menu shows a checkmark next to it. No restart is needed.

**Acceptance Scenarios**:

1. **Given** multiple speakers are listed in the menu, **When** the user clicks a speaker that is not currently selected, **Then** a "Connecting…" label immediately appears next to the clicked speaker; upon confirmed AirPlay handshake success the stream switches to that speaker, the checkmark moves to it, and the "Connecting…" label is removed. If the handshake does not succeed within 5 seconds the menu silently reverts to its prior state (prior checkmark restored, or no checkmark if none was active) and the active stream is unchanged.
2. **Given** a speaker is already selected and streaming, **When** the user clicks the same speaker again, **Then** nothing changes (idempotent — no reconnect is triggered).
3. **Given** three or fewer discovered speakers, **When** the user opens the tray menu, **Then** speakers appear as inline radio-button items directly in the context menu (no nested submenu).
4. **Given** four or more discovered speakers, **When** the user opens the tray menu, **Then** speakers are grouped inside a "Speakers" submenu.
5. **Given** Speaker A is showing a "Connecting…" label (its AirPlay handshake is in progress), **When** the user clicks Speaker B, **Then** the handshake for Speaker A is immediately cancelled, its "Connecting…" label is removed, a "Connecting…" label appears next to Speaker B, and a new AirPlay handshake begins for Speaker B. At most one "Connecting…" label is visible at any time.

---

### User Story 3 — Resume Last-Used Speaker After Restart (Priority: P2)

A user regularly streams to "Living Room". They close and reopen AirBeam. Without having to pick the speaker again, AirBeam automatically reconnects to "Living Room" as soon as it is rediscovered on the network.

**Why this priority**: Eliminates repetitive manual selection for the common single-speaker household. Depends on stories 1 and 2 being complete.

**Independent Test**: Select a speaker, close AirBeam, restart it while the same speaker is on the network. Within 10 seconds the speaker is auto-selected and the checkmark is shown without any user interaction.

**Acceptance Scenarios**:

1. **Given** the user previously selected "Living Room" and has since restarted AirBeam, **When** "Living Room" is rediscovered during startup, **Then** it is automatically marked as the active speaker and streaming begins immediately.
2. **Given** the previously-used speaker is not on the network at startup, **When** the user opens the menu, **Then** the "Searching for speakers…" placeholder is shown and no stale device name is displayed.
3. **Given** the previously-used speaker is not rediscovered within 30 seconds of AirBeam startup, **When** the 30-second auto-select window expires, **Then** the auto-select intent is abandoned; if the speaker later reappears it is NOT automatically selected, and the user must select it manually.

---

### User Story 4 — Stale Speaker Removal (Priority: P2)

A user powers off a speaker mid-session. After 60 seconds with no re-advertisement from that device, it disappears from the tray menu automatically. The user does not see phantom devices that no longer exist.

**Why this priority**: Prevents confusing UX where users try to stream to a device that is no longer available. Lower priority than the core connect flow.

**Independent Test**: Power off a discovered speaker and wait 60 seconds. Without any user interaction, the device name is removed from the tray menu and the menu accurately reflects only active devices.

**Acceptance Scenarios**:

1. **Given** a speaker is listed in the menu, **When** 60 seconds pass with no re-advertisement from that device, **Then** it is removed from the menu automatically.
2. **Given** the removed speaker was the active (checked) device, **When** it is removed (via Bonjour remove-event or 60-second stale timeout — whichever fires first), **Then** the active stream is immediately stopped and disconnected, the checkmark is cleared, and the menu returns to the "Searching for speakers…" state if no other devices remain. No automatic reconnect is attempted. (per FR-005a)
3. **Given** a speaker briefly drops off the network and re-advertises within 60 seconds, **When** its advertisement is received, **Then** it remains in the menu without being removed.

---

### User Story 5 — AirPlay 2-Only Device Filtering (Priority: P3)

A user's network includes a newer AirPlay 2-only device (e.g., a HomePod with no AirPlay 1 fallback). That device does not appear in the AirBeam speaker menu. The user only sees devices that AirBeam can actually stream to, avoiding confusing connection failures.

**Why this priority**: Correctness and trust. Showing devices that cannot work would cause silent failures and user frustration. Purely a filtering concern — no new UI required.

**Independent Test**: On a network with a known AirPlay 2-only device, confirm it does not appear in the menu. On a network with a known AirPlay 1 device, confirm it does appear.

**Acceptance Scenarios**:

1. **Given** a device advertises with a `pk` field in its TXT record (AirPlay 2 indicator), **When** AirBeam processes the discovery event, **Then** that device is excluded from the speaker menu.
2. **Given** a device advertises without a `pk` field and its `et` value contains "1" (AES encryption — AirPlay 1 indicator), **When** AirBeam processes the discovery event, **Then** the device is included in the speaker menu.
3. **Given** a device's TXT record does not include "1" in its `et` field, **When** AirBeam processes the discovery event, **Then** the device is excluded regardless of other fields.

---

### Edge Cases

- **Involuntary loss of connected speaker**: The active streaming target powers off or leaves the network while a stream is in progress — the Bonjour remove-event (immediate) or 60-second stale timeout (whichever fires first) triggers stream disconnection; the active stream is stopped, the checkmark is cleared, and the menu reverts to "Searching for speakers…" if no other speakers remain. No automatic reconnect is attempted to the lost device. (normative definition: FR-005a)
- **No network interface**: AirBeam starts without an active network adapter — discovery loop starts silently, menu shows "Searching for speakers…", and the loop recovers automatically when the network becomes available.
- **Bonjour service crash**: The Bonjour background service crashes mid-session — the discovery thread detects the failure, stops gracefully, and the menu shows a fallback message rather than crashing the application. (v1.0 scope: the discovery thread stops gracefully on DNS-SD error callback; full mid-session crash recovery with restart is deferred to v2.0)
- **Duplicate device names**: Two devices advertise with the same friendly name (e.g., two "AirPort Express" units) — both are listed and disambiguated by appending the model identifier in parentheses, or by appending a numeric suffix if models are also identical. (v1.0 scope: only append model identifier; numeric-suffix disambiguation when models are also identical is deferred to v2.0)
- **Very long device names**: A device name exceeds practical tray menu display width — the name is truncated with an ellipsis at a reasonable character limit (default: 40 characters).
- **Rapid churn**: Devices appear and disappear faster than the menu can rebuild — each rebuild reflects the current state at rebuild time; no coalescing is required because rebuilds are O(n log n) and idempotent for ≤100 devices.
- **No TXT "an" key**: A compliant but unusual device omits the friendly-name key — the mDNS service instance name (the part before `._raop._tcp`) is used as the display fallback.
- **Config file unreadable**: The saved last-used device identifier cannot be read at startup — discovery proceeds normally with no auto-selection, as if it is a first launch.
- **Connection attempt failure during speaker switch**: The user clicks a new speaker and the AirPlay handshake does not succeed within 5 seconds — the "Connecting…" label is removed from that speaker, the prior checkmark is restored (if one existed), and the active stream is left unchanged. No error message is displayed to the user.
- **Speaker switch while connection attempt is in progress**: The user clicks Speaker B while Speaker A is already showing a "Connecting…" label (handshake in progress) — Speaker A's handshake is immediately cancelled and its "Connecting…" label is removed; a "Connecting…" label appears next to Speaker B and a new AirPlay handshake begins for Speaker B. At most one "Connecting…" label is shown at any time; the prior checkmark (if any) remains until Speaker B's handshake succeeds or times out.

## Requirements *(mandatory)*

### Functional Requirements

#### Discovery

- **FR-001**: The application MUST continuously browse for `_raop._tcp` services on the local network using the installed Bonjour runtime from the moment it starts until it exits.
- **FR-002**: For each discovered service, the application MUST resolve its hostname, port, and TXT record fields before considering it a candidate for the speaker list.
- **FR-003**: The application MUST include a discovered device in the speaker list only when: (a) its TXT record's `et` field contains the value `"1"`, AND (b) its TXT record does NOT contain a `pk` field.
- **FR-004**: The application MUST extract and store the following TXT record fields per device: friendly name (`an`), model identifier (`am`), and encryption types (`et`). (codec support `cn` is retained for future use; not used in v1.0 filtering)
- **FR-005**: The application MUST remove a discovered device from the speaker list when a Bonjour remove-event fires for that device, or when 60 seconds pass without the device re-advertising its service — whichever occurs first.
- **FR-005a**: When the removed device was the active streaming target, the application MUST immediately stop and disconnect the active stream, clear the checkmark from that device's menu entry, and revert the menu to the "Searching for speakers…" state if no other devices are present. No automatic reconnect attempt is made to the lost device.
- **FR-006**: Discovery MUST run on a dedicated background thread that does not block or slow the main UI thread.
- **FR-007**: All changes to the displayed speaker list MUST be delivered to the UI thread via an asynchronous message so that list reads always occur on the UI thread.

#### Tray Menu

- **FR-008**: The tray context menu MUST display discovered AirPlay 1 speakers as individually selectable items.
- **FR-009**: When three or fewer speakers are discovered, they MUST appear as inline items directly in the context menu with checkmark visual style (MF_CHECKED).
- **FR-010**: When four or more speakers are discovered, they MUST be grouped inside a "Speakers" submenu.
- **FR-011**: The currently active (selected) speaker MUST display a checkmark; all other speakers MUST display without a checkmark.
- **FR-012**: When no speakers have been discovered yet, the menu MUST show a single grayed-out, non-clickable item with the text "Searching for speakers…"
- **FR-013**: When the Bonjour runtime is not present, the menu MUST show a single grayed-out, non-clickable item with the text "Install Bonjour to discover speakers".
- **FR-014**: The speaker menu MUST update dynamically as devices are discovered or removed, without requiring an application restart.
- **FR-015**: The display name for each speaker MUST be taken from the TXT `an` field; if a model identifier (`am`) is available, it MUST be appended in parentheses (e.g., "Living Room (AppleTV5,3)").
- **FR-016**: When the user clicks a speaker that is not currently active, the application MUST immediately display a "Connecting…" label next to that speaker in the tray menu and initiate an AirPlay handshake with the target device; the existing stream and checkmark MUST remain unchanged during this attempt.
- **FR-016a**: The checkmark MUST be committed to the newly selected speaker — and removed from the previously active speaker's entry — only after a confirmed successful AirPlay handshake with the target device.
- **FR-016b**: If the AirPlay handshake does not complete successfully within 5 seconds of the click, the application MUST silently revert: remove the "Connecting…" label, restore the prior checkmark (if one existed), and make no change to the active stream.
- **FR-016c**: If the user clicks a different speaker while an AirPlay handshake is already in progress for another speaker, the application MUST immediately cancel the in-progress handshake, remove the "Connecting…" label from the first speaker, display "Connecting…" next to the newly clicked speaker, and initiate a new AirPlay handshake for that speaker. At most one "Connecting…" label MUST be visible at any time.
- **FR-017**: Only one speaker may be active at a time (single-target streaming for v1.0).
- **FR-020**: The speaker list MUST be sorted alphabetically by display name (A→Z) at every menu rebuild. Sort order is stable across application restarts.

#### Persistence

- **FR-018**: When the user selects a speaker, the application MUST persist a stable device identifier (derived from the device's MAC address embedded in the mDNS service instance name) to the application configuration file.
- **FR-019**: At startup, if a persisted device identifier is present, the application MUST automatically select that device as the active speaker — without requiring user interaction — if and only if the matching device is discovered within 30 seconds of startup. If the 30-second auto-select window expires before the device is rediscovered, the auto-select intent is abandoned for that session and the user must select a speaker manually.

### Key Entities

- **Discovered Speaker**: Represents a single AirPlay 1 receiver visible on the network. Attributes: service instance name (unique key), friendly display name, model identifier, resolved hostname, port number, supported encryption types, supported codecs (future), last-seen timestamp.
- **Active Speaker**: The single speaker currently selected for streaming. Zero or one active speaker exists at any time. Selection is persisted across restarts by storing a stable device identifier.
- **Device Identifier**: A stable string derived from the mDNS service instance name that uniquely identifies a physical device across advertisement cycles and network changes (typically the device's MAC address encoded in the instance name).
- **Speaker List**: The ordered, in-memory collection of currently valid Discovered Speakers maintained by the discovery subsystem and read by the tray menu builder. Always accessed from the UI thread. Alphabetically ordered by display name (A→Z); order is re-applied on every rebuild.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Discovered speakers appear in the tray menu within 10 seconds of the device becoming reachable on the local network.
- **SC-002**: Stale devices (powered off or disconnected) are removed from the menu within 90 seconds of their last advertisement (60-second stale timeout plus up to 30 seconds for the next prune cycle).
- **SC-003**: On a successful AirPlay handshake, the "Connecting…" label appears within 200 ms of the user's click and the checkmark is committed within 2 seconds of the click. On handshake failure, the menu silently reverts to its prior state within 5 seconds of the click.
- **SC-004**: The application startup time is not measurably increased by the discovery subsystem; UI is interactive within the same time budget as a build without this feature (≤ 1 second additional overhead).
- **SC-005**: 100% of devices that pass the AirPlay 1 filter criteria (et contains "1", no pk field) appear in the menu; 0% of AirPlay 2-only devices appear.
- **SC-006**: After an application restart, the previously selected speaker is auto-selected within 10 seconds if it is present on the network (and within the 30-second auto-select window), requiring zero user interaction. If the device is not rediscovered within 30 seconds of startup, no auto-selection occurs and the user must select manually.
- **SC-007**: The application does not crash or hang when Bonjour is not installed, when the network is unavailable, or when all discovered devices go offline simultaneously.

## Assumptions

- The Bonjour SDK runtime (`dnssd.dll`) is delivered to the user's machine as part of the existing installation flow established by Features 005/006; this feature does not need to install it.
- The existing `AppConfig` class already handles reading and writing JSON to `%APPDATA%\AirBeam\config.json`; this feature only adds new keys to that file.
- The existing `BonjourBrowser` stub class provides a starting point for the discovery implementation; this feature completes and wires it.
- Multi-room (simultaneous streaming to multiple speakers) is explicitly out of scope for v1.0 and will be addressed in a future feature.
- AirPlay 2 devices that also expose an AirPlay 1 interface (i.e., they advertise `_raop._tcp` without a `pk` field) are treated as AirPlay 1 devices and are included — this is consistent with standard RAOP client behavior.
- The application runs on Windows 10 or later; no compatibility workarounds for older Windows versions are required.
- Network topology is assumed to be a single flat local subnet where mDNS multicast is reachable; VLANs or mDNS proxies are out of scope.
- Display name truncation threshold (40 characters) is a reasonable default; it can be adjusted without spec changes.
- (Note: FR-005a uses alphabetic suffix per its clarification history; FR-016a/b/c follow the same convention. Future sub-requirements should use alphabetic suffixes.)

---

## Clarifications

### Session 2026-03-26

- Q: What should happen to an active stream when the connected speaker is lost involuntarily (Bonjour remove-event or 60-second stale timeout)? → A: Immediately stop and disconnect the stream when the Bonjour remove-event fires or the 60-second stale timeout expires (whichever occurs first); clear the checkmark; show "Searching for speakers…". No automatic reconnect.
- Q: When a user clicks a new speaker and the connection attempt fails, what should happen? → A: Pessimistic / confirm-then-checkmark: show a "Connecting…" label next to the clicked speaker immediately; commit the checkmark only on confirmed AirPlay handshake success; revert to prior state (prior checkmark or none) silently if the handshake does not succeed within 5 seconds.
- Q: While "Connecting…" is shown for Speaker A (handshake in progress), the user clicks Speaker B — what should happen? → A: Cancel the in-progress handshake for Speaker A, remove its "Connecting…" label, immediately show "Connecting…" next to Speaker B, and begin a new handshake for Speaker B.
- Q: After restart, how long does AirBeam wait to auto-select the last-used speaker before giving up? → A: Fixed 30-second window: if the last-used speaker is not rediscovered within 30 seconds of startup, the auto-select intent is abandoned and the user must select manually.
- Q: In what order should discovered speakers appear in the tray menu? → A: Alphabetical by display name (A→Z), stable across restarts.
