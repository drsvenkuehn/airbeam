# Feature Specification: Bundle Bonjour Installer

**Feature Branch**: `006-bundle-bonjour-installer`
**Created**: 2026-03-25
**Status**: Complete
**Input**: User description: "Bundle Bonjour MSI in the AirBeam NSIS installer so that when Bonjour is missing, the installer silently installs the bundled Bonjour Print Services MSI automatically, with Apple's Bonjour license shown on the installer license page for compliance."

## Clarifications

### Session 2026-03-25

- Q: How is the Bonjour MSI acquired for build (CI and local)? → A: Fetch from a pinned Apple URL with SHA-256 checksum verification via a CMake custom command script; the same script runs for both local and CI builds. Binary is not committed to the repository.
- Q: Detection logic — skip if any Bonjour present, or version-compare and upgrade if older? → A: Skip if any Bonjour is present (presence-based detection, never touch an existing installation).
- Q: Uninstall behavior — remove Bonjour when AirBeam is uninstalled? → A: Leave Bonjour installed; other apps (iTunes, iCloud) may depend on it.
- Q: Which Apple package to bundle? → A: Bonjour Print Services (`BonjourPSSetup.exe`) — the redistributable package Apple publishes.

### Session 2026-03-26

- Q: How should both licenses be presented on the installer license page? → A: Single combined `.txt` file with clearly labeled section headers (`=== AirBeam MIT License ===` / `=== Apple Bonjour SDK License ===`) on the standard MUI license page.
- Q: What does the user see when Bonjour installation fails during the AirBeam install? → A: NSIS `MessageBox` showing a human-readable error with the exit code (e.g. "Bonjour installation failed — AirBeam cannot continue. Error code: $0") before `Abort` is called and files are rolled back.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — First-time Install Without Bonjour (Priority: P1)

A user downloads AirBeam and runs the installer on a fresh Windows machine (or one that never
had iTunes or iCloud). Bonjour is absent. The installer detects this, shows the Apple Bonjour
license on the license page, and installs Bonjour silently as part of the AirBeam installation.
The user never opens a browser and never visits an external website.

**Why this priority**: This is the primary motivation — eliminating the manual "go find Bonjour
yourself" step that breaks the install experience for new users.

**Independent Test**: Run the installer on a clean VM with no Bonjour present. AirBeam installs
and `dnssd.dll` is present afterwards with no browser interaction.

**Acceptance Scenarios**:

1. **Given** Bonjour is not installed, **When** the user runs the AirBeam installer and accepts
   licenses, **Then** Bonjour is installed silently with no separate wizard and no second UAC
   prompt, and `dnssd.dll` is available immediately after.
2. **Given** Bonjour is not installed, **When** the license page is displayed, **Then** both the
   AirBeam MIT license and the Apple Bonjour SDK redistribution license are visible before any
   files are copied.
3. **Given** Bonjour is already installed, **When** the user runs the AirBeam installer,
   **Then** the installer does NOT install or modify the existing Bonjour installation.

---

### User Story 2 — Upgrade / Repair Install (Priority: P2)

A user re-runs the AirBeam installer to repair or upgrade an existing installation. Bonjour is
already present. The installer passes through cleanly without touching Bonjour.

**Why this priority**: Upgrade scenarios must not silently downgrade or re-install an existing,
possibly newer, Bonjour version.

**Independent Test**: Install AirBeam (Bonjour installs with it), then re-run the installer.
Bonjour version on disk is unchanged after the second run.

**Acceptance Scenarios**:

1. **Given** Bonjour is already installed at version X, **When** the user runs the AirBeam
   installer again, **Then** the Bonjour version after install is still X (no downgrade, no
   re-install).
2. **Given** Bonjour was installed by iTunes at a newer version than the bundled copy,
   **When** the user installs AirBeam, **Then** the newer Bonjour is left untouched.

---

### User Story 3 — License Transparency (Priority: P2)

A user reads the license page and sees both the AirBeam MIT license and Apple's Bonjour SDK
license, so they know a third-party component is being installed and under what terms.

**Why this priority**: Required for legal compliance with Apple's redistribution terms and
builds user trust.

**Independent Test**: Step through the installer to the license page; confirm both licenses are
displayed and the installer cannot proceed without explicit acceptance.

**Acceptance Scenarios**:

1. **Given** the license page is shown, **When** the user reads it, **Then** both the AirBeam
   MIT license and the Apple Bonjour SDK redistribution license are visible (as separate labeled
   sections or tabs).
2. **Given** the license page is shown, **When** the user closes the window without accepting,
   **Then** no files are installed.

---

### Edge Cases

- What if Bonjour installation fails mid-installer (e.g., disk full, permissions error)?
  An NSIS `MessageBox` MUST display a human-readable error including the MSI exit code before
  `Abort` is called; the AirBeam install rolls back cleanly; no partial files remain.
- What if the bundled Bonjour MSI fails its SHA-256 checksum verification at build time?
  The CMake build step must fail with a clear error before any installer is produced.
- What if a 32-bit Bonjour is already installed on a 64-bit OS?
  If the `Bonjour Service` registry key is present, skip re-install regardless of bitness.
