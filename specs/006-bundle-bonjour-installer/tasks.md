# Tasks: Bundle Bonjour Installer (006)

**Input**: Design documents from `specs/006-bundle-bonjour-installer/`
**Branch**: `006-bundle-bonjour-installer`
**Spec**: `specs/006-bundle-bonjour-installer/spec.md`
**Plan**: `specs/006-bundle-bonjour-installer/plan.md`

## Format: `[ID] [P?] [Story?] Description`

- **[P]**: Can run in parallel (different files, no blocking dependencies)
- **[Story]**: Which user story this task belongs to (US1 / US2 / US3)

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Directory scaffolding and .gitignore hygiene before any scripts or content are written.

- [x] T001 Add `installer/deps/BonjourPSSetup.exe` to `.gitignore` (specific file, not whole dir — keep fetch script tracked)
- [x] T002 [P] Create empty `installer/deps/` directory (add `.gitkeep` so Git tracks it)
- [x] T003 [P] Create `installer/licenses/` directory (add `.gitkeep`)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Fetch script + CMake wiring must exist and work before the NSIS script can embed the binary.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete — the NSIS installer requires `installer/deps/BonjourPSSetup.exe` to be present at `makensis` time.

- [x] T004 Create `installer/deps/fetch-bonjour.ps1` — PowerShell script that: (1) downloads `https://support.apple.com/downloads/DL999/en_US/BonjourPSSetup.exe` to `$OutputDir\BonjourPSSetup.exe` using `Invoke-WebRequest`, (2) computes SHA-256 of the downloaded file, (3) compares against `$ExpectedSha256` constant — exits with code 1 and descriptive error if mismatch, (4) skips download if file already exists with matching hash. Accept `-OutputDir` parameter. See `research.md` R-001/R-003 for design.
- [x] T005 Manually run `pwsh installer\deps\fetch-bonjour.ps1 -OutputDir installer\deps` once; capture the SHA-256 output; set `$ExpectedSha256` in `fetch-bonjour.ps1` to the pinned hash. **This step pins the Bonjour version — record hash in a comment alongside the URL.**
- [x] T006 Add CMake `add_custom_command` + `add_custom_target(fetch-bonjour ALL ...)` to `CMakeLists.txt` to run `fetch-bonjour.ps1` as a pre-build step that produces `installer/deps/BonjourPSSetup.exe`. Use `find_program(POWERSHELL_PROG NAMES pwsh powershell REQUIRED)`. See `research.md` R-003 for the exact CMake snippet.
- [x] T007 Add CTest `bonjour-fetch-smoke` test to `tests/CMakeLists.txt` (label: `unit`) — runs `fetch-bonjour.ps1 -OutputDir <tmpdir>` and asserts: exit code 0, output file exists, SHA-256 matches pinned value. Use `add_test(NAME bonjour-fetch-smoke ...)` with `PASS_REGULAR_EXPRESSION` or CMake script mode.
- [x] T008 Verify the CMake build runs `fetch-bonjour` automatically: `cmake --preset msvc-x64-debug && cmake --build --preset msvc-x64-debug` — confirm `installer/deps/BonjourPSSetup.exe` is produced with correct hash, and `bonjour-fetch-smoke` CTest passes.

**Checkpoint**: `installer/deps/BonjourPSSetup.exe` present and hash-verified; CMake wiring confirmed; CTest passes.

---

## Phase 3: User Story 1 — First-time Install Without Bonjour (Priority: P1) 🎯 MVP

**Goal**: A user on a machine with no Bonjour completes AirBeam install; Bonjour is installed silently; no browser opened; no second UAC prompt.

**Independent Test**: Run `AirBeamSetup.exe` on a clean Windows 10/11 VM snapshot with no iTunes/iCloud/Bonjour. Verify `dnssd.dll` present in `System32` after install, no second UAC prompt appeared, and no browser was launched.

### Implementation for User Story 1

