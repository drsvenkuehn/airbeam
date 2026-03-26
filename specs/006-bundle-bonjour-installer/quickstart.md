# Developer Quickstart: Feature 006 — Bundle Bonjour Installer

## Overview

This feature bundles Apple Bonjour Print Services in the AirBeam installer.
The Bonjour MSI is **not committed** to the repository — it is fetched at build time.

## Prerequisites

- PowerShell 7+ (`pwsh`) on PATH
- Internet access (first build only; subsequent builds reuse the cached binary)
- NSIS 3.x on PATH (`makensis`)

## First-Time Setup: Pin the SHA-256 Hash

Before building the installer for the first time, determine the expected SHA-256 hash:

```powershell
# From repo root:
pwsh installer\deps\fetch-bonjour.ps1 -OutputDir installer\deps -ComputeHashOnly
# (or manually):
Invoke-WebRequest https://support.apple.com/downloads/DL999/en_US/BonjourPSSetup.exe -OutFile installer\deps\BonjourPSSetup.exe
(Get-FileHash installer\deps\BonjourPSSetup.exe -Algorithm SHA256).Hash
```

Copy the output hash and set `$ExpectedSha256` in `installer\deps\fetch-bonjour.ps1`.

## Fetch the Bonjour MSI (subsequent builds — automatic via CMake)

CMake runs the fetch script automatically as a pre-build step:

```powershell
cmake --preset msvc-x64-release
cmake --build --preset msvc-x64-release
# fetch-bonjour target runs automatically; BonjourPSSetup.exe is placed in installer\deps\
```

To run the fetch manually:
```powershell
pwsh installer\deps\fetch-bonjour.ps1 -OutputDir installer\deps
```

If the binary already exists and its SHA-256 matches, the script exits immediately (no download).

## Build the Installer

```powershell
# From repo root (after cmake build):
cd installer
makensis AirBeam.nsi
# Output: ..\build\AirBeamSetup.exe
```

## Test on Clean VM

1. Create a Windows 10/11 VM snapshot **without** Bonjour (no iTunes, no iCloud).
2. Copy `AirBeamSetup.exe` to the VM.
3. Run the installer and verify:
   - License page shows both AirBeam MIT and Apple Bonjour SDK licenses.
   - Installation completes silently with no second UAC prompt.
   - `dnssd.dll` is present in `%SystemRoot%\System32` after install.
   - Revert to snapshot; re-install — Bonjour install step is skipped.

## CI/CD

The `fetch-bonjour` CMake custom target is included in the `ALL` target, so CI builds
automatically fetch and verify `BonjourPSSetup.exe` on every run. No additional CI
configuration is needed beyond internet access on the `windows-latest` runner.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `FATAL: SHA-256 mismatch` | Apple updated the download | Re-download, compute new hash, update `$ExpectedSha256` in fetch script |
| `FATAL: Failed to download` | Network unreachable | Check internet / proxy; verify URL still valid |
| `BonjourPSSetup.exe not found` (NSIS) | Fetch step did not run | Run `cmake --build` before `makensis`, or run fetch script manually |
| Second UAC prompt during install | Installer not running elevated | Ensure `RequestExecutionLevel admin` present in `AirBeam.nsi` |