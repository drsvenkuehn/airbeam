# Tasks: CI/Build Hardening

**Input**: Design documents from `specs/002-ci-build-hardening/`
**Plan**: plan.md · **Spec**: spec.md · **data-model**: N/A · **contracts**: N/A
**Tests**: Not requested — this feature is CI/YAML/CMake configuration only, no test tasks generated.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no incomplete-task dependencies)
- **[Story]**: User story this task belongs to (US1–US4)
- Exact file paths in every description

---

## Phase 1: Setup — Verify File State

**Purpose**: Confirm current file structure matches the plan before making surgical edits.
Plan was authored from static analysis; verify line numbers and structure are still accurate.

- [x] T001 Read `CMakeLists.txt` lines 38–45, `CMakePresets.json` (full), and `.github/workflows/release.yml` lines 50–75 and 130–145 to confirm ALAC `GIT_TAG` value, presence of `base` preset with absolute MSVC paths, codesign step position, and Step 10 `publish_dir` TODO comment all match the plan

---

## Phase 2: Foundational — CI-Compatible CMake Presets + Constitution Amendment

**Purpose**: `CMakePresets.json` hardcodes absolute local MSVC paths that do not exist on GitHub runners.
A `base-ci` hidden preset and `msvc-x64-debug-ci` presets must exist **before** `ci.yml` can reference them.
The constitution §CI/CD policy must also be amended **in this same phase** — creating `ci.yml` (Phase 4) while the prohibition is still in effect would leave the repo non-compliant, even temporarily.

**⚠️ CRITICAL**: `ci.yml` (Phase 4 / US2) cannot be written until T002, T003, and T010 are complete — preset names and constitution alignment must both be confirmed first.

- [x] T002 [P] Add a `hidden: true` preset named `base-ci` to `CMakePresets.json` that inherits `"base"` and overrides: `CMAKE_C_COMPILER` → `"cl.exe"`, `CMAKE_CXX_COMPILER` → `"cl.exe"`, `CMAKE_LINKER` → `"link.exe"`, `CMAKE_RC_COMPILER` → `"rc.exe"`, `CMAKE_MT` → `"mt.exe"`; remove the `PATH`, `LIB`, and `INCLUDE` `environment` entries inherited from `base` by setting them to `null` (environment provided by `ilammy/msvc-dev-cmd` in the workflow)
- [x] T003 Add three presets to `CMakePresets.json` that inherit `"base-ci"`: (1) configure preset `"msvc-x64-debug-ci"` with `binaryDir: "${sourceDir}/build/msvc-x64-debug-ci"` and `CMAKE_BUILD_TYPE: Debug`; (2) build preset `"msvc-x64-debug-ci"` referencing the configure preset; (3) test preset `"msvc-x64-debug-ci"` referencing the configure preset with `filter: { label: { include: "unit" } }` and `output: { outputOnFailure: true }`
- [x] T010 [P] Amend `§CI/CD & Release Pipeline` in `.specify/memory/constitution.md` to replace the prohibition on PR/push triggers with the new policy: debug-only CI runs on non-`main` branch pushes and PRs targeting `main` are permitted; release builds remain tag-triggered via `release.yml`; all CI jobs must clean up build output on `always()` — **this task MUST be completed before T005 (ci.yml creation)** to maintain constitutional compliance

**Checkpoint**: `cmake --list-presets` shows `msvc-x64-debug-ci` configure, build, and test presets. Constitution amended. Foundation ready for ci.yml.

---

## Phase 3: User Story 1 — Reproducible ALAC Build (Priority: P1) 🎯 MVP

**Goal**: Every build fetches ALAC at an exact, documented commit SHA — not a mutable `master` branch tip.

**Independent Test**: Delete FetchContent cache (`build/` dir), run `cmake --preset msvc-x64-debug`, then run `git -C build/msvc-x64-debug/_deps/alac-src rev-parse HEAD` and verify it equals `c38887c5c5e64a4b31108733bd79ca9b2496d987`.

- [x] T004 [P] [US1] In `CMakeLists.txt` line 41: replace `GIT_TAG master` with `GIT_TAG c38887c5c5e64a4b31108733bd79ca9b2496d987` and add an inline comment on the same or next line: `# pinned 2016-05-11 (last commit on master; no tags exist on this repo — mirror: https://github.com/macosforge/alac)`

**Checkpoint**: `CMakeLists.txt` no longer references `master`. User Story 1 is complete and independently testable.

---

## Phase 4: User Story 2 — PR/Push CI Workflow (Priority: P1)

**Goal**: Every push to a non-`main` branch and every PR update against `main` automatically builds the debug preset and runs unit tests on a Windows runner.

