# Feature Specification: CI/Build Hardening

**Feature Branch**: `002-ci-build-hardening`  
**Created**: 2025-07-10  
**Status**: Draft  

## Clarifications

### Session 2026-03-22

- Q: Does CI trigger on pushes to all branches (including `main`) or only non-`main` branches + PRs targeting `main`? → A: Non-`main` branch pushes + PRs targeting `main` only; direct pushes to `main` are permitted (solo project).
- Q: Does CI need to install prerequisites (CMake/Ninja/MSVC) or use what's pre-installed on the runner? → A: Use pre-installed tools on `windows-latest` (CMake, Ninja, MSVC v143 are already present — no install step). CI jobs MUST clean up after themselves (delete build output and cache entries on completion).
- Q: What cache scope for the CMake build directory? → A: Per-branch cache key (`${{ github.ref }}`); cache is isolated per branch and auto-evicted when the branch is deleted.
- Q: Is configuring branch protection rules on `main` in scope for this feature? → A: **No — explicitly out of scope.** Solo project; direct pushes to `main` are the normal workflow. Branch protection adds PR-gate friction with no benefit. Re-evaluate if collaborators join.
- Q: Should CI build debug only or also release preset? → A: `msvc-x64-debug` only; release builds are covered by the tag-triggered `release.yml` pipeline.

---

## Summary

Four related build and CI hardening changes that must land before the first public release:

| # | Item | File |
|---|------|------|
| A | Pin ALAC `GIT_TAG master` to a reproducible commit SHA | `CMakeLists.txt:40` |
| B | Add `.github/workflows/ci.yml` for build-on-push / PR | `.github/workflows/` |
| C | Fix appcast `publish_dir` TODO in `release.yml:138` | `.github/workflows/release.yml` |
| D | Guard code-signing step with `CODESIGN_PFX` secret check | `.github/workflows/release.yml:54` |

---

## User Scenarios & Testing

### User Story 1 - Reproducible ALAC Build (Priority: P1)

A developer clones the repo six months from now, runs `cmake --preset msvc-x64-debug`, and gets an identical ALAC encoder library — even if the upstream `alac` repo has had new commits.

**Why this priority**: `GIT_TAG master` can silently break the build or produce a different binary when upstream commits land. This is a hard reproducibility blocker.

**Independent Test**: Delete the FetchContent cache, configure and build; verify the fetched ALAC commit SHA matches the pinned value in `CMakeLists.txt`.

**Acceptance Scenarios**:

1. **Given** a clean FetchContent cache, **When** `cmake --preset msvc-x64-debug` runs, **Then** ALAC is fetched at the pinned commit SHA (not `HEAD` of master).
2. **Given** a new commit lands on the upstream `alac` master branch, **When** CI builds AirBeam, **Then** the pinned SHA is still used and the build is unaffected.
3. **Given** the ALAC pinned SHA is changed in `CMakeLists.txt`, **When** a developer updates, **Then** only the explicitly changed SHA is fetched.

---

### User Story 2 - PR/Push CI Workflow (Priority: P1)

A contributor opens a pull request; within a few minutes they see a green check confirming their change builds and all unit tests pass on Windows — without needing to run anything locally.

**Why this priority**: The only existing workflow (`release.yml`) triggers on `v*` tags. A regression introduced on any PR branch will only be caught at release time — too late.

**Independent Test**: Open a draft PR against `main`; verify the `ci` workflow starts automatically, builds `msvc-x64-debug` preset, and runs `ctest -L unit`.

**Acceptance Scenarios**:

1. **Given** a push to any non-`main` branch, **When** the push lands on GitHub, **Then** the `ci` workflow starts automatically.
2. **Given** a pull request opened against `main`, **When** the PR is created or updated, **Then** the `ci` workflow runs and its result is visible as a status check (not a required gate — branch protection is out of scope for v1.0).
3. **Given** a unit-test failure (e.g., a broken `SpscRingBuffer`), **When** CI runs, **Then** the workflow exits with a non-zero code and the PR check is marked failing.
4. **Given** all tests pass, **When** CI completes, **Then** the workflow exits `0` and the PR check is marked passing.
5. **Given** a direct push to `main`, **When** it lands, **Then** CI does NOT trigger (intentional — direct pushes to main are the normal solo workflow).

---

### User Story 3 - Appcast Publish Directory (Priority: P2)

After tagging a release, the GitHub Pages appcast at `https://<org>.github.io/airbeam/appcast.xml` is updated automatically so that existing users receive an in-app update notification.

**Why this priority**: `release.yml:138` has an explicit `# TODO: adjust to appcast output dir` comment. Without the correct path, WinSparkle will never detect new releases.

**Independent Test**: Tag a test release, observe the `release` workflow; verify the appcast file is deployed to the correct subdirectory on the `gh-pages` branch.

**Acceptance Scenarios**:

1. **Given** a `v*` tag is pushed, **When** the release workflow completes, **Then** `appcast.xml` is written to the root of the `gh-pages` branch (or the subdirectory configured in `AIRBEAM_APPCAST_URL`).
2. **Given** an existing `gh-pages` branch with prior appcast entries, **When** a new release is published, **Then** the appcast is updated (not wiped) so previous versions remain listed.

---

### User Story 4 - Guard Code-Signing Secret (Priority: P2)

A fork maintainer who has not configured `CODESIGN_PFX` can still run the release workflow — it skips signing and produces an unsigned installer rather than failing with a cryptic secret-not-found error.

**Why this priority**: As written, `release.yml:54` reads `CODESIGN_PASSWORD` unconditionally. Forks used for testing or CI experiments will fail at the signing step even though the code itself is correct.

