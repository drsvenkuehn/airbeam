# Tasks: WinSparkle Auto-Update Key Pair

**Input**: `specs/004-winsparkle-autoupdate/`  
**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)  
**Total tasks**: 22 | **US1**: 5 | **US2**: 10 | **US3**: 2 | **Setup/Polish**: 5

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: User story label (US1 / US2 / US3)
- No C++ source changes required — `SparkleIntegration.cpp` is complete

---

## Phase 1: Setup — Download Tooling

**Purpose**: Make `winsparkle-sign.exe` and `WinSparkle.dll` available locally for key generation.

- [ ] T001 Download WinSparkle 0.8.5 release zip from `https://github.com/vslavik/winsparkle/releases/download/v0.8.5/WinSparkle-0.8.5.zip`; extract to a local working directory; locate `winsparkle-sign.exe` and `WinSparkle.dll` (see `quickstart.md` Step 1)

---

## Phase 2: Foundational — Key Generation & Public Key Embedding

**Purpose**: Generate the real Ed25519 key pair and embed the public key into the binary. Must complete before any US2/US1 pipeline work is testable.

**Independent test**: `resources/locales/strings_en.rc` contains no `TODO_REPLACE_WITH_ED25519_PUBLIC_KEY`; key length ≥ 43 chars (SC-005).

- [ ] T002 Run `winsparkle-sign.exe --generate-keys`; record `Private key:` and `Public key:` Base64 values from stdout — do NOT write private key to any file (see `quickstart.md` Step 2)
- [ ] T003 Add `SPARKLE_PRIVATE_KEY` repository secret at `https://github.com/drsvenkuehn/airbeam/settings/secrets/actions` using the private key from T002; confirm secret appears in the Actions secrets list (see `quickstart.md` Step 3)
- [ ] T004 Replace `TODO_REPLACE_WITH_ED25519_PUBLIC_KEY` with the real Base64 public key in all 7 `resources/locales/strings_*.rc` files (`strings_en.rc`, `strings_de.rc`, `strings_fr.rc`, `strings_es.rc`, `strings_ja.rc`, `strings_ko.rc`, `strings_zh-Hans.rc`) (see `quickstart.md` Step 4)
- [ ] T005 Update `resources/sparkle_pubkey.txt`: replace the `Current value: placeholder` line with the real Base64 public key and add `Generated: <DATE>`; update the TODO comment to note replacement is complete

---

## Phase 3: US2 — Release Pipeline Signs Artifacts

**Purpose**: Fix `release.yml` so that every `v*` tag produces a correctly signed installer and publishes an `appcast.xml` with the `sparkle:edSignature` attribute on the `<enclosure>` element to GitHub Pages.

**Independent test**: Push test tag → release workflow completes → `appcast.xml` on `gh-pages` contains non-empty `sparkle:edSignature` on `<enclosure>` → `winsparkle-sign --verify` exits `0` (SC-001, SC-004).

- [ ] T006 [US2] Add `WINSPARKLE_VERSION: "0.8.5"` and `SPARKLE_PRIVATE_KEY: ${{ secrets.SPARKLE_PRIVATE_KEY }}` to the job-level `env:` block in `.github/workflows/release.yml` (alongside existing `CODESIGN_PFX` / `CODESIGN_PASSWORD` entries)
- [ ] T007 [US2] Add a new "Download WinSparkle binaries" step as Step 1 in `.github/workflows/release.yml` (before Configure CMake): download `WinSparkle-${{ env.WINSPARKLE_VERSION }}.zip` from GitHub releases, extract `winsparkle-sign.exe` and `WinSparkle.dll`, copy both to `$PWD`, add `$PWD` to `$GITHUB_PATH` — see `plan.md` Phase 3 snippet
- [ ] T008 [US2] Fix Step 5 "Create portable ZIP" in `.github/workflows/release.yml`: remove `-ErrorAction SilentlyContinue` from `Copy-Item WinSparkle.dll` line (DLL is now guaranteed by T007; silent failure mask removed)
- [ ] T009 [US2] Add "Warn — EdDSA signing skipped" step immediately before the existing Step 7 in `.github/workflows/release.yml`: `if: env.SPARKLE_PRIVATE_KEY == ''`; emit `::warning::EdDSA signing skipped — SPARKLE_PRIVATE_KEY secret not set; update will be unsigned` (FR-008)
- [ ] T010 [US2] Fix Step 7 "Sign installer with winsparkle-sign" in `.github/workflows/release.yml`: (a) change `if:` to `if: env.SPARKLE_PRIVATE_KEY != ''`; (b) remove step-level `env: SPARKLE_PRIVATE_KEY:` block (secret already at job level from T006); (c) verify signing invocation correctly reads `$env:SPARKLE_PRIVATE_KEY` and captures stdout signature to `$env:GITHUB_ENV` (FR-005, FR-007)
- [ ] T011 [US2] Fix `.github/appcast.xml` seed template: (a) move `sparkle:edSignature` from child element of `<item>` to attribute of `<enclosure>`; (b) replace all `TODO_ORG` with `drsvenkuehn`; (c) update installer URL filename to `AirBeam-v1.0.0-Setup.exe` (with `v` prefix) — see `contracts/appcast-schema.md`
- [ ] T012 [US2] Fix Step 9 "Update appcast.xml on gh-pages" in `.github/workflows/release.yml`: (a) update `$item` heredoc — move `<sparkle:edSignature>$sig</sparkle:edSignature>` from `<item>` body to `sparkle:edSignature="$sig"` attribute on `<enclosure>`; (b) replace all `TODO_ORG` placeholder URLs in the step with `drsvenkuehn`; (c) replace manual `git checkout gh-pages / git push` block with `peaceiris/actions-gh-pages@v4` action call
- [ ] T013 [US2] Add `peaceiris/actions-gh-pages@v4` call at the end of Step 9 in `.github/workflows/release.yml`: configure `github_token`, `publish_branch: gh-pages`, `publish_dir: ./gh-pages-out`, write updated `appcast.xml` to `gh-pages-out/` before calling the action — replaces current manual git push (research.md Decision 4)
- [ ] T014 [US2] Verify the job-level `env:` block in `.github/workflows/release.yml` now contains all three secrets (`CODESIGN_PFX`, `CODESIGN_PASSWORD`, `SPARKLE_PRIVATE_KEY`) and that no signing step references `${{ secrets.* }}` directly inside a step-level `env:` — apply spec 002 lesson (research.md Decision 6)
- [ ] T015 [US2] Build the project in Release mode (`cmake --preset msvc-x64-release && cmake --build --preset msvc-x64-release`) to confirm the new public key compiles correctly with no errors; confirm no `TODO_REPLACE` string appears in the binary with `Select-String "TODO_REPLACE" build\Release\AirBeam.exe`