**Independent Test**: Open a draft PR against `main`; within ~5 minutes the `build-and-test` check appears on the PR. Introduce a deliberate compile error; confirm the check goes red and blocks merge.

**Depends on**: Phase 2 (T002 + T003 + T010) — preset names `msvc-x64-debug-ci` must be committed and constitution amendment must be in place before ci.yml is created.

- [x] T005 [US2] Create `.github/workflows/ci.yml` with the following complete structure:
  - **Triggers**: `on: push` with `branches-ignore: [main]`; `on: pull_request` with `branches: [main]`
  - **Job id**: `build-and-test` on `windows-latest`
  - **Steps in order**:
    1. `actions/checkout@v4`
    2. `ilammy/msvc-dev-cmd@v1` with `arch: x64`
    3. `actions/cache/restore@v4` (restore only) with `path: build/msvc-x64-debug-ci`, `key: ${{ runner.os }}-cmake-${{ github.ref }}-${{ hashFiles('CMakeLists.txt','CMakePresets.json') }}`, and `restore-keys: ${{ runner.os }}-cmake-${{ github.ref }}-`; capture `id: cache-restore`
    4. `cmake --preset msvc-x64-debug-ci`
    5. `cmake --build --preset msvc-x64-debug-ci`
    6. `ctest --preset msvc-x64-debug-ci` (label filter and output-on-failure already set in the test preset; label filter is `unit`)
    7. `actions/cache/save@v4` (explicit save, runs before cleanup) with `path: build/msvc-x64-debug-ci` and same key as step 3; guard with `if: steps.cache-restore.outputs.cache-hit != 'true'` to avoid redundant saves
    8. Cleanup step with `if: always()` that runs `Remove-Item -Recurse -Force build/msvc-x64-debug-ci -ErrorAction SilentlyContinue`
  - **FR-010 note**: The signing step in `release.yml` MUST NOT have `continue-on-error: true`; PowerShell propagates `signtool` exit codes natively — verify no suppression exists when editing T007

**Checkpoint**: Pushing to any non-`main` branch triggers the workflow. Unit tests run and results appear as a PR status check.

---

## Phase 5: User Story 3 — Appcast Publish Directory (Priority: P2)

**Goal**: After tagging a release, `appcast.xml` on the `gh-pages` branch is updated correctly so WinSparkle detects new releases.

**Independent Test**: Inspect `release.yml` — Step 10 (`peaceiris/actions-gh-pages@v4`) is absent. Step 9 (the git-based gh-pages push) is the sole appcast deploy mechanism, and a `docs/` directory does NOT need to exist.

- [x] T006 [P] [US3] In `.github/workflows/release.yml`: (1) remove the entire Step 10 block (`peaceiris/actions-gh-pages@v4` with `publish_dir: ./docs # TODO: adjust to appcast output dir`) — Step 9 already handles `gh-pages` via direct `git push`; Step 10 is dead scaffolding that would overwrite the appcast from a nonexistent directory; (2) read `CMakeLists.txt` lines 29–31 to find `AIRBEAM_APPCAST_URL` and verify that Step 9's `git checkout gh-pages` commit path deploys `appcast.xml` to the root of `gh-pages` (or subdirectory) that matches the URL path component — document alignment or flag a mismatch (FR-007)

**Checkpoint**: `release.yml` contains no `peaceiris/actions-gh-pages` reference. User Story 3 is complete.

---

## Phase 6: User Story 4 — Guard Code-Signing Secret (Priority: P2)

**Goal**: A fork without `CODESIGN_PFX` configured can run the release workflow and produce a valid unsigned installer, not a cryptic failure. The main repo signs correctly when the secret is present.

**Independent Test**: Confirm `CODESIGN_PFX` and `CODESIGN_PASSWORD` appear in the **job-level** `env:` block (not inside the signing step). Confirm a `::warning::` step exists immediately before the signing step guarded by `if: env.CODESIGN_PFX == ''`. Confirm the signing step retains `if: env.CODESIGN_PFX != ''`.

**Sequential after T006**: Both T007 and T008 modify `release.yml`; apply them after T006 to avoid conflicts.

- [x] T007 [US4] In `.github/workflows/release.yml`: move `CODESIGN_PFX: ${{ secrets.CODESIGN_PFX }}` and `CODESIGN_PASSWORD: ${{ secrets.CODESIGN_PASSWORD }}` from the step-level `env:` block inside the signing step to the **job-level `env:`** block (if no job-level env block exists, create one); the existing `if: env.CODESIGN_PFX != ''` condition on the signing step will now evaluate correctly from the job environment
- [x] T008 [US4] In `.github/workflows/release.yml`: insert a new step immediately **before** the codesign step with `name: Warn — code signing skipped`, `if: env.CODESIGN_PFX == ''`, and `run: Write-Host "::warning::Code signing skipped — CODESIGN_PFX secret not set; installer will be unsigned"` so the release author sees an annotation when signing is bypassed

