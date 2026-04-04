# Research: AirPlay 2 Speaker Support

**Phase 0 output for feature 010** | **Date**: 2026-04-04

---

## §1 — HAP Crypto Library Selection

### Decision
**libsodium (ISC) + csrp (MIT) + OpenSSL (Apache 2.0)**

### Rationale
HAP pairing requires five cryptographic primitives that no single MIT-compatible library provides:

| Primitive | Purpose | Provided by |
|-----------|---------|-------------|
| SRP-6a | PIN-authenticated key exchange | csrp (MIT) |
| Ed25519 | Long-term key signing / verification | libsodium (ISC) |
| X25519 / Curve25519 | Ephemeral ECDH key exchange | libsodium (ISC) |
| ChaCha20-Poly1305 | AEAD encryption of pairing frames | libsodium (ISC) |
| HKDF-SHA512 | Session key derivation from SRP secret | OpenSSL / libsodium |

**License compatibility**:
- libsodium (ISC) ✅ MIT-equivalent, unrestricted
- csrp (MIT) ✅ direct match
- OpenSSL 3.x (Apache 2.0) ✅ MIT-compatible, already standard on CI

**Windows/MSVC build story**:
- libsodium ships pre-built MSVC x64 binaries; CMake integration via `find_package(sodium)` or `add_subdirectory(third_party/libsodium)`
- csrp is a single `.c` / `.h` pair; trivial CMake `add_library(csrp STATIC ...)`
- OpenSSL: use system install or vcpkg (`vcpkg install openssl:x64-windows`)

**For symmetric crypto (AES-128-GCM, HKDF)**:
- **Windows BCrypt** (built-in, Bcrypt.lib) covers AES-128-GCM natively via `BCRYPT_CHAIN_MODE_GCM` — no external dependency
- HKDF-SHA256/SHA512 can be implemented in ~40 lines using HMAC from libsodium

### Alternatives Considered

| Library | License | Verdict |
|---------|---------|---------|
| wolfSSL | GPL-3 unless commercial | ❌ EXCLUDED — GPL incompatible with MIT |
| mbedTLS | Apache 2.0 | ❌ Missing SRP-6a, heavier than libsodium |
| OpenSSL alone | Apache 2.0 | ❌ SRP deprecated in 3.0 (legacy provider); ~10 MB binary overhead |
| Botan | BSD-2 | ❌ All primitives present but large; no prior project precedent |

### Vendoring Plan
```
third_party/
├── libsodium/        ← git-submodule pinned to stable release tag
└── csrp/             ← single-file copy (srp.c + srp.h), BSD-equivalent MIT
```
OpenSSL sourced from vcpkg (CI) or system install (developer machine).

---

## §2 — AirPlay 2 Wire Protocol

### Decision
Implement AirPlay 2 sender using HTTP/2-over-TLS control, RTP/UDP audio with AES-128-GCM, and Apple's PTP-variant timing. Reuse existing ALAC encoding and RTP framing unchanged.

### Key Protocol Facts (from openairplay2 / shairport-sync / pyatv research)

#### Control Channel
- **Transport**: HTTP/2 (binary framing) over TLS 1.2+
- **ALPN**: `"airplay"` (negotiated during TLS handshake)
- **Port**: Advertised in `_raop._tcp` TXT record, varies per device (commonly 7000)
- **Authentication**: Post-pairing TLS (client trusts any certificate once HAP pairing establishes mutual trust)
- **Key endpoints**: `POST /pair-setup`, `GET /pair-verify`, `POST /audio` (SETUP), `POST /controller` (volume)

#### Audio Transport
| Property | AirPlay 1 | AirPlay 2 |
|----------|-----------|-----------|
| Frame size | 352 samples (7.98 ms) | **352 samples — identical** |
| Codec | ALAC | **ALAC — identical** |
| Transport | RTP/UDP payload type 96 | **RTP/UDP payload type 96 — identical** |
| Encryption | AES-128-CBC, session-constant IV | **AES-128-GCM, per-packet nonce** |
| Key delivery | RSA-wrapped in ANNOUNCE SDP | HKDF-derived from HAP pairing secret |
| Auth tag | None | 16-byte GCM auth tag appended |

**AES-128-GCM packet structure:**
```
┌─────────────────────────────┐
│ RTP header (12 bytes)       │
├─────────────────────────────┤
│ ALAC frame (variable)       │ ← encrypted + authenticated
│ GCM auth tag (16 bytes)     │ ← integrity check
└─────────────────────────────┘
```

**IV/nonce derivation (per packet):**
```
nonce[0:4]  = session_salt  (random, generated at session init)
nonce[4:8]  = rtp_seq_num   (uint32 BE, increments per frame)
nonce[8:12] = zeroes
```

#### Timing Protocol
- **Protocol**: Apple PTP variant (gPTP, IEEE 802.1AS-inspired), NOT NTP-like
- **Precision**: Nanosecond timestamps vs. NTP microsecond
- **Packet interval**: ~3 seconds (same cadence as AirPlay 1)
- **Port**: Advertised in TXT record; separate UDP socket (same pattern as AirPlay 1)
- **Sync tolerance for multi-room**: < 10 ms required (vs. ±100 ms acceptable in AirPlay 1)
- **Clock source**: `QueryPerformanceCounter()` provides sufficient resolution on Windows 10/11