- [x] T009 [P] [US1] Create `installer/licenses/bonjour-sdk-license.txt` — Apple Bonjour SDK redistribution license text (obtain from Apple's Bonjour SDK download page or developer.apple.com; must be the exact license governing redistribution of `BonjourPSSetup.exe`).
- [x] T010 [P] [US1] Create `installer/licenses/combined-license.txt` — concatenate AirBeam MIT license (copy from `LICENSE`) and Bonjour SDK license (from T009) with labeled section headers exactly as: `=== AirBeam MIT License ===` and `=== Apple Bonjour SDK License ===`. Plain UTF-8, no BOM. This file is static — committed to repo.
- [x] T011 [US1] Modify `installer/AirBeam.nsi`: (1) Remove the old `BonjourPage`/`BonjourPageLeave` custom wizard page and its `Page custom BonjourPage BonjourPageLeave` entry entirely — the new flow silently auto-installs instead of prompting. (2) Update the license page: replace `!insertmacro MUI_PAGE_LICENSE "..\LICENSE"` with `!insertmacro MUI_PAGE_LICENSE "..\installer\licenses\combined-license.txt"`. (3) In the Install section, before copying AirBeam files: add registry-check logic (`ClearErrors` / `ReadRegStr` from `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service`) — if key absent, extract `BonjourPSSetup.exe` to `$PLUGINSDIR` with `File /oname` and run `ExecWait '"$PLUGINSDIR\BonjourPSSetup.exe" /quiet /norestart' $0`. (4) Add `MessageBox MB_OK|MB_ICONSTOP` with error code + `Abort` if `$0 != 0`. See `research.md` R-002 for exact NSIS snippet, `data-model.md` for state machine.
- [x] T012 [US1] Manual acceptance test on clean VM: (1) snapshot VM with no Bonjour; (2) run `AirBeamSetup.exe`; (3) accept licenses — verify both AirBeam MIT and Bonjour SDK sections visible; (4) complete install — verify no second UAC prompt, no browser opened; (5) verify `%SystemRoot%\System32\dnssd.dll` exists after install; (6) verify `Bonjour Service` registry key present.

**Checkpoint**: Fresh-machine install works end-to-end. US1 independently verified.

---

## Phase 4: User Story 2 — Upgrade / Repair Install (Priority: P2)

**Goal**: When Bonjour is already installed, the AirBeam installer skips Bonjour entirely — no version change, no re-install, no downgrade.

**Independent Test**: Install AirBeam once (Bonjour installs via US1 path). Note Bonjour version in registry. Re-run `AirBeamSetup.exe`. Verify Bonjour version/`ImagePath` registry value is unchanged after the second run.

### Implementation for User Story 2

- [x] T013 [US2] Review `installer/AirBeam.nsi` (from T011): confirm the registry-check `${If} ${Errors}` block correctly skips the `ExecWait` entirely when `Bonjour Service` key is present — no code change needed if T011 is correct; document as verified in this task.
- [x] T014 [US2] Manual acceptance test — already-installed path: (1) use VM from T012 post-install (Bonjour present); (2) record `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service\ImagePath`; (3) re-run `AirBeamSetup.exe`; (4) verify registry value unchanged; (5) repeat with a VM where iTunes installed a newer Bonjour — verify still skipped.

**Checkpoint**: Upgrade/repair path verified. Existing Bonjour installations are never touched.

---

## Phase 5: User Story 3 — License Transparency (Priority: P2)

**Goal**: Both the AirBeam MIT license and the Apple Bonjour SDK license are visible and must be accepted on the installer license page before any files are copied.

**Independent Test**: Step through the installer wizard to the license page. Scroll the text and confirm both `=== AirBeam MIT License ===` and `=== Apple Bonjour SDK License ===` sections are present. Attempt to advance without accepting — verify the Next button is disabled or shows an error.

### Implementation for User Story 3

- [x] T015 [US3] Verify `installer/AirBeam.nsi` license page change (from T011) renders correctly: build the installer and run it to the license page step; confirm both labeled license sections are visible in the MUI scrollable text control; confirm the installer cannot proceed past the license page without acceptance.
- [x] T016 [US3] Verify `installer/licenses/combined-license.txt` (from T010) is committed and contains no BOM, correct ASCII/UTF-8 encoding, both labeled sections present, and the Apple license text is the complete redistribution license (not a summary). Use `Get-FileHash` and `[System.Text.Encoding]::UTF8.GetPreamble()` check.

**Checkpoint**: License compliance verified. Both licenses visible before any file installation.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Build verification, test suite check, and branch commit.

- [x] T017 Run full test suite: `ctest --preset msvc-x64-debug-ci` — confirm all existing tests still pass and `bonjour-fetch-smoke` passes. No regressions.
- [x] T018 [P] Update `specs/006-bundle-bonjour-installer/spec.md` status from `Draft` to `Complete`.
- [x] T019 [P] Update `specs/roadmap.md` to mark feature 006 ✅ Complete.
- [x] T020 Commit all changes to branch `006-bundle-bonjour-installer` with message: `feat(installer): bundle Bonjour Print Services for silent auto-install` and merge to `main`.

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately; T002 and T003 are parallel
- **Foundational (Phase 2)**: Depends on Phase 1 — BLOCKS all user stories
  - T004 → T005 (must pin hash before test can validate it) → T006, T007 (parallel) → T008 (verifies both)
- **US1 (Phase 3)**: Depends on Phase 2 (needs `BonjourPSSetup.exe` present for NSIS embed)
  - T009 and T010 are parallel (different files)
  - T011 depends on T010 (needs combined-license.txt path)
  - T012 depends on T011 (manual test of built installer)
- **US2 (Phase 4)**: Depends on T011/T012 (needs US1 installer built and tested)
- **US3 (Phase 5)**: Depends on T010, T011 (license file + NSIS page change already done)
  - T015 and T016 can run in parallel
- **Polish (Phase 6)**: Depends on all user stories complete

### User Story Dependencies

```
Phase 1 (Setup)
    └── Phase 2 (Foundational: fetch script + CMake + CTest)
            ├── Phase 3 US1 (silent install path)  ← MVP
            │       └── Phase 4 US2 (skip-if-present path — reuses T011 code)
            └── Phase 5 US3 (license page — reuses T010/T011 artifacts)
```

US3 can proceed in parallel with US2 once T011 is complete.

### Parallel Opportunities

```text
# Phase 1 — all parallel:
T002 (installer/deps/.gitkeep) || T003 (installer/licenses/.gitkeep)

# Phase 2 — sequential then parallel:
T004 → T005 → [T006 || T007] → T008

# Phase 3 — parallel then sequential:
[T009 || T010] → T011 → T012

# Phase 5 — both parallel:
T015 || T016

# Phase 6 — parallel:
T018 || T019 (after T017)
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup (T001–T003)
2. Complete Phase 2: Foundational (T004–T008) — fetch script working and verified
3. Complete Phase 3: User Story 1 (T009–T012) — silent install working on clean VM
4. **STOP and VALIDATE**: Clean-VM test passes → installer ships feature 006 value immediately

### Incremental Delivery

1. Setup + Foundational → fetch script wired, CTest passes
2. US1 complete → fresh-machine install works (MVP)
3. US2 verified → upgrade safety confirmed (reuses US1 code, minimal extra work)
4. US3 verified → license compliance confirmed (reuses US1 artifacts)
5. Polish → tests green, committed to main

---

## Notes

- T005 (hash pinning) is a one-time manual step; once done, all future builds skip the download if the file is already present and hash-matches
- The old `BonjourPage`/`BonjourPageLeave` NSIS wizard page is deleted entirely in T011 — no partial migration
- US2 and US3 require no new code beyond what US1 introduces; their phases are purely validation
- `installer/licenses/bonjour-sdk-license.txt` is the canonical Apple license text; `combined-license.txt` is the installer-facing concatenation — both committed
- The `bonjour-fetch-smoke` CTest (T007) runs on CI; it requires internet access on the `windows-latest` runner (standard GitHub-hosted runners have internet access)