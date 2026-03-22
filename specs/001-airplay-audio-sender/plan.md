# Implementation Plan: AirBeam — Windows AirPlay Audio Sender

**Branch**: `001-airplay-audio-sender` | **Date**: 2026-03-21 | **Spec**: `specs/001-airplay-audio-sender/spec.md`  
**Constitution**: v1.3.1 | **Research**: `research.md` (all NEEDS CLARIFICATION resolved)

---

## Summary

AirBeam is a native Windows system-tray application (C++17, MSVC 2022, CMake 3.20+) that captures
all system audio via WASAPI loopback and streams it in real time to any AirPlay 1 (RAOP) receiver on
the local network — HomePods, AirPort Express, Apple TVs, and compatible third-party speakers.

The sender implements the full RAOP wire protocol: RTSP/TCP session control, RTP/UDP audio
transport, ALAC lossless encoding (352 samples/frame, 44100 Hz stereo S16LE), AES-128-CBC
per-packet encryption with RSA-2048 PKCS#1v15 session-key wrap, and NTP-like timing
synchronisation. A lock-free SPSC ring buffer decouples the MMCSS-boosted capture thread (Thread 3)
from the encoder/sender thread (Thread 4), both of which are fully real-time safe (zero heap alloc,
zero mutex, zero blocking system calls in the hot path).

The application ships as an NSIS installer and a portable ZIP, supports 7 languages, auto-updates
via WinSparkle (EdDSA-signed packages), and uses only MIT-compatible dependencies throughout.

---

## Technical Context

**Language/Version**: C++17, MSVC 2022 (v143), `/permissive-`  
**Build system**: CMake 3.20+; `FetchContent` for ALAC and nlohmann/json  
**Primary Dependencies**:

| Dependency | Version | License | Link |
|------------|---------|---------|------|
| Apple ALAC reference encoder | latest `macosforge/alac` | Apache 2.0 | FetchContent static |
| nlohmann/json | 3.11+ | MIT | FetchContent single-header |
| libsamplerate or r8brain-free-src | latest | BSD-2/MIT | FetchContent; conditional (resampler) |
| WinSparkle | x64 prebuilt | BSD-2-Clause | DLL shipped alongside exe |
| Bonjour SDK (`dnssd.dll`) | Apple | Redistribution | Dynamic `LoadLibrary` |
| Windows BCrypt (RSA + AES) | built-in Win32 | n/a | `bcrypt.lib` |
| Windows WASAPI / Avrt | built-in Win32 | n/a | `ole32.lib`, `avrt.lib` |

**Storage**: `%APPDATA%\AirBeam\config.json` (installed mode); `config.json` next to exe (portable mode)  
**Testing**: Google Test (GTest) via FetchContent; manual shairport-sync Docker container for RAOP integration  
**Target Platform**: Windows 10 (build 1903+) and Windows 11, x86-64 only  
**Project Type**: Native Win32 desktop tray application  
**Performance Goals**:

| Goal | Target | Mode |
|------|--------|------|
| Connection time | ≤ 3 s after speaker select | Any |
| End-to-end latency | ≤ 3 s | Standard (500 ms buffer) |
| End-to-end latency | ≤ 500 ms | Low-latency (100 ms buffer) |
| Audio losslessness | Cross-correlation > 0.99 | Any |
| Default-device re-attach | ≤ 1 s gap | Any |
| 24 h stress | Zero drift, zero memory growth | Buffered |

**Constraints**:

- Threads 3 & 4 MUST be real-time safe: **zero heap alloc, zero mutex, zero blocking** on hot path
- All buffers pre-allocated before streaming loop; static array ring buffer
- BCrypt only for crypto (no OpenSSL, no mbedTLS)
- No Win32 alternative to `Shell_NotifyIcon`; no external UI frameworks
- `dnssd.dll` dynamically linked with null-checked function-pointer table
- ALAC frame size fixed at 352 samples (constitutional; matches SDP `fmtp`)

**Scale/Scope**: Single active stream to one receiver at a time; v1.0 scope; 7 shipped locales

---

## Constitution Check

*GATE: evaluated pre-design against Constitution v1.3.1. Re-evaluated post-Phase 1.*

| # | Principle | Pre-Design | Post-Phase 1 | Notes |
|---|-----------|:----------:|:------------:|-------|
| I | Real-Time Audio Thread Safety | ✅ | ✅ | SPSC ring (R-008); MMCSS on T3; BCryptEncrypt in-place T4; all hot-path buffers pre-allocated |
| II | AirPlay 1 / RAOP Protocol Fidelity | ✅ | ✅ | Full RTSP seq (R-001), SDP (R-002), RSA wrap (R-003), RTP (R-006), AES (R-005), NTP (R-007), 512-packet retransmit, volume via SET_PARAMETER |
| III | Native Win32, No External UI Frameworks | ✅ | ✅ | Shell_NotifyIcon, CreatePopupMenu, TrackBar volume popup; no Qt/wx/Electron |
| IV | Test-Verified Correctness | ✅ | ✅ | Mandatory unit (ALAC, RTP, AES), integration (WASAPI corr, RAOP shairport-sync), E2E (1 kHz), stress (24h) — all gated pre-tag |
| V | Observable Failures — Never Silent | ✅ | ✅ | Every unrecoverable error → balloon; Bonjour missing → balloon; config corrupt → balloon; retry-exhausted → balloon |
| VI | Strict Scope Discipline (v1.0) | ✅ | ✅ | AirPlay 2 pairing, multi-room, per-app capture, video — all explicitly deferred; AirPlay 2-only receivers grayed out |
| VII | MIT-Compatible Licensing | ✅ | ✅ | ALAC (Apache 2.0), Bonjour (redistribution), WinSparkle (BSD-2-Clause), nlohmann/json (MIT), libsamplerate (BSD-2); no GPL anywhere |
| VIII | Localizable User Interface | ✅ | ✅ | All strings in RC resource files; 7 locales shipped (en/de/fr/es/ja/zh-Hans/ko); new locale = new resource file only |
| — | Auto-Update | ✅ | ✅ | WinSparkle 24 h check, opt-out via `autoUpdate`, "Check for Updates" always present, Ed25519 signed, appcast on gh-pages |

**Gate result**: PASS — no violations. No Complexity Tracking entries required.

---

## Project Structure

### Documentation (this feature)