---

## Phase 4: US1 — Auto-Update Detected and Verified

**Purpose**: Stand up GitHub Pages so WinSparkle can reach the appcast, and validate end-to-end signing and verification locally.

**Independent test**: `winsparkle-sign --verify <installer> --pubkey <pubkey>` exits `0`; a WinSparkle-enabled AirBeam build shows the update dialog when pointed at a crafted appcast (SC-001, SC-002, SC-005).

- [ ] T016 [US1] Create the `gh-pages` orphan branch with the corrected `appcast.xml` seed: `git checkout --orphan gh-pages`, remove all tracked files, copy `.github/appcast.xml` to `appcast.xml` at repo root, commit as `chore: initialize gh-pages with appcast.xml seed`, push to `origin gh-pages` — see `plan.md` Phase 6
- [ ] T017 [US1] Enable GitHub Pages on `drsvenkuehn/airbeam`: `Settings → Pages → Source: Deploy from branch → gh-pages / (root)` — confirm the Pages URL resolves to `https://drsvenkuehn.github.io/airbeam/appcast.xml` (FR-009)
- [ ] T018 [US1] Validate SC-001 locally: sign a debug build installer with `winsparkle-sign.exe build\Debug\AirBeam.exe` (using `$env:SPARKLE_PRIVATE_KEY` set to the real private key), then run `winsparkle-sign.exe --verify build\Debug\AirBeam.exe --pubkey <BASE64_PUBKEY>` and confirm exit code `0`
- [ ] T019 [US1] Validate SC-005: confirm `resources/sparkle_pubkey.txt` and all 7 RC files contain no placeholder string; check key length ≥ 43 chars: `(Select-String "TODO_REPLACE" resources\locales\strings_en.rc) -eq $null`
- [ ] T020 [US1] Validate SC-003 (tamper test): corrupt the `sparkle:edSignature` in a local copy of `appcast.xml`; point WinSparkle to that URL; confirm WinSparkle rejects the update with an error (does NOT install) — manual test, document result

---

## Phase 5: US3 — Key Pair Rotation Documentation

**Purpose**: Document the complete key generation and rotation procedure so it can be performed correctly under pressure.

**Independent test**: `docs/release-process.md` exists; contains sections for initial key generation, routine release signing, and emergency key rotation (FR-010).

- [ ] T021 [P] [US3] Create `docs/release-process.md` with three sections: (1) **Initial Key Generation** — steps from `quickstart.md` reformatted as a release checklist; (2) **Signing a Release** — what the CI pipeline does automatically + manual override if CI signing is skipped; (3) **Emergency Key Rotation** — step-by-step: delete secret → generate new pair → update RC files → rebuild → re-release with new key (FR-010)
- [ ] T022 [P] [US3] Add GitHub Pages setup checklist to `docs/release-process.md`: enable Pages, verify `appcast.xml` URL resolves, update `AIRBEAM_APPCAST_URL` if repo is ever transferred to a different org/user (FR-009 maintenance note)

---

## Dependencies

```
T001 → T002 → T003
             T002 → T004 → T015
             T002 → T005
T006 ─────────────────────→ T007 → T008
                                    T009 (parallel with T010)
                                    T010
T011 ─────────────────────→ T012 → T013 → T014
T004, T011, T013 → T015 → T016 → T017 → T018 → T019 → T020
T021 ──────────────────────────────────────────────────────── (independent)
T022 ──────────────────────────────────────────────────────── (independent)
```

## Parallel Execution

```
After T002 (keys generated):
  ├── T003  (add GitHub secret)
  ├── T004  (replace pubkey in all 7 RC files)
  └── T005  (update sparkle_pubkey.txt)

After T006 (job-level env updated):
  ├── T007  (download step)
  └── T011  (fix appcast.xml template)

T021 + T022 (docs): fully independent, can run any time
```

## Implementation Strategy

**MVP (US2 + US1 prerequisite)**: Complete Phases 1–2 (T001–T005) + T006–T015. This produces a release pipeline that correctly signs installers. US1 validation follows automatically.

**Full feature**: All 22 tasks including gh-pages live deployment (T016–T017) and documentation (T021–T022).

**Verification order**: T015 (build OK) → T016 (gh-pages branch) → T017 (GitHub Pages enabled) → T018 (local sign/verify) → T019 (SC-005) → T020 (SC-003 tamper test).
