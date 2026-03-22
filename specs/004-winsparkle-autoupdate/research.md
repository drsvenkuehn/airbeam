# Research: WinSparkle Auto-Update Key Pair

**Feature**: 004-winsparkle-autoupdate  
**Date**: 2026-03-22

---

## Decision 1: `winsparkle-sign` invocation syntax

**Decision**: `winsparkle-sign.exe <binary>` reads the Ed25519 private key from the `SPARKLE_PRIVATE_KEY` environment variable and prints the Base64 signature to stdout. Key generation uses `winsparkle-sign.exe --generate-keys`, which prints `Private key: <base64>` and `Public key: <base64>` to stdout.

**Rationale**: WinSparkle bundles `winsparkle-sign.exe` in its release zip (alongside `WinSparkle.dll`). The tool follows the Sparkle signing convention of reading the private key from an environment variable, which makes CI integration clean — the GitHub Actions secret maps directly to the env var.

**Alternatives considered**:
- `--private-key <path>` flag: Exists in some Sparkle variants but requires writing the key to disk temporarily, creating a window where it could be captured in runner artifacts. Env-var approach is safer.

---

## Decision 2: `sparkle:edSignature` placement in appcast.xml

**Decision**: `sparkle:edSignature` belongs on the **`<enclosure>`** element, not on `<item>`. The existing template (`.github/appcast.xml`) and release workflow Step 9 are incorrect — both place the attribute on `<item>`.

**Rationale**: WinSparkle verifies the signature of the download binary referenced by the `<enclosure>` element. The signature must be a sibling attribute of `url`, `length`, and `type` on that element. This mirrors the Sparkle for macOS specification and is confirmed by WinSparkle's own example appcasts.

**Impact**: Fix `.github/appcast.xml` template and release.yml Step 9 `$item` heredoc.

---

## Decision 3: `winsparkle-sign` download in CI

**Decision**: Add a **Step 0** in the release job to download the WinSparkle release zip (pinned to a specific version tag, e.g., `v0.8.5`), extract `winsparkle-sign.exe` and `WinSparkle.dll` to the working directory. Both the portable package (Step 5) and the signing step (Step 7) depend on this.

**Rationale**: `WinSparkle.dll` is not a build-time FetchContent dependency — it's a runtime DLL. The release workflow currently does `Copy-Item WinSparkle.dll -ErrorAction SilentlyContinue` which silently produces a broken portable package. Pinning the version ensures reproducible releases and consistent `winsparkle-sign` CLI behaviour.

**Pinned version**: `v0.8.5` (latest stable as of planning date). Stored as a workflow-level env var `WINSPARKLE_VERSION` so it can be updated in one place.

**Alternatives considered**:
- Commit `winsparkle-sign.exe` to the repo: Violates the policy of not committing binary blobs; large binary would bloat the repo.
- Use `chocolatey install winsparkle`: Not an official package; fragile.

---

## Decision 4: GitHub Pages deployment for appcast.xml

**Decision**: Use `peaceiris/actions-gh-pages@v4` to push `appcast.xml` to the `gh-pages` branch. This replaces the manual `git fetch origin gh-pages / git checkout gh-pages / git push` sequence in Step 9, which is brittle (no conflict handling, no retry, requires PAT or explicit push permissions).

**Rationale**: `peaceiris/actions-gh-pages@v4` is the standard, battle-tested action for this pattern. It handles branch creation, force-push safety, and the `github-actions[bot]` committer identity automatically. The existing `permissions: pages: write, id-token: write` in release.yml already grants the required permissions.

**Seed file**: `.github/appcast.xml` serves as the initial seed (for gh-pages branch creation). The workflow overwrites `appcast.xml` at the root of the gh-pages branch on every release.

**Alternatives considered**:
- Manual git push: Already in the existing Step 9 but brittle; requires careful error handling.
- GitHub Pages from `docs/` folder in main: Would require committing appcast.xml to main on every release, polluting commit history.

---

## Decision 5: Public key embedding — string resource vs preprocessor

**Decision**: Keep the **string resource approach** (`IDS_SPARKLE_PUBKEY` in RC locale files). The clarification said "compile-time embed via CMake preprocessor definition or generated header" but the existing implementation uses string resources, which achieves the same goal: the key is baked into the binary at compile time.

**Rationale**: String resources are already implemented and working in `SparkleIntegration.cpp`. Switching to a preprocessor define would require changing `SparkleIntegration.cpp`, CMakeLists.txt, and removing the 7 RC file entries. The current approach is correct (compile-time embedded, no runtime file read) and requires only replacing the placeholder value in RC files.

**Implication for tasks**: Replace `TODO_REPLACE_WITH_ED25519_PUBLIC_KEY` in all 7 `resources/locales/strings_*.rc` files. This is the only code change needed for FR-004.

---

## Decision 6: FR-007 signing guard pattern

**Decision**: Follow the **job-level `env:` pattern** established in spec 002 (code-signing). The `SPARKLE_PRIVATE_KEY` secret is declared in a `env:` block at the release job level, and the signing step uses `if: env.SPARKLE_PRIVATE_KEY != ''`. Step-level `env:` is evaluated AFTER `if:` guards, which would make the guard always false.

**Rationale**: This is the lesson hard-learned in spec 002 (FR-008 fix). Both the code-signing secret and the Sparkle signing secret follow the same pattern for consistency and correctness.

**Impact**: Add `SPARKLE_PRIVATE_KEY: ${{ secrets.SPARKLE_PRIVATE_KEY }}` to the job-level `env:` block; remove step-level `env:` from Step 7.
