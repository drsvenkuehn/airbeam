# Data Model: Branded Tray Icons

**Feature**: 003-branded-tray-icons  
**Date**: 2026-03-22

---

## Icon Inventory

### State → File → Resource ID → Visual

| State | `.ico` file | Resource ID | IDI value | Visual description |
|-------|-------------|-------------|-----------|-------------------|
| Idle | `airbeam_idle.ico` | `IDI_TRAY_IDLE` | 2001 | Gray, outlined speaker + arcs |
| Streaming | `airbeam_streaming.ico` | `IDI_TRAY_STREAMING` | 2002 | Green, filled speaker + arcs |
| Error | `airbeam_error.ico` | `IDI_TRAY_ERROR` | 2003 | Red speaker + X overlay |
| Connecting frame 1 | `airbeam_connecting_001.ico` | `IDI_TRAY_CONN_001` | 2011 | Blue, arc at 0° |
| Connecting frame 2 | `airbeam_connecting_002.ico` | `IDI_TRAY_CONN_002` | 2012 | Blue, arc at 45° |
| Connecting frame 3 | `airbeam_connecting_003.ico` | `IDI_TRAY_CONN_003` | 2013 | Blue, arc at 90° |
| Connecting frame 4 | `airbeam_connecting_004.ico` | `IDI_TRAY_CONN_004` | 2014 | Blue, arc at 135° |
| Connecting frame 5 | `airbeam_connecting_005.ico` | `IDI_TRAY_CONN_005` | 2015 | Blue, arc at 180° |
| Connecting frame 6 | `airbeam_connecting_006.ico` | `IDI_TRAY_CONN_006` | 2016 | Blue, arc at 225° |
| Connecting frame 7 | `airbeam_connecting_007.ico` | `IDI_TRAY_CONN_007` | 2017 | Blue, arc at 270° |
| Connecting frame 8 | `airbeam_connecting_008.ico` | `IDI_TRAY_CONN_008` | 2018 | Blue, arc at 315° |

---

## SVG Source Files

Committed to `resources/icons/src/` — the authoritative design source.

| SVG file | Produces | Notes |
|----------|---------|-------|
| `icon_idle.svg` | `airbeam_idle.ico` | Gray outlined version |
| `icon_streaming.svg` | `airbeam_streaming.ico` | Green filled version |
| `icon_error.svg` | `airbeam_error.ico` | Red + X overlay |
| `icon_connecting_base.svg` | Seed for frame generation | Single blue arc at 0° |
| `gen_frames.ps1` | `icon_connecting_001.svg` – `_008.svg` | Rotates connecting_base by 45° × N |

---

## ICO Frame Structure (per file)

Each `.ico` file MUST contain exactly 3 frames in this order:

| Frame | Dimensions | Bit depth | Notes |
|-------|-----------|-----------|-------|
| 1 | 16 × 16 | 32-bit RGBA | Tray icon at 100% DPI |
| 2 | 32 × 32 | 32-bit RGBA | Tray icon at 200% DPI; taskbar |
| 3 | 48 × 48 | 32-bit RGBA | Taskbar pinning; large icon requests |

---

## State Transition Diagram

```
           [App Start]
                │
                ▼
        ┌──────────────┐
        │     IDLE     │ ◄──────────────────────┐
        └──────────────┘                        │
                │ user selects receiver          │ disconnect / 3 retries exhausted
                ▼                               │
        ┌──────────────┐                        │
        │  CONNECTING  │ ──── connect OK ──────►│──► STREAMING
        │ (8 frames,   │                        │
        │  125ms each) │ ──── connect FAIL ─────►     ERROR
        └──────────────┘
```

Full state machine (matches `TrayState` enum in `TrayIcon.h`):

| From | Event | To |
|------|-------|-----|
| Idle | User selects receiver | Connecting |
| Connecting | RTSP RECORD success | Streaming |
| Connecting | All 3 retries exhausted | Error |
| Streaming | RTSP connection drop + retries exhausted | Error |
| Error | Reconnect success | Streaming |
| Error | User disconnects | Idle |
| Streaming | User disconnects | Idle |

---

## File Size Expectations

| File | Placeholder (current) | Target (branded) |
|------|----------------------|-----------------|
| `airbeam_idle.ico` | ~651 bytes | ~15–16 KB |
| `airbeam_streaming.ico` | ~682 bytes | ~15–16 KB |
| `airbeam_error.ico` | ~680 bytes | ~15–16 KB |
| `airbeam_connecting_00N.ico` (×8) | ~680–700 bytes | ~15–16 KB each |
| **Total** | ~7.5 KB | ~165–176 KB |

---

## Code References (No Changes Required)

| File | Reference | Status |
|------|-----------|--------|
| `resources/AirBeam.rc.in` | `IDI_TRAY_*   ICON  "icons/*.ico"` | ✅ Already correct |
| `resources/resource_ids.h` | `IDI_TRAY_IDLE` = 2001, etc. | ✅ Already correct |
| `src/ui/TrayIcon.cpp` | `LoadTrayIcon()`, `LoadAnimFrame()` | ✅ Correct (after SC-004 bug fix) |
| `src/ui/TrayIcon.cpp` | `ANIM_INTERVAL_MS = 150` | ⚠️ Must change to 125 (SC-004) |
