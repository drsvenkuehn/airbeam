# Tasks: Bonjour Install Guidance

**Feature**: `005-bonjour-install-guidance`  
**Input**: `specs/005-bonjour-install-guidance/`  
**Prerequisites**: plan.md ✅ | spec.md ✅ | data-model.md ✅ | contracts/balloon-text.md ✅ | research.md ✅ | quickstart.md ✅

**Organization**: Tasks grouped by user story to enable independent implementation and testing.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: User story label (US1, US2, US3)

---

## Phase 1: Setup (Resource IDs and Compile-Time Constant)

**Purpose**: Add new resource ID definitions and the shared URL constant that all implementation tasks depend on.

**⚠️ CRITICAL**: No implementation task can begin until this phase is complete — all C++ and RC changes reference these IDs.

- [ ] T001 Add `IDS_TOOLTIP_BONJOUR_MISSING 1005` to `resources/resource_ids.h` (after existing tooltip IDs, before balloon IDs)
- [ ] T002 Add `IDS_BALLOON_TITLE_BONJOUR_MISSING 1016` to `resources/resource_ids.h` (after `IDS_BALLOON_UPDATE_REJECTED 1015`)
- [ ] T003 Add compile-time constant `BONJOUR_DOWNLOAD_URL L"https://support.apple.com/downloads/bonjour-for-windows"` to `resources/resource_ids.h` (after IDS_ defines, before IDI_ defines)

**Checkpoint**: `resources/resource_ids.h` has all three additions; project builds with no errors.

---

## Phase 2: Foundational (TrayState Extension)

**Purpose**: Extend `TrayState` enum and `SetState()` so the Bonjour-specific icon+tooltip combination works. All US1/FR-008 tasks depend on this.

**⚠️ CRITICAL**: US1 Phase 3 cannot begin until this phase is complete.

- [ ] T004 Add `BonjourMissing` to `TrayState` enum in `src/ui/TrayIcon.h` (after `Error`)
- [ ] T005 Add `case TrayState::BonjourMissing:` to the `switch` in `TrayIcon::SetState()` in `src/ui/TrayIcon.cpp` — maps to `IDI_TRAY_ERROR` icon and `IDS_TOOLTIP_BONJOUR_MISSING` tooltip (no animation timer start/stop needed; mirrors `TrayState::Error` branch but uses different tooltip ID)

**Checkpoint**: `trayIcon_.SetState(TrayState::BonjourMissing)` compiles and sets the error icon with the Bonjour-specific tooltip text when called.

---

## Phase 3: User Story 1 — First-Run With No Bonjour (Priority: P1) 🎯 MVP

**Goal**: On first launch without `dnssd.dll`, the tray balloon fires within 5 s with correct title, body (including URL), and Bonjour-specific tooltip. Clicking the balloon opens the Apple download page.

**Independent Test**: Run `AirBeam.exe` on a machine with `dnssd.dll` renamed; verify balloon appears ≤5 s, tooltip shows Bonjour text, balloon click opens browser at the Apple URL.

### Implementation for User Story 1

