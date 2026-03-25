# Research: Bonjour Install Guidance

**Feature**: `005-bonjour-install-guidance`  
**Date**: 2026-03-25

---

## Resolved Decisions

### 1. Re-notification Behavior

**Decision**: Balloon fires on every AirBeam startup while `dnssd.dll` is absent.

**Rationale**: Requires zero persistent state — no "already shown" flag to write or read from `config.json`. Users who dismiss the balloon and restart AirBeam (e.g., after a failed Bonjour install) will always see guidance again. This is correct behavior for a v1.0 tray application where the notification is the sole feedback channel.

**Alternatives considered**:
- Once per installation (show-once flag in config.json): adds config key complexity with no user benefit; if Bonjour install fails silently, user gets no guidance on relaunch.
- Once per Windows session: middle ground, but adds session-scoped state that has no obvious storage mechanism in the current architecture.

---

### 2. URL Configurability

**Decision**: Apple download URL (`https://support.apple.com/downloads/bonjour-for-windows`) is a compile-time constant — not runtime-configurable.

**Rationale**: The URL is architecturally stable (Apple support article, not a CDN version-specific link). Constitution §VIII prohibits hardcoded *user-visible strings* in source, but a URL embedded in a locale RC string is externalized via the resource system. The `ShellExecuteW` call in C++ will reference a compile-time constant `BONJOUR_DOWNLOAD_URL` shared with the RC string to eliminate duplication.

**Alternatives considered**:
- Runtime configurable via `config.json`: adds a config key no user will ever need to change; constitution §VI (strict scope discipline) discourages unnecessary configurability.

---

### 3. TrayIcon API for FR-008 (Bonjour-Specific Tooltip)

**Decision**: Add `TrayState::BonjourMissing` to the `TrayState` enum. `SetState()` maps this value to `IDI_TRAY_ERROR` icon + `IDS_TOOLTIP_BONJOUR_MISSING` tooltip — no new API surface on `TrayIcon`.

**Rationale**: `TrayIcon::SetState()` already encapsulates the icon+tooltip combination atomically. Adding a new enum variant is the minimal, consistent change that keeps `AppController` calling the existing API. A `SetTooltip(UINT)` method would break the invariant that icon and tooltip always change together.

**Alternatives considered**:
- Add `SetTooltip(UINT id)` to `TrayIcon`: API surface bloat; decouples icon and tooltip changes, risking mismatched icon/tooltip combinations in future callers.
- Reuse `TrayState::Error` with a separate tooltip call: would require a `SetTooltip` method and still allows the icon/tooltip split problem.

---

### 4. Flag-Based Click Routing (`lastBalloonWasBonjour_`)

**Decision**: Use a `bool lastBalloonWasBonjour_` flag on `AppController` to gate the `ShellExecuteW` call in `NIN_BALLOONUSERCLICK`.

**Rationale**: Only one balloon type (Bonjour-missing) requires URL click routing in v1.0. A bool is the simplest correct implementation. The known race — another balloon fires after Bonjour balloon, user clicks it and gets Bonjour URL — is acceptable given the rarity of concurrent errors on first run.

**Alternatives considered**:
- `std::wstring lastBalloonClickUrl_` on `AppController`: would eliminate the race entirely. Deferred to v2.0 if multi-balloon-URL routing becomes necessary.
- Generic click-dispatch map: over-engineering for a single case.

---

### 5. SC-002 URL Verification (CI vs Manual)

**Decision**: `bonjour-url-check` CTest labeled `"unit"` — runs automatically in CI via `ctest --preset msvc-x64-debug-ci`. GitHub Actions `windows-latest` runners have internet access. A HEAD request to a stable Apple support article URL takes ~1–2 seconds and is fully deterministic.

**Rationale**: The CI test preset filters by `"label": "unit"`. Labeling this test `"unit"` requires zero changes to `ci.yml` or `CMakePresets.json`. The URL is an Apple support article (not a CDN endpoint) — it is stable and unlikely to flap. If it ever fails in CI, that's a legitimate signal requiring attention.

**Alternatives considered**:
- Label `"network"` + separate CI job: extra workflow complexity with no benefit.
- Manual pre-release gate only: user-hostile (requires human action per release); rejected.

---

### 6. Non-English Locale Translation Strategy

**Decision**: `IDS_BALLOON_TITLE_BONJOUR_MISSING` and `IDS_TOOLTIP_BONJOUR_MISSING` use English text as fallback in all 6 non-English locale files at initial commit, annotated with `// TODO(i18n): translate`. Body strings already have translations; only the URL needs appending.

**Rationale**: Constitution §VIII requires no empty values at release, but allows English fallback during development. The `TODO(i18n)` marker makes the outstanding translation work discoverable via `grep`. Community translations can be accepted as PRs before GA.

---

### 7. URL Deduplication

**Decision**: Define a compile-time constant `BONJOUR_DOWNLOAD_URL` (value: `https://support.apple.com/downloads/bonjour-for-windows`) in a shared header; embed it in both the RC string template and the `ShellExecuteW` call.

**Rationale**: Without this, the URL exists in two independent locations (RC string and C++ `ShellExecuteW` literal). A URL change would require two separate edits. Analysis finding A5 from `/speckit.analyze`.
