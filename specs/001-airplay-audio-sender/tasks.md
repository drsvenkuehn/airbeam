# Tasks: AirBeam — Windows AirPlay Audio Sender

**Feature Branch**: `001-airplay-audio-sender`
**Input**: `specs/001-airplay-audio-sender/` (plan.md · spec.md · data-model.md · contracts/ · research.md · quickstart.md)
**Stack**: C++17, MSVC 2022, CMake 3.20+, Windows x64, Win32 tray — no external UI frameworks

**Tests**: REQUIRED (Constitution Principle IV) — ALAC round-trip, RTP framing, AES-128-CBC KAT vectors,
WASAPI cross-correlation >0.99, RAOP vs shairport-sync Docker, 1 kHz e2e, 24 h stress. Write each test
before its corresponding implementation and confirm it **FAILS** first.

**Organization**: Grouped by user story. US1 (P1) is the full MVP; US2–US5 layer on top independently.

> **Phase numbering note**: Plan phases (`plan.md` §Phases 1–9) are organized by *technical subsystem* (Plan Phase 1 = Foundation, Plan Phase 3 = WASAPI, Plan Phase 5 = RTSP…). Task phases below are organized by *user story*. Task Phase 3 = US1 MVP (covering plan Phases 3–5); Task Phase 6 = US4 Resilience (plan Phase 6). See the "Phase Dependencies" section for the explicit mapping.

---

## Format: `[ID] [P?] [Story?] Description — file path(s)`

- **[P]**: Can run in parallel (different files, no inter-task dependencies within phase)
- **[USn]**: Maps task to user story for traceability (US1–US5)
- Story-phase tasks: MUST carry `[USn]` label
- Setup / Foundational / Polish phases: NO story label

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: CMake project scaffold — compiling skeleton before any feature work begins.

- [x] T001 Create top-level `CMakeLists.txt`: target `AirBeam.exe`; `/permissive-` `/W4`; C++17; `FetchContent` declarations for ALAC (`macosforge/alac`), nlohmann/json 3.11+, GTest, and conditional resampler — `CMakeLists.txt`
- [x] T002 [P] Create `CMakePresets.json` with `msvc-x64-release` and `msvc-x64-debug` presets targeting MSVC 2022 — `CMakePresets.json`
- [x] T003 [P] Create `resources/AirBeam.rc` stub: `VERSIONINFO` (major=1 minor=0 patch=0), empty `STRINGTABLE`, icon resource placeholders for all 4 tray states + 8 connecting animation frames — `resources/AirBeam.rc`
- [x] T004 [P] Create source tree skeleton: all subdirectories (`src/core/`, `src/audio/`, `src/protocol/`, `src/discovery/`, `src/ui/`, `src/update/`, `src/localization/`, `resources/icons/`, `resources/locales/`, `tests/unit/`, `tests/integration/`, `tests/e2e/`, `installer/`, `.github/workflows/`) with `.gitkeep` files — `CMakeLists.txt` (source groups)
- [x] T005 [P] Create `tests/CMakeLists.txt`: GTest `FetchContent`, `AirBeamTests` executable, include all test `.cpp` files, register with `ctest` — `tests/CMakeLists.txt`
- [x] T006 [P] Create `.github/workflows/release.yml` placeholder: trigger `push` tag `v*.*.*`, `windows-latest` runner, TODO stubs for each of the 10 pipeline steps — `.github/workflows/release.yml`

**Checkpoint**: `cmake --preset msvc-x64-debug && cmake --build` produces a runnable empty `.exe`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure MUST be complete before ANY user story implementation.

**⚠️ CRITICAL**: No user-story work until all Phase 2 tasks are done and both unit tests pass.