#### mDNS Discovery
**Critical finding**: AirPlay 2 receivers still advertise on `_raop._tcp` — there is **no separate `_airplay._tcp` browsing required**. The distinction is entirely in TXT records:

| TXT field | AirPlay 1 | AirPlay 2 |
|-----------|-----------|-----------|
| `pk` | absent | present (base64 Ed25519 public key) |
| `et` | `1` (RSA-AES) | `0` or no `1` bit (no RSA) |
| `vv` | absent | `"2"` |

Detection logic:
```cpp
if (!txt.Get("pk").empty()) {
    // AirPlay 2 capable
    receiver.hapDevicePublicKey = txt.Get("pk");
    receiver.supportsAirPlay2 = true;
    if (txt.Get("et").find('1') == npos) {
        receiver.isAirPlay2Only = true;   // HomePod, HomePod mini
    }
} else if (txt.Get("et").find('1') != npos) {
    receiver.isAirPlay1Compatible = true; // legacy AirPlay 1 only
}
```

### Rationale for WinHTTP (not nghttp2)
Windows 10 1903+ includes native HTTP/2 support in WinHTTP (`winhttp.dll`). Since AirBeam's minimum OS target is Windows 10 build 1903, WinHTTP covers 100% of users with zero additional dependency. nghttp2 (MIT) remains available as a fallback if WinHTTP's HTTP/2 API proves insufficient for AirPlay 2 framing needs (e.g., server-push, stream multiplexing).

### Rationale for Windows BCrypt (not OpenSSL for symmetric crypto)
`Bcrypt.lib` is a Windows inbox library (no DLL redistribution needed). It provides AES-128-GCM via `BCRYPT_CHAIN_MODE_GCM`. Eliminates one external dependency for the most performance-sensitive path (per-packet encryption on Thread 4).

---

## §3 — HKDF Implementation

### Decision
Implement HKDF-SHA512 and HKDF-SHA256 as a small header-only utility using libsodium's `crypto_auth_hmacsha512` / `crypto_auth_hmacsha256` primitives (~50 lines).

### Rationale
No external library needed. HKDF (RFC 5869) is a two-step function (Extract + Expand) that composes directly from HMAC. Using libsodium's already-vendored HMAC keeps the dependency count minimal.

---

## §4 — Multi-Room Coordination

### Decision
**Group sender model**: AirBeam acts as the group coordinator. Each AirPlay 2 speaker is an independent stream session (`AirPlay2Session`). `MultiRoomCoordinator` manages a set of sessions, sends synchronized PTP timestamps to all receivers, and adjusts each receiver's playback clock offset to achieve < 10 ms group sync.

### Rationale
Apple's own AirPlay 2 multi-room uses the "group leader" model where one device coordinates. As a sender, AirBeam can act as its own group leader by sending each receiver the same RTP presentation timestamp derived from a single reference clock. Each receiver adjusts its local buffer depth based on PTP clock offset feedback.

### Implementation notes
- Maximum 6 active sessions per spec (SC-005)
- Single WASAPI capture feeds all sessions via a fan-out after the SPSC ring buffer (Thread 4 reads once, writes to all active AirPlay2Session senders)
- Per-speaker volume is set via individual `POST /controller` HTTP/2 requests

---

## §5 — Windows Credential Manager for Pairing Credentials

### Decision
Use `CredWriteW` / `CredReadW` / `CredDeleteW` from `Advapi32.lib` (Windows inbox, no additional dependency).

### Storage schema
```
Target name: L"AirBeam:AirPlay2:<HAP_DEVICE_ID>"
  where HAP_DEVICE_ID = hex-encoded 6-byte device identifier from TXT record `pk` hash

Credential type: CRED_TYPE_GENERIC
Blob: JSON UTF-8
{
  "controller_id":  "<UUID string>",
  "controller_ltpk": "<base64 Ed25519 public key>",
  "controller_ltsk": "<base64 Ed25519 secret key>",
  "device_ltpk":    "<base64 device Ed25519 public key>",
  "device_name":    "<human readable name at time of pairing>"
}
Blob is DPAPI-encrypted at rest by Windows automatically.
```

### Rationale
DPAPI protection is automatic and OS-enforced (credential is bound to the Windows user account). No manual encryption needed. `Advapi32.lib` is always linked in Win32 apps (already present in AirBeam's link set). Survives reboots, Windows updates, and AirBeam reinstallation (unless user explicitly deletes credentials or reinstalls Windows).

---

## §6 — AirBeam Controller Identity

### Decision
AirBeam generates a single long-term Ed25519 key pair on first launch (the "controller identity") stored in Windows Credential Manager under `L"AirBeam:ControllerIdentity"`. This key pair is used in all HAP pairing ceremonies.

### Rationale
HAP requires the controller (AirBeam) to have a stable long-term identity. If the controller key changes, all paired devices must be re-paired. Storing in Credential Manager (user-scoped) means the same identity persists across AirBeam reinstallations as long as the user account is intact.