**Independent Test**: Fork the repo (no `CODESIGN_PFX` secret), push a `v9.9.9` test tag; verify the workflow completes and produces an unsigned installer, not a workflow error.

**Acceptance Scenarios**:

1. **Given** a fork with no `CODESIGN_PFX` secret configured, **When** the release workflow runs, **Then** the signing step is skipped and the workflow continues.
2. **Given** a fork with no `CODESIGN_PFX` secret, **When** the installer is produced, **Then** it is uploaded as an artifact and the installer file is present (unsigned).
3. **Given** the main repo with `CODESIGN_PFX` configured, **When** the release workflow runs, **Then** the signing step executes normally and the installer is Authenticode-signed.
4. **Given** `CODESIGN_PFX` is set but `CODESIGN_PASSWORD` is wrong, **When** signing runs, **Then** the workflow fails with a clear error (not silently produces an unsigned binary).

---

### Edge Cases

- What if the pinned ALAC SHA is later force-pushed or the repo is deleted? → Document in `CMakeLists.txt` comment with a backup mirror URL.
- What if CI runs on a Windows runner that doesn't have Strawberry Perl / CMake in `PATH`? → Not applicable: `windows-latest` ships with CMake, Ninja, and MSVC v143 pre-installed. No install step is needed.
- What if two release tags are pushed in quick succession? → Each workflow run is independent; the second appcast deploy overwrites the first (acceptable — only the latest is needed).
- What if the code-signing step skips but the job still reports success? → Ensure a warning annotation is emitted so the release author notices the binary is unsigned.

---

## Requirements

### Functional Requirements

- **FR-001**: `CMakeLists.txt` MUST specify `GIT_TAG` for the ALAC dependency as a full 40-character commit SHA (not a branch name or `master`).
- **FR-002**: The pinned ALAC commit MUST be documented with a comment indicating the equivalent upstream tag/date.
- **FR-003**: A new workflow file `.github/workflows/ci.yml` MUST trigger on `push` to all branches **except `main`** and on `pull_request` events targeting `main`. Direct pushes to `main` are permitted (solo workflow); branch protection is explicitly out of scope.
- **FR-004**: The CI workflow MUST configure the `msvc-x64-debug-ci` CMake preset only (the CI-specific preset added alongside `base-ci`; not the developer `msvc-x64-debug` preset) and run `ctest --preset msvc-x64-debug-ci`. The `msvc-x64-release` preset is intentionally excluded from CI — it is built by the tag-triggered `release.yml` pipeline.
- **FR-005**: The CI workflow MUST use a `windows-latest` GitHub-hosted runner. CMake, Ninja, and MSVC v143 are pre-installed — no install step required. The workflow MUST cache the CMake build directory using `actions/cache/restore` and `actions/cache/save` (explicit split) with key `${{ runner.os }}-cmake-${{ github.ref }}-${{ hashFiles('CMakeLists.txt','CMakePresets.json') }}` so caches are isolated per branch, invalidated on CMake file changes, and not poisoned across OS types.
- **FR-005a**: Every CI job MUST include a cleanup step (runs on `always()`) that deletes the build output directory, ensuring runners are not left with stale artifacts. Cache key eviction relies on GitHub's branch-scoped TTL (7-day / 10 GB platform limit); no active API eviction step is required.
- **FR-006**: `release.yml` line 138 `publish_dir` value MUST be set to the directory that contains the generated `appcast.xml` (not a placeholder TODO comment).
- **FR-007**: The `publish_dir` value MUST match the path implied by `AIRBEAM_APPCAST_URL` in `CMakeLists.txt`.
- **FR-008**: The `CODESIGN_PFX` and `CODESIGN_PASSWORD` secrets MUST be declared in the **job-level `env:`** block (not step-level), and the code-signing step MUST be guarded with `if: env.CODESIGN_PFX != ''` so the condition evaluates from the job environment (step-level `env:` is populated after `if:` expressions are evaluated, making a step-level guard always false).
- **FR-009**: When signing is skipped, the workflow MUST emit a `::warning::` annotation ("Code signing skipped — CODESIGN_PFX secret not set") so the release author is notified.
- **FR-010**: When signing is present, the step MUST still fail loudly if `signtool` returns non-zero (no silent failure).
- ~~**FR-011**~~: _(Removed)_ Branch protection is explicitly out of scope for v1.0 — solo project, direct pushes to `main` are the normal workflow.

### Key Entities

- **`CMakeLists.txt:40`**: The `FetchContent_Declare(alac ...)` block where `GIT_TAG` must be changed from `master` to a pinned SHA.
- **`.github/workflows/ci.yml`**: New workflow file for PR/push CI.
- **`.github/workflows/release.yml:54`**: The code-signing step needing the `if:` guard.
- **`.github/workflows/release.yml:138`**: The `publish_dir` field for the appcast deployment action.

---

## Success Criteria

### Measurable Outcomes

- **SC-001**: `cmake --configure` with a clean FetchContent cache fetches ALAC at exactly the pinned SHA — verified by `git -C <fetchcontent-dir> rev-parse HEAD`.
- **SC-002**: Every push to a non-`main` branch (and every PR update against `main`) triggers a CI run that completes in under 15 minutes on a `windows-latest` runner.
- **SC-003**: A PR with a deliberately broken unit test shows a failing required status check blocking merge.
- **SC-004**: A fork with no `CODESIGN_PFX` produces a complete (unsigned) installer artifact without any workflow error exit code.
- **SC-005**: After a tagged release, `https://<org>.github.io/airbeam/appcast.xml` is reachable and contains the new version within 5 minutes of the workflow completing.
