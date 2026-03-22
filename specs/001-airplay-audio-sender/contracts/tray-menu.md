# Contract: Tray Menu States & Items

**Scope**: The complete tray context menu specification — which items appear, in what state, in each application state.

---

## Application States

| State | Icon | Tooltip |
|-------|------|---------|
| `Idle` | `idle.ico` | IDS_TOOLTIP_IDLE ("AirBeam — Not connected") |
| `Connecting` | `connecting_N.ico` (animated pulse, 8 frames at 125 ms) | IDS_TOOLTIP_CONNECTING ("AirBeam — Connecting to {device}…") |
| `Streaming` | `streaming.ico` | IDS_TOOLTIP_STREAMING ("AirBeam — Streaming to {device}") |
| `Error` | `error.ico` | IDS_TOOLTIP_ERROR ("AirBeam — Connection failed") |

---

## Menu Structure

The tray context menu is rebuilt from scratch on every right-click. Items are shown in this fixed order:

```
┌─────────────────────────────────────────────────────────────┐
│  [Speaker list section — dynamic]                           │
│                                                             │
│  ● Living Room HomePod         ← active device (checkmark) │
│    Kitchen AirPort Express     ← available AirPlay 1        │
│    Bedroom Apple TV (AirPlay 2 — not supported)  [GRAYED]  │
│                                                             │
│  ────────────────────────────────────────────────────────── │
│                                                             │
│  Volume:  ──────●──────  75%   ← slider popup on click     │
│                                                             │
│  ✓ Low-latency mode            ← checkmark if enabled      │
│    Launch at startup           ← checkmark if enabled      │
│                                                             │
│  ────────────────────────────────────────────────────────── │
│                                                             │
│    Open log folder                                          │
│    Check for Updates                                        │
│                                                             │
│  ────────────────────────────────────────────────────────── │
│                                                             │
│    Quit                                                     │
└─────────────────────────────────────────────────────────────┘
```

---

## Item Availability by State

| Menu Item | Idle | Connecting | Streaming | Error |
|-----------|:----:|:----------:|:---------:|:-----:|
| AirPlay 1 device (unconnected) | ✅ clickable | ✅ clickable | ✅ clickable | ✅ clickable |
| AirPlay 1 device (active, checkmark) | — | ✅ click=disconnect | ✅ click=disconnect | — |
| AirPlay 2 device (grayed) | 🚫 grayed | 🚫 grayed | 🚫 grayed | 🚫 grayed |
| No devices discovered ("No speakers found") | ✅ shown, grayed | ✅ shown, grayed | — | ✅ shown, grayed |
| Volume slider | 🚫 grayed | 🚫 grayed | ✅ active | 🚫 grayed |
| Low-latency mode toggle | ✅ | ✅ | ✅ | ✅ |
| Launch at startup toggle | ✅ | ✅ | ✅ | ✅ |
| Launch at startup (portable mode) | 🚫 hidden | 🚫 hidden | 🚫 hidden | 🚫 hidden |
| Open log folder | ✅ | ✅ | ✅ | ✅ |
| Check for Updates | ✅ | ✅ | ✅ | ✅ |
| Quit | ✅ | ✅ | ✅ | ✅ |

---

## Item Behaviour Details

### Speaker List Section

- **Dynamically populated** from `ReceiverList` at the moment the menu is shown.
- Each AirPlay 1-compatible device: shown with its `displayName`; clicking triggers `ConnectionController::Connect(receiver)`.
- The currently active device (if in Streaming/Connecting state): shown with a checkmark; clicking triggers `ConnectionController::Disconnect()`.
- Each AirPlay 2-only device: shown grayed out; label appended with ` (IDS_AIRPLAY2_SUFFIX)` = " (AirPlay 2 — not supported in v1.0)"; clicking has **no effect** (handler does nothing).
- If `ReceiverList` is empty: show a single grayed-out item `IDS_MENU_NO_SPEAKERS` = "No speakers found".

### Volume Slider

- Implemented as a Win32 `TrackBar` control in a popup child window.
- Range: 0–100 integer; displayed as "Volume: N%".
- Slider position reflects `Config::volume * 100.0f` at open time.
- `WM_HSCROLL` → `RaopSession::SetVolume(pos / 100.0f)` (only when Streaming).
- `TB_ENDTRACK` → `Config::volume = pos / 100.0f; Config::Save()`.
- Keyboard accessible: arrow keys adjust by ±1.

### Low-latency Mode Toggle

- Reflects `Config::lowLatency` with a checkmark.
- On toggle: `Config::lowLatency = !lowLatency; Config::Save()`.
- If currently Streaming: disconnect (TEARDOWN), resize SPSC buffer, reconnect. A brief audio gap is acceptable.
- If not streaming: takes effect on next connection.

