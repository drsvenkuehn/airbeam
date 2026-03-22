# Research: AirBeam — Windows AirPlay Audio Sender

**Phase 0 Output** | Branch: `001-airplay-audio-sender` | Date: 2026-03-21  
All NEEDS CLARIFICATION items resolved. Ready for Phase 1 design.

---

## R-001: RAOP/AirPlay 1 RTSP Wire Sequence

**Decision**: Full RTSP sequence: OPTIONS → ANNOUNCE (with SDP) → SETUP → RECORD  
**Details**:

```
C→R  OPTIONS * RTSP/1.0
     CSeq: 1
R→C  200 OK
     Public: ANNOUNCE, SETUP, RECORD, TEARDOWN, SET_PARAMETER

C→R  ANNOUNCE rtsp://<receiver_ip>:<port>/<stream_id> RTSP/1.0
     CSeq: 2
     Content-Type: application/sdp
     Content-Length: <n>
     User-Agent: AirBeam/1.0
     Client-Instance: <16 random hex chars>
     [SDP body — see R-002]
R→C  200 OK

C→R  SETUP rtsp://<receiver_ip>:<port>/<stream_id> RTSP/1.0
     CSeq: 3
     Transport: RTP/AVP/UDP;unicast;interleaved=0-1;
                control_port=<client_ctrl_udp>;timing_port=<client_timing_udp>
R→C  200 OK
     Transport: RTP/AVP/UDP;unicast;
                server_port=<rcvr_audio_udp>;
                control_port=<rcvr_ctrl_udp>;timing_port=<rcvr_timing_udp>
     Session: <session_token>

C→R  RECORD rtsp://<receiver_ip>:<port>/<stream_id> RTSP/1.0
     CSeq: 4
     Range: npt=0-
     Session: <session_token>
R→C  200 OK

     [ RTP/UDP audio packets begin flowing ]

C→R  SET_PARAMETER rtsp://... RTSP/1.0  (volume changes)
     CSeq: N
     Content-Type: text/parameters
     Session: <session_token>
     volume: -15.5\r\n

C→R  TEARDOWN rtsp://... RTSP/1.0
     CSeq: N+1
     Session: <session_token>
R→C  200 OK
```

**Rationale**: Derived from open RAOP implementations (shairport-sync read-only reference) and
AirPlay 1 protocol documentation. RTSP/1.0 is the transport; SDP carries codec and encryption
parameters in the ANNOUNCE body.

**Alternatives considered**: Binary protocol stub (rejected — RTSP is the required wire format);
partial ANNOUNCE without encryption fields (rejected — receivers reject connections without `rsaaeskey`/`aesiv`).

---

## R-002: SDP Body for ANNOUNCE

**Decision**: Use `a=rtpmap:96 AppleLossless` with ALAC `fmtp` parameters and RSA-encrypted AES key fields.

```
v=0
o=AirBeam <session_id> <session_ver> IN IP4 <client_ip>
s=AirBeam
c=IN IP4 <receiver_ip>
t=0 0
m=audio 0 RTP/AVP 96
a=rtpmap:96 AppleLossless
a=fmtp:96 352 0 16 40 10 14 2 44100 0 0
a=rsaaeskey:<base64url(RSA_PKCS1v15_encrypt(aes_session_key_16_bytes))>
a=aesiv:<base64url(aes_iv_16_bytes)>
```

`fmtp` field meanings (positional): frameLength=352, compatibleVersion=0, bitDepth=16,
tuningCurrentBackOff=40, tuningMaxBackOff=10, byteStreamVersion=14, numChannels=2,
maxRun=44100, maxFrameBytes=0 (unknown), avgBitRate=0 (unknown).

**Rationale**: These exact field values are required by AirPlay 1 receivers. Deviation causes
receivers to reject the ANNOUNCE with 400 Bad Request.

**Alternatives considered**: `a=rtpmap:96 mpeg4-generic` (wrong — AirPlay 1 uses AppleLossless);
variable frame length (rejected — constitutionally fixed at 352).

---

## R-003: RSA Session-Key Wrap

**Decision**: AES-128 session key (16 random bytes) encrypted with PKCS#1 v1.5 RSA-2048 using the
fixed AirPlay 1 public key. The ciphertext is base64url-encoded for the SDP `rsaaeskey` attribute.