**Checkpoint**: `release.yml` job-level env contains codesign secrets. Signing step guard evaluates from job env. Warning annotation step present. User Story 4 is complete.

---

## Phase 7: Polish & Cross-Cutting Concerns

**Purpose**: One-time post-merge operation — branch protection must be applied after the CI workflow is live on GitHub.

- [x] T009 Configure branch protection on `main` via `gh` CLI (run once after the PR for this feature is merged): require status check `build-and-test`, set `strict: true`, set `enforce_admins: true`, set `required_pull_request_reviews: null`, set `restrictions: null` — exact command is in `specs/002-ci-build-hardening/plan.md` §quickstart

---

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup)**: No dependencies — start immediately
- **Phase 2 (Foundational)**: Depends on Phase 1 verification — **BLOCKS Phase 4 (US2)**; includes T010 (constitution amendment) which must precede T005
- **Phase 3 (US1)**: Independent of Phase 2 — can run in parallel with Phase 2
- **Phase 4 (US2)**: Depends on Phase 2 completion (preset names must exist)
- **Phase 5 (US3)**: Independent of all other stories — can run in parallel with Phase 3 and 4
- **Phase 6 (US4)**: Depends on Phase 5 (both modify `release.yml` — apply sequentially)
- **Phase 7 (Polish)**: T009 depends on Phase 4 being merged and the workflow live on GitHub; T010 has moved to Phase 2

### User Story Dependencies

- **US1 (P1)**: No dependencies on other stories — independent `CMakeLists.txt` change
- **US2 (P1)**: Depends on Foundational (Phase 2) presets — no dependency on US1, US3, or US4
- **US3 (P2)**: No dependencies — isolated `release.yml` Step 10 removal
- **US4 (P2)**: Must apply after US3 to avoid `release.yml` edit conflicts

### Parallel Opportunities

```
T001 (verify)
  │
  ├─► T002 [P] (base-ci preset)         T004 [P] [US1] (pin ALAC SHA)
  │   T010 [P] (constitution amendment) ← must complete before T005
  │         │
  │       T003 (msvc-x64-debug-ci presets)
  │         │
  └─────────┴──► T005 [US2] (ci.yml)    T006 [P] [US3] (remove Step 10 + FR-007 check)
                                                   │
                                                 T007 [US4] (move codesign env + FR-010 verify)
                                                   │
                                                 T008 [US4] (add warning step)
                                                   │
                                        T009 (branch protection — post-merge)
```

---

## Implementation Strategy

### MVP First (Regression Safety — US1 + US2)

1. Complete Phase 1: Verify file state
2. Complete Phase 2: Add CI-compatible presets (blocks nothing else while US1 runs)
3. Complete Phase 3: Pin ALAC SHA → delivers reproducible builds immediately
4. Complete Phase 4: Create `ci.yml` → delivers PR regression detection
5. **STOP and VALIDATE**: Push a test branch, confirm `build-and-test` check appears
6. US1 + US2 together constitute the MVP — safe to review/merge at this point

### Incremental Delivery

1. Setup + Foundational → presets committed
2. US1 + US2 → reproducible builds + PR CI (MVP)
3. US3 → appcast deploy fixed (can merge independently)
4. US4 → codesign guard fixed (depends on US3 for release.yml sequencing)
5. Polish → branch protection enforced (T009, post-merge)

---

## Notes

- No test tasks generated — all changes are YAML/CMake configuration; correctness is verified by running the workflows and CMake itself
- T002, T004, and T010 can be done in parallel (different files: CMakePresets.json, CMakeLists.txt, constitution.md)
- T005 and T006 can be done in parallel (different files: ci.yml vs release.yml)
- T010 (constitution amendment) is in Phase 2 and MUST be committed before T005 (ci.yml) — they should land in the same PR
- T009 (branch protection) requires the CI workflow to be live on GitHub; it cannot be applied before the PR is merged
- The `base` preset in `CMakePresets.json` MUST NOT be modified — it is the developer local build preset; `base-ci` inherits from it and only overrides what CI needs different
- ALAC SHA `c38887c5c5e64a4b31108733bd79ca9b2496d987` is the only commit on master; the repo has been frozen since 2016-05-11 — no tags exist
