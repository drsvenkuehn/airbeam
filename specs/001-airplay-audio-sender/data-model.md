# Data Model: AirBeam — Windows AirPlay Audio Sender

**Phase 1 Output** | Branch: `001-airplay-audio-sender` | Date: 2026-03-21

---

## Entities

### 1. `AirPlayReceiver`

A discovered AirPlay-capable device on the local network, found via mDNS/DNS-SD.

| Field | Type | Source | Description |
|-------|------|--------|-------------|
| `instanceName` | `std::wstring` | mDNS PTR record | Bonjour service instance name (unique per device, used as `lastDevice` key) |
| `displayName` | `std::wstring` | mDNS PTR record / TXT `an` | Human-readable speaker name shown in tray menu |
| `hostName` | `std::string` | mDNS resolve | Hostname for TCP/UDP connection |
| `ipAddress` | `std::string` | DNSServiceGetAddrInfo | IPv4 address (AirPlay 1 uses IPv4) |
| `port` | `uint16_t` | mDNS resolve | RTSP port (default 5000) |
| `encryptionTypes` | `std::string` | TXT `et` | Raw `et` value, e.g. `"1"`, `"0,1"`, `"4"` |
| `deviceModel` | `std::string` | TXT `am` | Device model string, e.g. `"AirPort5,1"`, `"AudioAccessory1,1"` |
| `protocolVersion` | `std::string` | TXT `vs` | Advertised AirPlay version string |
| `isAirPlay1Compatible` | `bool` | Derived | `true` when `encryptionTypes` contains `"1"` AND `pk` TXT key absent |
| `lastSeenTime` | `ULONGLONG` (100-ns ticks) | Updated by mDNS refresh | For 60-second stale-entry eviction |

**Validation rules**:
- `instanceName` must be non-empty (primary key)
- `port` must be in range 1–65535
- `isAirPlay1Compatible` is derived on construction and never mutated separately

**State**: Receivers are immutable value objects once constructed; `ReceiverList` holds them by value and replaces on mDNS refresh.

---

### 2. `AudioStream` (State Machine)

Represents the active streaming session. Only one can exist at a time (v1.0 scope).

**States**:
```
    ┌─────────────────────────────────────────┐
    │                                         │
    ▼                                         │
  IDLE ──[user selects device]──► CONNECTING  │
    ▲                                 │       │
    │                          [RAOP ok]      │
    │                                 │       │
    │                                 ▼       │
    │                            STREAMING ───┤
    │                                 │       │
    │                         [connection lost]│
    │                                 │       │
    │                           RECONNECTING  │
    │                            (up to 3x)   │
    │                                 │       │
    │                        [3 retries fail] │
    │                                 │       │
    └────[user disconnects]───── ERROR ───────┘
                                  │
                          [user dismisses /
                           new connect]
                                  │
                                 IDLE
```

| Field | Type | Description |
|-------|------|-------------|
| `state` | `std::atomic<StreamState>` | Current state (Idle/Connecting/Streaming/Reconnecting/Error); atomic so Thread 1 can read without lock |
| `activeReceiver` | `std::optional<AirPlayReceiver>` | Set when state ≥ Connecting; guarded by Thread 1 access only |
| `volumeLinear` | `float` | 0.0–1.0 linear; applied via `SET_PARAMETER` when streaming |
| `retryCount` | `int` | Current reconnect attempt (0–3); reset to 0 on successful connect |
| `sessionStartTime` | `ULONGLONG` | Win32 tick count at RECORD success; for telemetry / stress test |

**Transitions**:
| From | Event | To | Side Effect |
|------|-------|----|-------------|
| Idle | UserSelectsDevice | Connecting | Start Threads 3+4+5; pulsing icon |
| Connecting | RaopConnected | Streaming | Streaming icon; save `lastDevice` to config |
| Connecting | RaopFailed | Reconnecting | ExponentialBackoff timer set |
| Streaming | ConnectionLost | Reconnecting | Stop Thread 4 RTP; backoff timer |
| Reconnecting | RaopConnected | Streaming | Resume Thread 4; streaming icon |
| Reconnecting | AllRetriesExhausted | Error | Error icon; balloon notification |
| Streaming | UserDisconnects | Idle | TEARDOWN; stop Threads 3+4 |
| Error | UserSelectsDevice | Connecting | Reset retry count; restart attempt |

---

### 3. `Configuration`

Persisted user preferences. Lives in `config.json`.

| Field | C++ Type | JSON Key | Default | Notes |
|-------|----------|----------|---------|-------|
| `lastDevice` | `std::wstring` | `"lastDevice"` | `""` | Service instance name of last connected receiver |
| `volume` | `float` | `"volume"` | `1.0` | Linear 0.0–1.0; clamped on read |
| `lowLatency` | `bool` | `"lowLatency"` | `false` | Selects SPSC buffer size (32 vs 128 frames) |
| `launchAtStartup` | `bool` | `"launchAtStartup"` | `false` | Mirrors HKCU Run registry state |
| `autoUpdate` | `bool` | `"autoUpdate"` | `true` | Controls WinSparkle background check |

