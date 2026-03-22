# AirBeam — Windows AirPlay Audio Sender

## Project Overview

**AirBeam** is a lightweight Windows system tray application that captures system audio via WASAPI loopback and streams it in real-time to AirPlay (RAOP) receivers on the local network. The user selects a discovered AirPlay endpoint from the tray menu; the application then transparently captures all audio playing on the default output device and forwards it to the selected AirPlay speaker(s).

### Target Platform

- Windows 10 (1903+) and Windows 11
- x86-64 architecture only
- No kernel-mode driver required

### Core Value Proposition

Replaces the now-discontinued Airfoil for Windows and the limited TuneBlade with a modern, open-source alternative. Zero configuration beyond selecting the target speaker.

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                     AirBeam Process                      │
│                                                          │
│  ┌─────────────┐   ┌──────────────┐   ┌──────────────┐  │
│  │  Tray UI    │   │ Audio Engine │   │  AirPlay     │  │
│  │  (Win32/WPF)│◄─►│  (WASAPI     │──►│  Sender      │  │
│  │             │   │   Loopback)  │   │  (RAOP/RTP)  │  │
│  └─────────────┘   └──────┬───────┘   └──────┬───────┘  │
│                           │                   │          │
│  ┌─────────────┐          │           ┌──────┴───────┐  │
│  │  mDNS       │          │           │  ALAC        │  │
│  │  Discovery  │          │           │  Encoder     │  │
│  │  (DNS-SD)   │          │           └──────────────┘  │
│  └─────────────┘          │                              │
│                    ┌──────┴───────┐                      │
│                    │  Resampler   │                      │
│                    │  (if needed) │                      │
│                    └──────────────┘                      │
└──────────────────────────────────────────────────────────┘
         │                                    │
         │ mDNS queries/responses             │ RTP/UDP audio
         │ (UDP 5353)                         │ RTSP/TCP control
         ▼                                    ▼
   ┌──────────┐                        ┌──────────────┐
   │  Local    │                        │  AirPlay     │
   │  Network  │                        │  Receiver(s) │
   └──────────┘                        └──────────────┘