```text
specs/001-airplay-audio-sender/
├── plan.md              ← this file
├── research.md          ← Phase 0 output (complete)
├── data-model.md        ← Phase 1 output (complete)
├── quickstart.md        ← Phase 1 output
├── contracts/
│   ├── config-schema.md
│   ├── raop-session.md
│   ├── rtp-packet.md
│   └── tray-menu.md
└── tasks.md             ← Phase 2 output (/speckit.tasks — NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
AirBeam/                          ← CMake root
├── CMakeLists.txt                ← top-level; FetchContent for ALAC, nlohmann/json, GTest, resampler
├── CMakePresets.json             ← msvc-x64-release / msvc-x64-debug presets
│
├── src/
│   ├── main.cpp                  ← WinMain; single-instance mutex; WinSparkle init; message loop
│   │
│   ├── core/
│   │   ├── AppController.{h,cpp} ← top-level orchestrator; owns all subsystems
│   │   ├── ConnectionController.{h,cpp} ← AudioStream state machine; retry logic
│   │   ├── Config.{h,cpp}        ← config.json load/save; portable mode detection
│   │   └── Logger.{h,cpp}        ← rolling log; CRITICAL_SECTION; 7-day retention
│   │
│   ├── audio/
│   │   ├── SpscRingBuffer.h      ← lock-free SPSC; static array; atomic indices (header-only)
│   │   ├── AudioFrame.h          ← int16_t[704] + frameCount (header-only POD)
│   │   ├── WasapiCapture.{h,cpp} ← Thread 3; MMCSS; IAudioClient loopback; resampler dispatch
│   │   ├── Resampler.{h,cpp}     ← libsamplerate/r8brain wrapper; converts to 44100 Hz S16LE stereo
│   │   └── AlacEncoderThread.{h,cpp} ← Thread 4; ALAC encode; AES-CBC encrypt; RTP packetize; UDP send
│   │
│   ├── protocol/
│   │   ├── RaopSession.{h,cpp}   ← Thread 5; RTSP TCP; OPTIONS/ANNOUNCE/SETUP/RECORD/TEARDOWN
│   │   ├── SdpBuilder.{h,cpp}    ← SDP body generation for ANNOUNCE
│   │   ├── RsaKeyWrap.{h,cpp}    ← BCrypt RSA-PKCS1v15 wrap of AES session key
│   │   ├── AesCbcCipher.{h,cpp}  ← BCrypt AES-128-CBC; in-place encrypt; pre-keyed per session
│   │   ├── RtpPacket.h           ← wire-format struct; uint8_t[1500]; (header-only POD)
│   │   ├── RetransmitBuffer.{h,cpp} ← std::array<RtpPacket,512>; O(1) seq & 511 lookup
│   │   ├── NtpClock.{h,cpp}      ← GetSystemTimeAsFileTime → NTP epoch; timing-port responder
│   │   └── VolumeMapper.{h,cpp}  ← linear [0,1] ↔ dB [−144, 0]; SET_PARAMETER formatting
│   │
│   ├── discovery/
│   │   ├── BonjourLoader.{h,cpp} ← LoadLibrary("dnssd.dll"); function-pointer table; null checks
│   │   ├── MdnsDiscovery.{h,cpp} ← Thread 2; DNSServiceBrowse/Resolve/GetAddrInfo loop
│   │   ├── ReceiverList.{h,cpp}  ← thread-safe list; 60s stale eviction; WM_RECEIVERS_UPDATED post
│   │   └── TxtRecord.{h,cpp}     ← parse et/pk/am/vs → AirPlayReceiver fields
│   │
│   ├── ui/
│   │   ├── TrayIcon.{h,cpp}      ← Shell_NotifyIcon; 4 states; animated connecting (timer-based)
│   │   ├── TrayMenu.{h,cpp}      ← CreatePopupMenu; receiver items; volume popup; toggles; Quit
│   │   ├── VolumePopup.{h,cpp}   ← owner-drawn TrackBar popup window for volume slider
│   │   ├── BalloonNotify.{h,cpp} ← NIIF_INFO/WARNING/ERROR balloon wrappers; all text via IDS_*
│   │   └── StartupRegistry.{h,cpp} ← HKCU\...\Run key read/write for launch-at-startup
│   │
│   ├── update/
│   │   └── SparkleIntegration.{h,cpp} ← win_sparkle_set_appcast_url; win_sparkle_init; check_update_with_ui
│   │
│   └── localization/
│       └── StringLoader.{h,cpp}  ← LoadString wrapper; detect Windows locale; en fallback
│
├── resources/
│   ├── AirBeam.rc                ← VERSIONINFO; STRINGTABLE (en); icon resources; Sparkle pubkey
│   ├── icons/
│   │   ├── tray_idle.ico
│   │   ├── tray_connecting_001.ico … tray_connecting_008.ico  ← animation frames
│   │   ├── tray_streaming.ico
│   │   └── tray_error.ico
│   ├── locales/
│   │   ├── strings_en.rc
│   │   ├── strings_de.rc
│   │   ├── strings_fr.rc
│   │   ├── strings_es.rc
│   │   ├── strings_ja.rc
│   │   ├── strings_zh-Hans.rc
│   │   └── strings_ko.rc
│   └── sparkle_pubkey.txt        ← Ed25519 public key; injected into RC at build time
│
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/
│   │   ├── test_alac_roundtrip.cpp     ← bit-exact encode/decode check
│   │   ├── test_rtp_framing.cpp        ← parse & validate RTP header fields
│   │   ├── test_aes_vectors.cpp        ← NIST AES-128-CBC known-answer vectors
│   │   ├── test_spsc_ring.cpp          ← ring full/empty, wrap-around, producer/consumer
│   │   ├── test_config.cpp             ← corrupt JSON reset; portable mode; defaults
│   │   ├── test_mdns_txt.cpp           ← TXT record et/pk parsing → isAirPlay1Compatible
│   │   └── test_volume_mapper.cpp      ← linear↔dB edge cases (0.0, 1.0, 0.5)
│   ├── integration/
│   │   ├── test_wasapi_correlation.cpp ← capture known WAV; verify cross-corr > 0.99
│   │   └── test_raop_shairport.cpp     ← full RAOP session vs shairport-sync Docker
│   └── e2e/
│       └── test_1khz_sine.cpp          ← stream 1 kHz tone; verify at receiver
│
├── installer/
│   ├── AirBeam.nsi                     ← NSIS script; Bonjour detection; WinSparkle.dll bundle
│   └── AirBeam.wxs                     ← WiX alternative (optional)
│
└── .github/
    └── workflows/
        └── release.yml                 ← trigger: vX.Y.Z tag; build/sign/package/appcast/gh-pages
```