- [x] T007 Implement `Logger` singleton: `CRITICAL_SECTION` (not `std::mutex`); rolling daily log `%APPDATA%\AirBeam\logs\airbeam-YYYYMMDD.log`; levels TRACE/DEBUG/INFO/WARN/ERROR; format `YYYY-MM-DD HH:MM:SS.mmm [LEVEL] [TID] msg`; delete files older than 7 days on startup; **NEVER called from Threads 3 or 4** — `src/core/Logger.h`, `src/core/Logger.cpp`
- [x] T008 Implement `Config` class using `nlohmann::json`: portable-mode detection (`.exe` directory vs `%APPDATA%\AirBeam\`); load with silent default-create on missing file; corrupt JSON → `IDS_BALLOON_CONFIG_RESET` balloon + overwrite with defaults; atomic write via temp-file rename; schema fields `lastDevice`, `volume` (clamped 0–1), `lowLatency`, `launchAtStartup`, `autoUpdate` — `src/core/Config.h`, `src/core/Config.cpp`
- [x] T009 [P] Implement `StringLoader`: wrap `LoadStringW`; locale detection via `GetUserDefaultLocaleName`; map Windows locale tag prefix → `{en, de, fr, es, ja, zh, ko}`; English fallback for unmapped locales; `StringLoader::Get(UINT id) → std::wstring` — `src/localization/StringLoader.h`, `src/localization/StringLoader.cpp`
- [x] T010 [P] Create all 7 locale resource files with complete `IDS_*` key sets (English is canonical): `IDS_TOOLTIP_IDLE`, `IDS_TOOLTIP_CONNECTING`, `IDS_TOOLTIP_STREAMING`, `IDS_TOOLTIP_ERROR`, `IDS_BALLOON_BONJOUR_MISSING`, `IDS_BALLOON_CONNECTED`, `IDS_BALLOON_DISCONNECTED`, `IDS_BALLOON_CONNECTION_FAILED`, `IDS_BALLOON_CONFIG_RESET`, `IDS_BALLOON_UPDATE_REJECTED`, `IDS_LABEL_AIRPLAY2_UNSUPPORTED`, `IDS_MENU_VOLUME`, `IDS_MENU_LOW_LATENCY`, `IDS_MENU_LAUNCH_AT_STARTUP`, `IDS_MENU_OPEN_LOG_FOLDER`, `IDS_MENU_CHECK_FOR_UPDATES`, `IDS_MENU_QUIT`, `IDS_INSTALLER_BONJOUR_PROMPT`, `IDS_SPARKLE_PUBKEY` — `resources/locales/strings_en.rc`, `strings_de.rc`, `strings_fr.rc`, `strings_es.rc`, `strings_ja.rc`, `strings_zh-Hans.rc`, `strings_ko.rc`
- [x] T011 [P] Add CMake custom target `check-locale-keys` that diffs `IDS_*` key sets across all 7 locale RC files and fails the build if any non-English file is missing a key defined in `strings_en.rc` — `CMakeLists.txt`
- [x] T012 [P] Implement `BalloonNotify` wrapper: `ShowInfo`, `ShowWarning`, `ShowError` each accepting an `IDS_*` constant and optional format string; wraps `Shell_NotifyIcon(NIM_MODIFY, ...)` with `NIIF_*` flags; all message text via `StringLoader` — `src/ui/BalloonNotify.h`, `src/ui/BalloonNotify.cpp`
- [x] T013 Implement `SparkleIntegration` init skeleton: `LoadLibrary("WinSparkle.dll")`; set appcast URL from compile-time `AIRBEAM_APPCAST_URL` constant; `win_sparkle_set_app_details`; `win_sparkle_init`; expose `CheckForUpdates()` → `win_sparkle_check_update_with_ui()`; `Cleanup()` → `win_sparkle_cleanup()` — `src/update/SparkleIntegration.h`, `src/update/SparkleIntegration.cpp`
- [x] T014 Implement `TrayIcon`: `Shell_NotifyIcon` registration; 4 icon states (Idle, Connecting, Streaming, Error) loaded from `resources/icons/`; animated connecting state — 150 ms `SetTimer` cycling through 8 frames `tray_connecting_001.ico`–`tray_connecting_008.ico`; tooltip updated via `StringLoader::Get(IDS_TOOLTIP_*)` on every state transition — `src/ui/TrayIcon.h`, `src/ui/TrayIcon.cpp`
- [x] T015 Implement `TrayMenu` skeleton: `CreatePopupMenu` on `WM_TRAY_CONTEXTMENU`; static items _(speaker list placeholder "No speakers found")_, separator, Volume, Low-latency mode (checkmark), Launch at startup (checkmark), Open log folder, Check for Updates, separator, Quit; all labels via `StringLoader`; `TrackPopupMenu` with `TPM_RETURNCMD` — `src/ui/TrayMenu.h`, `src/ui/TrayMenu.cpp`
- [x] T016 [P] Implement `StartupRegistry`: `Enable()` writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\AirBeam`; `Disable()` deletes the value; `IsEnabled()` queries key presence for menu checkmark sync — `src/ui/StartupRegistry.h`, `src/ui/StartupRegistry.cpp`
- [x] T017 [P] Implement `VolumeMapper`: `LinearToDb(float) → double` mapping 0.0→-144.0 dB, 1.0→0.0 dB; `DbToLinear(double) → float`; format `SET_PARAMETER` body string `"volume: %.6f\r\n"` — `src/protocol/VolumeMapper.h`, `src/protocol/VolumeMapper.cpp`
- [x] T018 Implement single-instance enforcement in `WinMain`: create named mutex `Global\AirBeam_SingleInstance`; if already exists → find window by registered class name → `PostMessage(hwnd, WM_TRAY_POPUP_MENU, 0, 0)` → `ExitProcess(0)`; receiving instance handles `WM_TRAY_POPUP_MENU` by calling `TrackPopupMenu` to bring icon to focus; additionally parse `--startup` command-line flag in `WinMain`: if present, skip sending `WM_TRAY_POPUP_MENU` on this launch (suppresses unwanted tray focus on OS-triggered startup launches, FR-013) — `src/main.cpp`
- [x] T019 Implement `AppController` skeleton and complete `WinMain` message loop: register window class; create hidden message window; instantiate `Logger`, `Config`, `StringLoader`, `TrayIcon`, `TrayMenu`, `SparkleIntegration`; run `GetMessage` / `DispatchMessage` loop; handle `WM_TRAY_CONTEXTMENU`; handle `WM_COMMAND` dispatch to menu items — `src/core/AppController.h`, `src/core/AppController.cpp`, `src/main.cpp`
- [x] T020 [P] Write `test_config.cpp`: (a) corrupt JSON → reset to defaults and balloon; (b) missing file → silent create with defaults; (c) portable mode: `config.json` next to exe overrides `%APPDATA%`; (d) partial keys preserved; (e) UTF-8 CJK `lastDevice` round-trip — `tests/unit/test_config.cpp`
- [x] T021 [P] Write `test_volume_mapper.cpp`: `0.0 → -144.0 dB` (±0.01 dB); `1.0 → 0.0 dB` (±0.01 dB); `0.5 → ~-6.02 dB` (±0.01 dB); `DbToLinear(LinearToDb(0.75f))` round-trips within ±0.001 — `tests/unit/test_volume_mapper.cpp`

**Checkpoint**: Compiling tray app with icon visible; Config/VolumeMapper unit tests pass; locale RC files complete

---

## Phase 3: User Story 1 — Stream System Audio to an AirPlay Speaker (Priority: P1) 🎯 MVP

**Goal**: Full end-to-end streaming pipeline — discover AirPlay receivers via mDNS, capture system audio via
WASAPI loopback, encode with ALAC, encrypt with AES-128-CBC, packetize as RTP, stream over UDP, control
session via RTSP. User right-clicks tray icon, sees speakers, clicks one, audio plays from that speaker.

**FRs covered**: FR-001 · FR-002 · FR-003 · FR-004 · FR-005 · FR-009 · FR-016

**Independent Test**: Install app, connect AirPlay receiver to same Wi-Fi, click speaker in tray menu,
confirm audio plays at receiver within 3 s; click again to disconnect; tray icon reflects correct state.

### Tests for User Story 1 ⚠️ Write FIRST — verify FAIL before implementing each component

- [x] T022 [P] [US1] Write `test_spsc_ring.cpp`: full-buffer TryPush returns `false`; empty TryPop returns `false`; sequential produce/consume 1000 items preserves FIFO order; wrap-around at N boundary; no data loss on N-1 items — `tests/unit/test_spsc_ring.cpp`
- [x] T023 [P] [US1] Write `test_mdns_txt.cpp`: `et="1"` + no `pk` → `isAirPlay1Compatible=true`; `et="0,1"` + no `pk` → `true`; `et="4"` → `false`; `pk` present → `false` regardless of `et`; empty TXT → `false`; `am` and `vs` fields parsed correctly — `tests/unit/test_mdns_txt.cpp`
- [x] T024 [P] [US1] Write `test_alac_roundtrip.cpp`: encode 352 silence samples (all-zero `int16_t[704]`) then decode with `ALACDecoder`; encode 352 sine-wave samples; encode 352 max-amplitude samples (`±32767`); each case: decoded output MUST be bit-exact equal to input — `tests/unit/test_alac_roundtrip.cpp`
- [x] T025 [P] [US1] Write `test_rtp_framing.cpp`: construct `RtpPacket`; verify byte 0 = `0x80`, byte 1 = `0x60`; seq increments wrap `65535→0`; timestamp increments by 352 per packet; SSRC constant per session; `RetransmitBuffer` slot at `seq & 511` is O(1) and overwrites circularly — `tests/unit/test_rtp_framing.cpp`
- [x] T026 [P] [US1] Write `test_aes_vectors.cpp`: at least 3 NIST AES-128-CBC encrypt KAT vectors + 3 decrypt KAT vectors; `BCryptEncrypt` output MUST match reference byte-for-byte; `ivCopy` is not mutated across calls (session IV is constant per AirPlay 1 spec) — `tests/unit/test_aes_vectors.cpp`
- [x] T027 [P] [US1] Write `tests/integration/test_wasapi_correlation.cpp`: play a known 5 s PCM WAV file to default output; capture same period via `WasapiCapture` loopback; compute Pearson cross-correlation between source and captured samples; assert r > 0.99 — `tests/integration/test_wasapi_correlation.cpp`
- [x] T028 [P] [US1] Write `tests/integration/test_raop_shairport.cpp`: start shairport-sync Docker container (`ghcr.io/mikebrady/shairport-sync`); initiate full RAOP OPTIONS→ANNOUNCE→SETUP→RECORD sequence against container; verify 200 OK for each step and that audio UDP frames are received; provide `docker-compose.yml` for CI environment — `tests/integration/test_raop_shairport.cpp`, `docker-compose.yml`
- [x] T029 [P] [US1] Write `tests/e2e/test_1khz_sine.cpp`: generate 1 kHz sine wave; stream via AirBeam pipeline to shairport-sync Docker; capture output at receiver; FFT the received audio; assert spectral peak at 1000 Hz ±5 Hz — `tests/e2e/test_1khz_sine.cpp`

### Implementation for User Story 1

- [x] T030 [P] [US1] Implement `AudioFrame` POD struct: `int16_t samples[704]` (352 stereo samples × 2 channels), `uint32_t frameCount` — `src/audio/AudioFrame.h`
- [x] T031 [P] [US1] Implement `RtpPacket` wire-format struct: `uint8_t data[1500]`, `uint16_t payloadLen`; inline helpers `SetSeq`, `SetTimestamp`, `SetSsrc` writing big-endian into `data[0..11]`; byte 0 = `0x80`, byte 1 = `0x60` fixed — `src/protocol/RtpPacket.h`
- [x] T032 [US1] Implement `SpscRingBuffer<T, N>` header-only lock-free template: `std::array<T, N>` static storage (N must be power-of-2); `std::atomic<uint32_t> head_` (writer) and `tail_` (reader); `TryPush` acquire/release on `head_`; `TryPop` acquire/release on `tail_`; `IsEmpty()`, `IsFull()` — **zero heap alloc, zero mutex** — `src/audio/SpscRingBuffer.h`
- [x] T033 [P] [US1] Implement `TxtRecord::Parse(const unsigned char* txt, uint16_t len) → AirPlayReceiver fields`: extract `et`, `pk`, `am`, `vs` using `TXTRecordGetValuePtr`; set `isAirPlay1Compatible = (et contains "1") AND (pk absent)` — `src/discovery/TxtRecord.h`, `src/discovery/TxtRecord.cpp`
- [x] T034 [P] [US1] Implement `BonjourLoader`: `LoadLibrary("dnssd.dll")`; on failure check registry `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service`; if still absent post `WM_BONJOUR_MISSING`; expose null-checked function-pointer table: `DNSServiceBrowse`, `DNSServiceResolve`, `DNSServiceGetAddrInfo`, `DNSServiceRefSockFD`, `DNSServiceProcessResult`, `DNSServiceRefDeallocate`, `TXTRecordGetValuePtr` — `src/discovery/BonjourLoader.h`, `src/discovery/BonjourLoader.cpp`
- [x] T035 [US1] Implement `ReceiverList`: `std::vector<AirPlayReceiver>` under a `CRITICAL_SECTION`; `Update(receiver)` upserts by `instanceName`; `Remove(instanceName)`; `PruneStale(ULONGLONG nowTicks)` evicts entries older than 60 s; after any change `PostMessage(mainHwnd, WM_RECEIVERS_UPDATED, 0, 0)` — `src/discovery/ReceiverList.h`, `src/discovery/ReceiverList.cpp`
- [x] T036 [US1] Implement `MdnsDiscovery` Thread 2: `DNSServiceBrowse` for `_raop._tcp.local`; on `kDNSServiceFlagsAdd` → `DNSServiceResolve` → `DNSServiceGetAddrInfo` (IPv4) → `TxtRecord::Parse` → `ReceiverList::Update`; on `kDNSServiceFlagsRemove` → `ReceiverList::Remove`; event-driven via `select()` on `DNSServiceRefSockFD` with ~100 ms timeout; atomic stop flag; join on `AppController` shutdown — `src/discovery/MdnsDiscovery.h`, `src/discovery/MdnsDiscovery.cpp`
- [x] T037 [US1] Wire `WM_RECEIVERS_UPDATED` handler in `AppController` → `TrayMenu::RebuildSpeakerSection`: AirPlay 1 receivers as enabled items; AirPlay 2-only receivers grayed out with `IDS_LABEL_AIRPLAY2_UNSUPPORTED`; clicking grayed item has no effect — `src/ui/TrayMenu.cpp`, `src/core/AppController.cpp`
- [x] T038 [P] [US1] Implement `AesCbcCipher`: `BCryptOpenAlgorithmProvider(BCRYPT_AES_ALGORITHM)` + `BCryptSetProperty(BCRYPT_CHAIN_MODE_CBC)` + `BCryptGenerateSymmetricKey(sessionKey[16])` in ctor; per-packet `Encrypt(alacBuf, paddedLen, ivCopy, encBuf)` — `ivCopy[16]` is per-packet copy of constant session IV; zero-fill ALAC frame to next 16-byte boundary before encrypt; **no heap alloc in hot path** — `src/protocol/AesCbcCipher.h`, `src/protocol/AesCbcCipher.cpp`
- [x] T039 [P] [US1] Implement `RsaKeyWrap::Wrap(const uint8_t key[16]) → std::string` (base64url): `BCryptOpenAlgorithmProvider(BCRYPT_RSA_ALGORITHM)` → import hardcoded AirPlay 1 RSA-2048 public key constant via `BCryptImportKeyPair(BCRYPT_RSAPUBLIC_BLOB)` → `BCryptEncrypt(BCRYPT_PAD_PKCS1)` → base64url encode result; called once per RAOP session on Thread 5 — `src/protocol/RsaKeyWrap.h`, `src/protocol/RsaKeyWrap.cpp`
- [x] T040 [P] [US1] Implement `NtpClock`: `NowSeconds() → uint32_t` using `GetSystemTimeAsFileTime` → NTP epoch (offset 2208988800); `NowNtp64() → uint64_t` returning full NTP 64-bit timestamp; thread-safe (read-only Win32 call) — `src/protocol/NtpClock.h`, `src/protocol/NtpClock.cpp`
- [x] T041 [P] [US1] Implement `RetransmitBuffer`: `std::array<RtpPacket, 512>` pre-allocated; `Store(const RtpPacket&, uint16_t seq)` writes to slot `seq & 511` (O(1) circular overwrite); `Retrieve(uint16_t seq) → const RtpPacket*` looks up same slot — no lock required per D-11 (512-packet window >> LAN RTT) — `src/protocol/RetransmitBuffer.h`, `src/protocol/RetransmitBuffer.cpp`
- [x] T042 [P] [US1] Implement `SdpBuilder::Build(clientIP, receiverIP, sessionId, rsaAesKey_b64, aesIv_b64) → std::string`: exact SDP format per R-002; include `fmtp:96 352 0 16 40 10 14 2 44100 0 0`; `rtpmap:96 AppleLossless/44100/2`; `a=rsaaeskey:<key>`, `a=aesiv:<iv>` — `src/protocol/SdpBuilder.h`, `src/protocol/SdpBuilder.cpp`
- [x] T043 [P] [US1] Implement `Resampler` wrapper: conditional on detected mix format — if 44100 Hz stereo S16LE passthrough; else instantiate libsamplerate `SRC_SINC_BEST_QUALITY` or r8brain-free-src; `Process(float* in, int16_t* out, int inFrames) → int outFrames`; resampler state allocated on Thread 1 before streaming loop, allocation-free during `Process` — `src/audio/Resampler.h`, `src/audio/Resampler.cpp`
- [x] T044 [US1] Implement `WasapiCapture` Thread 3: `AvSetMmThreadCharacteristics(L"Audio", &taskIndex)` first; `CoCreateInstance(IMMDeviceEnumerator)` → default render device → `IAudioClient::Initialize(AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, ...)`; `SetEventHandle(captureEvent)`; capture loop: `WaitForSingleObject(captureEvent, INFINITE)` → `IAudioCaptureClient::GetBuffer` → `FrameAccumulator` helper accumulates until exactly 352 samples → `Resampler::Process` if needed → `spsc.TryPush(frame)` (on full: increment `droppedFrameCount`, no log) → `ReleaseBuffer`; **zero `new`/`malloc`, zero mutex, zero file I/O in hot path** — `src/audio/WasapiCapture.h`, `src/audio/WasapiCapture.cpp`
- [x] T045 [US1] Implement `AlacEncoderThread` Thread 4 — ALAC encode + AES encrypt + RTP assembly: call `ALACEncoderNew()` + `ALACEncoderInit` before loop (on Thread 1); hot path: `spsc.TryPop(frame)` → `ALACEncode(encoder, 352, frame.samples, outputBuf, &bitOffset)` (stack-local `uint8_t outputBuf[4096]`) → zero-pad ALAC frame to 16-byte boundary → `AesCbcCipher::Encrypt` in-place → write 12-byte RTP header into `RetransmitBuffer` slot → `RetransmitBuffer::Store` — **zero heap alloc in hot path** — `src/audio/AlacEncoderThread.h`, `src/audio/AlacEncoderThread.cpp`
- [x] T046 [US1] Implement UDP audio `sendto` in `AlacEncoderThread`: `sendto(audioSocket, packet.data, 12 + packet.payloadLen, 0, &receiverAudioAddr, addrLen)` on `WSAEWOULDBLOCK` increment `udpDropCount` (no log); audio UDP socket fd passed in from Thread 5 `RaopSession::SETUP` — Thread 4 owns audio socket only; control socket stays exclusively with Thread 5 (D-08, RT-safety) — `src/audio/AlacEncoderThread.cpp`
- [x] T047 [US1] Implement `RaopSession` Thread 5 — RTSP TCP lifecycle: `connect()` with 3 s timeout (on failure post `WM_RAOP_FAILED`); `OPTIONS` → `ANNOUNCE` (with SDP body from `SdpBuilder`) → `SETUP` (send client UDP ports, parse server ports from response, bind UDP sockets) → `RECORD` (on 200 OK post `WM_RAOP_CONNECTED`); monotonic `CSeq`; `Client-Instance` 16 random hex chars; `Session` token from SETUP response; `TEARDOWN` with 200 OK wait (1 s timeout) — `src/protocol/RaopSession.h`, `src/protocol/RaopSession.cpp`
- [x] T048 [US1] Implement timing port responder, NTP sync emission, and retransmit handler in `RaopSession`: `select()` multiplexing on TCP socket + control UDP + timing UDP; timing port: 32-byte request → fill `receiveTime` + `transmitTime` via `NtpClock::NowNtp64()` → send response; emit NTP sync packet on control UDP port every `ntpSyncInterval` RTP packets: 8-byte payload `[0x00, 0xD4, 0x00, 0x00, <RTP_ts_be32>, <NTP_secs_be32>]` (Thread 5 owns control socket — RT-safe, fixes I1); retransmit: parse lost `seq` range from control port → `RetransmitBuffer::Retrieve(seq)` → `sendto` audio UDP; `SET_PARAMETER` volume: `VolumeMapper::LinearToDb` → send request — `src/protocol/RaopSession.cpp`
- [x] T049 [US1] Implement `ConnectionController::Connect(AirPlayReceiver)`: allocate `SpscRingBuffer<AudioFrame, 128>` (or `<32>` if `lowLatency`), `RetransmitBuffer`, `AesCbcCipher` (random 16-byte session key + IV), `ALACEncoder` state, `Resampler` — all on Thread 1 before threads start; start Thread 3 (`WasapiCapture`), Thread 4 (`AlacEncoderThread`), Thread 5 (`RaopSession`); set `AudioStream.state = Connecting`; set tray icon to connecting (animated) — `src/core/ConnectionController.h`, `src/core/ConnectionController.cpp`
- [x] T050 [US1] Implement `ConnectionController::Disconnect`: post TEARDOWN request to Thread 5; stop Thread 4 (signal atomic stop flag + join); stop Thread 3 (signal + join); destroy session objects; set `AudioStream.state = Idle`; set tray icon to idle — `src/core/ConnectionController.cpp`
- [x] T051 [US1] Wire tray menu speaker selection → `ConnectionController::Connect` / `Disconnect` in `AppController`: clicking active speaker → `Disconnect()`; clicking different speaker while streaming → `Disconnect()` then `Connect(newReceiver)` (seamless swap, brief gap acceptable); clicking active speaker while idle → `Connect()` — `src/ui/TrayMenu.cpp`, `src/core/AppController.cpp`
- [x] T052 [US1] Wire `WM_RAOP_CONNECTED` → `AudioStream::state = Streaming` + streaming icon + save `config.lastDevice` + `BalloonNotify::ShowInfo(IDS_BALLOON_CONNECTED)` — `src/core/AppController.cpp`, `src/ui/TrayIcon.cpp`
- [x] T053 [US1] Wire `WM_RAOP_FAILED` → `AudioStream::state = Error` + error icon + `BalloonNotify::ShowError(IDS_BALLOON_CONNECTION_FAILED)` (after retry exhaustion); wire `WM_BONJOUR_MISSING` → `BalloonNotify::ShowWarning(IDS_BALLOON_BONJOUR_MISSING)` — `src/core/AppController.cpp`
- [x] T100 [US1] Implement `SpscRingBufferPtr` type-erasure in `SpscRingBuffer.h`: define `using SpscRingBufferPtr = std::variant<SpscRingBuffer<AudioFrame,128>*, SpscRingBuffer<AudioFrame,32>*>`; add free helpers `RingTryPush(SpscRingBufferPtr&, const AudioFrame&)` and `RingTryPop(SpscRingBufferPtr&, AudioFrame&)` using `std::visit`; `ConnectionController` stores `SpscRingBufferPtr` so Threads 3 and 4 receive the same erased pointer regardless of low-latency mode (prerequisite for T049) — `src/audio/SpscRingBuffer.h`

**Checkpoint**: Full pipeline functional — audio streams from AirPlay receiver within 3 s of speaker click; all US1 unit/integration/e2e tests pass; tray icon transitions Idle→Connecting→Streaming→Idle correctly

---

## Phase 4: User Story 2 — Automatic Reconnection to Last Speaker (Priority: P2)

**Goal**: On app start, if `config.lastDevice` is set, silently reconnect within a 5-second discovery window.
Tray shows animated "connecting" state throughout. If speaker not found: silent idle. If connection drops during
streaming: retry up to 3 times (exponential backoff) before showing failure balloon.

**FRs covered**: FR-008 · FR-011 · FR-009

**Independent Test**: Connect to speaker, quit, restart app, confirm audio resumes without interaction if speaker reachable. Verify 5 s expiry is silent if speaker absent.

### Tests for User Story 2

- [x] T054 [P] [US2] Write integration test: set `config.lastDevice` to shairport-sync container name; start `AppController`; verify `ConnectionController::Connect` is called automatically within 5 s without any `WM_TRAY_CONTEXTMENU` interaction — `tests/integration/test_raop_shairport.cpp`

### Implementation for User Story 2

- [x] T055 [US2] Implement startup auto-reconnect in `AppController::Start`: after mDNS thread starts, if `config.lastDevice` is non-empty → set `AudioStream.state = Connecting` → start connecting icon → `SetTimer(hwnd, TIMER_RECONNECT_WINDOW, 5000, nullptr)` — `src/core/AppController.cpp`
- [x] T056 [US2] Implement `WM_RECEIVERS_UPDATED` check during 5 s auto-reconnect window: if timer still active and `ReceiverList` contains a receiver whose `instanceName == config.lastDevice` → call `ConnectionController::Connect(receiver)` and cancel timer — `src/core/AppController.cpp`
- [x] T057 [US2] Implement `WM_TIMER` handler for reconnect-window expiry: if timer fires and still Connecting → set `AudioStream.state = Idle` → idle icon → **no notification** (silent per FR-008 / spec) — `src/core/AppController.cpp`
- [x] T058 [US2] Implement `ConnectionController::OnRaopFailed` retry logic: if `retryCount < 3` → increment counter → `SetTimer` with exponential backoff delays (1 s / 2 s / 4 s) → on timer fire → `Connect(lastReceiver)` again; after 3rd failure → `AudioStream.state = Error` → error icon → `BalloonNotify::ShowError(IDS_BALLOON_CONNECTION_FAILED)` — `src/core/ConnectionController.cpp`
- [x] T059 [US2] Implement `IDS_BALLOON_DISCONNECTED` balloon: wire `WM_RAOP_FAILED` during active Streaming state (unexpected drop) → `BalloonNotify::ShowWarning(IDS_BALLOON_DISCONNECTED)` before entering reconnect loop — `src/core/AppController.cpp`

**Checkpoint**: App auto-reconnects at startup; retry loop with 1 s/2 s/4 s backoff exhausts 3 attempts before failure balloon; 5 s expiry transitions silently to idle; `--startup` arg suppresses initial tray focus

---

## Phase 5: User Story 3 — Control Volume and Application Settings (Priority: P3)

**Goal**: Volume slider in tray menu independently controls AirPlay receiver volume. Low-latency toggle
changes buffer size. Launch-at-startup writes registry key. "Open log folder" opens `%APPDATA%\AirBeam\logs\`.
All preferences survive app restart. "Check for Updates" available in all icon states.

**FRs covered**: FR-006 · FR-007 · FR-012 · FR-013 · FR-016 · FR-018 (partial)

**Independent Test**: Move volume slider, verify receiver volume changes; enable low-latency, confirm reconnect with 32-frame buffer; toggle startup, reboot, verify presence/absence of auto-start.

### Tests for User Story 3

- [x] T060 [P] [US3] Extend `test_config.cpp`: verify `config.volume` round-trips through `Config::Save` + reload within ±0.001; verify `config.lowLatency = true` persists; verify `config.launchAtStartup` persists — `tests/unit/test_config.cpp`

### Implementation for User Story 3

- [x] T061 [US3] Implement `VolumePopup`: `CreateWindow(TRACKBAR_CLASS, ...)` as `WS_POPUP` window; range 0–100, page-size 5; position adjacent to tray icon area (query `Shell_NotifyIconGetRect`); dismiss on `WM_KILLFOCUS` or `WM_LBUTTONDOWN` outside window bounds — `src/ui/VolumePopup.h`, `src/ui/VolumePopup.cpp`
- [x] T062 [US3] Wire `VolumePopup` `WM_HSCROLL` → `volumeLinear = pos / 100.0f` → `RaopSession::SetVolume` (sends `SET_PARAMETER` via `VolumeMapper::LinearToDb`) if streaming → `Config::Save()` — `src/ui/VolumePopup.cpp`
- [x] T063 [US3] Implement low-latency mode toggle: `TrayMenu` checkmark → `config.lowLatency = !config.lowLatency` → `Config::Save()` → if currently streaming: `ConnectionController::Disconnect()` then `ConnectionController::Connect(activeReceiver)` with new ring buffer size (`SpscRingBuffer<AudioFrame, 32>` vs `<128>`) — `src/ui/TrayMenu.cpp`, `src/core/ConnectionController.cpp`
- [x] T064 [US3] Implement launch-at-startup toggle: `TrayMenu` checkmark → `StartupRegistry::Enable()` or `::Disable()` → `config.launchAtStartup = !config.launchAtStartup` → `Config::Save()`; sync checkmark state with `StartupRegistry::IsEnabled()` on menu build — `src/ui/TrayMenu.cpp`
- [x] T065 [US3] Implement "Open log folder" tray menu item: `ShellExecuteW(nullptr, L"open", logDirPath, nullptr, nullptr, SW_SHOWNORMAL)` where `logDirPath` = `%APPDATA%\AirBeam\logs\` (resolved at runtime) — `src/ui/TrayMenu.cpp`
- [x] T066 [US3] Restore `volume`, `lowLatency`, `launchAtStartup` from `Config` on `AppController::Start`: apply saved `volumeLinear` to `AudioStream`; sync tray menu checkmarks; set initial TrackBar position in `VolumePopup` — `src/core/AppController.cpp`

**Checkpoint**: Volume slider changes receiver dB within 1 s; low-latency toggle reconnects with correct buffer; startup toggle persists; "Check for Updates" always present and functional in all icon states

---

## Phase 6: User Story 4 — Resilient Streaming Under Real-World Conditions (Priority: P4)

**Goal**: Silent WASAPI re-attach on Windows default audio device change (≤1 s gap). Clean quit with RTSP
TEARDOWN ≤2 s (FR-020). `WM_ENDSESSION` handler exits within 1 s budget (FR-021). Balloon notifications
for every unrecoverable error — zero silent failures (SC-008). Silence frames maintain receiver sync.

**FRs covered**: FR-010 · FR-011 · FR-019 (focus) · FR-020 · FR-021 · SC-008

**Independent Test**: Stream audio, change Windows default audio output device, confirm gap ≤1 s; simulate disconnect; verify 3-retry balloon. Click Quit while streaming, verify TEARDOWN completes within 2 s.

### Tests for User Story 4

- [x] T067 [P] [US4] Extend `tests/integration/test_wasapi_correlation.cpp`: after 2 s of capture, programmatically change default audio output device via `IMMDeviceEnumerator::SetDefaultAudioEndpoint` (test helper) and verify capture resumes on new device within 1 s with continuous cross-correlation > 0.99 — `tests/integration/test_wasapi_correlation.cpp`

### Implementation for User Story 4

- [x] T068 [P] [US4] Implement `IMMNotificationClient` on `WasapiCapture`: `RegisterEndpointNotificationCallback` from Thread 1 at init; `OnDefaultDeviceChanged` callback posts `WM_DEFAULT_DEVICE_CHANGED` to main window — `src/audio/WasapiCapture.h`, `src/audio/WasapiCapture.cpp`
- [x] T069 [US4] Implement `WM_DEFAULT_DEVICE_CHANGED` handler in `AppController`: signal Thread 3 atomic stop flag → wait for Thread 3 acknowledge (~50 ms) → re-initialize `WasapiCapture` on new default device → restart Thread 3 capture loop; target ≤1 s total gap (SC-007) — `src/core/AppController.cpp`
- [x] T070 [US4] Implement `WM_ENDSESSION` handler in `AppController`: if `lParam & ENDSESSION_CLOSEAPP` → post TEARDOWN to Thread 5 → `WaitForSingleObject(teardownEvent, 1000)` → `ExitProcess(0)`; OS may kill if budget exceeded — acceptable per FR-021 — `src/core/AppController.cpp`
- [x] T071 [US4] Implement clean-quit `WM_DESTROY` sequence: stop Thread 2 (mDNS, join with 150 ms timeout cap — one select() cycle max); if streaming: post TEARDOWN to Thread 5, wait ≤1500 ms; stop Thread 4 (signal + join); stop Thread 3 (signal + join); call `SparkleIntegration::Cleanup()`; `Shell_NotifyIcon(NIM_DELETE, ...)`; `DestroyWindow`; total measured wall-clock budget ≤1.9 s under an active stream (add a debug-build assertion for this, FR-020) — `src/core/AppController.cpp`
- [x] T072 [US4] Implement silence-frame maintenance: when `WasapiCapture` `IAudioCaptureClient::GetBuffer` returns 0 frames (system silence), push a zero-filled `AudioFrame` to SPSC ring to keep Thread 4 streaming silence to receiver (maintains AirPlay sync, prevents receiver disconnect) — `src/audio/WasapiCapture.cpp`
- [x] T073 [US4] Implement `IDS_BALLOON_UPDATE_REJECTED` notification: wire WinSparkle's update-rejected callback (if available via `win_sparkle_set_did_not_install_update_callback`) → `BalloonNotify::ShowWarning(IDS_BALLOON_UPDATE_REJECTED)` on EdDSA signature failure — `src/core/AppController.cpp`, `src/update/SparkleIntegration.cpp`

**Checkpoint**: Default device change recovers ≤1 s; clean quit completes ≤2 s; WM_ENDSESSION handled ≤1 s; no silent failure states remain; silence frames prevent receiver disconnect

---

## Phase 7: User Story 5 — Receive and Install Software Updates Automatically (Priority: P3)

**Goal**: WinSparkle checks for updates every 24 h in background. "Check for Updates" always triggers
immediate check. `autoUpdate: false` suppresses background check but not manual check. All distributed
packages are EdDSA-signed; invalid signatures rejected with balloon notification.

**FRs covered**: FR-018 (all six sub-requirements)

**Independent Test**: Point app at test appcast URL with newer version; confirm update dialog appears within 24 h / immediately on manual check. Set `autoUpdate: false`; confirm no automatic dialog after 24 h.

### Tests for User Story 5

- [x] T074 [P] [US5] Extend `test_config.cpp`: verify `autoUpdate: false` round-trips through Config; verify `autoUpdate` defaults to `true` when key absent — `tests/unit/test_config.cpp`

### Implementation for User Story 5

- [x] T075 [P] [US5] Generate Ed25519 key pair for build pipeline; place public key in `resources/sparkle_pubkey.txt`; add `IDS_SPARKLE_PUBKEY` to all 7 locale RC files containing the PEM-encoded public key; document private key storage in `CODESIGN_PFX` / `SPARKLE_PRIVATE_KEY` Actions secrets — `resources/sparkle_pubkey.txt`, `resources/locales/strings_en.rc` (and 6 others)
- [x] T076 [US5] Complete `SparkleIntegration` init: read `IDS_SPARKLE_PUBKEY` via `StringLoader`; call `win_sparkle_set_eddsa_pub_key(pubkeyUtf8)` (Ed25519 API — not the legacy DSA variant); call `win_sparkle_set_app_details(companyName, appName, version)`; call `win_sparkle_init()` (triggers 24 h background check by default); if `config.autoUpdate == false` call `win_sparkle_set_automatic_check_for_updates(0)` after init — `src/update/SparkleIntegration.cpp`
- [x] T077 [US5] Wire "Check for Updates" tray menu item → `SparkleIntegration::CheckForUpdates()` in all four tray icon states (idle, connecting, streaming, error); ensure item is never disabled — `src/ui/TrayMenu.cpp`
- [x] T078 [US5] Set `AIRBEAM_APPCAST_URL` compile-time constant in `CMakeLists.txt` pointing to `https://<org>.github.io/airbeam/appcast.xml`; wire into `SparkleIntegration` via `configure_file` or `target_compile_definitions` — `CMakeLists.txt`, `src/update/SparkleIntegration.cpp`

**Checkpoint**: WinSparkle shows update dialog against test appcast; `autoUpdate: false` suppresses 24 h check; `IDS_BALLOON_UPDATE_REJECTED` fires on signature mismatch; "Check for Updates" functional in all states

---

## Phase 8: Testing — Mandatory Pre-Tag Gates

**Purpose**: All tests MUST pass on the local dev machine before any `vX.Y.Z` tag is created (Constitution Principle IV).

- [x] T079 [P] Run all unit tests and verify PASS: `cmake --build --preset msvc-x64-debug && ctest -R "unit" --output-on-failure`; confirm ALAC round-trip bit-exact, RTP framing, AES-128-CBC KAT, SPSC ring, Config, mDNS TXT, VolumeMapper all green — `tests/unit/`
- [x] T080 [P] Run WASAPI cross-correlation integration test: `ctest -R "wasapi_correlation" --output-on-failure`; verify Pearson r > 0.99 between source WAV and loopback capture — `tests/integration/test_wasapi_correlation.cpp`
- [x] T081 Run RAOP shairport-sync integration test: start Docker container via `docker compose up -d`; run `ctest -R "raop_shairport" --output-on-failure`; verify RTSP 200 OK for each step; verify audio UDP frames received at container — `tests/integration/test_raop_shairport.cpp`
- [x] T082 Run e2e 1 kHz sine wave test: `ctest -R "1khz_sine" --output-on-failure`; verify FFT spectral peak at 1000 Hz ±5 Hz in captured output — `tests/e2e/test_1khz_sine.cpp`
- [x] T083 Run 24 h stress test with heap profiler (WinDbg / Application Verifier attached): zero heap growth across session; RTP timestamp monotonically increasing (no drift); no crashes or assertions; `droppedFrameCount` and `udpDropCount` within acceptable thresholds — `tests/e2e/test_stress_24h.cpp` (separate file from `test_1khz_sine.cpp`; targetable independently via `ctest -R stress_24h`)
- [x] T084 [P] Run CMake locale key-sync target: `cmake --build --target check-locale-keys`; verify all 7 locale RC files have complete `IDS_*` coverage with no missing keys — `CMakeLists.txt`
- [x] T085 [P] Full Release build verification: `cmake --preset msvc-x64-release && cmake --build --preset msvc-x64-release --config Release`; verify `AirBeam.exe` produced with no warnings at `/W4 /permissive-` — `CMakeLists.txt`
- [x] T101 [P] Write `tests/unit/test_frame_accumulator.cpp`: (a) empty input → no `AudioFrame` emitted; (b) exactly 352 stereo samples → one frame emitted, no carry; (c) 300 samples then 52 samples → one frame after second push (carry-across-call boundary); (d) 704 samples → two frames emitted; (e) 1058 samples (3×352 + 2) → three frames emitted + 2-sample carry remaining — `tests/unit/test_frame_accumulator.cpp`

**Checkpoint**: All mandatory tests green; no W4 warnings; locale coverage complete; 24 h stress passes; Release build clean

---

## Phase 9: Installer, Portable ZIP & CI/CD

**Purpose**: Production-ready distribution artifacts and automated release pipeline.

- [x] T086 Write NSIS installer script: install to `%PROGRAMFILES%\AirBeam\`; bundle `AirBeam.exe`, `WinSparkle.dll`, all locale RC resources, icons; detect `dnssd.dll` via registry + `LoadLibrary` probe; if absent offer download from `support.apple.com` (localized `IDS_INSTALLER_BONJOUR_PROMPT`); create Start Menu shortcut; optional `launchAtStartup` Run-key checkbox; uninstaller removes `%PROGRAMFILES%\AirBeam\` only (user data preserved) — `installer/AirBeam.nsi`
- [x] T087 [P] Define portable ZIP contents in release pipeline: `AirBeam.exe` + `dnssd.dll` + `WinSparkle.dll` + `resources/` (icons + locale files); no installer, no registry writes; `config.json` created next to `.exe` on first run (portable mode detection in `Config`) — `installer/AirBeam.nsi` (portable section), `.github/workflows/release.yml`
- [x] T088 [P] Create `appcast.xml` template on `gh-pages` branch: `<item>` with version, URL, `<sparkle:edSignature>`, length, release notes link; document manual update procedure for each release — `.github/appcast.xml`
- [x] T089 Complete `.github/workflows/release.yml` release pipeline: trigger `push` tag `v*.*.*`; runner `windows-latest` (MSVC 2022); steps: (1) checkout (2) `cmake --preset msvc-x64-release` (3) `cmake --build` (4) `signtool` sign with `CODESIGN_PFX` secret if present (5) `makensis AirBeam.nsi` → setup exe (6) `Compress-Archive` → portable zip (7) `winsparkle-sign` with `SPARKLE_PRIVATE_KEY` secret → `edSignature` (8) `gh release create vX.Y.Z` + upload both artifacts (9) update `appcast.xml` `<item>` (10) push `appcast.xml` to `gh-pages` — `.github/workflows/release.yml`
- [x] T090 Validate NSIS installer on clean Windows 10 (build 1903+) VM: install, launch, verify tray icon appears; uninstall, verify `%APPDATA%\AirBeam\` preserved; repeat on Windows 11 — `installer/AirBeam.nsi`
- [x] T097 [P] Create `CONTRIBUTORS.md` in repo root with initial maintainer entry and PR acknowledgement policy (Constitution §Funding MUST: all merged-PR contributors acknowledged) — `CONTRIBUTORS.md`
- [x] T098 [P] Create `CONTRIBUTING.md` with contribution workflow: fork → branch → PR → review → merge; commit message format; code style notes; how to run unit tests locally — `CONTRIBUTING.md`
- [x] T099 [P] Update `README.md`: add BMAC badge near top (resolve `TODO(BMAC_USERNAME)` in constitution and `FUNDING.yml`); project description; install instructions; links to `CONTRIBUTING.md` and releases page (Constitution §Funding MUST: BMAC badge in README) — `README.md`, `FUNDING.yml`

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Build hygiene, RT-safety audit, and final validation before tagging v1.0.

- [x] T091 [P] Add `CMakeLists.txt` `configure_file` step: inject CMake `PROJECT_VERSION_MAJOR/MINOR/PATCH` into `resources/AirBeam.rc` `VERSIONINFO` at configure time so version is single-source-of-truth — `CMakeLists.txt`, `resources/AirBeam.rc`
- [x] T092 [P] RT-safety audit of Threads 3 and 4 hot paths: grep source for `new`, `malloc`, `delete`, `free`, `std::mutex`, `std::lock_guard`, `WaitForSingleObject` (outside event), `fwrite`, `fprintf`, `OutputDebugString`; document any violation and remediate before tagging — `src/audio/WasapiCapture.cpp`, `src/audio/AlacEncoderThread.cpp`
- [x] T093 [P] Validate AirPlay 2-only receiver handling end-to-end: start shairport-sync container with `et=4` only TXT record; verify grayed-out menu item with `IDS_LABEL_AIRPLAY2_UNSUPPORTED`; verify clicking grayed item has no effect and no connection attempt — `src/ui/TrayMenu.cpp`
- [x] T094 [P] Validate portable mode end-to-end: place `AirBeam.exe` + DLLs in a temp folder with no `%APPDATA%` config; run app; verify `config.json` created in exe directory; change volume; verify persisted in portable `config.json` not `%APPDATA%` — `src/core/Config.cpp`
- [x] T095 Run `quickstart.md` manual validation checklist: (a) cold-start mDNS discovery within 10 s; (b) streaming within 3 s of speaker click; (c) volume slider controls receiver; (d) startup auto-reconnect; (e) low-latency toggle; (f) log folder opens; (g) Check for Updates responds; (h) Quit while streaming completes cleanly — `specs/001-airplay-audio-sender/quickstart.md`
- [x] T096 [P] Final code review: verify `Config::Save` uses atomic temp-file rename (not direct write); verify `Logger` is never called from Threads 3/4; verify `BonjourLoader` null-checks all function pointers before every call site; verify `win_sparkle_cleanup()` called in `WM_DESTROY` — `src/core/Config.cpp`, `src/core/Logger.cpp`, `src/discovery/BonjourLoader.cpp`, `src/update/SparkleIntegration.cpp`

---

## Dependencies & Execution Order

### Phase Dependencies

```
Phase 1 (Setup)
  └─ No dependencies — start immediately

Phase 2 (Foundational)
  └─ Depends on Phase 1 completion
  └─ BLOCKS all user story phases

Phase 3 (US1 — MVP)
  └─ Depends on Phase 2 completion
  └─ Tests (T022–T029) can be written in parallel with implementation
  └─ Implementation within Phase 3 follows: PODs → lock-free structures → discovery
     → audio pipeline → protocol → orchestration

Phase 4 (US2 — Reconnect)
  └─ Depends on Phase 3 checkpoint (ConnectionController functional)

Phase 5 (US3 — Settings)
  └─ Depends on Phase 3 checkpoint (streaming pipeline functional)
  └─ Can proceed in parallel with Phase 4

Phase 6 (US4 — Resilience)
  └─ Depends on Phase 3 checkpoint
  └─ Can proceed in parallel with Phases 4 & 5

Phase 7 (US5 — Updates)
  └─ Depends on Phase 2 (SparkleIntegration skeleton)
  └─ Can proceed in parallel with Phases 4–6

Phase 8 (Testing Gates)
  └─ Depends on Phases 3–7 complete

Phase 9 (Installer / CI)
  └─ Depends on Phase 8 all-green

Phase 10 (Polish)
  └─ Can begin after Phase 3; final items depend on Phase 9
```

### User Story Dependencies

| Story | Depends On | Notes |
|-------|-----------|-------|
| **US1 (P1)** | Foundational (Phase 2) | Core pipeline; no story dependency |
| **US2 (P2)** | US1 `ConnectionController` | Reconnect builds on connect/disconnect |
| **US3 (P3)** | US1 streaming pipeline | Volume/settings layer on active stream |
| **US4 (P4)** | US1 Threads 3+4+5 | Resilience wraps the existing threads |
| **US5 (P3)** | Phase 2 `SparkleIntegration` skeleton | Update independent of audio pipeline |

### Within Phase 3 (US1) Critical Path

```
T030 AudioFrame POD ─┐
T031 RtpPacket POD  ─┤
                     ├─→ T032 SpscRingBuffer ──────────→ T044 WasapiCapture (T3)
                     │                                         ↓
T033 TxtRecord ──────┤                                   T045 AlacEncoderThread (T4)
T034 BonjourLoader ──┤                                         ↓ (SPSC consumer)
                     └─→ T035 ReceiverList                T046 UDP Send + NTP sync
                               ↓
                         T036 MdnsDiscovery (T2)      T038 AesCbcCipher ─┐
                               ↓                      T039 RsaKeyWrap   ─┤
                         T037 TrayMenu speakers        T040 NtpClock     ─┤
                                                       T041 RetransmitBuf─┤
                                                       T042 SdpBuilder   ─┤
                                                                          └→ T047 RaopSession (T5)
                                                                                  ↓
                                                                            T048 Timing+Retransmit
                                                                                  ↓
                                                       T049 ConnectionController::Connect
                                                       T050 ConnectionController::Disconnect
                                                       T051 Wire tray → ConnectionController
                                                       T052 Wire WM_RAOP_CONNECTED
                                                       T053 Wire WM_RAOP_FAILED
```

---

## Parallel Examples

### Parallel Execution — Phase 3 (US1) Test Writing

```
# All 8 US1 test files can be written in parallel:
Task T022: tests/unit/test_spsc_ring.cpp
Task T023: tests/unit/test_mdns_txt.cpp
Task T024: tests/unit/test_alac_roundtrip.cpp
Task T025: tests/unit/test_rtp_framing.cpp
Task T026: tests/unit/test_aes_vectors.cpp
Task T027: tests/integration/test_wasapi_correlation.cpp
Task T028: tests/integration/test_raop_shairport.cpp
Task T029: tests/e2e/test_1khz_sine.cpp
```

### Parallel Execution — Phase 3 (US1) Protocol Components

```
# These protocol components have no inter-dependencies:
Task T038: src/protocol/AesCbcCipher.{h,cpp}
Task T039: src/protocol/RsaKeyWrap.{h,cpp}
Task T040: src/protocol/NtpClock.{h,cpp}
Task T041: src/protocol/RetransmitBuffer.{h,cpp}
Task T042: src/protocol/SdpBuilder.{h,cpp}
Task T043: src/audio/Resampler.{h,cpp}
```

### Parallel Execution — User Stories (after Phase 3 complete)

```
# All four stories can be worked independently after Phase 3 checkpoint:
Developer A: Phase 4 — US2 (Reconnect) — src/core/AppController.cpp (reconnect path)
Developer B: Phase 5 — US3 (Settings)  — src/ui/VolumePopup.{h,cpp}, TrayMenu.cpp
Developer C: Phase 6 — US4 (Resilience) — src/audio/WasapiCapture.cpp (device change)
Developer D: Phase 7 — US5 (Updates)   — src/update/SparkleIntegration.cpp
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete **Phase 1**: Setup — CMake builds cleanly
2. Complete **Phase 2**: Foundational — tray icon visible; Config/Logger working; unit tests pass
3. Complete **Phase 3**: User Story 1 — full streaming pipeline; all US1 tests pass
4. **STOP AND VALIDATE**: Stream audio to real AirPlay receiver; confirm SC-001 through SC-005
5. Demo / internal release of MVP

### Incremental Delivery

| Stage | Delivers | Validate |
|-------|---------|---------|
| MVP (P1+P2) | Streaming + Foundation | Stream audio; observe tray states |
| + US2 | Auto-reconnect | Quit+restart; audio resumes without click |
| + US3 | Volume + Settings | Slider controls dB; low-latency toggle; startup |
| + US4 | Resilience | Device change ≤1 s; clean quit ≤2 s; no silent failures |
| + US5 | Auto-Updates | Update dialog on test appcast; EdDSA rejection balloon |
| + Phase 8 | Mandatory tests all green | ALAC/RTP/AES/SPSC/WASAPI/RAOP/1kHz/24h |
| + Phase 9 | NSIS installer; portable ZIP; CI/CD | Install on clean VM; release tag fires pipeline |

---

## Summary

| Phase | Tasks | Story | Parallelizable |
|-------|------:|------:|---------------:|
| Phase 1: Setup | T001–T006 | — | 5 of 6 |
| Phase 2: Foundational | T007–T021 | — | 9 of 15 |
| Phase 3: US1 Stream Audio (MVP) | T022–T053, T100 | US1 | 17 of 33 |
| Phase 4: US2 Auto-Reconnect | T054–T059 | US2 | 1 of 6 |
| Phase 5: US3 Settings + Controls | T060–T066 | US3 | 1 of 7 |
| Phase 6: US4 Resilience | T067–T073 | US4 | 2 of 7 |
| Phase 7: US5 Auto-Updates | T074–T078 | US5 | 1 of 5 |
| Phase 8: Testing Gates | T079–T085, T101 | — | 5 of 8 |
| Phase 9: Installer + CI/CD | T086–T090, T097–T099 | — | 5 of 8 |
| Phase 10: Polish | T091–T096 | — | 5 of 6 |
| **Total** | **101 tasks** | | **52 parallelizable** |

**Key FRs mapped to tasks**: FR-017 (7-language l10n, Windows locale) → T009, T010, T011, T084; FR-018 (WinSparkle, EdDSA) → T013, T075, T076, T077, T078; FR-019 (single-instance) → T018; FR-020 (clean quit TEARDOWN ≤2 s) → T071; FR-021 (WM_ENDSESSION ≤1 s) → T070.

**Suggested MVP scope**: Phases 1–3 (T001–T053). At Phase 3 checkpoint, the entire core value proposition is deliverable and independently testable.

