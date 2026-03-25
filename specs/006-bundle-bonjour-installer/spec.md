# Feature Specification: Bundle Bonjour Installer

**Feature Branch**: `006-bundle-bonjour-installer`
**Created**: 2026-03-25
**Status**: Draft
**Input**: User description: "Bundle Bonjour MSI in the AirBeam NSIS installer so that when Bonjour is missing, the installer silently installs the bundled Bonjour Print Services MSI automatically, with Apple's Bonjour license shown on the installer license page for compliance."

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
  The AirBeam install must roll back cleanly; user sees a clear error message; no partial files remain.
- What if the bundled Bonjour MSI is corrupted in the AirBeam installer package?
  The installer must detect the failure and abort with a clear error, not silently continue
  in a broken Bonjour-less state.
- What if a 32-bit Bonjour is already installed on a 64-bit OS?
  If any Bonjour service is registered (`Bonjour Service` registry key present), skip re-install
  regardless of bitness.
- What if UAC elevation was granted for AirBeam but Bonjour's MSI would normally request a
  second elevation?
  The bundled MSI must be run with the already-elevated token so no second UAC prompt appears.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The AirBeam NSIS installer MUST include the Apple Bonjour Print Services MSI
  as an embedded payload inside the installer binary.
- **FR-002**: The installer MUST detect whether the Bonjour service is already present on
  the target machine before copying any AirBeam files.
- **FR-003**: When Bonjour is absent, the installer MUST install Bonjour silently (no
  secondary wizard, no second UAC prompt) using the elevated token already granted to the
  AirBeam installer process.
- **FR-004**: When Bonjour is already installed, the installer MUST skip the Bonjour
  installation step entirely and MUST NOT modify the existing Bonjour installation.
- **FR-005**: The installer license page MUST display the Apple Bonjour SDK redistribution
  license in addition to the AirBeam MIT license, with both clearly labeled.
- **FR-006**: The installer MUST roll back cleanly (remove all installed AirBeam files) if
  the Bonjour installation step fails for any reason.
- **FR-007**: The bundled Bonjour MSI MUST be the official Apple Bonjour Print Services
  package for Windows (x64), sourced directly from Apple and version-pinned.
- **FR-008**: The installer MUST NOT install Bonjour if a newer version is already present
  on the machine.

### Key Entities

- **Bundled Bonjour MSI**: The Apple Bonjour Print Services installer binary embedded inside
  the AirBeam NSIS installer. Attributes: version number, file checksum, associated license text.
- **Bonjour Detection Result**: A flag computed at installer startup indicating whether Bonjour
  is already present. Drives conditional install logic.
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

## Assumptions

- Apple's Bonjour Print Services MSI is freely redistributable under Apple's Bonjour SDK
  license — confirmed by Apple's published terms.
- The bundled Bonjour version is pinned to a specific release; no automatic Bonjour
  version tracking is required.
- The AirBeam installer already requests Administrator elevation (UAC); the Bonjour MSI
  can be launched quietly under that same elevation without a second UAC prompt.
- Bonjour presence is detected via the `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service`
  registry key, consistent with the existing detection logic in `installer/AirBeam.nsi`.
- The installer is built with NSIS 3.x (as per the existing `installer/AirBeam.nsi`).
- This feature does not change the application's runtime behavior (feature 005 balloon/click
  handler remains for cases where Bonjour is removed after installation).

## Dependencies

- `installer/AirBeam.nsi` — existing NSIS script to be modified
- Apple Bonjour Print Services MSI — to be downloaded from Apple and stored at
  `installer/deps/BonjourPSSetup.exe`
- NSIS 3.x `ExecWait` with elevated execution — already available

### Referenced Files

- `installer/AirBeam.nsi` — NSIS installer script (primary modification target)
- `installer/deps/BonjourPSSetup.exe` — bundled Bonjour MSI (to be added)
- `installer/licenses/bonjour-sdk-license.txt` — Apple Bonjour license text (to be added)