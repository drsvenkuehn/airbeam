# Tasks: Branded Tray Icons (003)

**Input**: Design documents from `specs/003-branded-tray-icons/`
**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md) | **Data Model**: [data-model.md](data-model.md)

**Status note**: Phase 1 setup and all icon generation were completed in a prior session
(commit `9aa3f35`). Tasks marked `[x]` are already committed to `main`. Remaining work
is CTest wiring, build verification, contract correction, and manual acceptance review.

---

## Phase 1: Setup & Tooling

**Purpose**: SC-004 bug fix, icon generator, validation script, and all 11 branded ICOs.

- [x] T001 Fix `ANIM_INTERVAL_MS` in `src/ui/TrayIcon.cpp` line 9: change `150` → `125` (SC-004: 125 ms × 8 = 1000 ms cycle, was 1200 ms)
- [x] T002 Create `resources/icons/src/gen_icons.ps1`: pure PowerShell/GDI+ icon generator — draws speaker + arcs shape in 4 states (idle/streaming/error/connecting×8) at 16/32/48 px PNG-in-ICO, no external tools required
- [x] T003 [P] Create `resources/icons/validate_icons.ps1`: reads ICO magic bytes + frame count; exits 0 on pass (all 11 files exist, valid ICO header, ≥2 frames, >1 KB); exits 1 on any failure
- [x] T004 Run `gen_icons.ps1` to replace all 11 placeholder (~680 byte) ICOs in `resources/icons/` with branded 3-frame icons (~1.5–2 KB each)
- [x] T005 Confirm `validate_icons.ps1` exits 0 (all 11: valid magic, 3 frames, >1 KB) — verified in session

**Checkpoint**: All 11 branded ICOs committed; SC-004 bug fixed; validation script operational.

---

## Phase 2: Foundational (Build Integration)

**Purpose**: Wire validation into CMake/CTest so placeholder regressions are caught automatically.

**⚠️ CRITICAL**: CTest test must be registered before any user story can be declared CI-verified.

- [ ] T006 Add `icon-validation` CTest entry in `tests/CMakeLists.txt`: `add_test(NAME icon-validation COMMAND pwsh ${PROJECT_SOURCE_DIR}/resources/icons/validate_icons.ps1)` — test must pass with current branded ICOs
- [ ] T007 Run `cmake --build build --config Release` from repo root and confirm build succeeds with new ICOs (rc.exe must embed them without error; no linker changes required)

**Checkpoint**: `ctest -R icon-validation` exits 0; Release build succeeds with all 11 ICOs embedded.

---

## Phase 3: User Story 1 — Idle State Icon (Priority: P1) 🎯 MVP

**Goal**: Branded idle icon visible in system tray when AirBeam is running with no receiver connected.

**Independent Test**: Launch AirBeam; verify the custom idle icon (gray speaker outline) appears in the tray, not a generic Windows app icon.

- [x] T008 [US1] Generate `resources/icons/airbeam_idle.ico` — 3-frame (16/32/48 px), gray outlined speaker + arcs, 1.9 KB *(completed T004)*
- [ ] T009 [US1] Manual SC-001: Launch built `.exe`, verify `airbeam_idle.ico` renders without artefacts at 16×16 on 100% DPI display
- [ ] T010 [US1] Manual SC-001 DPI: On a 150% DPI display (or Windows display settings), verify idle icon scales cleanly from the 32×32 frame
- [ ] T011 [US1] Manual SC-002: Confirm idle icon is recognisable as AirBeam (not a generic audio/Wi-Fi icon) at 16×16 without tooltip

**Checkpoint**: Idle icon renders correctly at 100% and 150% DPI; visually distinct from generic Windows icons.

---

## Phase 4: User Story 2 — Streaming State Icon (Priority: P1)

**Goal**: Green "active streaming" icon in tray while audio is flowing to an AirPlay receiver.

**Independent Test**: Connect to shairport-sync and begin streaming; verify the tray shows a green icon visually distinct from the idle (gray) icon.

- [x] T012 [P] [US2] Generate `resources/icons/airbeam_streaming.ico` — 3-frame, green filled speaker + arcs, 1.9 KB *(completed T004)*
- [ ] T013 [US2] Manual SC-002: Trigger streaming state; verify streaming icon is distinctly green/filled vs. idle gray/outlined at 16×16
- [ ] T014 [US2] Manual: Hover over tray while streaming; verify tooltip reads "AirBeam — Streaming to [device name]" (spec acceptance scenario 2)

**Checkpoint**: Streaming icon clearly distinct from idle; tooltip correct.

---

## Phase 5: User Story 3 — Error State Icon (Priority: P1)

**Goal**: Red "error/disconnected" icon in tray after a failed connection or dropped stream.

**Independent Test**: Simulate network drop mid-stream; verify tray transitions to red error icon and balloon notification is shown.

- [x] T015 [P] [US3] Generate `resources/icons/airbeam_error.ico` — 3-frame, red filled speaker + white X overlay, 2 KB *(completed T004)*
- [ ] T016 [US3] Manual SC-002: Trigger error state (disconnect network or kill shairport-sync); verify error icon (red + X) is visible and distinct from idle and streaming at 16×16
- [ ] T017 [US3] Manual: Verify `IDI_APPLICATION` fallback is NOT used when error ICO is present (inspect loaded resource via Spy++ or `Resource Hacker`)

