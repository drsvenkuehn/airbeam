# Quickstart: Creating Branded Tray Icons

**Feature**: 003-branded-tray-icons  
**Audience**: Designer or developer producing the icon assets  
**Date**: 2026-03-22

---

## Prerequisites

- **ImageMagick 7+** installed and on `$PATH` (`magick --version`)
- An SVG editor: Inkscape (free), Figma, Illustrator, etc.
- PowerShell 7+ (for the frame-generation script)

---

## Step 1 — Create the SVG Source Files

Create 4 base SVG files in `resources/icons/src/`:

| File | Description |
|------|-------------|
| `icon_idle.svg` | Gray outlined speaker + arcs; no fill on arc strokes |
| `icon_streaming.svg` | Green filled speaker + 3 solid arc strokes |
| `icon_error.svg` | Red speaker body + X overlay on the speaker cone |
| `icon_connecting_base.svg` | Blue speaker body + single partial arc at 12 o'clock (0°) |

**Canvas**: 64×64 px (will be downscaled; keep artwork in a central ~48×48 safe zone).  
**Color palette**:
- Idle: `#9E9E9E`
- Streaming: `#4CAF50`
- Error: `#F44336`
- Connecting: `#2196F3`

**Accessibility requirement (FR-003)**: States must be distinguishable without color — use shape differences (outline vs filled vs X vs partial arc).

---

## Step 2 — Generate the 8 Connecting-Frame SVGs

Run the frame-generation script to produce 8 rotated variants of the connecting icon:

```powershell
cd resources/icons/src
./gen_frames.ps1
# Produces: icon_connecting_001.svg through icon_connecting_008.svg
# Each rotates the arc element of icon_connecting_base.svg by 45° × (N-1)
```

Verify 8 files are created and visually inspect frames 1, 3, 5, 7 for correct rotation.

---

## Step 3 — Export SVGs to Multi-Frame ICOs

Run the ICO export script:

```powershell
cd resources/icons
./export_icons.ps1
```

This script (committed at `resources/icons/export_icons.ps1`) does the following for each of the 11 icon states:

```powershell
# Example for airbeam_idle.ico
magick convert src/icon_idle.svg -resize 16x16 -depth 32 -background none tmp_16.png
magick convert src/icon_idle.svg -resize 32x32 -depth 32 -background none tmp_32.png
magick convert src/icon_idle.svg -resize 48x48 -depth 32 -background none tmp_48.png
magick convert tmp_16.png tmp_32.png tmp_48.png airbeam_idle.ico
Remove-Item tmp_16.png, tmp_32.png, tmp_48.png
```

**Expected output**: 11 `.ico` files in `resources/icons/`, each ~15–16 KB (3 frames: 16×16, 32×32, 48×48, 32-bit RGBA).

---

## Step 4 — Validate the ICO Files

Run the validation script:

```powershell
cd C:\path\to\airbeam
pwsh resources/icons/validate_icons.ps1
```

Each file should report:
```
airbeam_idle.ico            OK  3 frames  [16x16, 32x32, 48x48]  15.2 KB
airbeam_streaming.ico       OK  3 frames  [16x16, 32x32, 48x48]  15.1 KB
...
```

---

## Step 5 — Visual Review

1. Build the project: `cmake --preset msvc-x64-debug && cmake --build --preset msvc-x64-debug`
2. Run AirBeam: `build\Debug\AirBeam.exe`
3. Check each state:
   - **Idle**: appears in system tray immediately
   - **Streaming**: connect to a shairport-sync receiver
   - **Error**: simulate network drop or invalid address
   - **Connecting**: watch the animation while connecting (should cycle in ~1 second)

---

## Step 6 — Commit the Assets

```powershell
git add resources/icons/*.ico resources/icons/src/ resources/icons/export_icons.ps1 resources/icons/validate_icons.ps1
git commit -m "feat(003): add branded tray icons (idle, streaming, error, 8 connecting frames)"
```

---

## High-Contrast Mode Note

If you have a high-contrast variant:
1. Add `icon_idle_hc.svg`, etc. to `resources/icons/src/`
2. Export to `airbeam_idle_hc.ico` etc.
3. Register them with additional `IDI_TRAY_*_HC` IDs in `resource_ids.h` (future enhancement — not required for v1.0)

For v1.0: ensure icon outlines are thick enough (≥ 1.5 px at 16×16) to remain visible in high-contrast mode without a dedicated variant.
