# Data Model: Bonjour Install Guidance

**Feature**: `005-bonjour-install-guidance`  
**Date**: 2026-03-25

---

## String Resource Inventory

### New IDs (to add to `resources/resource_ids.h`)

| ID Constant | Value | Section | Description |
|-------------|-------|---------|-------------|
| `IDS_TOOLTIP_BONJOUR_MISSING` | 1005 | Tooltip | "AirBeam — Bonjour Not Found" |
| `IDS_BALLOON_TITLE_BONJOUR_MISSING` | 1016 | Balloon | "AirBeam — Bonjour Required" |

### Modified IDs

| ID Constant | Value | Change |
|-------------|-------|--------|
| `IDS_BALLOON_BONJOUR_MISSING` | 1010 | Body text updated to include Apple URL |

### Compile-Time Constant (new header or `resource_ids.h`)

| Constant | Value |
|----------|-------|
| `BONJOUR_DOWNLOAD_URL` | `L"https://support.apple.com/downloads/bonjour-for-windows"` |

---

## Locale String Values

### English (`strings_en.rc`) — canonical

| ID | String |
|----|--------|
| `IDS_TOOLTIP_BONJOUR_MISSING` | `"AirBeam \x2014 Bonjour Not Found"` |
| `IDS_BALLOON_TITLE_BONJOUR_MISSING` | `"AirBeam \x2014 Bonjour Required"` |
| `IDS_BALLOON_BONJOUR_MISSING` | `"Bonjour is not installed. AirPlay speaker discovery requires Bonjour. Visit: https://support.apple.com/downloads/bonjour-for-windows"` |

### Non-English locales (de, fr, es, ja, ko, zh-Hans)

All 6 locales must:
1. Append the Apple URL to the existing `IDS_BALLOON_BONJOUR_MISSING` translation, using a locale-appropriate URL-intro verb (see table below)
2. Add `IDS_TOOLTIP_BONJOUR_MISSING` with English fallback + `// TODO(i18n): translate`
3. Add `IDS_BALLOON_TITLE_BONJOUR_MISSING` with English fallback + `// TODO(i18n): translate`

#### Locale URL-intro verbs

| Locale file | URL-intro verb | Example suffix |
|-------------|----------------|----------------|
| `strings_de.rc` | `Besuchen Sie:` | `… Besuchen Sie: https://support.apple.com/downloads/bonjour-for-windows` |
| `strings_fr.rc` | `Consultez :` | `… Consultez : https://support.apple.com/downloads/bonjour-for-windows` |
| `strings_es.rc` | `Visite:` | `… Visite: https://support.apple.com/downloads/bonjour-for-windows` |
| `strings_ja.rc` | `URL：` | `… URL：https://support.apple.com/downloads/bonjour-for-windows` |
| `strings_ko.rc` | `URL：` | `… URL：https://support.apple.com/downloads/bonjour-for-windows` |
| `strings_zh-Hans.rc` | `网址：` | `… 网址：https://support.apple.com/downloads/bonjour-for-windows` |

---

## TrayState Enum

### Current values

```
TrayState::Idle
TrayState::Connecting
TrayState::Streaming
TrayState::Error
```

### New value

```
TrayState::BonjourMissing
```

### SetState() mapping (TrayIcon.cpp)

| TrayState | Icon Resource | Tooltip ID |
|-----------|--------------|------------|
| Idle | `IDI_TRAY_IDLE` | `IDS_TOOLTIP_IDLE` |
| Connecting | `IDI_TRAY_CONN_001`…`008` (animated) | `IDS_TOOLTIP_CONNECTING` |
| Streaming | `IDI_TRAY_STREAMING` | `IDS_TOOLTIP_STREAMING` |
| Error | `IDI_TRAY_ERROR` | `IDS_TOOLTIP_ERROR` |
| **BonjourMissing** | `IDI_TRAY_ERROR` *(reuses error icon)* | `IDS_TOOLTIP_BONJOUR_MISSING` |

---

## State Transitions

```
[Any state]
    │
    │  BonjourLoader::Load() fails → PostMessageW(WM_BONJOUR_MISSING)
    ▼
TrayState::BonjourMissing
    │  Balloon fires: ShowWarning(IDS_BALLOON_TITLE_BONJOUR_MISSING, IDS_BALLOON_BONJOUR_MISSING)
    │  lastBalloonWasBonjour_ = true
    │
    ├── User clicks balloon (NIN_BALLOONUSERCLICK)
    │       → ShellExecuteW(BONJOUR_DOWNLOAD_URL)
    │       → lastBalloonWasBonjour_ = false
    │       → State remains BonjourMissing (icon+tooltip persist until relaunch)
    │
    ├── User dismisses balloon
    │       → State remains BonjourMissing (icon+tooltip persist)
    │       → Balloon re-fires on next AirBeam launch if dnssd.dll still absent (FR-009)
    │
    └── User installs Bonjour, relaunches AirBeam
            → BonjourLoader::Load() succeeds → MdnsDiscovery starts
            → TrayState transitions to Idle (normal startup path)
```

---

## AppController Members (changes)

| Member | Type | Default | Purpose |
|--------|------|---------|---------|
| `lastBalloonWasBonjour_` | `bool` | `false` | Gates `ShellExecuteW` on `NIN_BALLOONUSERCLICK` — true only while Bonjour-missing balloon was the last balloon shown |

**Known limitation**: If another balloon fires after the Bonjour balloon (e.g., `HandleRaopFailed`), `lastBalloonWasBonjour_` remains `true` until the user clicks or the flag is explicitly cleared. Acceptable v1.0 limitation given rarity of concurrent errors on first run.