```

### Component Responsibilities

| Component | Responsibility |
|-----------|---------------|
| **Tray UI** | System tray icon, context menu with discovered speakers, volume control, status display |
| **mDNS Discovery** | Continuous discovery of `_raop._tcp` services via DNS-SD on the local network |
| **Audio Engine** | WASAPI loopback capture from the default audio render endpoint |
| **Resampler** | Sample rate conversion to 44100 Hz stereo S16LE if the system output differs |
| **ALAC Encoder** | Encode PCM frames to Apple Lossless for RAOP transport |
| **AirPlay Sender** | RTSP session management, RTP audio packet framing, timing/sync protocol |

---

## Technology Stack

| Layer | Technology | Rationale |
|-------|-----------|-----------|
| Language | **C++17** or **Rust** | Low-latency audio path; direct Win32/COM API access. Rust preferred for memory safety if developer is proficient. |
| UI | **Win32 Shell_NotifyIcon** | Minimal tray app, no heavy UI framework needed. Alternatively WinUI 3 for a modern context menu. |
| Audio capture | **WASAPI** (Windows Audio Session API) | Loopback mode captures post-mix audio from any render endpoint. User-mode, no driver. |
| mDNS | **dns-sd** (Apple Bonjour SDK) or **mdns-sd** (Rust crate) or **embedded mDNSResponder** | Required for `_raop._tcp` service discovery. Bonjour SDK is redistributable and battle-tested. |
| ALAC encoding | **Apple ALAC** (open source, Apache 2.0) | Reference encoder from https://github.com/macosforge/alac |
| Resampling | **libsamplerate** or **r8brain-free-src** | High-quality sample rate conversion if system output ≠ 44100 Hz |
| Networking | **OS sockets** (Winsock2) | Raw TCP for RTSP, raw UDP for RTP and timing. No external dependency needed. |
| Build | **CMake** (C++) or **Cargo** (Rust) | Cross-IDE build system |

### External Dependencies

- **Bonjour SDK for Windows** (Apple, free redistribution) — or bundle `dns-sd.exe` / `mDNSResponder.dll`. Note: many Windows machines already have Bonjour installed (via iTunes, iCloud). The installer should check and optionally install the Bonjour Print Services package if absent.
- **Apple ALAC reference encoder** — compile from source, statically link.

---

## Detailed Component Specifications

### 1. WASAPI Loopback Audio Capture

#### Initialization

1. Enumerate audio render endpoints via `IMMDeviceEnumerator`.
2. Obtain the default audio render endpoint (`eRender`, `eConsole`).
3. Open the device in **loopback** capture mode:
   - `IAudioClient::Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, ...)`.
   - Buffer duration: 20 ms (request `hnsBufferDuration = 200000`). This provides a good trade-off between latency and CPU wake-up frequency.
4. Retrieve the mix format via `IAudioClient::GetMixFormat()`. Typical Windows output: 48000 Hz, 32-bit float, stereo. The capture format will match.
5. Obtain `IAudioCaptureClient` interface.
6. Register for `IMMNotificationClient` to detect default device changes (user switches output) and automatically re-attach loopback.

#### Capture Loop

- Dedicated capture thread, MMCSS-boosted (`AvSetMmThreadCharacteristics("Audio", ...)`).
- Event-driven: use `IAudioClient::SetEventHandle()` with a `WaitForSingleObject` loop.
- On each wake:
  1. `IAudioCaptureClient::GetBuffer()` → retrieve PCM frames.
  2. Push frames into a lock-free ring buffer (SPSC) shared with the encoder thread.
  3. `IAudioCaptureClient::ReleaseBuffer()`.
- Handle silence padding: WASAPI loopback delivers silence-flagged buffers when no audio is playing. Pass these through (AirPlay receivers expect continuous audio frames to maintain sync).

#### Sample Format Handling

| System output format | Action |
|---------------------|--------|
| 44100 Hz, 16-bit int, stereo | Direct pass-through to ALAC encoder |
| 44100 Hz, 32-bit float, stereo | Convert float → S16LE (scale, clamp, truncate) |
| 48000 Hz, 32-bit float, stereo | Resample 48000→44100 + float→S16LE conversion |
| Other rates | Resample to 44100 + format conversion |
| Mono / >2 channels | Downmix to stereo |

AirPlay 1 (RAOP) expects: **44100 Hz, 16-bit signed integer, stereo (interleaved)**.

#### Default Device Change Handling

When `IMMNotificationClient::OnDefaultDeviceChanged` fires:
1. Stop current capture session.
2. Re-initialize loopback on the new default device.
3. Maintain AirPlay session (no reconnect needed — just a brief audio gap).

---

### 2. mDNS / DNS-SD Service Discovery

#### Service Type

Query for: `_raop._tcp.local.`

AirPlay audio receivers advertise themselves as RAOP (Remote Audio Output Protocol) services via DNS-SD.

#### Discovery Flow

1. On application start, begin continuous browsing for `_raop._tcp`.
2. For each discovered service, resolve to:
   - Hostname / IP address
   - Port (typically 7000 for AirPlay, but varies)
   - TXT record fields (contains feature flags, encryption type, codec support, device model, etc.)
3. Parse TXT record for:
   - `am` — device model (e.g., `AppleTV5,3`, `AudioAccessory5,1` for HomePod)
   - `md` — device name (user-visible name)
   - `sf` / `ft` / `fv` — feature flags (determine AirPlay 1 vs 2 capability, encryption requirements)
   - `et` — encryption types supported (0=none, 1=RSA, 3=FairPlay, 4=MFi)
   - `cn` — codecs supported (0=PCM, 1=ALAC, 2=AAC, 3=AAC ELD)
4. Maintain a live list of available receivers. Remove stale entries on DNS-SD goodbye packets or after a configurable timeout (default: 60 s without re-advertisement).

#### AirPlay 1 vs AirPlay 2 Detection

- AirPlay 1 receivers: `et` includes `1` (RSA encryption), no HomeKit pairing required.
- AirPlay 2 receivers (HomePod, newer Apple TV): `et` includes `4` (MFi) or require HomeKit pairing (SRP/Ed25519). **AirPlay 2 pairing is not implemented in v1.0 of this spec** — see Future Work.

The initial implementation targets **AirPlay 1** receivers only. AirPlay 2 devices that fall back to AirPlay 1 (many do when probed correctly) should also work.

---

### 3. AirPlay 1 (RAOP) Sender Protocol

The RAOP protocol is RTSP-based with RTP audio transport. The sequence:

#### 3.1 RTSP Session Setup

All RTSP communication happens over a single TCP connection to the receiver's advertised port.

```
Client                                    Receiver
  │                                          │
  │─── OPTIONS * RTSP/1.0 ─────────────────►│
  │◄── 200 OK (Public: ANNOUNCE, ...) ──────│
  │                                          │
  │─── ANNOUNCE rtsp://... RTSP/1.0 ───────►│
  │    SDP body:                             │
  │      a=rtpmap:96 AppleLossless           │
  │      a=fmtp:96 352 0 16 40 10 14 2      │
  │        44100 0 0                         │
  │      a=rsaaeskey:<base64 AES key>        │
  │      a=aesiv:<base64 AES IV>             │
  │◄── 200 OK ──────────────────────────────│
  │                                          │
  │─── SETUP rtsp://... RTSP/1.0 ──────────►│
  │    Transport: RTP/AVP/UDP;unicast;       │
  │      interleaved=0-1;                    │
  │      control_port=<C>;timing_port=<T>    │
  │◄── 200 OK                               │
  │    Transport: ...;server_port=<S>;       │
  │      control_port=<SC>;timing_port=<ST>  │
  │                                          │
  │─── RECORD rtsp://... RTSP/1.0 ─────────►│
  │◄── 200 OK ──────────────────────────────│
  │                                          │
  │   === Audio streaming begins ===         │
  │                                          │
  │─── SET_PARAMETER (volume) ──────────────►│
  │◄── 200 OK ──────────────────────────────│
  │                                          │
  │─── TEARDOWN ────────────────────────────►│
  │◄── 200 OK ──────────────────────────────│