- [ ] T006 [P] [US1] Update `IDS_BALLOON_BONJOUR_MISSING` body string in `resources/locales/strings_en.rc` — replace existing body text with: `"Bonjour is not installed. AirPlay speaker discovery requires Bonjour. Visit: https://support.apple.com/downloads/bonjour-for-windows"` (FR-001, FR-006, contract: body ≤255 chars)
- [ ] T007 [P] [US1] Add `IDS_TOOLTIP_BONJOUR_MISSING "AirBeam \x2014 Bonjour Not Found"` to `resources/locales/strings_en.rc` (after existing tooltip strings, FR-008)
- [ ] T008 [P] [US1] Add `IDS_BALLOON_TITLE_BONJOUR_MISSING "AirBeam \x2014 Bonjour Required"` to `resources/locales/strings_en.rc` (after existing balloon strings, FR-005)
- [ ] T009 [US1] Add `bool lastBalloonWasBonjour_ = false;` member to `AppController` private section in `src/core/AppController.h`
- [ ] T010 [US1] Fix `AppController::HandleBonjourMissing()` in `src/core/AppController.cpp` — replace `trayIcon_.SetState(TrayState::Error)` (if present) + existing `balloonNotify_.ShowWarning(IDS_BALLOON_BONJOUR_MISSING, IDS_BALLOON_BONJOUR_MISSING)` with: `trayIcon_.SetState(TrayState::BonjourMissing); lastBalloonWasBonjour_ = true; balloonNotify_.ShowWarning(IDS_BALLOON_TITLE_BONJOUR_MISSING, IDS_BALLOON_BONJOUR_MISSING);` (depends on T004, T005, T008, T009) — **Note**: `lastBalloonWasBonjour_` is a per-session bool; confirm `ConfigManager` is NOT touched here and this flag does NOT appear in `config.json` serialization (FR-009)
- [ ] T011 [US1] Add `case NIN_BALLOONUSERCLICK:` handler to `AppController::HandleTrayCallback()` switch in `src/core/AppController.cpp` — body: `if (lastBalloonWasBonjour_) { ShellExecuteW(nullptr, L"open", BONJOUR_DOWNLOAD_URL, nullptr, nullptr, SW_SHOWNORMAL); lastBalloonWasBonjour_ = false; } break;` (FR-007, SC-004; depends on T003, T009)

**Checkpoint**: US1 fully functional — balloon fires with correct title/body/tooltip, click opens browser. Verify with quickstart.md Scenario 1.

---

## Phase 4: User Story 2 — Post-Install Bonjour Recovery (Priority: P2)

**Goal**: After Bonjour is installed and AirBeam relaunched, NO Bonjour-missing balloon fires and mDNS discovery starts normally.

**Independent Test**: Ensure `dnssd.dll` is present; launch AirBeam; verify no `WM_BONJOUR_MISSING` notification and tray icon is in Idle state. Run quickstart.md Scenario 3.

**Status**: Already implemented by existing `BonjourLoader::Load()` success path. No new code needed.

- [ ] T012 [US2] Open `src/discovery/BonjourLoader.cpp`; read the branch where `Load()` returns `true`; confirm that `PostMessageW(WM_BONJOUR_MISSING, …)` is NOT present on the success path (read-only code review; no file change expected; flag if found)

**Checkpoint**: US2 validated — no regression introduced by US1 changes.

---

## Phase 5: User Story 3 — Localised Guidance (Priority: P3)

**Goal**: All 6 non-English locale RC files contain updated `IDS_BALLOON_BONJOUR_MISSING` (with URL appended) plus new `IDS_TOOLTIP_BONJOUR_MISSING` and `IDS_BALLOON_TITLE_BONJOUR_MISSING` (English fallback with `TODO(i18n)` comment per translation strategy).

**Independent Test**: Verify all 7 RC files have non-empty values for all three IDS keys; URL present in all body strings. Run quickstart.md Scenario 4 (German locale).