**Structure Decision**: Single-project layout (no frontend/backend split). Source grouped by
subsystem (`audio/`, `protocol/`, `discovery/`, `ui/`, `update/`). Tests separated by type
(unit / integration / e2e) per Constitution Principle IV.

---

## Implementation Phases

### Phase 1 — Foundation & Infrastructure
*Goal: compiling skeleton with tray icon, config, logging, single-instance, and WinSparkle init.*

#### 1.1 CMake Project Scaffold
- `CMakeLists.txt` at repo root; target `AirBeam.exe`; `/permissive-` `/W4`; C++17
- `CMakePresets.json` with `msvc-x64-release` and `msvc-x64-debug`
- `FetchContent` declarations for ALAC, nlohmann/json, GTest; conditional for resampler
- `AirBeam.rc` stub with VERSIONINFO (major=1, minor=0, patch=0) and empty STRINGTABLE

**Dependencies**: none  
**Deliverable**: `cmake --preset msvc-x64-debug && cmake --build` produces a runnable (empty) exe

#### 1.2 Logger
- `Logger` singleton with `CRITICAL_SECTION` (not `std::mutex` — Win32 CS is lighter)
- Rolling daily file: `%APPDATA%\AirBeam\logs\airbeam-YYYYMMDD.log`
- Levels: TRACE / DEBUG / INFO / WARN / ERROR; format: `YYYY-MM-DD HH:MM:SS.mmm [LEVEL] [TID] msg`
- Startup: delete files older than 7 days before opening today's file
- **RT contract**: Logger is NEVER called from Thread 3 or Thread 4