```

#### 3.2 Encryption

AirPlay 1 uses RSA + AES-CBC encryption for the audio stream:

1. **Generate a random 128-bit AES key and 128-bit IV** per session.
2. **Encrypt the AES key** with the AirPlay RSA public key (well-known, 2048-bit). The public key is embedded in the application (it's the same for all AirPlay 1 receivers — extracted from Apple's firmware, widely published).
3. Include the RSA-encrypted AES key and the IV in the ANNOUNCE SDP body (`a=rsaaeskey`, `a=aesiv`).
4. **Encrypt each audio packet payload** with AES-128-CBC using the generated key/IV.

The RSA public key (base64, PEM format) is a fixed constant to embed in source.

#### 3.3 RTP Audio Packet Format

Each RTP packet carries one ALAC frame (352 samples = ~7.98 ms at 44100 Hz).

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X| CC=0  |M|  PT=96     |       Sequence Number          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                              SSRC                             |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    AES-encrypted ALAC payload                 |
|                             ...                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- Payload type: 96 (dynamic, negotiated in ANNOUNCE)
- Timestamp: increments by 352 per packet (one ALAC frame)
- SSRC: random, constant per session
- Sequence number: monotonically incrementing uint16, wraps

#### 3.4 Timing Synchronization Protocol

A UDP-based NTP-like protocol between sender and receiver for clock synchronization.

- **Timing port**: sender opens a UDP socket, sends timing request packets to the receiver's timing port.
- Packet format: 32-byte packet containing a reference timestamp and a receive timestamp (NTP-format: seconds since 1900-01-01 + fractional seconds as uint32).
- The receiver echoes the packet with its own timestamps, allowing round-trip time estimation.
- Timing requests are sent every 3 seconds during streaming.

#### 3.5 Control Channel

A UDP channel for sync/resend control:

- **Retransmit requests**: the receiver may request retransmission of lost packets via this channel.
- **Sync packets**: the sender periodically (every ~second) sends sync packets containing the current RTP timestamp and corresponding NTP timestamp, so the receiver can map RTP time to wall-clock time for buffer management.

#### 3.6 Volume Control

Volume is set via RTSP `SET_PARAMETER`:

```
SET_PARAMETER rtsp://... RTSP/1.0
Content-Type: text/parameters