- [ ] T013 [P] [US3] Update `resources/locales/strings_de.rc` — append ` Besuchen Sie: https://support.apple.com/downloads/bonjour-for-windows` to existing `IDS_BALLOON_BONJOUR_MISSING` body; add `IDS_TOOLTIP_BONJOUR_MISSING "AirBeam \x2014 Bonjour Not Found" // TODO(i18n): translate`; add `IDS_BALLOON_TITLE_BONJOUR_MISSING "AirBeam \x2014 Bonjour Required" // TODO(i18n): translate`
- [ ] T014 [P] [US3] Update `resources/locales/strings_fr.rc` — append ` Consultez : https://support.apple.com/downloads/bonjour-for-windows` to existing `IDS_BALLOON_BONJOUR_MISSING` body; add `IDS_TOOLTIP_BONJOUR_MISSING "AirBeam \x2014 Bonjour Not Found" // TODO(i18n): translate`; add `IDS_BALLOON_TITLE_BONJOUR_MISSING "AirBeam \x2014 Bonjour Required" // TODO(i18n): translate`
- [ ] T015 [P] [US3] Update `resources/locales/strings_es.rc` — append ` Visite: https://support.apple.com/downloads/bonjour-for-windows` to existing `IDS_BALLOON_BONJOUR_MISSING` body; add `IDS_TOOLTIP_BONJOUR_MISSING "AirBeam \x2014 Bonjour Not Found" // TODO(i18n): translate`; add `IDS_BALLOON_TITLE_BONJOUR_MISSING "AirBeam \x2014 Bonjour Required" // TODO(i18n): translate`
- [ ] T016 [P] [US3] Update `resources/locales/strings_ja.rc` — append ` URL\uff1ahttps://support.apple.com/downloads/bonjour-for-windows` to existing `IDS_BALLOON_BONJOUR_MISSING` body (existing body uses Unicode escapes; append URL as plain ASCII with fullwidth colon escape `\uff1a`); add `IDS_TOOLTIP_BONJOUR_MISSING "AirBeam \x2014 Bonjour Not Found" // TODO(i18n): translate`; add `IDS_BALLOON_TITLE_BONJOUR_MISSING "AirBeam \x2014 Bonjour Required" // TODO(i18n): translate`
- [ ] T017 [P] [US3] Update `resources/locales/strings_ko.rc` — append ` URL\uff1ahttps://support.apple.com/downloads/bonjour-for-windows` to existing `IDS_BALLOON_BONJOUR_MISSING` body (same fullwidth colon escape as T016); add `IDS_TOOLTIP_BONJOUR_MISSING "AirBeam \x2014 Bonjour Not Found" // TODO(i18n): translate`; add `IDS_BALLOON_TITLE_BONJOUR_MISSING "AirBeam \x2014 Bonjour Required" // TODO(i18n): translate`
- [ ] T018 [P] [US3] Update `resources/locales/strings_zh-Hans.rc` — append ` \u7f51\u5740\uff1ahttps://support.apple.com/downloads/bonjour-for-windows` to existing `IDS_BALLOON_BONJOUR_MISSING` body (`\u7f51\u5740` = 网址, `\uff1a` = fullwidth colon); add `IDS_TOOLTIP_BONJOUR_MISSING "AirBeam \x2014 Bonjour Not Found" // TODO(i18n): translate`; add `IDS_BALLOON_TITLE_BONJOUR_MISSING "AirBeam \x2014 Bonjour Required" // TODO(i18n): translate`

**Checkpoint**: SC-003 satisfied — all 7 locale files have non-empty, non-placeholder values for all three IDS keys. All body strings contain the Apple URL.

---

## Phase 6: CTest URL Validation (SC-002)

**Purpose**: Add automated `bonjour-url-check` CTest so SC-002 (URL reachability) is verified on every CI push — no manual gate.

- [ ] T019 Add `bonjour-url-check` CTest to `tests/CMakeLists.txt` — use existing `find_program(PWSH_EXECUTABLE …)` guard; label MUST be `"unit"` (not `"network"`) so it runs under `ctest --preset msvc-x64-debug-ci`; command: `pwsh -Command "try { $r = Invoke-WebRequest 'https://support.apple.com/downloads/bonjour-for-windows' -Method Head -UseBasicParsing -TimeoutSec 15; if ($r.StatusCode -ge 200 -and $r.StatusCode -lt 400) { exit 0 } else { exit 1 } } catch { exit 1 }"`

**Checkpoint**: `ctest --preset msvc-x64-debug-ci -R bonjour-url-check` passes locally and in CI.

---

## Phase 7: Polish & Cross-Cutting Concerns

