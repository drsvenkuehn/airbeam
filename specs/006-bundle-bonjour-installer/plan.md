# Implementation Plan: Bundle Bonjour Installer

**Branch**: `006-bundle-bonjour-installer` | **Date**: 2026-03-26 | **Spec**: `specs/006-bundle-bonjour-installer/spec.md`
**Input**: Feature specification from `/specs/006-bundle-bonjour-installer/spec.md`

## Summary

Silently bundle Apple Bonjour Print Services (`BonjourPSSetup.exe`) in the AirBeam NSIS installer.
Fetch the binary at build time via a PowerShell CMake custom command that downloads from a pinned
Apple URL and verifies SHA-256. At install time, detect Bonjour presence via registry; if absent,
run the embedded installer silently under the already-elevated UAC token. Show a combined
AirBeam+Bonjour license on the existing MUI license page. On Bonjour install failure, show a
descriptive NSIS `MessageBox` then `Abort` (rolls back all installed AirBeam files).

## Technical Context

**Language/Version**: PowerShell 7+ (fetch script), NSIS 3.x (installer script), CMake 3.20+  
**Primary Dependencies**: `Invoke-WebRequest` (PS), NSIS MUI2 + LogicLib (existing), `ExecWait` (NSIS)  
**Storage**: `installer/deps/BonjourPSSetup.exe` — fetched binary, gitignored, never committed  
**Testing**: CTest (existing framework) — new `bonjour-fetch-smoke` label `unit` test  
**Target Platform**: Windows installer build environment (local + CI, `windows-latest`)  
**Project Type**: Build tooling + installer enhancement  
**Performance Goals**: Bonjour install step ≤ 15 s on typical machine (SC-002)  
**Constraints**: Installer size increase ≤ 5 MB (SC-006); fetch script must work offline-clean by caching verified binary

## Constitution Check

| Principle | Status | Notes |
|-----------|--------|-------|
| §I Real-Time Audio Thread Safety | ✅ N/A | No audio thread changes |
| §II AirPlay 1 / RAOP Protocol Fidelity | ✅ N/A | No protocol changes |
| §III Native Win32, No External UI Frameworks | ✅ N/A | No app UI changes |
| §III-A Visual Color Palette | ✅ N/A | No icon changes |
| §IV Test-Verified Correctness | ✅ Partial | Installer flow tested manually on clean VM; CTest smoke test verifies fetch script produces verified binary. Full automated installer test out of scope (NSIS VM harness not in project). |
| §V Observable Failures — Never Silent | ✅ | FR-006: `MessageBox` with exit code shown before `Abort`. Runtime balloon (feature 005) covers post-install Bonjour removal. |
| §VI Strict Scope Discipline | ✅ | Installer enhancement is in scope |
| §VII MIT-Compatible Licensing | ✅ | Apple Bonjour redistribution terms permit bundling; both licenses shown on license page (FR-005) |
| §VIII Localizable UI | ⚠️ Accepted | NSIS installer uses its own English-only MUI language layer, separate from Win32 `.rc` locale files. Existing `AirBeam.nsi` already has English-only strings. NSIS-side localization is deferred — documented limitation, not a new violation. |

## Project Structure

### Documentation (this feature)

```text
specs/006-bundle-bonjour-installer/
├── plan.md              ← this file
├── research.md          ← Phase 0 output
├── data-model.md        ← Phase 1 output
├── quickstart.md        ← Phase 1 output
└── tasks.md             ← Phase 2 output (/speckit.tasks — not created by /speckit.plan)
```

### Source Code (changes for this feature)

```text
installer/
├── AirBeam.nsi                       # modified: license page, detection, embed, silent install, rollback
├── deps/
│   ├── fetch-bonjour.ps1             # new: CMake custom command — download + SHA-256 verify
│   └── BonjourPSSetup.exe            # fetched at build time — GITIGNORED
└── licenses/
    ├── combined-license.txt          # new: AirBeam MIT + Apple Bonjour SDK (static, committed)
    └── bonjour-sdk-license.txt       # new: Apple Bonjour SDK license text (reference copy)

CMakeLists.txt                        # modified: add fetch-bonjour custom_target
.gitignore                            # modified: add installer/deps/BonjourPSSetup.exe
tests/CMakeLists.txt                  # modified: add bonjour-fetch-smoke CTest
```

**Structure Decision**: Single-project layout. All changes are in `installer/` and build system
files. No new source directories required.

## Complexity Tracking

No constitution violations requiring justification.