volume: -20.0
```

Volume range: `-144.0` (mute) to `0.0` (full). Scale is dB-like but not precisely calibrated. Map the UI slider (0–100%) to this range with a sensible curve (e.g., linear in dB: -30.0 to 0.0 for the usable range).

---

### 4. ALAC Encoding

#### Parameters

| Parameter | Value |
|-----------|-------|
| Sample rate | 44100 Hz |
| Bit depth | 16 |
| Channels | 2 (stereo) |
| Frame size | 352 samples |
| Compatible version | 0 |
| Max coded frame size | ~4096 bytes (worst case, uncompressible audio) |

#### ALAC `fmtp` String

The SDP `fmtp` line describes the ALAC configuration:

```
a=fmtp:96 352 0 16 40 10 14 2 44100 0 0
```

Fields: `frameLength maxRun bitDepth historyMult initialHistory kModifier channels sampleRate maxFrameBytes avgBitRate avgFrameBytes`

#### Implementation

Use the Apple ALAC open-source encoder directly. It is self-contained C++ with no external dependencies. Statically link the encoder.

Input: interleaved S16LE stereo PCM, 352 samples per call.
Output: variable-length ALAC compressed frame.

---

### 5. Tray Application UI

#### System Tray Icon

- Default state: greyed-out speaker icon (not connected).
- Active state: colored speaker icon with "waves" indicating streaming.
- Error state: icon with red exclamation mark.

#### Context Menu (right-click)

```
┌────────────────────────────────────┐
│  AirBeam                      v1.0 │
│────────────────────────────────────│
│  ▸ Living Room (HomePod)        ○  │
│  ▸ Kitchen (AirPort Express)    ○  │
│  ▸ Office (Apple TV)            ○  │
│────────────────────────────────────│
│  Volume  ████████░░░░  75%         │
│────────────────────────────────────│
│  ☐ Launch at startup               │
│  ☐ Low-latency mode                │
│────────────────────────────────────│
│  Quit                              │
└────────────────────────────────────┘
```

- **Speaker list**: dynamically populated from mDNS discovery. Each entry is a toggle (click to connect / disconnect). Multiple simultaneous connections: **defer to v2.0** — v1.0 supports single target only.
- **Volume slider**: controls AirPlay volume via `SET_PARAMETER`, not the local Windows volume.
- **Low-latency mode**: when enabled, uses a shorter audio buffer (higher risk of dropouts on congested Wi-Fi, but lower end-to-end latency). Default: off (buffered mode for stability).
- **Launch at startup**: writes/removes a `Run` registry key under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

#### Notifications

- Balloon/toast notification on first connection: "Now streaming to [Device Name]".
- Notification on disconnect (unexpected): "Lost connection to [Device Name]".

---

### 6. Configuration & State Persistence

Store settings in `%APPDATA%\AirBeam\config.json`:

```json
{
  "version": 1,
  "launchAtStartup": false,
  "lowLatencyMode": false,
  "lastDevice": "AA:BB:CC:DD:EE:FF",
  "volume": 75,
  "bufferDurationMs": 500
}
```

- `lastDevice`: MAC address of last-connected receiver. On startup, auto-connect if the device is discovered within 5 s.
- `bufferDurationMs`: audio buffer depth on the sender side before transmission. Default 500 ms (buffered mode), 100 ms (low-latency mode).

---

## Threading Model

```
┌────────────────────────────────────────────────────┐
│ Thread 1: UI / Main                                │
│   - Win32 message loop                             │
│   - Tray icon, context menu                        │
│   - User interaction dispatch                      │
├────────────────────────────────────────────────────┤
│ Thread 2: mDNS Discovery                           │
│   - Bonjour DNSServiceBrowse callback loop         │
│   - Updates shared device list (mutex-protected)   │
├────────────────────────────────────────────────────┤
│ Thread 3: Audio Capture (MMCSS "Audio")            │
│   - WASAPI event-driven loopback capture           │
│   - Pushes raw PCM into SPSC ring buffer           │
├────────────────────────────────────────────────────┤
│ Thread 4: Encoder + Sender                         │
│   - Reads from ring buffer                         │
│   - Resamples if necessary                         │
│   - ALAC encodes (352-sample frames)               │
│   - AES-CBC encrypts                               │
│   - Sends RTP packets via UDP                      │
│   - Sends timing sync packets                      │
├────────────────────────────────────────────────────┤
│ Thread 5: RTSP Control                             │
│   - Manages RTSP TCP session                       │
│   - Handles retransmit requests from receiver      │
│   - Sends periodic sync packets                    │
└────────────────────────────────────────────────────┘
```

The critical real-time path is Thread 3 → ring buffer → Thread 4. The ring buffer must be lock-free (SPSC) to avoid priority inversion.

---

## Latency Budget

| Stage | Estimated latency |
|-------|------------------|
| WASAPI loopback capture buffer | 20 ms |
| Ring buffer transit | ~5 ms |
| ALAC encoding (352 samples) | <1 ms |
| Sender-side buffer (buffered mode) | 500 ms |
| Sender-side buffer (low-latency mode) | 100 ms |
| Network transmission (LAN) | <5 ms |
| Receiver-side buffer (device-dependent) | 500–2000 ms |
| **Total (buffered)** | **~1000–2500 ms** |
| **Total (low-latency)** | **~130–300 ms** |

Note: receiver-side buffering is not under our control. AirPlay 1 receivers typically buffer 1–2 s for robustness. AirPlay 2 devices in buffered mode can add significantly more.

---

## Error Handling

| Condition | Behavior |
|-----------|----------|
| AirPlay receiver unreachable | Retry RTSP connection 3× with exponential backoff (1 s, 2 s, 4 s). If all fail, show error notification and revert to disconnected state. |
| Wi-Fi dropout during streaming | Detect via RTSP TCP socket error or RTP send failure. Attempt reconnect once network is available. |
| Default audio device changes | Seamlessly re-attach WASAPI loopback to new device. Brief audio gap (~50 ms) is acceptable. |
| Receiver requests retransmit | Maintain a sliding window of the last 512 RTP packets. Retransmit from cache if available; ignore if packet has already fallen out of window. |
| ALAC encoder error | Should not occur with valid PCM input. Log and skip frame. |
| Bonjour service unavailable | Show notification suggesting user install Bonjour Print Services. Provide download link in tray menu. |

---

## Build & Distribution

### Build System

- CMake 3.20+ (C++) or Cargo (Rust)
- MSVC toolchain (Visual Studio 2022 Build Tools)
- Static linking of ALAC encoder, libsamplerate
- Dynamic linking of Bonjour SDK (`dnssd.dll`)

### Installer

- **NSIS** or **WiX** based MSI installer
- Check for Bonjour runtime; offer to install if missing
- Register startup entry if user opts in
- Install to `%PROGRAMFILES%\AirBeam\`

### Portable Mode

- If a `config.json` exists next to the executable, use it instead of `%APPDATA%`. This enables portable/USB operation.

---

## Testing Strategy

| Test category | Approach |
|---------------|----------|
| Unit: ALAC encoding | Encode known PCM → decode with reference decoder → bit-exact comparison |
| Unit: RTP packet framing | Verify header fields, sequence numbering, timestamp increments |
| Unit: AES encryption | Encrypt/decrypt round-trip with known key/IV |
| Integration: WASAPI capture | Automated test: play a known WAV via Windows audio, capture via loopback, compare waveform (cross-correlation > 0.99) |
| Integration: mDNS discovery | Use a mock mDNS responder (e.g., Avahi on WSL2) advertising a fake `_raop._tcp` service |
| Integration: RAOP session | Use shairport-sync running in WSL2/Docker as a test receiver. Verify RTSP handshake, audio playback. |
| End-to-end | Stream a 1 kHz sine wave → capture output at AirPlay receiver → verify frequency and continuity |
| Stress: long-running | Stream for 24 h, verify no memory leaks (track working set), no audio drift |

### Reference Test Receivers

- **shairport-sync** (Linux, open source) — gold standard for AirPlay 1 protocol validation
- **AirPort Express** (hardware) — real Apple hardware baseline
- **Apple TV** (hardware) — tests both AirPlay 1 fallback and feature negotiation

---

## Future Work (v2.0+)

- **AirPlay 2 support**: HomeKit pairing (SRP-6a, Ed25519, Curve25519), buffered audio mode. Significant protocol engineering effort.
- **Multi-room streaming**: simultaneous output to multiple AirPlay receivers with synchronized playback.
- **Per-application audio capture**: use WASAPI process loopback (Windows 10 2004+, `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY`) to capture audio from a specific application instead of system-wide.
- **Virtual audio device**: register as a Windows audio endpoint via a user-mode Audio Processing Object (APO) or Audio Endpoint Builder, if feasible without a kernel driver.
- **Audio-video sync**: adjustable latency offset for synchronizing with video playback on the PC.
- **DLNA/Chromecast output**: extend to non-AirPlay receivers.

---

## License

Recommend **MIT** for maximum adoption, given that the Apple ALAC reference encoder is Apache 2.0 (compatible). If using shairport-sync code as reference, note it is GPL — do not copy code, only use for protocol understanding.

---

## References

1. **RAOP Protocol**: Unofficial documentation at https://nto.github.io/AirPlay.html (comprehensive reverse-engineering reference)
2. **Apple ALAC Encoder**: https://github.com/macosforge/alac (Apache 2.0)
3. **shairport-sync**: https://github.com/mikebrady/shairport-sync (GPL, reference AirPlay receiver)
4. **WASAPI Loopback**: Microsoft documentation at https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording
5. **Bonjour SDK**: https://developer.apple.com/bonjour/ (redistributable)
6. **SYSVAD Sample** (for future virtual device work): https://github.com/microsoft/Windows-driver-samples/tree/main/audio/sysvad
