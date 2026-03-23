# Contract: ICO File Format

**Feature**: 003-branded-tray-icons  
**Consumer**: `rc.exe` (Win32 RC compiler) via `resources/AirBeam.rc.in`  
**Producer**: `resources/icons/export_icons.ps1` (offline, developer machine)

---

## Required Files

The following 11 `.ico` files MUST exist in `resources/icons/` and be referenced by `AirBeam.rc.in`:

| Filename | Resource ID | IDI constant |
|----------|-------------|-------------|
| `airbeam_idle.ico` | `IDI_TRAY_IDLE` | 2001 |
| `airbeam_streaming.ico` | `IDI_TRAY_STREAMING` | 2002 |
| `airbeam_error.ico` | `IDI_TRAY_ERROR` | 2003 |
| `airbeam_connecting_001.ico` | `IDI_TRAY_CONN_001` | 2011 |
| `airbeam_connecting_002.ico` | `IDI_TRAY_CONN_002` | 2012 |
| `airbeam_connecting_003.ico` | `IDI_TRAY_CONN_003` | 2013 |
| `airbeam_connecting_004.ico` | `IDI_TRAY_CONN_004` | 2014 |
| `airbeam_connecting_005.ico` | `IDI_TRAY_CONN_005` | 2015 |
| `airbeam_connecting_006.ico` | `IDI_TRAY_CONN_006` | 2016 |
| `airbeam_connecting_007.ico` | `IDI_TRAY_CONN_007` | 2017 |
| `airbeam_connecting_008.ico` | `IDI_TRAY_CONN_008` | 2018 |

---

## Per-File Frame Contract

Each `.ico` file MUST satisfy all constraints:

| Constraint | Value | Rationale |
|-----------|-------|-----------|
| Frame count | ≥ 2 (MUST), 3 (RECOMMENDED) | Windows requests different sizes by DPI |
| Frame: 16×16 | REQUIRED, 32-bit RGBA | System tray at 100% DPI |
| Frame: 32×32 | REQUIRED, 32-bit RGBA | System tray at 200% DPI; taskbar |
| Frame: 48×48 | RECOMMENDED, 32-bit RGBA | Taskbar pinning |
| Bit depth | 32-bit (RGBA with alpha channel) | Smooth edges; transparent background |
| File size | ≥ 1 KB (branded), < 100 KB | Size check distinguishes placeholders from real assets. Note: programmatic sparse artwork (mostly-transparent PNG frames) compresses to ~1.5–2 KB — well above the 680-byte placeholder baseline. The original estimate of ≥ 10 KB assumed uncompressed RGBA bitmaps; PNG-in-ICO compression reduces this by ~8×. |
| Background | Transparent (alpha = 0 outside icon shape) | Tray shows OS background through icon |

---

## Validation Script Contract

`resources/icons/validate_icons.ps1` MUST:
1. Verify all 11 files exist
2. Verify each file is ≥ 10 KB
3. Parse ICO header and verify ≥ 2 frame directory entries
4. Exit with code 0 on pass, code 1 on any failure
5. Print a summary table to stdout

This script is invoked by the `icon-validation` CTest test.

---

## RC File Contract

`resources/AirBeam.rc.in` already contains correct `ICON` resource statements. No changes are needed.
The RC compiler (`rc.exe`) will embed all 11 ICO files into the `.res` file at build time.

**MUST NOT change**: Resource IDs (`IDI_TRAY_*`), filenames, or the `#include "resource_ids.h"` line.