- **RSA public key**: 2048-bit, PEM format; same key hardcoded in every AirPlay 1 receiver. This
  key is public and widely documented — it is not secret. It is stored in `src/protocol/RsaKeyWrap.cpp`
  as a string constant.
- **IV**: 16 random bytes, base64url-encoded raw (NOT encrypted) in the `aesiv` attribute.
- **Win32 implementation**: `BCryptOpenAlgorithmProvider(BCRYPT_RSA_ALGORITHM)` +
  `BCryptImportKeyPair` + `BCryptEncrypt(BCRYPT_PAD_PKCS1)`. No OpenSSL required.

**Rationale**: Using BCrypt (built into Windows) avoids any additional dependency for RSA. BCrypt is
available on all Windows 10/11 targets.

**Alternatives considered**: OpenSSL (rejected — GPL/LGPL risk, external dependency);
mbedTLS (acceptable license but unnecessary complexity when BCrypt suffices).

---

## R-004: Apple ALAC Encoder Integration

**Decision**: Apple reference encoder from `https://github.com/macosforge/alac` (Apache 2.0),
statically linked via `FetchContent` + CMake.

**C API surface**:
```c
ALACEncoder* ALACEncoderNew(void);
void         ALACEncoderInit(ALACEncoder*, const ALACSpecificConfig*);
int32_t      ALACEncode(ALACEncoder*, uint32_t inNumSamples,
                        const void* pcmIn, void* alacOut, uint32_t* outBitOffset);
void         ALACEncoderDelete(ALACEncoder*);
```

**Fixed parameters** (constitutional):
| Parameter | Value |
|-----------|-------|
| `frameLength` | 352 samples (7.98 ms @ 44100 Hz) |
| `bitDepth` | 16 |
| `channels` | 2 (stereo, interleaved L/R) |
| Sample rate | 44100 Hz (input MUST be resampled first) |
| PCM input size | 352 × 2 × 2 = **1408 bytes** per call |
| Output buffer | **4096 bytes** pre-allocated (worst-case uncompressible audio) |

**RT-safety**: `ALACEncode` uses only stack and pre-allocated encoder state — no heap alloc on hot path.

**Alternatives considered**: FFmpeg ALAC (LGPL — license risk); self-written ALAC (prohibited by
constitution Principle IV — test correctness requirement mandates reference implementation).

---

## R-005: AES-128-CBC Encryption Scope

**Decision**: Encrypt the entire RTP payload (ALAC frame bytes, starting at byte 12 of the RTP
packet). The 12-byte RTP header is left in plaintext. Session IV is reused for every packet in the
session (AirPlay 1 protocol behaviour — same IV per session).

**Padding**: ALAC frame is padded to 16-byte boundary before encryption (AES block size). Padding
bytes are zero. Padding is accounted for in RTP payload length.

**Win32 implementation**: `BCryptOpenAlgorithmProvider(BCRYPT_AES_ALGORITHM)` + `BCryptSetProperty(BCRYPT_CHAINING_MODE, BCRYPT_CHAIN_MODE_CBC)` + `BCryptGenerateSymmetricKey` (called once at session init). `BCryptEncrypt` called per packet with pre-allocated key object and pre-allocated IV copy.

**Rationale**: Constant IV is the documented AirPlay 1 behaviour. AirPlay 2 uses per-packet IVs —
this is out of scope for v1.0.

**Alternatives considered**: Per-packet IV derived from RTP sequence (AirPlay 2 scheme — rejected as
out-of-scope and incompatible with AirPlay 1 receivers).

---

## R-006: RTP Packet Format

**Decision**:
```
Byte  0:    0x80       (V=2, P=0, X=0, CC=0)
Byte  1:    0x60       (M=0, PT=96)
Bytes 2-3:  seq        (uint16 big-endian; starts random, +1 per packet, wraps)
Bytes 4-7:  timestamp  (uint32 big-endian; starts random, +352 per packet, wraps)
Bytes 8-11: ssrc       (uint32 big-endian; random constant per session)
Bytes 12+:  payload    (AES-128-CBC encrypted ALAC frame, padded to 16-byte boundary)
```

Total packet size: 12 bytes header + padded(ALAC frame) bytes. Typically 12 + ~1420 = ~1432 bytes,
well within Ethernet MTU.