- What if UAC elevation was granted for AirBeam but Bonjour's MSI would normally request a
  second elevation?
  The bundled MSI must be run with the already-elevated token so no second UAC prompt appears.
- What if the Apple download URL is unreachable during a local or CI build?
  The CMake custom command must fail with a clear error message identifying the URL and instructing
  the developer to verify connectivity or update the pinned URL.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The AirBeam NSIS installer MUST include the Apple Bonjour Print Services MSI
  as an embedded payload inside the installer binary.
- **FR-002**: The installer MUST detect whether the Bonjour service is already present on
  the target machine before copying any AirBeam files.
- **FR-003**: When Bonjour is absent, the installer MUST install Bonjour silently (no
  secondary wizard, no second UAC prompt) using the elevated token already granted to the
  AirBeam installer process.
- **FR-004**: When Bonjour is already installed (registry key `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service`
  present), the installer MUST skip the Bonjour installation step entirely and MUST NOT modify
  the existing Bonjour installation, regardless of the installed version or bitness.
- **FR-005**: The installer license page MUST display both the AirBeam MIT license and the Apple
  Bonjour SDK redistribution license in a single combined `.txt` file, with each license preceded
  by a clearly labeled section header (`=== AirBeam MIT License ===` and
  `=== Apple Bonjour SDK License ===`), shown on the standard MUI license page.
- **FR-006**: If the Bonjour installation step fails for any reason, the installer MUST display an
  NSIS `MessageBox` with a human-readable error message including the MSI exit code, then call
  `Abort` to roll back all installed AirBeam files cleanly.
- **FR-007**: The Bonjour MSI MUST be fetched from a pinned Apple URL with SHA-256 checksum
  verification at build time via a CMake custom command script. The binary MUST NOT be committed
  to the repository. The same script MUST work for both local developer builds and CI builds.
- **FR-008**: The CMake fetch script MUST fail the build with a descriptive error if the
  downloaded file's SHA-256 does not match the pinned value.

### Key Entities

- **Bonjour MSI Fetch Script**: A CMake custom command script that downloads the Apple Bonjour
  Print Services MSI from a pinned URL, verifies its SHA-256 checksum, and places it at
  `installer/deps/BonjourPSSetup.exe`. Attributes: pinned URL, expected SHA-256 hash, Bonjour
  version number.
- **Bonjour Detection Result**: A flag computed at installer startup indicating whether Bonjour
  is already present (via registry key). Drives conditional install logic.
- **Installer License Page**: The NSIS license page showing both AirBeam MIT and Apple Bonjour
  SDK licenses before file installation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user on a machine with no Bonjour can complete the full AirBeam installation
  (including Bonjour) without opening a browser or visiting any external URL.
- **SC-002**: The Bonjour installation step adds no more than 15 seconds to total install time
  on a typical machine.
- **SC-003**: On a machine with Bonjour already installed, the Bonjour version is unchanged
  before and after running the AirBeam installer.
- **SC-004**: Both the AirBeam MIT license and the Apple Bonjour SDK redistribution license
  are visible on the installer license page before any files are written to disk.
- **SC-005**: If Bonjour installation fails, zero AirBeam files remain on disk after rollback.
- **SC-006**: The AirBeam installer binary size increases by no more than 5 MB compared to
  the pre-bundling baseline.
- **SC-007**: The CMake fetch script completes successfully on a clean local developer machine
  with internet access and produces a verified `installer/deps/BonjourPSSetup.exe`.

## Assumptions

- Apple's Bonjour Print Services MSI is freely redistributable under Apple's Bonjour SDK
  license — confirmed by Apple's published terms.
- The bundled Bonjour version is pinned to a specific release; no automatic Bonjour
  version tracking is required.
- The AirBeam installer already requests Administrator elevation (UAC); the Bonjour MSI
  can be launched quietly under that same elevation without a second UAC prompt.
- Bonjour presence is detected via the `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service`
  registry key, consistent with the existing detection logic in `installer/AirBeam.nsi`.
- Detection is presence-based (any registered Bonjour service = skip), not version-comparison-based.
- The installer is built with NSIS 3.x (as per the existing `installer/AirBeam.nsi`).
- This feature does not change the application's runtime behavior (feature 005 balloon/click
  handler remains for cases where Bonjour is removed after installation).
- `installer/deps/BonjourPSSetup.exe` is added to `.gitignore` (file-specific, not the whole directory); the fetched MSI is never committed.

## Dependencies

- `installer/AirBeam.nsi` — existing NSIS script to be modified
- CMake custom command fetch script — to be created at `installer/deps/fetch-bonjour.ps1`
- Apple Bonjour Print Services MSI — fetched at build time to `installer/deps/BonjourPSSetup.exe`
- NSIS 3.x `ExecWait` with elevated execution — already available

### Referenced Files

- `installer/AirBeam.nsi` — NSIS installer script (primary modification target)
- `installer/deps/fetch-bonjour.ps1` — CMake custom command fetch + verify script (to be created)
- `installer/deps/BonjourPSSetup.exe` — fetched Bonjour MSI, gitignored (not committed)
- `installer/licenses/bonjour-sdk-license.txt` — Apple Bonjour license text (to be added)