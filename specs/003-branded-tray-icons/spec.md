# Feature Specification: Branded Tray Icons

**Feature Branch**: `003-branded-tray-icons`  
**Created**: 2025-07-10  
**Status**: Draft  

## Summary

Replace the 11 generated solid-color placeholder `.ico` files in `resources/icons/` with actual branded artwork. The tray icon is the primary user-visible surface of AirBeam — it must be polished before public release.

### Icon Inventory

| File | State | Description |
|------|-------|-------------|
| `airbeam_idle.ico` | Idle / not connected | Gray / neutral |
| `airbeam_streaming.ico` | Actively streaming | Green / active |
| `airbeam_error.ico` | Error / disconnected | Red / warning |
| `airbeam_connecting_001.ico` – `_008.ico` | 8-frame connecting animation | Blue pulse cycle |

---

## User Scenarios & Testing

### User Story 1 - Idle State Icon (Priority: P1)

When AirBeam is running but not connected to any AirPlay receiver, the system tray shows a recognisable AirBeam icon that is visually distinct from other tray icons and indicates an idle/ready state.

**Why this priority**: The idle icon is visible 100% of the time. It is the user's primary signal that AirBeam is running. A placeholder circle is indistinguishable from noise.

**Independent Test**: Launch AirBeam without connecting to any receiver; verify the custom idle icon appears in the tray (not the generic Windows app icon).

**Acceptance Scenarios**:

1. **Given** AirBeam starts with no target device configured, **When** the tray icon appears, **Then** it shows the branded idle icon at 16×16 pixels without visual artefacts.
2. **Given** a high-DPI display (e.g., 150% scaling), **When** Windows requests a larger icon, **Then** the icon scales cleanly (SVG source or multi-resolution ICO with 16, 32, 48 px frames).
3. **Given** AirBeam disconnects from a receiver, **When** the state transitions from streaming to idle, **Then** the icon reverts to the idle design within one frame.

---

### User Story 2 - Streaming State Icon (Priority: P1)

While actively streaming audio, the tray icon changes to a clearly "active" appearance so users can confirm at a glance that audio is flowing.

**Why this priority**: Users need immediate visual feedback that streaming is working. An ambiguous icon leads to support requests ("is it doing anything?").

**Independent Test**: Connect to a shairport-sync receiver and begin streaming; verify the streaming icon is displayed and is visually distinct from the idle icon.

**Acceptance Scenarios**:

1. **Given** a successful RTSP RECORD, **When** the first RTP packet is sent, **Then** the tray switches to the streaming icon.
2. **Given** the streaming icon is active, **When** the user hovers over the tray, **Then** the tooltip reads "AirBeam — Streaming to [device name]".

---

### User Story 3 - Error State Icon (Priority: P1)

When a connection attempt fails or an active stream drops unexpectedly, the tray icon changes to an error/warning state so the user knows intervention may be needed.

**Why this priority**: Silent failures are confusing. A red/warning icon immediately communicates that something went wrong.

**Independent Test**: Simulate a network drop mid-stream; verify the tray transitions to the error icon and a balloon notification is shown.

**Acceptance Scenarios**:

1. **Given** an active stream, **When** the RTSP connection drops and all 3 retries are exhausted, **Then** the tray displays the error icon.
2. **Given** the error icon is active, **When** reconnection succeeds, **Then** the icon returns to streaming (not idle).

---

### User Story 4 - Connecting Animation (Priority: P2)

While AirBeam is establishing a connection (RTSP ANNOUNCE → SETUP → RECORD handshake), the tray icon plays an 8-frame animation indicating progress.

**Why this priority**: The connection takes 1–3 seconds. A static icon during this time makes the app feel frozen. The animation confirms work is in progress.

**Independent Test**: Initiate a connection to a known receiver on a throttled network; verify the 8 animation frames cycle at approximately 125 ms per frame during the handshake.

**Acceptance Scenarios**:

1. **Given** the user selects a receiver from the tray menu, **When** the RTSP session starts, **Then** the tray begins cycling through the 8 connecting frames.
2. **Given** the animation is running, **When** the connection succeeds, **Then** the animation stops and the streaming icon is shown.
3. **Given** the animation is running, **When** the connection fails, **Then** the animation stops and the error icon is shown.
4. **Given** the 8 frames play at 125 ms each, **When** the connection takes longer than 1 second, **Then** the animation loops seamlessly.

---

### Edge Cases

- What if the `.ico` file is missing from the executable's resource section? → `TrayIcon.cpp` already falls back to `IDI_APPLICATION`; the fallback must remain in place.
- What if Windows is in high-contrast mode? → Icons should use the Windows `GetSysColor` theme or have a high-contrast variant.
- What if the animation timer fires after the `HWND` is destroyed? → The timer is already stopped in `TrayIcon::Destroy()`; verify this path.
- What are the multi-resolution ICO requirements? → Each `.ico` MUST contain at minimum 16×16 and 32×32 frames. 48×48 is recommended for taskbar pinning.

---

## Requirements

### Functional Requirements

- **FR-001**: Each `.ico` file MUST contain at minimum 16×16 and 32×32 pixel frames in 32-bit RGBA.
- **FR-002**: Each `.ico` file MUST be placed at `resources/icons/<name>.ico` and listed in `resources/AirBeam.rc.in` with the corresponding `IDI_TRAY_*` resource ID.
- **FR-003**: The idle icon MUST be visually distinct from the streaming, error, and connecting icons at 16×16 pixels (distinguishable without color alone, per accessibility guidelines).
- **FR-004**: The 8 connecting frames (`_001` through `_008`) MUST form a smooth looping animation cycle representing an in-progress state.
- **FR-005**: All icons MUST include the AirBeam brand mark or wordmark so the tray icon is recognisable as AirBeam (not a generic audio or Wi-Fi icon).
- **FR-006**: SVG source files for each icon state MUST be committed to `resources/icons/src/` to enable future resizing and rebranding.
- **FR-007**: The build MUST remain successful (non-placeholder `.ico` files are binary-compatible with the existing `ICON` resource statements).
- **FR-008**: `TrayIcon.cpp` fallback to `IDI_APPLICATION` MUST be retained for headless/test environments where resource loading fails.

### Key Entities

- **`resources/icons/*.ico`**: 11 ICO files — 1 idle, 1 streaming, 1 error, 8 connecting frames.
- **`resources/icons/src/`**: SVG or layered source files (Figma export, Inkscape, etc.).
- **`resources/AirBeam.rc.in`**: Already references all 11 `IDI_TRAY_*` IDs — no change needed to the RC file.
- **`resource_ids.h`**: `IDI_TRAY_IDLE` (2001), `IDI_TRAY_STREAMING` (2002), `IDI_TRAY_ERROR` (2003), `IDI_TRAY_CONN_001`–`008` (2011–2018).

---

## Success Criteria

### Measurable Outcomes

- **SC-001**: All 11 `.ico` files render without visual artefacts at 16×16 and 32×32 on a 100% and 150% DPI display.
- **SC-002**: A human reviewer can correctly identify the idle, streaming, error, and connecting states purely from the tray icon at 16×16, without any tooltip.
- **SC-003**: The build produces an `.exe` with all 11 icons embedded in its resource section, verified by `Resource Hacker` or equivalent.
- **SC-004**: The connecting animation cycles through all 8 frames within ±50 ms of the target 1-second cycle time.