**Portable mode detection**: If `config.json` exists in the same directory as `AirBeam.exe` at startup, that file is used as the config path. Otherwise `%APPDATA%\AirBeam\config.json` is used (created on first save if absent).

**Invariants**:
- `volume` ∈ [0.0, 1.0]; values outside range clamped on load
- Missing keys replaced with defaults on load (partial/migrated config is valid)
- A corrupt JSON file is silently reset to defaults and immediately overwritten

---

### 4. `RtpPacket`

Wire-format representation of a single RTP audio packet. Pre-allocated in the `RetransmitBuffer`.

| Field | Offset | Size | Description |
|-------|--------|------|-------------|
| `data[0]` | 0 | 1 byte | `0x80` (V=2, P=0, X=0, CC=0) |
| `data[1]` | 1 | 1 byte | `0x60` (M=0, PT=96) |
| `sequence` | 2 | 2 bytes | Big-endian uint16; wraps at 65535→0 |
| `timestamp` | 4 | 4 bytes | Big-endian uint32; increments by 352 per packet; wraps |
| `ssrc` | 8 | 4 bytes | Big-endian uint32; random constant per session |
| `payload` | 12 | variable | AES-128-CBC encrypted ALAC frame, padded to 16-byte boundary |
| `payloadLen` | — | metadata | Stored alongside `data` for retransmit |

**Storage**: `RetransmitBuffer` = `std::array<RtpPacket, 512>` where `RtpPacket::data` is `uint8_t[1500]`. Total memory: 512 × 1500 = **750 KB**, pre-allocated on Thread 1 before streaming begins.

**Index**: `sequence_number & 511` (O(1) lookup, power-of-2 mask). Oldest packets are silently overwritten by the write head.

---

### 5. `AudioFrame`

The inter-thread currency flowing through the SPSC ring buffer between Thread 3 and Thread 4.

| Field | Type | Description |
|-------|------|-------------|
| `samples[704]` | `int16_t[704]` | 352 stereo samples, interleaved L/R; 1408 bytes |
| `frameCount` | `uint32_t` | Always 352 in steady state; may be < 352 on stream-stop flush |

**Storage**: `SpscRingBuffer<AudioFrame, 128>` (standard) or `SpscRingBuffer<AudioFrame, 32>` (low-latency). Stack-allocated in the ring buffer's static array; never heap-allocated after init.

---

### 6. `LogEntry` (ephemeral)

Not persisted as a struct; written directly to the rolling log file.

**Format**: `YYYY-MM-DD HH:MM:SS.mmm [LEVEL] [ThreadId] Message\r\n`

**Levels**: `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`

**Rolling policy**: One file per calendar day. Files older than 7 days deleted on startup. Max log file name: `airbeam-YYYYMMDD.log`.

**Thread safety**: `Logger` uses a `CRITICAL_SECTION` (not a mutex) to protect file writes. The logger is NEVER called from Thread 3 or Thread 4 (RT threads) — only from Threads 1, 2, and 5.

---

## State Transition Summary

```
Application lifecycle:

  start
    ↓
  [A] Init subsystems (Thread 1)
    ↓
  [B] BonjourLoader check
    ├── FAIL → balloon (IDS_BALLOON_BONJOUR_MISSING) → AppState = Idle (discovery disabled)
    └── OK   → start Thread 2 (mDNS discovery)
    ↓
  [C] Load config; check lastDevice
    ├── empty → AppState = Idle
    └── non-empty → AppState = Connecting, start 5s startup timer
                     ├── device found in 5s → connect → Streaming
                     └── timer expires       → AppState = Idle (silent)
    ↓
  [D] Win32 message loop running
    ├── User right-clicks tray → show menu
    │    ├── Click available receiver → ConnectionController.Connect(receiver)
    │    ├── Click active receiver    → ConnectionController.Disconnect()
    │    ├── Volume slider            → RaopSession.SetVolume()
    │    ├── Low-latency toggle       → Config.lowLatency = !; reconnect if streaming
    │    ├── Launch at startup        → StartupRegistry.Toggle()
    │    ├── Open log folder          → ShellExecuteW("open", logDir)
    │    ├── Check for Updates        → win_sparkle_check_update_with_ui()
    │    └── Quit                     → PostQuitMessage(0)
    ├── WM_RAOP_CONNECTED    → AppState = Streaming; balloon
    ├── WM_RAOP_FAILED       → backoff retry or Error balloon
    ├── WM_RECEIVERS_UPDATED → rebuild tray menu
    └── WM_DEFAULT_DEVICE_CHANGED → WasapiCapture re-attach
```

---

## Entity Relationships

```
Configuration  ──[lastDevice]──► AirPlayReceiver (by instanceName lookup)
AudioStream    ──[activeReceiver]──► AirPlayReceiver
AudioStream    ──[volume]──► RaopSession (SET_PARAMETER)
SpscRingBuffer ──[AudioFrame]──► between Thread 3 and Thread 4
RetransmitBuffer ──[RtpPacket[512]]──► referenced by Thread 4 (write) and Thread 5 (retransmit read)
Logger         ──[LogEntry]──► rolling log files (Threads 1, 2, 5 only)
```
