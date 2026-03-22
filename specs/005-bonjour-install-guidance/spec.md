# Feature Specification: Bonjour Install Guidance

**Feature Branch**: `005-bonjour-install-guidance`  
**Created**: 2025-07-10  
**Status**: Draft  

## Summary

When AirBeam starts and `dnssd.dll` (Apple Bonjour) is not installed, it shows a tray balloon notification guiding the user to install it. The current `IDS_BALLOON_BONJOUR_MISSING` string in all 7 locale `.rc` files needs to be verified — and if necessary updated — to contain the correct Apple Bonjour for Windows download URL and clear, actionable language.

---

## User Scenarios & Testing

### User Story 1 - First-Run With No Bonjour (Priority: P1)

A user installs AirBeam on a fresh Windows PC that does not have Apple's Bonjour service. On first launch, a tray balloon tells them exactly what is missing and provides a direct download link — so they can fix the problem without searching the web.

**Why this priority**: Without Bonjour, AirBeam cannot discover any AirPlay speakers. This is the most common failure mode for new users (Bonjour is only pre-installed on machines that have had iTunes or other Apple software).

**Independent Test**: Run AirBeam on a machine with `dnssd.dll` removed from `System32`; verify the balloon notification appears within 5 seconds of startup with the correct text and URL.

**Acceptance Scenarios**:

1. **Given** `dnssd.dll` is not present in `System32` or the PATH, **When** `BonjourLoader` fails to load, **Then** AirBeam posts `WM_BONJOUR_MISSING` and a balloon notification fires.
2. **Given** the balloon notification fires, **When** it is displayed, **Then** the text identifies "Bonjour" by name, explains that it is required for speaker discovery, and includes a URL to the Apple download page.
3. **Given** the balloon text includes a URL, **When** the user clicks the balloon, **Then** the URL opens in the default browser (or the installer runs if the NSIS installer bundled Bonjour).
4. **Given** the URL in the balloon, **When** a user follows it, **Then** they reach the Apple Bonjour for Windows download page (URL resolves to a valid Apple support or download page).

---

### User Story 2 - Post-Install Bonjour Recovery (Priority: P2)

After installing Bonjour, the user relaunches AirBeam and speaker discovery works normally — no second notification appears.

**Why this priority**: If the "Bonjour missing" notification fires even after Bonjour is installed, users will be confused and file support tickets.

**Independent Test**: Install Bonjour, then launch AirBeam; verify no `WM_BONJOUR_MISSING` notification is sent and mDNS discovery starts successfully.

**Acceptance Scenarios**:

1. **Given** Bonjour is installed, **When** AirBeam starts, **Then** `BonjourLoader::Load()` succeeds and `WM_BONJOUR_MISSING` is never posted.
2. **Given** Bonjour is installed, **When** the tray context menu appears, **Then** mDNS discovery entries (or "No speakers found" placeholder) are present — not a Bonjour error state.

---

### User Story 3 - Localised Guidance (Priority: P3)

Users running AirBeam in a non-English Windows locale see the Bonjour guidance notification in their language (or a graceful English fallback).

**Why this priority**: Seven locale RC files exist but only the English text has been specified. The URL must be the same across all locales (Apple's download page is region-neutral); only the surrounding instructional text needs translation.

**Independent Test**: Set system locale to German, run AirBeam without Bonjour; verify the balloon uses `strings_de.rc` text (or falls back to English) and still contains the correct Apple URL.

**Acceptance Scenarios**:

1. **Given** a German locale, **When** the Bonjour-missing balloon fires, **Then** the notification text is in German (from `strings_de.rc`) with the correct Apple download URL.
2. **Given** a locale for which no translation exists, **When** the balloon fires, **Then** the English string is used as the fallback.

---

### Edge Cases

- What if Apple changes the Bonjour download URL? → The URL must be a stable Apple support article URL (e.g., `https://support.apple.com/downloads/bonjour-for-windows`), not a direct CDN link that changes with each version.
- What if the NSIS installer is configured to bundle/auto-install Bonjour? → The balloon guidance text should still be present for cases where the user bypasses the installer bundled Bonjour option.
- What if `BonjourLoader` loads `dnssd.dll` but the service is stopped? → This is a separate error path; the balloon text should be specific to the DLL-not-found case, not generic.
- What if the balloon is dismissed before the user reads it? → The tray icon remains in error state; the tooltip should also mention the missing Bonjour dependency.

---

## Requirements

### Functional Requirements

- **FR-001**: `IDS_BALLOON_BONJOUR_MISSING` in `resources/locales/strings_en.rc` MUST contain the word "Bonjour" and a direct URL to the Apple Bonjour for Windows download/support page.
- **FR-002**: The URL MUST be a stable Apple support URL (not a CDN `.pkg`/`.exe` URL that changes with each release).
- **FR-003**: The URL MUST be verified as reachable at spec-authoring time (HTTP 200 or 301/302 to a valid Apple page).
- **FR-004**: All 7 locale RC files (`strings_en.rc`, `strings_de.rc`, `strings_fr.rc`, `strings_es.rc`, `strings_ja.rc`, `strings_ko.rc`, `strings_zh.rc`) MUST have a non-placeholder `IDS_BALLOON_BONJOUR_MISSING` string — either translated or explicitly set to the English fallback until translation is available.
- **FR-005**: The balloon title MUST identify the application by name (e.g., "AirBeam — Bonjour Required").
- **FR-006**: The balloon body text MUST include: (a) what is missing, (b) why it is needed, and (c) where to get it (URL).
- **FR-007**: When the balloon is clicked (`NIN_BALLOONUSERCLICK`), AirBeam MUST open the Bonjour download URL in the default browser using `ShellExecuteW`.
- **FR-008**: The `WM_BONJOUR_MISSING` tray tooltip (set via `Shell_NotifyIcon`) MUST also reference Bonjour so keyboard-only users who miss the balloon can still discover the issue.

### Key Entities

- **`resources/locales/strings_en.rc`**: English locale strings; `IDS_BALLOON_BONJOUR_MISSING` is the primary string to verify/fix.
- **`resources/locales/strings_de.rc`, `strings_fr.rc`, etc.**: Six additional locale files needing consistent `IDS_BALLOON_BONJOUR_MISSING` entries.
- **`src/ui/TrayIcon.cpp`** (or `AppController.cpp`): The `WM_BONJOUR_MISSING` handler that calls `Shell_NotifyIcon` with the balloon and handles `NIN_BALLOONUSERCLICK`.
- **Apple Bonjour download URL**: `https://support.apple.com/downloads/bonjour-for-windows` (stable support article; redirects to latest installer).

---

## Success Criteria

### Measurable Outcomes

- **SC-001**: On a machine without `dnssd.dll`, AirBeam displays the Bonjour guidance balloon within 5 seconds of startup.
- **SC-002**: The URL in the balloon resolves to a valid Apple page (HTTP 200 or 301) — verified by an automated `Invoke-WebRequest` check in CI or manually before release.
- **SC-003**: All 7 locale `.rc` files contain a non-empty, non-placeholder `IDS_BALLOON_BONJOUR_MISSING` string.
- **SC-004**: Clicking the balloon opens `https://support.apple.com/downloads/bonjour-for-windows` (or the configured URL) in the default browser.
