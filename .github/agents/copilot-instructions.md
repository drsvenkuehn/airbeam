# airbeam Development Guidelines

Auto-generated from all feature plans. Last updated: 2026-03-30

## Active Technologies
- [e.g., Python 3.11, Swift 5.9, Rust 1.75 or NEEDS CLARIFICATION] + [e.g., FastAPI, UIKit, LLVM or NEEDS CLARIFICATION] (main)
- [if applicable, e.g., PostgreSQL, CoreData, files or N/A] (main)
- PowerShell 7+ (fetch script), NSIS 3.x (installer script), CMake 3.20+ + `Invoke-WebRequest` (PS), NSIS MUI2 + LogicLib (existing), `ExecWait` (NSIS) (006-bundle-bonjour-installer)
- `installer/deps/BonjourPSSetup.exe` — fetched binary, gitignored, never committed (006-bundle-bonjour-installer)
- C++17, MSVC 2022 (v143), `/permissive-` + Win32 API, Bonjour SDK (`dnssd.dll`, dynamic), WinSparkle (already (008-mdns-tray-discovery)
- `%APPDATA%\AirBeam\config.json` — existing `Config` class, `lastDevice` wstring (008-mdns-tray-discovery)
- C++17 — MSVC 2022 (v143), `/permissive-` + Win32 API (SetTimer/KillTimer, PostMessage, OutputDebugString), (009-connection-controller)
- `%APPDATA%\AirBeam\config.json` — read/written via existing `Config` class (009-connection-controller)
- C++17, MSVC 2022 (v143), `/permissive-` + Win32 WASAPI (`mmdeviceapi.h`, `audioclient.h`, `avrt.lib`, `ole32.lib`), libspeexdsp (vendored BSD-3-Clause, integer resampler), Google Test 1.14.0 (007-wasapi-loopback-capture)
- N/A — audio data flows in-memory through a lock-free SPSC ring buffer (007-wasapi-loopback-capture)

- C++17, MSVC 2022 (v143), `/permissive-` (001-airplay-audio-sender)

## Project Structure

```text
src/
tests/
```

## Commands

# Add commands for C++17, MSVC 2022 (v143), `/permissive-`

## Code Style

C++17, MSVC 2022 (v143), `/permissive-`: Follow standard conventions

## Recent Changes
- 007-wasapi-loopback-capture: Added C++17, MSVC 2022 (v143), `/permissive-` + Win32 WASAPI (`mmdeviceapi.h`, `audioclient.h`, `avrt.lib`, `ole32.lib`), libspeexdsp (vendored BSD-3-Clause, integer resampler), Google Test 1.14.0
- 009-connection-controller: Added C++17 — MSVC 2022 (v143), `/permissive-` + Win32 API (SetTimer/KillTimer, PostMessage, OutputDebugString),
- 008-mdns-tray-discovery: Added C++17, MSVC 2022 (v143), `/permissive-` + Win32 API, Bonjour SDK (`dnssd.dll`, dynamic), WinSparkle (already


<!-- MANUAL ADDITIONS START -->
<!-- MANUAL ADDITIONS END -->