**Dependencies**: 1.1  
**Deliverable**: `test_config.cpp` logs a message; file appears in `%APPDATA%\AirBeam\logs\`

#### 1.3 Config I/O
- `Config` class; `nlohmann::json` for parse/serialise
- Portable-mode detection: if `config.json` exists next to `.exe` → use that path; else `%APPDATA%\AirBeam\config.json`
- Load: missing file → write defaults silently; malformed JSON → balloon + reset + overwrite
- Save: atomic write (temp file + rename) to avoid partial writes
- Schema: `lastDevice`, `volume` (clamped [0,1]), `lowLatency`, `launchAtStartup`, `autoUpdate`

**Dependencies**: 1.1, 1.2  
**Tests**: `test_config.cpp` — corrupt JSON; defaults; portable mode override; partial keys

#### 1.4 Localization System
- `StringLoader` wraps `LoadStringW` with locale detection via `GetUserDefaultLocaleName`
- Maps Windows locale tag → resource DLL / satellite RC (en/de/fr/es/ja/zh-Hans/ko)
- Fallback: if locale not among 7, use `en`
- All 7 locale RC files (`strings_*.rc`) define every `IDS_*` constant; English is canonical

**Dependencies**: 1.1  
**Contract**: adding a new language = new `strings_XX.rc` only; zero code changes

#### 1.5 Single-Instance Enforcement
- Named mutex `Global\AirBeam_SingleInstance` checked in `WinMain`
- If already exists: find existing window by registered window class name → `PostMessage(hwnd, WM_TRAY_POPUP_MENU, 0, 0)` → `ExitProcess(0)`
- Receiving `WM_TRAY_POPUP_MENU` on existing instance: call `TrackPopupMenu` to bring icon to focus

**Dependencies**: 1.1  
**Test**: manual — launch two instances; second exits; first opens menu

#### 1.6 WinSparkle Initialization
- Load `WinSparkle.dll` via `LoadLibrary` (must be next to `.exe`)
- Call `win_sparkle_set_appcast_url(APPCAST_URL_CONST)` where `APPCAST_URL_CONST` is a
  compile-time string constant from `CMakeLists.txt` configure step
- Call `win_sparkle_set_eddsa_pub_key` / `win_sparkle_set_app_details` (version from VERSIONINFO)
- Call `win_sparkle_init()` before message loop
- Ed25519 public key embedded in RC string `IDS_SPARKLE_PUBKEY`; read and passed to WinSparkle
- `autoUpdate == false` → call `win_sparkle_set_automatic_check_for_updates(0)` after init

**Dependencies**: 1.1, 1.3  
**Note**: WinSparkle DLL must be present alongside exe in both installer and portable ZIP

#### 1.7 Win32 Tray Icon & Menu Skeleton
- `TrayIcon` registers `Shell_NotifyIcon` with 4 icon states
- **Idle**: static `tray_idle.ico`
- **Connecting**: timer (150 ms) cycles through 8 animation frames (`tray_connecting_001..008.ico`)
- **Streaming**: static `tray_streaming.ico`
- **Error**: static `tray_error.ico`
- Tooltip text from `IDS_TOOLTIP_*` per state
- `TrayMenu` builds context menu on right-click `WM_TRAY_CONTEXTMENU`:
  - _(speaker list — placeholder "No speakers found" initially)_
  - Separator
  - Volume → opens `VolumePopup`
  - Low-latency mode (checkmark toggle)
  - Launch at startup (checkmark toggle)
  - Open log folder (`ShellExecuteW("open", logDir)`)
  - Check for Updates → `win_sparkle_check_update_with_ui()`
  - Separator
  - Quit → `PostQuitMessage(0)`

**Dependencies**: 1.1–1.6  
**Deliverable**: Tray icon visible; right-click shows menu; all items present but non-functional

#### 1.8 Startup Registry (Launch at Startup)
- `StartupRegistry::Enable()`: write `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\AirBeam` = `"<exe_path> --startup"`
- `StartupRegistry::Disable()`: delete the Run key value
- `StartupRegistry::IsEnabled()`: query key presence for menu checkmark

**Dependencies**: 1.7

---

### Phase 2 — mDNS Discovery (Thread 2)
*Goal: populate tray menu with live AirPlay receivers.*

#### 2.1 BonjourLoader
- `LoadLibrary("dnssd.dll")` at startup; if fails: check registry `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service`
- If still not found: post `WM_BONJOUR_MISSING` to main window → `BalloonNotify::ShowWarning(IDS_BALLOON_BONJOUR_MISSING)`
- Expose typed function-pointer table: `DNSServiceBrowse`, `DNSServiceResolve`, `DNSServiceGetAddrInfo`, `DNSServiceRefSockFD`, `DNSServiceProcessResult`, `DNSServiceRefDeallocate`, `TXTRecordGetValuePtr`
- Every call site null-checks the pointer before use

**Dependencies**: Phase 1  
**Deliverable**: App starts cleanly with and without Bonjour; balloon shown if missing

#### 2.2 mDNS Browse Loop (Thread 2)
- Browse for `_raop._tcp.local` service type via `DNSServiceBrowse`
- On `kDNSServiceFlagsAdd`: `DNSServiceResolve` → on resolve: `DNSServiceGetAddrInfo` (kDNSServiceProtocol_IPv4)
- On `kDNSServiceFlagsRemove`: remove from `ReceiverList` immediately
- Event-driven via `select()` on `DNSServiceRefSockFD`; ~100 ms timeout to check stop flag
- Thread stop: atomic `bool`; join on `AppController` shutdown

#### 2.3 TXT Record Parsing
- `TxtRecord::Parse(const unsigned char* txt, uint16_t len)` → populates `AirPlayReceiver` fields
- `et` key: `isAirPlay1Compatible = (et contains "1") AND (pk key absent)`
- `am` → `deviceModel`; `vs` → `protocolVersion`
- 60-second stale timer: `ReceiverList::PruneStale()` called by Thread 2 on each browse cycle

#### 2.4 ReceiverList & WM_RECEIVERS_UPDATED
- `ReceiverList` owns `std::vector<AirPlayReceiver>` under a `CRITICAL_SECTION`
- Thread 2 calls `ReceiverList::Update(receiver)` / `Remove(instanceName)`
- After any change: `PostMessage(mainHwnd, WM_RECEIVERS_UPDATED, 0, 0)`
- Thread 1 handles `WM_RECEIVERS_UPDATED`: rebuilds tray menu speaker section
- AirPlay 1 receivers: enabled menu items; AirPlay 2-only: grayed out with `IDS_LABEL_AIRPLAY2_UNSUPPORTED`

**Deliverable**: Real AirPlay receivers appear in tray menu within 10 s of app start (SC-001)

---

### Phase 3 — WASAPI Audio Capture (Thread 3)
*Goal: RT-safe loopback capture pushing AudioFrames into SPSC ring buffer.*

#### 3.1 SpscRingBuffer (Header-Only)
```cpp
template<typename T, uint32_t N>  // N must be power of 2
class SpscRingBuffer {
    std::array<T, N> buf_;        // static allocation; no heap
    std::atomic<uint32_t> head_;  // writer index (Thread 3)
    std::atomic<uint32_t> tail_;  // reader index (Thread 4)
public:
    bool TryPush(const T&) noexcept;   // acquire/release on head_
    bool TryPop(T&) noexcept;          // acquire/release on tail_
    bool IsEmpty() const noexcept;
    bool IsFull()  const noexcept;
};
```
- `SpscRingBuffer<AudioFrame, 128>` for standard mode (~1 s headroom)
- `SpscRingBuffer<AudioFrame, 32>` for low-latency mode (~255 ms headroom)
- Allocated in `AppController` before threads start; pointer passed to Thread 3 and Thread 4

**Tests**: `test_spsc_ring.cpp` — full/empty boundary, wrap-around, sequential produce/consume

#### 3.2 WasapiCapture (Thread 3)
- Thread entry: `AvSetMmThreadCharacteristics(L"Audio", &taskIndex)` first
- Init (called from Thread 1 before loop): `CoCreateInstance(IMMDeviceEnumerator)` → default render device → `IAudioClient::Initialize(AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK, ...)` in shared mode
- `IAudioClient::SetEventHandle(captureEvent)` — event-driven
- Capture loop: `WaitForSingleObject(captureEvent, INFINITE)` → `IAudioCaptureClient::GetBuffer` → accumulate into `AudioFrame` struct → `spsc.TryPush(frame)` → `ReleaseBuffer`
- If `TryPush` fails (ring full): increment `droppedFrameCount` counter; no log (RT thread)
- **RT-safety invariants**: no `new`/`malloc`; no mutex; no file I/O; no WaitForSingleObject with timeout on non-event path
- Partial frame accumulation: `AudioFrame` accumulates until exactly 352 samples collected; remainder carried across `GetBuffer` calls

#### 3.3 Format Detection & Resampler
- After `IAudioClient::GetMixFormat()`: check if format == 44100 Hz, stereo, S16LE (PCM 16-bit)
- If YES: pass raw samples directly to SPSC ring (no resampler)
- If NO (e.g., 48000 Hz float32): instantiate `Resampler` (libsamplerate `SRC_SINC_BEST_QUALITY` or r8brain-free-src) converting to 44100 Hz S16LE stereo
- Resampler state allocated before Thread 3 starts; `Resampler::Process(float* in, int16_t* out, int frames)` called on Thread 3 — must be RT-safe (libsamplerate process function is allocation-free after init)

#### 3.4 Default Device Change Re-Attach
- `IMMNotificationClient::OnDefaultDeviceChanged` (registered from Thread 1 with `IMMDeviceEnumerator::RegisterEndpointNotificationCallback`)
- Posts `WM_DEFAULT_DEVICE_CHANGED` to main window
- Thread 1 handler: signals Thread 3 to stop capture loop; re-initialises `WasapiCapture` with new default device; restarts capture loop — target: ≤ 1 s gap (SC-007)

---

### Phase 4 — ALAC Encoder + RTP/AES Sender (Thread 4)
*Goal: RT-safe encode/encrypt/packetise/send loop consuming AudioFrames from SPSC ring.*

#### 4.1 ALAC Encoder Integration
- `FetchContent_Declare(alac GIT_REPOSITORY https://github.com/macosforge/alac.git)`
- CMake target: `alac_static`; added as link dependency to `AlacEncoderThread`
- Per-session: `ALACEncoderNew()` + `ALACEncoderInit(&config)` called before streaming loop; result stored in pre-allocated encoder state object on Thread 1
- Hot path: `ALACEncode(encoder, 352, frame.samples, outputBuf, &bitOffset)` — no heap alloc
- `outputBuf`: pre-allocated `uint8_t[4096]` in Thread 4's stack frame (worst-case uncompressible)

**Tests**: `test_alac_roundtrip.cpp` — encode 352 silence frames, 352 sine frames, 352 max-amplitude frames; decode with `ALACDecoder`; verify bit-exact round-trip

#### 4.2 AES-128-CBC Session Cipher
- `AesCbcCipher` created once per RAOP session (before Thread 4 starts)
- Init: `BCryptOpenAlgorithmProvider(BCRYPT_AES_ALGORITHM)` → `BCryptSetProperty(BCRYPT_CHAIN_MODE_CBC)` → `BCryptGenerateSymmetricKey(sessionKey16bytes)`
- Per-packet: `BCryptEncrypt(keyObject, alacBuf, paddedLen, NULL, ivCopy, 16, encBuf, ...)` — `ivCopy` is per-packet copy of session IV (session IV constant per AirPlay 1 spec)
- Padding: zero-fill ALAC frame to next 16-byte boundary before encrypt; track padded length for RTP payload size
- Pre-allocated: `ivCopy[16]` + `encBuf[4096+16]` on Thread 4 stack; no heap in hot path

**Tests**: `test_aes_vectors.cpp` — NIST AES-128-CBC Known Answer Test vectors (at least 3 encrypt + 3 decrypt); verify BCrypt output matches reference

#### 4.3 RTP Packet Assembly
- After AES-CBC encrypt, write 12-byte RTP header into `RetransmitBuffer` slot:
  - `data[0] = 0x80`, `data[1] = 0x60`
  - `data[2..3]` = `seq_be` (wrapping uint16, starts random)
  - `data[4..7]` = `timestamp_be` (wrapping uint32, starts random, +352 per packet)
  - `data[8..11]` = `ssrc_be` (random constant per session)
  - `data[12..]` = encrypted+padded ALAC frame
- `payloadLen` = padded ALAC size (stored alongside `data` in `RtpPacket`)
- RetransmitBuffer index = `seq & 511` (O(1) circular overwrite)

**Tests**: `test_rtp_framing.cpp` — construct RTP packet; parse header fields; verify seq/timestamp increment; verify retransmit slot O(1) lookup

#### 4.4 UDP Audio Send
- Thread 4 owns UDP socket `audioSocket` (created on Thread 5 during SETUP, fd passed to T4)
- `sendto(audioSocket, packet.data, 12 + packet.payloadLen, 0, &receiverAudioAddr, addrLen)`
- No blocking: `sendto` on UDP is non-blocking for small datagrams on LAN (~1432 bytes)
- On `WSAEWOULDBLOCK` (extremely rare): increment `udpDropCount`; no log

#### 4.5 NTP Sync Packet Emission
- Thread 4 (or Thread 5 — see §5.4): emit sync packet on control UDP port every ~1 s:
  - 8-byte payload: `[0x00, 0xD4, 0x00, 0x00, <RTP_ts_be32>, <NTP_secs_be32>]`
  - NTP time from `NtpClock::NowSeconds()` (Thread-safe `GetSystemTimeAsFileTime`)
- Counter: `uint32_t syncCounter`; increment per packet; emit when `(packetCount % ntpSyncInterval) == 0`

---

### Phase 5 — RTSP Control & Retransmit Handler (Thread 5)
*Goal: RAOP session lifecycle — OPTIONS/ANNOUNCE/SETUP/RECORD/TEARDOWN — and retransmit service.*

#### 5.1 RTSP TCP Connection
- Thread 5 owns `tcpSocket` to receiver on RTSP port (default 5000)
- `connect()` with timeout (3 s); on failure: post `WM_RAOP_FAILED` to Thread 1
- All RTSP messages: `send(tcpSocket, rtspMsg, len, 0)` + `recv` response; parse status line
- CSeq counter incremented monotonically per request
- `Client-Instance` header: 16 random hex chars generated at session start
- `Session` token: captured from `SETUP` 200 OK response header

#### 5.2 RSA Session-Key Wrap
- `RsaKeyWrap::Wrap(const uint8_t key[16]) → std::string` (base64url encoded)
- BCrypt: `BCryptOpenAlgorithmProvider(BCRYPT_RSA_ALGORITHM)` → import hardcoded AirPlay 1 public key PEM via `BCryptImportKeyPair(BCRYPT_RSAPUBLIC_BLOB)` → `BCryptEncrypt(BCRYPT_PAD_PKCS1)`
- Public key stored as string constant in `RsaKeyWrap.cpp`; not a secret
- Called once per RAOP session (Thread 5, before ANNOUNCE)

#### 5.3 SDP Body & ANNOUNCE
- `SdpBuilder::Build(clientIP, receiverIP, sessionId, rsaAesKey_b64, aesIv_b64) → std::string`
- Exact SDP format per R-002 (see research.md); `fmtp:96 352 0 16 40 10 14 2 44100 0 0`
- ANNOUNCE request: `Content-Type: application/sdp`; `Content-Length: <n>`; body = SDP string

#### 5.4 SETUP, RECORD, SET_PARAMETER, TEARDOWN
- **SETUP**: send client UDP ports (audio, control, timing); parse response for server ports; bind UDP sockets
- **RECORD**: send after SETUP 200 OK; on 200 OK: post `WM_RAOP_CONNECTED` to Thread 1; signal Thread 3+4 to start
- **SET_PARAMETER (volume)**: `VolumeMapper::LinearToDb(float) → double`; send `volume: <dB>\r\n`; called whenever `AudioStream.volumeLinear` changes
- **TEARDOWN**: graceful shutdown; send on quit or speaker swap; wait for 200 OK (1 s timeout); close TCP socket

#### 5.5 Timing Port Responder
- Thread 5 selects on timing UDP socket + control UDP socket + TCP socket (multiplexed via `select()`)
- Timing port: on receiving 32-byte request: fill `receiveTime` and `transmitTime` using `NtpClock::NowNtp64()`; send response
- Also emits NTP sync packet on control port every ~1 s (moved here from T4 to keep T4 RT-safe)

#### 5.6 Retransmit Handler
- Control port: on receiving retransmit request (identifies lost `seq` range): look up `RetransmitBuffer[seq & 511]`; re-send via UDP to receiver's audio port
- Thread 5 reads `RetransmitBuffer` (read-only); Thread 4 writes it (write). No lock needed: packet slot is written by T4 before T5 could request it (512-packet window >> typical RTT)

---

### Phase 6 — Stream Orchestration & Resilience
*Goal: ConnectionController state machine; reconnect; speaker swap; WM_ENDSESSION; low-latency toggle.*

#### 6.1 ConnectionController & AudioStream State Machine
- `AudioStream.state: std::atomic<StreamState>` — Idle / Connecting / Streaming / Reconnecting / Error
- `Connect(receiver)`: allocate session objects (SPSC ring, retransmit buffer, AES cipher, ALAC encoder); start Threads 3+4+5; set icon to connecting
- `OnRaopConnected()` (from WM_RAOP_CONNECTED): set state = Streaming; streaming icon; save `lastDevice`; balloon `IDS_BALLOON_CONNECTED`
- `OnRaopFailed()` (from WM_RAOP_FAILED): if `retryCount < 3` → schedule `SetTimer(1s/2s/4s)` → on timer fire → retry `Connect()`; else → Error state; balloon `IDS_BALLOON_CONNECTION_FAILED`
- `Disconnect()`: send TEARDOWN; stop threads; state = Idle; idle icon

#### 6.2 Startup Auto-Reconnect
- On startup: if `config.lastDevice` non-empty → set state = Connecting; start 5 s `SetTimer`
- If receiver appears in `ReceiverList` before timer fires → `Connect(receiver)`
- If timer fires with no match → state = Idle; no notification (silent per spec)
- Connecting icon shown throughout 5 s window

#### 6.3 Seamless Speaker Swap
- `UserSelectsDevice(newReceiver)` while `state == Streaming`:
  1. `Disconnect()` from current (TEARDOWN, stop T3+4+5)
  2. `Connect(newReceiver)` immediately
  - Brief audio gap acceptable; no confirmation dialog

#### 6.4 WM_ENDSESSION Handler
- `WM_ENDSESSION` arrives on Thread 1 message loop
- If `lParam & ENDSESSION_CLOSEAPP`: post TEARDOWN request to Thread 5; wait up to 1000 ms for completion via `WaitForSingleObject(teardownEvent, 1000)`; then `ExitProcess(0)`
- OS may kill process if budget exceeded — this is acceptable (spec §FR-021)

#### 6.5 Low-Latency Mode Toggle
- Menu checkmark toggle → `config.lowLatency = !config.lowLatency` → `Config::Save()`
- If currently streaming: `Disconnect()` then `Connect(activeReceiver)` with new ring buffer size
- `SpscRingBuffer<AudioFrame, 32>` (low-latency) vs `SpscRingBuffer<AudioFrame, 128>` (standard)
- Type-erased via template + virtual or `std::variant`; pointer passed to T3+T4 at connect time

#### 6.6 Silent WASAPI Re-Attach on Device Change
- `WM_DEFAULT_DEVICE_CHANGED` handler in Thread 1:
  1. Stop Thread 3 capture loop (signal atomic stop flag)
  2. Wait for Thread 3 to acknowledge (~50 ms)
  3. Re-initialise `WasapiCapture` on new default device
  4. Resume Thread 3 — streaming continues; Thread 4 drains any residual frames from ring

#### 6.7 Clean Quit (≤ 2 s)
- `PostQuitMessage(0)` from Quit menu item
- `WM_DESTROY`: stop Thread 2 (mDNS); if streaming: TEARDOWN (Thread 5, 1.5 s budget); stop Threads 3+4; `win_sparkle_cleanup()`; destroy tray icon; `DestroyWindow`
- `ExitProcess(0)` must complete within 2 s of user selecting Quit (SC implied by FR-020)

---

### Phase 7 — Tray UI Polish & Localization
*Goal: complete, production-quality tray menu with volume slider, all 7 locales, balloon notifications.*

#### 7.1 Volume Popup Window
- `VolumePopup`: popup window with `CreateWindow(TRACKBAR_CLASS, ...)`, range 0–100, page-size 5
- Positioned adjacent to tray icon area; dismissed on `WM_KILLFOCUS` or `WM_LBUTTONDOWN` outside
- `WM_HSCROLL` → `volumeLinear = (pos / 100.0f)` → `RaopSession::SetVolume()` if streaming → `Config::Save()`

#### 7.2 Balloon Notification Coverage
All `IDS_BALLOON_*` keys defined in all 7 locales:

| ID | Trigger |
|----|---------|
| `IDS_BALLOON_BONJOUR_MISSING` | Bonjour DLL not found |
| `IDS_BALLOON_CONNECTED` | RAOP session established |
| `IDS_BALLOON_DISCONNECTED` | Unexpected stream loss |
| `IDS_BALLOON_CONNECTION_FAILED` | 3 retries exhausted |
| `IDS_BALLOON_CONFIG_RESET` | `config.json` corrupt / reset |
| `IDS_BALLOON_UPDATE_REJECTED` | EdDSA signature invalid |

#### 7.3 Full Locale Coverage
- All `IDS_*` constants defined in `strings_en.rc` (canonical)
- Each non-English RC file must cover every key — enforced by a CMake custom target that diffs key sets
- No user-selectable locale preference; Windows `GetUserDefaultLocaleName` is sole input
- English fallback: if locale prefix not in `{en,de,fr,es,ja,zh,ko}`, use `strings_en.rc`

#### 7.4 Tray Icon Tooltip
- `IDS_TOOLTIP_IDLE` / `IDS_TOOLTIP_CONNECTING` / `IDS_TOOLTIP_STREAMING(deviceName)` / `IDS_TOOLTIP_ERROR`
- Updated via `Shell_NotifyIcon(NIM_MODIFY, &nid)` on every state transition
- Format string interpolation: `wsprintf(buf, IDS_TOOLTIP_STREAMING, displayName)`

---

### Phase 8 — Testing (Mandatory Pre-Tag)
*All tests MUST pass locally before any vX.Y.Z tag is created.*

#### 8.1 Unit Tests (GTest)

| Test File | What It Verifies | Pass Criterion |
|-----------|-----------------|----------------|
| `test_alac_roundtrip.cpp` | Encode 352 silence/sine/max-amplitude frames; decode; compare | Bit-exact match |
| `test_rtp_framing.cpp` | Build RTP packet; parse header; verify seq/timestamp increment | All fields correct |
| `test_aes_vectors.cpp` | NIST AES-128-CBC KAT (≥3 encrypt + 3 decrypt vectors) | Exact byte match |
| `test_spsc_ring.cpp` | Full/empty boundary; wrap-around; concurrent produce/consume | No data loss or corruption |
| `test_config.cpp` | Corrupt JSON reset; portable mode; missing keys → defaults | Correct fallback behaviour |
| `test_mdns_txt.cpp` | `et=1` → compatible; `et=4` → AirPlay 2; `pk` present → AirPlay 2 | Correct `isAirPlay1Compatible` |
| `test_volume_mapper.cpp` | `0.0 → -144.0 dB`; `1.0 → 0.0 dB`; `0.5 → ~-6.02 dB` | Within ±0.01 dB |

#### 8.2 Integration Tests

| Test | How | Pass Criterion |
|------|-----|----------------|
| WASAPI cross-correlation | Capture known 5 s WAV via loopback; compute Pearson r between source and captured | r > 0.99 |
| RAOP session vs shairport-sync | Start shairport-sync Docker container; run AirBeam in headless mode; verify RAOP handshake + audio reception | Session established; no RTSP error |

#### 8.3 End-to-End Test

| Test | How | Pass Criterion |
|------|-----|----------------|
| 1 kHz sine wave | Generate 1 kHz sine; stream via AirBeam; capture at receiver (loopback/real device); FFT peak | Peak at 1000 Hz ± 5 Hz |

#### 8.4 Stress Test

| Test | Duration | Pass Criterion |
|------|----------|----------------|
| 24 h continuous stream | WinDbg / heap profiler attached | Zero heap growth; RTP timestamp monotonic (no drift); no crashes |

---

### Phase 9 — Installer, Portable ZIP & CI/CD
*Goal: NSIS installer; portable ZIP; GitHub Actions release pipeline.*

#### 9.1 NSIS Installer
- Installs to `%PROGRAMFILES%\AirBeam\` by default (user-selectable)
- Bundles: `AirBeam.exe`, `WinSparkle.dll`, all locale RC resources, icons
- Detects `dnssd.dll` availability (registry key + `LoadLibrary` probe)
- If Bonjour not found: offer to download from `support.apple.com` (localized message `IDS_INSTALLER_BONJOUR_PROMPT`)
- Creates Start Menu shortcut; optionally adds to Run key (UI checkbox = `launchAtStartup` default)
- Uninstaller: removes `%PROGRAMFILES%\AirBeam\`; does NOT remove `%APPDATA%\AirBeam\` (user data preserved)

#### 9.2 Portable ZIP
- Contents: `AirBeam.exe` + `dnssd.dll` + `WinSparkle.dll` + locale resource files
- User unzips; runs `AirBeam.exe` directly; `config.json` created next to exe (portable mode)
- No installer required; no registry written on first run

#### 9.3 GitHub Actions Release Pipeline (`release.yml`)
**Trigger**: `push` with tag matching `v[0-9]+.[0-9]+.[0-9]+`  
**Runner**: `windows-latest` (MSVC 2022)

```
Steps:
1.  checkout
2.  cmake --preset msvc-x64-release
3.  cmake --build --preset msvc-x64-release --config Release
4.  (optional) signtool sign AirBeam.exe with OV cert from secret CODESIGN_PFX
5.  makensis AirBeam.nsi  →  AirBeam-vX.Y.Z-win64-setup.exe
6.  Compress-Archive AirBeam.exe,dnssd.dll,WinSparkle.dll,resources/ → AirBeam-vX.Y.Z-win64-portable.zip
7.  winsparkle-sign AirBeam-vX.Y.Z-win64-setup.exe  →  edSignature (using SPARKLE_PRIVATE_KEY secret)
8.  gh release create vX.Y.Z --notes-from-tag  +  upload both artifacts
9.  Update appcast.xml <item> (version, URL, length, edSignature)
10. git push appcast.xml to gh-pages branch
```

- `SPARKLE_PRIVATE_KEY` — encrypted Actions secret; NEVER committed
- Unsigned builds labelled in release notes if `CODESIGN_PFX` not set

---

## Key Design Decisions

| # | Decision | Rationale |
|---|----------|-----------|
| D-01 | All Thread 3/4 buffers statically allocated on Thread 1 before streaming loop | Constitution Principle I — zero heap alloc on RT threads |
| D-02 | `SpscRingBuffer<T, N>` template with `std::array<T, N>` (no heap) | Enforces RT-safety; lock-free via two `std::atomic<uint32_t>` acquire/release |
| D-03 | BCrypt for all crypto (RSA + AES) | Built into Windows 10+; no external dep; avoids GPL/LGPL risk |
| D-04 | Apple ALAC reference encoder (FetchContent, Apache 2.0) | Only encoder with bit-exact test guarantee; RT-safe hot path |
| D-05 | `LoadLibrary("dnssd.dll")` dynamic linking | Bonjour not guaranteed present; graceful degradation with balloon |
| D-06 | `nlohmann/json` single-header (MIT) for config | Zero build complexity; no generator; portable; MIT-clean |
| D-07 | Constant session IV (AirPlay 1 spec, R-005) | Required by AirPlay 1 receivers; AirPlay 2 per-packet IV is out-of-scope |
| D-08 | Thread 5 (non-RT) owns RTSP TCP + timing/retransmit UDP; T4 owns RTP UDP only | Keeps T4 hot path to minimal send; RTSP blocking calls safely on T5 |
| D-09 | Owner-drawn TrackBar popup for volume | Win32-native; no external framework; dismissed on focus loss |
| D-10 | WinSparkle for updates; Ed25519 pubkey in RC | Supply-chain security; update UI owned by WinSparkle; BDS-2-Clause |
| D-11 | 512-packet retransmit buffer indexed by `seq & 511` | O(1) lookup; 512 × 7.98 ms = ~4 s window; pre-allocated 750 KB |
| D-12 | Resampler only when format ≠ 44100 Hz S16LE stereo | Avoid resampler overhead on typical Windows setups; libsamplerate RT-safe after init |
| D-13 | Logger uses `CRITICAL_SECTION`, NEVER called from T3/T4 | CRITICAL_SECTION is lighter than `std::mutex`; RT threads must not log |
| D-14 | CI runs ONLY on `vX.Y.Z` tags (no PR/push CI) | Constitution §CI/CD — minimise Actions minutes; local dev responsibility |

---

## Risks & Mitigations

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|:----------:|:------:|------------|
| R-01 | libsamplerate / r8brain not RT-safe after init | Medium | High | Verify source: both libraries process from pre-alloc state after `src_new` / constructor; if not RT-safe, pre-allocate scratch on T1 |
| R-02 | AirPlay 1 RSA public key mismatch on specific receiver models | Low | High | Test against HomePod mini, AirPort Express, and shairport-sync; key is documented and universal across all AirPlay 1 hardware |
| R-03 | WASAPI `GetBuffer` delivers > 352 samples per call | High | Medium | `WasapiCapture` must accumulate partial frames across calls; implement `FrameAccumulator` helper |
| R-04 | WM_ENDSESSION 1 s budget exceeded when receiver unreachable | Medium | Low | Acceptable per spec; use `WSASetBlockingHook` or async TCP teardown; worst case OS kills process |
| R-05 | shairport-sync Docker not available in dev environment for integration tests | Medium | Medium | Provide `docker-compose.yml` with shairport-sync; document WSL2 setup in quickstart.md |
| R-06 | Volume slider in tray menu: TrackBar WM_HSCROLL conflicts with parent menu | Medium | Medium | Use popup window (not in-menu) for slider; `VolumePopup` is a separate `WS_POPUP` window |
| R-07 | Animated connecting icon flicker if SetTimer interval too short | Low | Low | Use 150 ms timer; 8 frames = ~1.2 s cycle; perceptually smooth |
| R-08 | nlohmann/json parsing UTF-8 device names (CJK) | Low | Medium | Use `std::wstring` ↔ UTF-8 conversion for `lastDevice`; validate round-trip in `test_config.cpp` |
| R-09 | RetransmitBuffer race: T5 reads slot being overwritten by T4 | Low | Low | 512-packet window >> LAN RTT; slot is valid for many seconds before overwrite; no lock needed |
| R-10 | NSIS Bonjour detection false-negative on ARM64 Windows | Low | Medium | v1.0 is x86-64 only (constitution); ARM64 out of scope |

---

## Dependency Order (Critical Path)

```
Phase 1 (Foundation)
  └── 1.1 CMake scaffold
        ├── 1.2 Logger
        │     └── 1.3 Config I/O ──────────────── 6.1 ConnectionController
        ├── 1.4 Localization
        ├── 1.5 Single-instance
        ├── 1.6 WinSparkle init
        └── 1.7 Tray skeleton ──── 7.1 Volume popup
                                   7.2 Balloons
                                   7.3 Locales
                                   7.4 Tooltips

Phase 2 (Discovery)
  └── 2.1 BonjourLoader
        └── 2.2 mDNS Browse Thread
              ├── 2.3 TXT parsing
              └── 2.4 ReceiverList → 1.7 Tray menu (receivers)

Phase 3 (Capture)
  └── 3.1 SpscRingBuffer ──────── 4.x (consumer)
        └── 3.2 WasapiCapture (T3)
              ├── 3.3 Resampler (conditional)
              └── 3.4 Device change re-attach ─── 6.6

Phase 4 (Encode/Send)
  └── 4.1 ALAC encoder
        └── 4.2 AES-CBC cipher
              └── 4.3 RTP assembly
                    └── 4.4 UDP audio send
                          └── 4.5 NTP sync (→ Phase 5)

Phase 5 (RTSP/Control)
  └── 5.1 RTSP TCP
        ├── 5.2 RSA key wrap
        │     └── 5.3 SDP + ANNOUNCE
        └── 5.4 SETUP / RECORD / SET_PARAMETER / TEARDOWN
              ├── 5.5 Timing port responder
              └── 5.6 Retransmit handler

Phase 6 (Orchestration)
  └── 6.1 AudioStream state machine ← Phase 3, 4, 5
        ├── 6.2 Startup auto-reconnect
        ├── 6.3 Speaker swap
        ├── 6.4 WM_ENDSESSION
        ├── 6.5 Low-latency toggle
        ├── 6.6 Device change re-attach (→ 3.4)
        └── 6.7 Clean quit

Phase 7 (UI Polish) — parallel to Phase 6
Phase 8 (Testing) — requires Phases 3–6 complete
Phase 9 (CI/CD) — requires Phase 8 pass
```

---

## Complexity Tracking

> No Constitution violations — this table lists only engineering complexity hotspots.

| Area | Why Complex | Approach |
|------|------------|----------|
| Thread 3/4 real-time safety | Zero alloc/mutex/blocking on hot path; all state pre-allocated | `SpscRingBuffer` static array; ALAC encoder state pre-alloc; BCrypt key object pre-keyed; `outputBuf[4096]` stack-local |
| WASAPI partial-frame accumulation | `GetBuffer` may deliver arbitrary sample counts, not multiples of 352 | `FrameAccumulator` helper: carry remainder across calls; memcpy into `AudioFrame.samples` |
| RSA + AES both via BCrypt | BCrypt API is verbose; key import differs between RSA and AES | Thin wrappers: `RsaKeyWrap` (RSA PKCS#1v15) and `AesCbcCipher` (AES-CBC); each tested independently |
| mDNS stale-entry eviction | Thread 2 updates list; Thread 1 reads it for menu rebuild | `ReceiverList` CRITICAL_SECTION; prune called on Thread 2 only; `WM_RECEIVERS_UPDATED` posted to Thread 1 |
| Volume slider in tray | Win32 tray menus do not natively host controls | `VolumePopup` is a `WS_POPUP \| WS_CHILD` window; dismissed on focus loss; avoids in-menu owner-draw complexity |
| WM_ENDSESSION 1 s budget | RTSP TEARDOWN is a blocking TCP send/recv; receiver may be unreachable | Thread 5 TEARDOWN with 500 ms send timeout + 500 ms recv timeout; `WaitForSingleObject(teardownEvent, 1000)` on Thread 1 |
| 7-language RC coverage | Keeping all locale files in sync | CMake custom target: diff IDS key sets across all RC files; build fails if any locale missing a key |

---

## Artifacts Produced by This Plan

| Artifact | Path | Status |
|----------|------|--------|
| Feature spec | `specs/001-airplay-audio-sender/spec.md` | ✅ Authoritative input |
| Research | `specs/001-airplay-audio-sender/research.md` | ✅ Phase 0 complete |
| Data model | `specs/001-airplay-audio-sender/data-model.md` | ✅ Phase 1 complete |
| Contracts | `specs/001-airplay-audio-sender/contracts/` | ✅ Phase 1 complete |
| Implementation plan | `specs/001-airplay-audio-sender/plan.md` | ✅ This file |
| Task list | `specs/001-airplay-audio-sender/tasks.md` | ⏳ Next: `/speckit.tasks` |