**Retransmit buffer**: 512-element circular array of pre-allocated `RtpPacket` structs (each 1500
bytes max). Index = `sequence_number & 511`. On retransmit request, O(1) lookup and UDP resend.

**Alternatives considered**: Marker bit set on first packet (rejected — AirPlay 1 receivers do not
require this and it is unnecessary).

---

## R-007: NTP-Like Timing Synchronization

**Decision**: Two timing mechanisms run concurrently during streaming:

1. **Timing port (UDP, ~3 s interval)**: Receiver sends timing requests; AirBeam responds:
   - Request: 32 bytes with originate timestamp
   - Response: same 32-byte format with receive + transmit timestamps filled in
   - Timestamps: NTP epoch (seconds since 1900-01-01) using Win32 `GetSystemTimeAsFileTime` + conversion

2. **Sync packets (control port UDP, ~1 s interval)**: AirBeam proactively sends timing sync:
   - 8-byte payload: `[0x00, 0xD4, 0x00, 0x00, <RTP_ts_uint32_be>, <NTP_secs_uint32_be>]`
   - Allows receiver to map RTP timestamp → wall clock for playout scheduling

**Win32 NTP time**: `GetSystemTimeAsFileTime` → convert FILETIME to NTP epoch (offset from 1900-01-01 vs 1601-01-01 = 9435484800 seconds).

**Alternatives considered**: RTCP SR packets (not used in AirPlay 1 — custom NTP-like protocol is
the documented standard for RAOP).

---

## R-008: SPSC Ring Buffer Design

**Decision**: Lock-free SPSC ring buffer using two `std::atomic<uint32_t>` indices, power-of-2
capacity, statically-sized `std::array<AudioFrame, N>` (no heap alloc).

| Mode | N (frames) | Memory | Approx. latency |
|------|-----------|--------|-----------------|
| Standard (500 ms) | 128 | ~180 KB | 128 × 7.98 ms ≈ 1.02 s headroom |
| Low-latency (100 ms) | 32 | ~45 KB | 32 × 7.98 ms ≈ 255 ms headroom |

`AudioFrame` = 1408 bytes (352 samples × 2 channels × 2 bytes).

The ring buffer instance is allocated on Thread 1 before threads start (pre-allocation). Read (Thread 4) and write (Thread 3) indices use acquire/release semantics. Neither thread ever calls `new`/`malloc` in the hot path.

**Alternatives considered**: `std::queue` with mutex (rejected — mutex prohibited on RT threads);
double buffer (rejected — only safe with a size-1 exchange, limiting latency control).

---

## R-009: mDNS TXT Record — AirPlay Version Detection

**Decision**: Parse `et` (encryption type) key from DNS TXT record:

| `et` value | Classification | Tray treatment |
|------------|---------------|----------------|
| Contains `"1"` | AirPlay 1 compatible | Selectable (enabled) |
| Contains `"4"` only | AirPlay 2 only (MFi/FairPlay) | Grayed out; label "AirPlay 2 — not supported in v1.0" |
| Has `pk` key (HomeKit public key) | AirPlay 2 only | Grayed out |
| Neither `et=1` nor `pk` | Unknown | Gray out with "?" suffix |

Secondary signal: `am` (device model) for display name formatting (e.g., "HomePod mini" vs
"AirPort Express").

60-second stale timeout: any device whose mDNS record has not been refreshed within 60 s is removed
from `ReceiverList`.

**Alternatives considered**: Query receiver with OPTIONS before showing in list (rejected —
unnecessary latency; TXT record is authoritative for v1 detection).

---

## R-010: WinSparkle + EdDSA Integration

**Decision**: WinSparkle prebuilt x64 DLL (BSD-2-Clause). Ed25519 key pair generated with
`winsparkle-sign` tool (bundled with WinSparkle SDK).

- **Private key** (`SPARKLE_PRIVATE_KEY`): stored ONLY as encrypted GitHub Actions secret. Never committed.
- **Public key**: embedded in RC string resource (`IDS_SPARKLE_PUBKEY`) at build time. Injected via CMake `configure_file` from a checked-in `sparkle_pubkey.txt`.
- **Appcast format**:
  ```xml
  <item>
    <title>AirBeam v1.0.1</title>
    <link>https://github.com/<user>/airbeam/releases/tag/v1.0.1</link>
    <sparkle:version>1.0.1</sparkle:version>
    <pubDate>...</pubDate>
    <enclosure url="https://github.com/.../AirBeam-v1.0.1-win64-setup.exe"
               sparkle:edSignature="<base64_ed25519_sig>"
               length="<bytes>"
               type="application/octet-stream"/>
  </item>
  ```
