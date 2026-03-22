# Developer Quickstart: AirBeam

**Branch**: `001-airplay-audio-sender`  
Target: Windows 10 (1903+) / Windows 11 x86-64  
Toolchain: MSVC 2022 (v143), CMake 3.20+, Ninja

---

## Prerequisites

Install these tools before cloning:

| Tool | Where to get | Notes |
|------|-------------|-------|
| Visual Studio 2022 | [visualstudio.microsoft.com](https://visualstudio.microsoft.com) | Install "Desktop development with C++" workload |
| CMake 3.20+ | [cmake.org/download](https://cmake.org/download) | Or use the one bundled with VS 2022 |
| Ninja | Bundled with VS 2022 | `ninja --version` should work from VS Developer CMD |
| Git | [git-scm.com](https://git-scm.com) | — |
| Python 3.9+ | [python.org](https://python.org) | Used by build tools and string-check script |
| Docker Desktop (optional) | [docker.com](https://www.docker.com) | For shairport-sync integration tests |
| Bonjour SDK | [developer.apple.com/bonjour](https://developer.apple.com/bonjour/) | Runtime: install iTunes or Bonjour for Windows |
| NSIS 3.x (optional) | [nsis.sourceforge.io](https://nsis.sourceforge.io) | Only needed to build the installer |

> **Tip**: All build commands below assume you are running from a **VS 2022 Developer Command Prompt** (or an x64 Native Tools Command Prompt) so that MSVC is on `PATH`.

---

## Clone & Bootstrap

```powershell
git clone https://github.com/<your-username>/airbeam.git
cd airbeam

# CMake will fetch third-party deps automatically via FetchContent:
# - Apple ALAC encoder (Apache 2.0)
# - nlohmann/json (MIT)
# - libsamplerate (BSD-2-Clause)
# WinSparkle DLL is pre-copied to third_party/WinSparkle/ in the repo.
```

---

## Configure & Build (Debug)

```powershell
cmake --preset windows-msvc2022-debug
cmake --build --preset windows-msvc2022-debug
```

Output: `build\debug\AirBeam.exe`

## Configure & Build (Release)

```powershell
cmake --preset windows-msvc2022-release
cmake --build --preset windows-msvc2022-release
```

Output: `build\release\AirBeam.exe`

---

## Run the App

```powershell
# Copy WinSparkle.dll and dnssd.dll next to the exe first:
Copy-Item third_party\WinSparkle\bin\WinSparkle.dll build\debug\
# dnssd.dll comes from Bonjour for Windows — usually already in System32

.\build\debug\AirBeam.exe
```

AirBeam starts as a **system tray icon** (no console window). Right-click the tray icon to use it.

---

## Run Unit Tests

```powershell
cd build\debug
ctest --output-on-failure -R "unit_"
```

All unit tests must pass locally before creating a release tag.

```
unit_alac_roundtrip    - ALAC encode/decode bit-exact verification
unit_rtp_framing       - RTP header field validation
unit_aes_vectors       - AES-128-CBC NIST reference vector check
unit_spsc_buffer       - Lock-free ring buffer concurrent producer/consumer
```

---

## Run Integration Tests

Integration tests require additional setup:

### WASAPI Correlation Test

```powershell
# Plays a known WAV file through the loopback device and verifies
# cross-correlation > 0.99 at the ALAC encoder output.
# Requires an audio output device to be present.
ctest --output-on-failure -R "integration_wasapi"
```

### RAOP Session Test (shairport-sync)

```powershell
# Option 1: Docker (recommended)
docker run -d --network host --name shairport mikebrady/shairport-sync
ctest --output-on-failure -R "integration_raop"
docker stop shairport && docker rm shairport

# Option 2: WSL2 with shairport-sync installed
# In WSL2: sudo apt install shairport-sync && shairport-sync &
# Then from Windows:
ctest --output-on-failure -R "integration_raop"
```

---

## Run Stress Test

```powershell
# 24-hour continuous stream test. Requires shairport-sync running.
# Monitor with Task Manager for memory growth > 1 MB/hr (fail threshold).
ctest --output-on-failure -R "stress_24h" --timeout 90000
```

---

## Check Localization Completeness

```powershell
python tools\check_strings.py src\resources\strings\
```

Output: lists any missing string IDs in non-English locale files. All 7 locales must be complete before release.

---

## Build the Installer

```powershell
# After building Release configuration:
cd installer
makensis AirBeam.nsi
# Output: AirBeam-vX.Y.Z-win64-setup.exe
```

---

## Project Layout Quick Reference

```
src\
  app\          Application lifecycle, state machine
  audio\        WASAPI capture, ALAC encoder, resampler, SPSC ring buffer
  transport\    AES encryption, RTP packetizer, retransmit buffer, UDP socket
  protocol\     RTSP/RAOP session, SDP builder, RSA key wrap, NTP timing
  discovery\    Bonjour loader, mDNS device discovery
  ui\           Tray icon, context menu, volume slider, balloon notifications
  config\       JSON config, startup registry
  update\       WinSparkle integration
  logging\      Rolling log file
  resources\    Icons, RC string tables (7 languages)
tests\
  unit\         ALAC, RTP, AES, SPSC — fast, no hardware required
  integration\  WASAPI correlation, RAOP session — require audio device / shairport-sync
  stress\       24h stream — requires shairport-sync + extended runtime
installer\      NSIS script
third_party\    ALAC, nlohmann_json, libsamplerate, WinSparkle (pre-built DLL)
```

---

## Threading Overview (read before touching audio code)

| Thread | Name | RT-safe? | Key rule |
|--------|------|:--------:|----------|
| 1 | UI / Win32 message loop | No | All user interaction; orchestrates other threads |
| 2 | mDNS discovery | No | Bonjour callbacks; posts `WM_RECEIVERS_UPDATED` |
| 3 | WASAPI capture | **YES** | NO alloc, NO mutex, NO blocking. MMCSS-boosted. |
| 4 | ALAC encoder + RTP sender | **YES** | NO alloc, NO mutex, NO blocking. |
| 5 | RTSP control | No | TCP blocking OK. Posts `WM_RAOP_*` messages. |

**Thread 3 → Thread 4 communication**: SPSC lock-free ring buffer ONLY.  
**All other cross-thread communication**: Win32 message posting (`PostMessage`) or `std::atomic`.

> ⚠️ **Never call `Logger::Log()` from Thread 3 or Thread 4.** The logger uses a mutex and will violate RT-safety.

---

## Adding a New Locale

1. Copy `src\resources\strings\strings_en.rc` to `strings_<locale>.rc`
2. Translate all string values (do not change the IDS_ key names)
3. Add a `LANGUAGE LANG_<XX>, SUBLANG_DEFAULT` header matching the Windows LANGID
4. Include the new file in `AirBeam.rc` under the appropriate `#ifdef LANG_<XX>` block
5. Run `python tools\check_strings.py` and confirm zero missing keys
6. No other code changes required

---

## Release Checklist (local, before tagging)

- [ ] `cmake --build --preset windows-msvc2022-release` succeeds with zero warnings
- [ ] All unit tests pass: `ctest -R unit_`
- [ ] WASAPI correlation test passes: `ctest -R integration_wasapi`
- [ ] RAOP session test passes against shairport-sync: `ctest -R integration_raop`
- [ ] All 7 locale files complete: `python tools\check_strings.py`
- [ ] `CHANGELOG.md` updated with changes since last release
- [ ] `CMakeLists.txt` version bumped to match the tag
- [ ] `appcast\appcast.xml` updated (automated in CI; verify locally if needed)

Then create and push the tag:
```powershell
git tag v1.0.0
git push origin v1.0.0
```

GitHub Actions release pipeline triggers automatically.
