# Quickstart: Testing Bonjour Install Guidance

**Feature**: `005-bonjour-install-guidance`  
**Date**: 2026-03-25

---

## Prerequisites

- AirBeam built in Debug configuration (`cmake --build build/msvc-x64-debug`)
- A Windows 10/11 test machine (or VM) where you can rename/restore `dnssd.dll`

---

## Scenario 1 — First-Run, No Bonjour (SC-001, FR-001–FR-008, FR-009)

**Goal**: Verify balloon fires, contains correct text and URL, click opens browser.

### Setup
```powershell
# Rename dnssd.dll to simulate missing Bonjour
$dll = "$env:SystemRoot\System32\dnssd.dll"
if (Test-Path $dll) {
    Rename-Item $dll "$env:SystemRoot\System32\dnssd.dll.bak"
    Write-Host "Renamed dnssd.dll — Bonjour is now unavailable"
} else {
    Write-Host "dnssd.dll not found — Bonjour already absent"
}
```

### Steps
1. Launch `AirBeam.exe` — note the time (T0)
2. **Within 5 seconds** — a tray balloon MUST appear (SC-001)
3. Verify balloon **title** contains "AirBeam" and "Bonjour" (FR-005)
4. Verify balloon **body** contains "Bonjour", a reference to speaker discovery, and the URL `https://support.apple.com/downloads/bonjour-for-windows` (FR-006)
5. Verify tray **tooltip** (hover over icon) contains "AirBeam" and "Bonjour" — NOT generic "AirBeam — Error" (FR-008)
6. **Click** the balloon — default browser MUST open `https://support.apple.com/downloads/bonjour-for-windows` (SC-004)

### Teardown
```powershell
Rename-Item "$env:SystemRoot\System32\dnssd.dll.bak" "$env:SystemRoot\System32\dnssd.dll"
```

---

## Scenario 2 — Re-notification on Relaunch (FR-009)

**Goal**: Verify balloon fires again on the next launch while Bonjour is still absent.

### Steps (continue from Scenario 1 teardown skipped)
1. Dismiss the balloon (click X or wait for it to time out)
2. Quit AirBeam
3. Relaunch `AirBeam.exe`
4. Verify the balloon fires **again** within 5 seconds

---

## Scenario 3 — Post-Install Recovery (US2)

**Goal**: Verify NO balloon fires after Bonjour is installed.

### Steps
1. Ensure `dnssd.dll` is present in `System32` (restore from Scenario 1 if needed)
2. Launch `AirBeam.exe`
3. Wait 10 seconds
4. Verify **no** Bonjour-missing balloon appears
5. Verify tray icon shows **Idle** state (not error)
6. Verify tray tooltip reads "AirBeam — Ready" (not Bonjour error text)

---

## Scenario 4 — Locale Verification (US3, SC-003)

**Goal**: Verify German locale uses `strings_de.rc` text with the correct URL.

### Steps
1. Set Windows display language to German (Settings → Language)
2. Remove `dnssd.dll` (see Scenario 1 setup)
3. Launch `AirBeam.exe`
4. Verify balloon text is in German
5. Verify the URL `https://support.apple.com/downloads/bonjour-for-windows` is still present verbatim in the balloon body

---

## Automated Test: SC-002 URL Reachability

```powershell
# Runs automatically in CI — also runnable locally:
ctest --preset msvc-x64-debug-ci -R bonjour-url-check --output-on-failure
# Expected: 1 test passed
```

> This test is labeled `"unit"` and runs on every CI push. No manual verification required.

---

## Checklist

| Check | SC/FR | Pass? |
|-------|-------|-------|
| Balloon appears within 5 s | SC-001 | |
| Balloon title contains "AirBeam" and "Bonjour" | FR-005 | |
| Balloon body contains name, reason, and URL | FR-006 | |
| Balloon body URL is the stable Apple support URL | FR-001, FR-002 | |
| Clicking balloon opens browser at correct URL | FR-007, SC-004 | |
| Tray tooltip references Bonjour (not generic error) | FR-008 | |
| Balloon fires again on relaunch without Bonjour | FR-009 | |
| No balloon after Bonjour installed | US2 AC1 | |
| German locale shows German text with correct URL | US3 AC1 | |
| All 7 RC files have non-empty balloon strings | SC-003 | |
| URL resolves to valid Apple page | SC-002 | |