- WinSparkle validates EdDSA signature before installation — unsigned or tampered packages are rejected.

**Rationale**: EdDSA (Ed25519) is the only signature scheme supported by modern WinSparkle versions for Windows. ECDSA or RSA-based signatures are not used.

**Alternatives considered**: Manual update check via GitHub Releases API (rejected — no signature verification, more code to maintain); Squirrel.Windows (GPL-adjacent risks).

---

## R-011: Bonjour Runtime Detection Strategy

**Decision**: Dynamic `LoadLibrary("dnssd.dll")`. Check in order:
1. `LoadLibrary` with bare name (system PATH / System32)
2. Check `HKLM\SYSTEM\CurrentControlSet\Services\Bonjour Service` registry key for installed Bonjour

If not found: post `WM_BONJOUR_MISSING` to main window → tray balloon with text:  
*"Bonjour not installed. mDNS discovery is unavailable. Install Bonjour for Windows at support.apple.com — then restart AirBeam."*  
(IDS_BALLOON_BONJOUR_MISSING with localized body text)

All `dnssd.dll` function pointers are kept in `BonjourLoader` struct and always null-checked before use.

**Alternatives considered**: Static link (impossible — Apple does not provide a static library); embedding mDNSResponder source (rejected — complex, different license review required, and dnssd.dll is the constitutional choice).

---

## R-012: Config JSON Schema

**Decision**: `nlohmann/json` (MIT, single-header) for config read/write.

Config keys (see `contracts/config-schema.md` for full schema):
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `lastDevice` | string | `""` | Bonjour service instance name of last connected device |
| `volume` | number | `1.0` | Linear volume 0.0–1.0 |
| `lowLatency` | bool | `false` | 100 ms vs 500 ms buffer mode |
| `launchAtStartup` | bool | `false` | Registry Run key enabled |
| `autoUpdate` | bool | `true` | WinSparkle 24 h background check enabled |

Corrupt JSON → reset to defaults, log warning, overwrite file on next save.

**Alternatives considered**: Windows Registry for config (rejected — portable mode requirement means file-based config is mandatory); INI file (rejected — JSON is more extensible and supported by nlohmann/json MIT library).

---

## Summary of All Decisions

| # | Topic | Decision |
|---|-------|----------|
| R-001 | RTSP sequence | OPTIONS → ANNOUNCE → SETUP → RECORD → SET_PARAMETER / TEARDOWN |
| R-002 | SDP body | `rtpmap:96 AppleLossless` + `fmtp` with 352-frame ALAC params + `rsaaeskey`/`aesiv` |
| R-003 | RSA key wrap | BCrypt RSA-PKCS1v15 with hardcoded AirPlay 1 public key; base64url in SDP |
| R-004 | ALAC encoder | Apple reference (Apache 2.0); 352 samples/frame, 44100 Hz S16LE stereo, 4096 B output buf |
| R-005 | AES-CBC | BCrypt AES-128-CBC; encrypt full RTP payload; constant session IV (AirPlay 1 spec) |
| R-006 | RTP format | PT=96, M=0, +352 timestamp, 512-packet retransmit circular buffer |
| R-007 | NTP timing | Timing-port responder + 1 Hz sync packets on control port; Win32 FILETIME conversion |
| R-008 | SPSC ring | Power-of-2 static array; 128 frames (buffered) / 32 frames (low-latency); atomic indices |
| R-009 | mDNS TXT | `et` key: contains "1" → AirPlay 1 ok; "4"-only or `pk` present → AirPlay 2 grayed out |
| R-010 | WinSparkle | Ed25519 public key in RC; private key in Actions secret; appcast on gh-pages |
| R-011 | Bonjour | `LoadLibrary("dnssd.dll")` dynamic; registry fallback check; balloon on missing |
| R-012 | Config | nlohmann/json (MIT) single-header; 5 keys; portable-mode file-next-to-exe override |
