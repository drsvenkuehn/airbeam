# Contract: Bonjour-Missing Balloon Text

**Feature**: `005-bonjour-install-guidance`  
**Date**: 2026-03-25  
**Requirement refs**: FR-001, FR-005, FR-006, FR-008

---

## Balloon Notification Contract

This contract defines what the tray balloon MUST contain when `WM_BONJOUR_MISSING` fires. Any implementation that deviates from these rules fails the feature acceptance criteria.

### Title (IDS_BALLOON_TITLE_BONJOUR_MISSING)

| Rule | Requirement |
|------|-------------|
| MUST identify the application by name | Contains "AirBeam" |
| MUST identify what is missing | Contains "Bonjour" |
| MUST NOT exceed 63 characters | Win32 `szInfoTitle` limit is 63 + null |

**Canonical English value**: `AirBeam — Bonjour Required`  
**Character count**: 26 ✅

### Body (IDS_BALLOON_BONJOUR_MISSING)

| Rule | Requirement |
|------|-------------|
| MUST name what is missing (FR-006a) | Contains "Bonjour" |
| MUST explain why it is needed (FR-006b) | Contains reference to AirPlay speaker discovery |
| MUST include the download URL (FR-006c, FR-001) | Contains `https://support.apple.com/downloads/bonjour-for-windows` |
| MUST be actionable | Contains a verb directing user action (e.g., "Visit:", "Install:") |
| MUST NOT exceed 255 characters | Win32 `szInfo` limit is 255 + null |

**Canonical English value**:  
`Bonjour is not installed. AirPlay speaker discovery requires Bonjour. Visit: https://support.apple.com/downloads/bonjour-for-windows`  
**Character count**: 131 ✅

### Tooltip (IDS_TOOLTIP_BONJOUR_MISSING)

| Rule | Requirement |
|------|-------------|
| MUST identify the application by name | Contains "AirBeam" |
| MUST reference Bonjour (FR-008) | Contains "Bonjour" |
| MUST NOT exceed 127 characters | Win32 `szTip` limit is 127 + null |

**Canonical English value**: `AirBeam — Bonjour Not Found`  
**Character count**: 27 ✅

---

## URL Contract

| Property | Value |
|----------|-------|
| URL | `https://support.apple.com/downloads/bonjour-for-windows` |
| Stability requirement | Apple support article — NOT a CDN version-specific link (FR-002) |
| Verified reachable | HTTP 200 (verified 2026-03-23 per research.md §1) |
| Configurability | Compile-time constant `BONJOUR_DOWNLOAD_URL` — not runtime-configurable |
| Appearance | Embedded verbatim in balloon body string AND in `ShellExecuteW` call |

---

## Behavior Contract

| Trigger | Expected Action |
|---------|----------------|
| `BonjourLoader::Load()` returns `false` | `WM_BONJOUR_MISSING` posted to message loop; balloon fires within 5 s of startup (SC-001) |
| `NIN_BALLOONUSERCLICK` while `lastBalloonWasBonjour_` is `true` | `ShellExecuteW` opens `BONJOUR_DOWNLOAD_URL` in default browser (FR-007, SC-004) |
| Balloon dismissed (any method) | TrayState remains `BonjourMissing`; tooltip persists (FR-008) |
| AirBeam restarted while `dnssd.dll` still absent | Balloon fires again — no suppression (FR-009) |
| `BonjourLoader::Load()` returns `true` | `WM_BONJOUR_MISSING` is NOT posted; no balloon |

---

## Cross-Locale Invariants

All 7 locale files MUST satisfy these rules regardless of translated text:

1. `IDS_BALLOON_BONJOUR_MISSING` MUST contain the exact URL string `https://support.apple.com/downloads/bonjour-for-windows` (URL is locale-neutral)
2. `IDS_BALLOON_BONJOUR_MISSING` MUST contain the word "Bonjour" (product name, not translated)
3. `IDS_BALLOON_TITLE_BONJOUR_MISSING` MUST contain "AirBeam" (product name, not translated)
4. All three IDS values MUST be non-empty and non-placeholder
5. Character count limits (63 / 255 / 127) MUST be respected in every locale