### Launch at Startup Toggle

- Reflects `StartupRegistry::IsEnabled()` with a checkmark.
- On toggle: `StartupRegistry::Toggle()` (adds or removes HKCU Run entry).
- `Config::launchAtStartup` is synced to match registry state.
- **Hidden** in portable mode (no registry writes for portable installs).

### Open Log Folder

- `ShellExecuteW(NULL, L"open", Logger::GetLogDirectory(), NULL, NULL, SW_SHOWNORMAL)`
- Always enabled (log directory is always created at startup).

### Check for Updates

- `win_sparkle_check_update_with_ui()` — always calls WinSparkle regardless of `autoUpdate` setting.
- **Always present and enabled** in all four icon states (constitutional requirement).
- If WinSparkle DLL failed to load: item is shown grayed out with label `IDS_MENU_UPDATE_UNAVAILABLE` = "Check for Updates (unavailable)".

### Quit

- `PostQuitMessage(0)` → `WM_QUIT` exits message loop → `App::Shutdown()` tears down all threads.

---

## Balloon Notification Events

These are shown via `Shell_NotifyIcon` `NIF_INFO` — not menu items, but user-facing messages.

| Event | Type | Title (IDS) | Body (IDS) |
|-------|------|-------------|------------|
| Connected | INFO | IDS_BALLOON_CONNECTED_TITLE | IDS_BALLOON_CONNECTED_BODY ("Now streaming to {device}") |
| Disconnected unexpectedly | WARNING | IDS_BALLOON_DISCONNECTED_TITLE | IDS_BALLOON_DISCONNECTED_BODY ("Lost connection to {device}") |
| Reconnect failed (3 retries) | ERROR | IDS_BALLOON_RECONNECT_FAILED_TITLE | IDS_BALLOON_RECONNECT_FAILED_BODY ("Could not reconnect to {device}") |
| Bonjour runtime missing | WARNING | IDS_BALLOON_BONJOUR_TITLE | IDS_BALLOON_BONJOUR_BODY ("Bonjour is not installed. Install from support.apple.com then restart AirBeam.") |
| Update signature invalid | ERROR | IDS_BALLOON_INVALID_SIG_TITLE | IDS_BALLOON_INVALID_SIG_BODY ("Update package could not be verified and was not installed.") |
| Generic unrecoverable error | ERROR | IDS_BALLOON_ERROR_TITLE | IDS_BALLOON_ERROR_BODY ("{error description}") |

---

## String Table Keys

All strings are loaded via `LoadStringW`. The English (en) resource file is the canonical source.

```
IDS_TOOLTIP_IDLE                  "AirBeam — Not connected"
IDS_TOOLTIP_CONNECTING            "AirBeam — Connecting to %s…"
IDS_TOOLTIP_STREAMING             "AirBeam — Streaming to %s"
IDS_TOOLTIP_ERROR                 "AirBeam — Connection failed"

IDS_MENU_NO_SPEAKERS              "No speakers found"
IDS_AIRPLAY2_SUFFIX               " (AirPlay 2 — not supported in v1.0)"
IDS_MENU_VOLUME_FMT               "Volume: %d%%"
IDS_MENU_LOW_LATENCY              "Low-latency mode"
IDS_MENU_LAUNCH_AT_STARTUP        "Launch at startup"
IDS_MENU_OPEN_LOG_FOLDER          "Open log folder"
IDS_MENU_CHECK_FOR_UPDATES        "Check for Updates"
IDS_MENU_UPDATE_UNAVAILABLE       "Check for Updates (unavailable)"
IDS_MENU_QUIT                     "Quit"

IDS_BALLOON_CONNECTED_TITLE       "Connected"
IDS_BALLOON_CONNECTED_BODY        "Now streaming to %s"
IDS_BALLOON_DISCONNECTED_TITLE    "Connection lost"
IDS_BALLOON_DISCONNECTED_BODY     "Lost connection to %s"
IDS_BALLOON_RECONNECT_FAILED_TITLE  "Reconnect failed"
IDS_BALLOON_RECONNECT_FAILED_BODY   "Could not reconnect to %s"
IDS_BALLOON_BONJOUR_TITLE         "Bonjour not installed"
IDS_BALLOON_BONJOUR_BODY          "mDNS discovery is unavailable. Install Bonjour for Windows at support.apple.com, then restart AirBeam."
IDS_BALLOON_INVALID_SIG_TITLE     "Update not installed"
IDS_BALLOON_INVALID_SIG_BODY      "The update package could not be verified and was not installed."
IDS_BALLOON_ERROR_TITLE           "AirBeam error"
IDS_BALLOON_ERROR_BODY            "%s"
```

All seven languages (de, fr, es, ja, zh-Hans, ko) must define every key listed above with no missing or empty values before a release tag is created.
