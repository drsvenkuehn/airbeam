# Research: Branded Tray Icons

**Feature**: 003-branded-tray-icons  
**Date**: 2026-03-22

---

## Decision 1: ICO generation toolchain

**Decision**: Use **ImageMagick** (`magick convert`) for SVG→PNG rasterization and PNG→ICO bundling.
ICO files are committed to the repository as binary blobs; conversion runs **offline on the developer machine**, not in CI.

**Rationale**: Native Windows apps embed ICO files as compiled resources — the RC compiler (`rc.exe`) consumes them at build time. Generating ICOs at CMake configure time adds unnecessary toolchain complexity (ImageMagick on CI, LLVM/Inkscape for SVG rendering, etc.). Pre-committed ICOs are the standard approach for Win32 resource assets. ImageMagick is free, scriptable, and produces standards-compliant multi-frame ICOs.

**ICO build command** (per icon state):
```powershell
# Rasterize SVG → PNGs, then pack into multi-frame ICO
magick convert icon_idle.svg -resize 16x16 -depth 32 icon_idle_16.png
magick convert icon_idle.svg -resize 32x32 -depth 32 icon_idle_32.png
magick convert icon_idle.svg -resize 48x48 -depth 32 icon_idle_48.png
magick convert icon_idle_16.png icon_idle_32.png icon_idle_48.png airbeam_idle.ico
```

**Alternatives considered**:
- Inkscape CLI + Python Pillow: More steps, requires two extra tools; no advantage over ImageMagick for this use case.
- IcoFX / GIMP GUI export: Not scriptable; cannot be version-controlled or reproducible.
- CMake FetchContent ImageMagick: Adds ~300 MB download to CI for a file that rarely changes.

---

## Decision 2: SVG design approach

**Decision**: Create **4 distinct SVG source files** (idle, streaming, error, connecting-base) plus a **PowerShell generation script** that produces 8 connecting-frame SVGs by rotating an arc element by 45° increments.

**SVG file layout** (`resources/icons/src/`):
```
icon_idle.svg             # Gray, outlined
icon_streaming.svg        # Green, filled
icon_error.svg            # Red, with X overlay
icon_connecting_base.svg  # Blue arc (partial ring, 45° arc segment)
gen_frames.ps1            # Rotates connecting_base to produce _001–_008 SVGs
```

**Rationale**: The 8 animation frames are rotationally related — generating them from a single source ensures visual consistency and makes the animation smooth. Separate SVG files for non-animation states keep the design process simple.

**Alternatives considered**:
- 11 separate hand-crafted SVGs: Would be inconsistent unless done in a design tool with a shared symbol library; harder to maintain.
- Single SVG with 11 layers: Tool-dependent (Figma / Inkscape specific); harder to script.

---

## Decision 3: Icon design concept

**Decision**: The AirBeam icon is a **stylized sound-emission mark** — a speaker glyph on the left with 2–3 curved arcs radiating to the right (similar to the Wi-Fi / AirPlay symbol), rendered in a simple geometric style legible at 16×16. Each state applies a color + shape modifier:

| State | Color | Shape modifier |
|-------|-------|----------------|
| Idle | `#9E9E9E` (gray) | Outlined, no fill on arcs |
| Streaming | `#4CAF50` (green) | Filled arcs, all 3 visible |
| Error | `#F44336` (red) | X overlay on speaker body |
| Connecting | `#2196F3` (blue) | Single arc segment rotating (8 positions × 45°) |

**Accessibility (FR-003)**: States are distinguishable without color via shape:
- Idle = outline only
- Streaming = solid/filled arcs
- Error = X shape present
- Connecting = single arc (partial)

**Rationale**: Sound + wireless emission is universally understood for an audio-streaming app. The simple geometric style remains recognizable at 16×16. The 4-state color/shape matrix ensures all states are distinguishable by color-blind users.

---

## Decision 4: ICO frame requirements

**Decision**: Each ICO file MUST contain exactly **3 frames**: 16×16, 32×32, and 48×48, all in 32-bit RGBA (with alpha channel). The 48×48 frame is included for taskbar pinning.

**Rationale**: Windows requests different frame sizes depending on DPI and context (tray = 16×16 at 100% DPI, 32×32 at 200% DPI; taskbar = 32×32 or 48×48). Without the larger frames, Windows scales up the 16×16 which looks blurry. The RC compiler accepts any valid ICO; no RC file changes are required.

**Expected ICO file sizes** (3 frames, 32-bit RGBA):
- 16×16: 1,128 bytes
- 32×32: 4,232 bytes
- 48×48: 9,528 bytes
- ICO header + directory: ~48 bytes
- **Total per file**: ~15–16 KB (vs. current ~680-byte single-frame placeholder)

---

## Decision 5: Animation interval — SC-004 bug fix

**Decision**: Fix `ANIM_INTERVAL_MS` in `TrayIcon.cpp` from **150 ms → 125 ms** to meet SC-004.

**Rationale**: The spec and SC-004 require the 8-frame connecting animation to complete within ±50 ms of a 1-second cycle (950–1050 ms). The current code uses 150 ms per frame = 1200 ms cycle, which is **200 ms outside** the tolerance. Changing to 125 ms × 8 = 1000 ms hits the target exactly. This is a one-line fix in `TrayIcon.cpp`.

**Impact**: The change is on the UI message-loop thread (Thread 1); it has no effect on the real-time audio threads (Thread 3/4). No constitution implications.

---

## Decision 6: Build verification (CI)

**Decision**: Add a **PowerShell CI check** that validates:
1. All 11 `.ico` files exist in `resources/icons/`
2. Each `.ico` is ≥ 10 KB (i.e., not a placeholder)
3. Each `.ico` has ≥ 2 frames (16×16 and 32×32 minimum)

This check runs as part of the existing CMake configure step or as a ctest unit test.

**Rationale**: The build will succeed even with placeholder ICOs because the RC compiler doesn't validate icon content. An automated size/frame check catches regressions (e.g., accidentally committing a single-frame placeholder again).

**Implementation**: A CTest `icon-validation` test using a small PowerShell script that reads ICO file headers.

---

## Decision 7: No Figma/design-tool lock-in

**Decision**: SVG files in `resources/icons/src/` are the single source of truth. No proprietary design format (Figma `.fig`, Adobe XD, Sketch) is required or stored in the repo.

**Rationale**: Contributors without access to proprietary design tools must be able to modify icons. SVG is open, version-controllable, and diff-able. Figma or similar tools may be used for design but exports must be committed as SVGs.