- [ ] T020 [P] Build Debug configuration (`cmake --build build/msvc-x64-debug`) — confirm zero errors and zero warnings related to new code
- [ ] T021 Run `ctest --preset msvc-x64-debug-ci` — confirm all tests pass including `bonjour-url-check` (SC-002) — **must run after T020 build completes**
- [ ] T022 Verify `resources/resource_ids.h` ID assignments: 1005 (tooltip) in tooltip block, 1016 (balloon title) in balloon block, no collisions with existing IDs 1001–1004 and 1010–1015
- [ ] T023 [P] Review all 7 locale RC files for character limit compliance per `contracts/balloon-text.md`: title ≤63 chars, body ≤255 chars, tooltip ≤127 chars
- [ ] T024 Run quickstart.md Scenario 1 manually on machine with `dnssd.dll` renamed — verify SC-001 (balloon ≤5 s), FR-005 (title), FR-006 (body+URL), FR-007 (click), FR-008 (tooltip)
- [ ] T025 Run quickstart.md Scenario 2 — verify FR-009 (balloon fires again on relaunch)
- [ ] T026 Update `specs/roadmap.md` — mark feature 005 status as 🔧 In Progress (or ✅ Complete if all tests pass)
- [ ] T027 **Pre-release gate** (constitution §VIII): Run `Select-String 'TODO(i18n)' resources/locales/*.rc | Measure-Object -Line` — result MUST be `0` before tagging any release; if non-zero, resolve or document an explicit constitution amendment approving the deferred translations

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Resource IDs)**: No dependencies — start immediately
- **Phase 2 (TrayState)**: Depends on Phase 1 (T001 for `IDS_TOOLTIP_BONJOUR_MISSING`)
- **Phase 3 (US1)**: Depends on Phase 1 (T001–T003) + Phase 2 (T004–T005) — BLOCKS all US1 tasks
- **Phase 4 (US2)**: Independent of Phase 3 — validation only
- **Phase 5 (US3)**: Independent of Phases 2–3 — depends only on Phase 1 (T001–T003 for IDS values to exist)
- **Phase 6 (CTest)**: Independent of all implementation — can run after Phase 1
- **Phase 7 (Polish)**: Depends on all phases complete

### User Story Dependencies

- **US1 (P1)**: Requires Phase 1 + Phase 2 complete
- **US2 (P2)**: No new code — validation only; can run after US1 changes are in
- **US3 (P3)**: Requires Phase 1 complete (IDS constants must exist); RC string tasks [T013–T018] are fully parallel with each other and with US1 implementation

### Within US1

- T006, T007, T008 [P] — all touch different files/strings; fully parallel
- T009 can run in parallel with T006/T007/T008 (different file: AppController.h)
- T010 depends on T004, T005 (TrayState::BonjourMissing), T008 (IDS_BALLOON_TITLE_BONJOUR_MISSING), T009 (lastBalloonWasBonjour_ member declared)
- T011 depends on T003 (BONJOUR_DOWNLOAD_URL constant) and T009 (lastBalloonWasBonjour_)

---

## Parallel Opportunities

```
Phase 1 (T001, T002, T003): All parallel — different lines in same file, non-overlapping
Phase 2 (T004, T005): Sequential — T005 depends on T004's enum value
Phase 3: T006, T007, T008 parallel | T009, T010 after T004/T005 | T011 after T003+T010
Phase 5: T013, T014, T015, T016, T017, T018 ALL parallel
Phase 7: T020 first → T021 after T020 | T022 and T023 parallel with T021
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1 (T001–T003): Resource IDs — ~5 min
2. Complete Phase 2 (T004–T005): TrayState::BonjourMissing — ~10 min
3. Complete Phase 3 (T006–T011): US1 implementation — ~20 min
4. **STOP and VALIDATE**: Run quickstart.md Scenario 1
5. US1 balloon fully working — 4 files changed, ~35 lines total

### Incremental Delivery

1. Phase 1 + 2 → Foundation ready (~15 min)
2. Phase 3 (US1) → Balloon fires + click works → Validate
3. Phase 5 (US3) → All locales updated → SC-003 satisfied
4. Phase 6 (T019) → CTest added → SC-002 automated
5. Phase 4 (T012) + Phase 7 → Polish + verify

---

## Notes

- T001–T003 are sequential edits to the same file but are listed separately for clear tracking; they can be done in one edit pass
- T013–T018 (locale files) are ideal for parallel sub-agent execution — each touches a single independent file
- The `lastBalloonWasBonjour_` flag (T010–T011) is the only AppController member change — no constructor change needed (default `= false` initializer)
- `BONJOUR_DOWNLOAD_URL` in `resource_ids.h` (T003) is a `constexpr` or `#define` — recommend `#define` for RC string compatibility if ever needed; for C++ source a `constexpr wchar_t[]` is cleaner (use `constexpr`)
- US2 (T012) is intentionally a read-only verification task — no code change expected
