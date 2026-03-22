# Contract: RTP Packet Layout

**Scope**: The binary format of RTP audio packets sent by AirBeam to an AirPlay 1 receiver.

---

## Packet Format

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X| CC=0  |M|    PT=96    |        Sequence Number        |  bytes 0-3
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Timestamp                             |  bytes 4-7
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             SSRC                              |  bytes 8-11
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|         AES-128-CBC Encrypted ALAC Payload                    |  bytes 12+
|         (padded to 16-byte boundary with zero bytes)          |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

---

## Header Fields

| Field | Bits | Value | Notes |
|-------|------|-------|-------|
| V (version) | 2 | `2` | Always 2 |
| P (padding) | 1 | `0` | No RTP-level padding |
| X (extension) | 1 | `0` | No RTP extension header |
| CC (CSRC count) | 4 | `0` | No CSRCs |
| M (marker) | 1 | `0` | Always 0 for AirPlay 1 audio |
| PT (payload type) | 7 | `96` | Dynamic; negotiated in SDP `rtpmap` |
| Sequence Number | 16 | monotonic | Random start; +1 per packet; uint16 big-endian; wraps 65535→0 |
| Timestamp | 32 | monotonic | Random start; +352 per packet (one ALAC frame); uint32 big-endian; wraps |
| SSRC | 32 | random | Randomly generated at session start; constant for entire session; uint32 big-endian |

Byte 0 (fixed): `0x80`  
Byte 1 (fixed): `0x60`  

---

## Payload

The payload begins at byte offset 12.

| Step | Description |
|------|-------------|
| 1 | Encode 352 stereo S16LE samples (1408 bytes) with Apple ALAC encoder → `alacFrame` (variable length, ≤ 4096 bytes) |
| 2 | Pad `alacFrame` to the next 16-byte boundary with zero bytes → `paddedFrame` |
| 3 | `AES-128-CBC.encrypt(paddedFrame, sessionKey, sessionIV)` → `encryptedPayload` |
| 4 | Copy `encryptedPayload` into RTP packet starting at byte 12 |

The payload length is `ceil(len(alacFrame) / 16) * 16` bytes.

---

## Timing & Rate

| Parameter | Value |
|-----------|-------|
| Audio format | 44100 Hz / stereo / 16-bit signed LE |
| Samples per frame | 352 |
| Frame duration | 352 / 44100 ≈ **7.979 ms** |
| Packets per second | ≈ 125.3 |
| Timestamp increment | **352** per packet |
| Timestamp clock rate | 44100 Hz |

---

## Retransmit Buffer

AirBeam maintains a sliding window retransmit buffer:

| Property | Value |
|----------|-------|
| Window size | **512 packets** |
| Index formula | `sequence_number & 511` (power-of-2 mask) |
| Storage | `RtpPacket data[512]` — `data` is `uint8_t[1500]` each |
| Total memory | 512 × 1500 = **750 KB** (pre-allocated before streaming starts) |
| Write | Thread 4 writes each new packet before UDP send |
| Read | Thread 5 reads on retransmit request (O(1) lookup) |

Oldest packets are silently overwritten when the write head advances beyond 512 positions. If a retransmit request arrives for a packet no longer in the window, AirBeam logs the miss and sends no response.

---

## Typical Packet Size

- ALAC compression ratio for typical stereo music: ~50–70% of raw PCM
- Raw PCM per frame: 1408 bytes
- Typical ALAC output: ~700–1000 bytes
- Padded to 16 bytes: ~704–1008 bytes
- Total RTP packet: 12 + ~704–1008 ≈ **716–1020 bytes**

Well within standard Ethernet MTU (1500 bytes). No fragmentation expected on local networks.
