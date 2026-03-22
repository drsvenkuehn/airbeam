# AirBeam

[![Build](https://github.com/TODO_ORG/airbeam/actions/workflows/release.yml/badge.svg)](https://github.com/TODO_ORG/airbeam/actions)
[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-support-yellow.svg)](https://buymeacoffee.com/TODO_BMAC_USERNAME)

**AirBeam** streams Windows system audio to AirPlay (RAOP) speakers on your local network — no drivers, no virtual audio devices, just a tray icon.

## Features

- 🔊 Stream to any AirPlay 1 speaker (AirPlay TV, Airport Express, compatible third-party speakers)
- 🔒 AES-128-CBC + RSA key wrap per RAOP spec
- 🎛 Volume control and low-latency mode
- 🔄 Auto-reconnect on device change or connection drop
- 🚀 Launch at startup support
- 🌍 Localized (English, German, French, Spanish, Japanese, Chinese Simplified, Korean)

## Requirements

- Windows 10 (1903+) or Windows 11, x86-64
- [Bonjour for Windows](https://support.apple.com/downloads/bonjour-for-windows) (for speaker discovery)

## Installation

1. Download `AirBeamSetup.exe` from the [latest release](https://github.com/TODO_ORG/airbeam/releases/latest)
2. Run the installer — it will prompt to install Bonjour if missing
3. AirBeam appears in the system tray; right-click to select a speaker

**Portable mode:** Extract `AirBeam-portable.zip` and run `AirBeam.exe` from any folder.

## Building from Source

```powershell
git clone https://github.com/TODO_ORG/airbeam
cd airbeam
cmake --preset msvc-x64-debug
cmake --build --preset msvc-x64-debug
```

Requires: Visual Studio 2022 (v143), CMake 3.20+

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). All contributors are acknowledged in [CONTRIBUTORS.md](CONTRIBUTORS.md).

## License

MIT — see [LICENSE](LICENSE). Dependencies: Apple ALAC (Apache 2.0), Bonjour SDK (redistributable).
