# Implementation Plan: mDNS Discovery and Tray Speaker Menu

**Branch**: `008-mdns-tray-discovery` | **Date**: 2025-07-17 | **Spec**: [spec.md](spec.md)  
**Input**: Feature specification from `/specs/008-mdns-tray-discovery/spec.md`

## Summary

Completes the Bonjour/mDNS discovery pipeline (Thread 2) and wires it to the Win32 tray
context menu so that AirPlay 1 receivers found on the local network appear as selectable items
within 10 seconds of launch. Core behaviors: AirPlay 1 filter (`et` contains `"1"`, no `pk`),
alphabetical display, 3-vs-4 inline/submenu threshold, pessimistic "Connecting…" / checkmark
flow, 30-second startup auto-select window, 60-second stale removal with immediate disconnect,
and full locale coverage for all 7 supported languages.

## Technical Context

**Language/Version**: C++17, MSVC 2022 (v143), `/permissive-`  
**Primary Dependencies**: Win32 API, Bonjour SDK (`dnssd.dll`, dynamic), WinSparkle (already
wired), GoogleTest (unit tests), libspeexdsp (resampler from feature 007 — not touched by
this feature)  
**Storage**: `%APPDATA%\AirBeam\config.json` — existing `Config` class, `lastDevice` wstring
key already present  
**Testing**: GoogleTest via `ctest --preset msvc-x64-debug-ci`, label `unit`  
**Target Platform**: Windows 10 (build 1903+) and Windows 11, x86-64  
**Project Type**: Desktop tray application (Win32, no UI framework)  
**Performance Goals**: Speaker list populated within 10 s of device becoming reachable
(SC-001); "Connecting…" label within 200 ms of click (SC-003); startup overhead ≤ 1 s (SC-004)  
**Constraints**: No heap allocation or mutex locks on audio hot path (Threads 3/4 — this
feature does not touch those threads); all speaker-list mutations marshalled to UI thread via
`PostMessage`; `TrayMenu::Show()` always builds fresh from sorted snapshot (no menu-handle
caching)  
**Scale/Scope**: Up to 100 discovered speakers (IDM_DEVICE_MAX_COUNT = 100); 7 locales; single
binary, no additional DLLs introduced by this feature

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Real-Time Audio Thread Safety | ✅ PASS | Discovery (Thread 2) and UI (Thread 1) only. Threads 3 & 4 untouched. All speaker-list updates use `PostMessage` — no mutex on hot path. |
| II. AirPlay 1 / RAOP Protocol Fidelity | ✅ PASS | AirPlay 1 filter (`et` contains `"1"` AND no `pk`) is spec-locked. RAOP session is used as-is; no protocol changes. |
| III. Native Win32, No External UI Frameworks | ✅ PASS | Menu built with `CreatePopupMenu` / `AppendMenuW` / `TrackPopupMenu`. Submenu via `CreatePopupMenu` + `MF_POPUP`. |
| III-A. Visual Color Palette | ✅ PASS | No new icons or color values introduced. |
| IV. Test-Verified Correctness | ✅ PASS | New unit tests: `test_mdns_txt` (extended), `test_tray_menu`, `test_receiver_list`. See test plan below. |
| V. Observable Failures — Never Silent | ✅ PASS | Bonjour-missing state shown in menu (FR-013). Stale removal with active speaker triggers immediate disconnect (FR-005a). FR-016b (silent revert on initial handshake timeout) is a UX design decision per clarification Session 2026-03-26 Q2 — initial speaker-selection attempts are not error conditions; mid-stream RTSP failures remain fully surfaced per §V. |
| VI. Strict Scope Discipline (v1.0) | ✅ PASS | No multi-room, no AirPlay 2 implementation, no new DLL. |
| VII. MIT-Compatible Licensing | ✅ PASS | No new dependencies. Bonjour SDK dynamic link already approved. |
| VIII. Localizable User Interface | ⚠️ VIOLATION → JUSTIFIED | Four new user-visible strings must be added: `IDS_MENU_SEARCHING`, `IDS_MENU_BONJOUR_MISSING`, `IDS_MENU_CONNECTING`, `IDS_MENU_SPEAKERS`. Violation exists in the current codebase (`L"No speakers found"` hardcoded). This feature **fixes** it: all four strings will be extracted to `resource_ids.h` and added to all 7 locale `.rc` files. |

**Post-design re-check**: No new violations introduced. The Constitution VIII violation that
pre-existed in `TrayMenu.cpp` (`L"No speakers found"`) is resolved by this feature.

## Project Structure

### Documentation (this feature)

```text
specs/008-mdns-tray-discovery/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   ├── wm-messages.md
│   ├── txt-record-parsing.md
│   └── menu-command-ids.md
└── tasks.md             # Phase 2 output (/speckit.tasks — NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
src/
├── core/
│   ├── AppController.h          # +connectingReceiverIdx_, priorConnectedIdx_,
│   │                            #  sortedReceivers_, bonjourMissing_, TIMER_HANDSHAKE_TIMEOUT
│   └── AppController.cpp        # Rewrite HandleReceiversUpdated, HandleRaopConnected,
│                                 # HandleRaopFailed, Connect(), HandleTimer, HandleCommand
├── discovery/
│   ├── AirPlayReceiver.h        # +stableId (wstring), lastSeenTick DWORD→ULONGLONG
│   ├── TxtRecord.cpp            # +an field parse, display name construction, 40-char truncation
│   ├── MdnsDiscovery.cpp        # +DeviceIdFromInstance(), 30000ms reconnect window fix
│   └── ReceiverList.cpp         # +AirPlay1 filter in Update()
└── ui/
    ├── TrayMenu.h               # +bonjourMissing param, connectingReceiverIdx param
    └── TrayMenu.cpp             # Full rebuild: 3-vs-4 threshold, submenu, Connecting label,
                                 #  Searching/BonjourMissing placeholders, alphabetical sort

resources/
├── resource_ids.h               # +IDS_MENU_SEARCHING, IDS_MENU_BONJOUR_MISSING,
│                                #  IDS_MENU_CONNECTING, IDS_MENU_SPEAKERS (IDs 1028–1031)
└── locales/
    ├── strings_en.rc            # +4 new string entries
    ├── strings_de.rc            # +4 new string entries
    ├── strings_fr.rc            # +4 new string entries
    ├── strings_es.rc            # +4 new string entries
    ├── strings_ja.rc            # +4 new string entries
    ├── strings_zh-Hans.rc       # +4 new string entries
    └── strings_ko.rc            # +4 new string entries

tests/unit/
├── test_mdns_txt.cpp            # Extended: an field, display name, truncation, stableId
├── test_tray_menu.cpp           # New: 3-vs-4 threshold, Connecting label, placeholders
└── test_receiver_list.cpp       # New: alphabetical sort, AirPlay1 filter, stale pruning
```

**Structure Decision**: Single-project Win32 desktop app (existing layout). All changes are
modifications to existing files except three new/extended test files and one new contracts
sub-directory.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| Constitution VIII — 4 new locale strings (partial, pre-existing) | Required by spec (FR-012, FR-013, FR-016) | Hardcoding would be a permanent Constitution VIII violation; extracting now pays the debt |