**Checkpoint**: Error icon shows after stream drop; fallback NOT triggered under normal conditions.

---

## Phase 6: User Story 4 — Connecting Animation (Priority: P2)

**Goal**: 8-frame blue spinner animation in tray during the RTSP ANNOUNCE→SETUP→RECORD handshake.

**Independent Test**: Initiate a connection on a throttled network; verify 8 animation frames cycle at ~125 ms per frame during handshake (1000 ms ±50 ms total cycle).

- [x] T018 [US4] Generate `resources/icons/airbeam_connecting_001.ico` through `_008.ico` — 3-frame each, blue speaker outline + rotating 90° arc, 1.5–1.7 KB each *(completed T004)*
- [ ] T019 [US4] Manual SC-004: Initiate connection; confirm animation cycles (all 8 frames visible) and completes a full loop in approximately 1 second
- [ ] T020 [US4] Manual: Verify animation stops immediately on connect-success (transitions to streaming icon) per spec acceptance scenario 2
- [ ] T021 [US4] Manual: Verify animation stops immediately on connect-fail (transitions to error icon) per spec acceptance scenario 3

**Checkpoint**: Connecting animation cycles at correct speed; stops cleanly on success or failure.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: Contract accuracy, SC-003 build verification, roadmap update.

- [ ] T022 [P] Update `specs/003-branded-tray-icons/contracts/ico-format.md`: correct file size threshold from `≥ 10 KB` to `≥ 1 KB` — the original estimate assumed uncompressed RGBA; PNG-compressed sparse artwork yields ~1.5–2 KB per file (still valid; well above the 680-byte placeholder baseline)
- [ ] T023 [P] SC-003: Open built `.exe` in `Resource Hacker` (or `sigcheck -i`); confirm all 11 `IDI_TRAY_*` resources are embedded with correct IDs (2001–2003, 2011–2018)
- [ ] T024 [P] Update `specs/roadmap.md`: change 003 status from `🔧 In Progress` to `✅ Complete`
- [ ] T025 Commit T006/T007/T022/T023/T024 changes with message `feat(003): complete — CTest wiring, build verified, contract corrected`

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup) ──→ Phase 2 (Build Integration) ──→ Phases 3–6 (User Stories)
                                                   └──→ Phase 7 (Polish)
```

- **Phase 1**: Already complete — no blockers
- **Phase 2**: Must complete T006/T007 before manual acceptance tests (need a working build)
- **Phases 3–6**: Parallel manual reviews after Phase 2 (single reviewer: sequential; two reviewers: parallel)
- **Phase 7**: T022–T024 can run in parallel; T025 depends on all of T022–T024

### User Story Dependencies

| Story | Depends On | Can Parallelise With |
|-------|-----------|---------------------|
| US1 (Idle) | Phase 2 done | US2, US3 |
| US2 (Streaming) | Phase 2 done | US1, US3 |
| US3 (Error) | Phase 2 done | US1, US2 |
| US4 (Connecting) | Phase 2 done | US1–US3 (different state) |

---

## Parallel Example: Phases 3–6 Manual Review

```
After Phase 2 completes (build verified):

  Reviewer Task A: US1 manual review (T009–T011)
  Reviewer Task B: US2 manual review (T013–T014)   ← can run same session, different state
  Reviewer Task C: US3 manual review (T016–T017)   ← trigger error state separately
  Reviewer Task D: US4 manual review (T019–T021)   ← requires throttled network or mock
```

---

## Implementation Strategy

### MVP First (User Story 1 + Build Integration)

1. Complete Phase 2: T006 (CTest) + T007 (build verify)
2. Complete Phase 3 manual review (US1 — idle icon)
3. **STOP and VALIDATE**: Idle icon renders at 100% and 150% DPI
4. Proceed to US2/US3 streaming + error review
5. US4 connecting animation (P2 — can defer if schedule is tight)

### Remaining Work Summary

| Phase | Tasks | Status |
|-------|-------|--------|
| Phase 1 Setup | T001–T005 | ✅ All done |
| Phase 2 Build | T006–T007 | ⏳ Pending |
| Phase 3 US1 Idle | T008–T011 | T008 done; T009–T011 manual |
| Phase 4 US2 Streaming | T012–T014 | T012 done; T013–T014 manual |
| Phase 5 US3 Error | T015–T017 | T015 done; T016–T017 manual |
| Phase 6 US4 Connecting | T018–T021 | T018 done; T019–T021 manual |
| Phase 7 Polish | T022–T025 | ⏳ Pending |

**Remaining automated tasks**: T006, T007, T022, T023, T024, T025 (6 tasks — agent-executable)  
**Manual review tasks**: T009–T011, T013–T014, T016–T017, T019–T021 (9 tasks — require running app)

---

## Notes

- `[P]` tasks = different files, no dependencies on each other
- `[US#]` label maps task to its user story for traceability
- `gen_icons.ps1` is the authoritative source for icon geometry (supersedes SVG source files per FR-006 — programmatic generation is reproducible and version-controlled)
- All 11 ICOs currently pass `validate_icons.ps1` — do not regress below 1 KB or below 2 frames
- `IDI_APPLICATION` fallback in `TrayIcon.cpp` MUST remain — do not remove (FR-008)
