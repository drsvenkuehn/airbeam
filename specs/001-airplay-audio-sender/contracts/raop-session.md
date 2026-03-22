# Contract: RAOP Session Protocol

**Scope**: The AirBeam RTSP/RAOP sender-side protocol contract for establishing, maintaining,  
and tearing down an AirPlay 1 audio streaming session.

---

## Overview

AirBeam acts as the **RTSP client** and **RTP sender**. The AirPlay receiver acts as the RTSP server.  
All RTSP exchanges are over TCP (port 5000 by default). RTP audio, timing, and retransmit traffic are over UDP.

---

## Session Ports

| Channel | Protocol | Direction | Description |
|---------|----------|-----------|-------------|
| RTSP control | TCP | C→R | Session setup, volume, teardown |
| RTP audio | UDP | C→R | Encrypted ALAC audio frames |
| Control (retransmit) | UDP | C↔R | Retransmit requests / responses; timing sync packets |
| Timing | UDP | C↔R | NTP-like timing request/response |

AirBeam binds **ephemeral local UDP ports** for control and timing before sending SETUP.  
The receiver's server ports are returned in the SETUP response `Transport` header.

---

## RTSP Request / Response Sequence

### Step 1 — OPTIONS (optional probe)

```
→  OPTIONS * RTSP/1.0\r\n
   CSeq: 1\r\n
   User-Agent: AirBeam/1.0\r\n
   \r\n

←  RTSP/1.0 200 OK\r\n
   CSeq: 1\r\n
   Public: ANNOUNCE, SETUP, RECORD, TEARDOWN, SET_PARAMETER, GET_PARAMETER\r\n
   \r\n
```

### Step 2 — ANNOUNCE

```
→  ANNOUNCE rtsp://<receiver_ip>:<port>/<stream_id> RTSP/1.0\r\n
   CSeq: 2\r\n
   Content-Type: application/sdp\r\n
   Content-Length: <N>\r\n
   User-Agent: AirBeam/1.0\r\n
   Client-Instance: <16 random hex chars>\r\n
   \r\n
   <SDP body — see below>

←  RTSP/1.0 200 OK\r\n
   CSeq: 2\r\n
   \r\n
```

**SDP body**:
```
v=0\r\n
o=AirBeam <session_id> <session_version> IN IP4 <client_ip>\r\n
s=AirBeam\r\n
c=IN IP4 <receiver_ip>\r\n
t=0 0\r\n
m=audio 0 RTP/AVP 96\r\n
a=rtpmap:96 AppleLossless\r\n
a=fmtp:96 352 0 16 40 10 14 2 44100 0 0\r\n
a=rsaaeskey:<base64url(RSA_PKCS1v15_wrap(16_byte_aes_session_key))>\r\n
a=aesiv:<base64url(16_byte_aes_iv)>\r\n
```

Field notes:
- `<session_id>`: random uint64 (NTP time format), constant for session
- `<stream_id>`: same random value used in RTSP URL path component
- `fmtp` positional fields: frameLen=352, compatVer=0, bitDepth=16, pb=40, mb=10, kb=14, numCh=2, maxRun=44100, maxFrameBytes=0, avgBitRate=0

### Step 3 — SETUP

```
→  SETUP rtsp://<receiver_ip>:<port>/<stream_id> RTSP/1.0\r\n
   CSeq: 3\r\n
   User-Agent: AirBeam/1.0\r\n
   Transport: RTP/AVP/UDP;unicast;interleaved=0-1;\r\n
              control_port=<local_control_udp>;\r\n
              timing_port=<local_timing_udp>\r\n
   \r\n

←  RTSP/1.0 200 OK\r\n
   CSeq: 3\r\n
   Transport: RTP/AVP/UDP;unicast;\r\n
              server_port=<rcvr_audio_udp>;\r\n
              control_port=<rcvr_control_udp>;\r\n
              timing_port=<rcvr_timing_udp>\r\n
   Session: <session_token>\r\n
   \r\n
```

AirBeam MUST include the `Session` token in all subsequent requests.

### Step 4 — RECORD

```
→  RECORD rtsp://<receiver_ip>:<port>/<stream_id> RTSP/1.0\r\n
   CSeq: 4\r\n
   User-Agent: AirBeam/1.0\r\n
   Session: <session_token>\r\n
   Range: npt=0-\r\n
   \r\n

←  RTSP/1.0 200 OK\r\n
   CSeq: 4\r\n
   \r\n
```

After receiving 200 OK for RECORD, Thread 4 begins emitting RTP/UDP packets.

### Volume Change (during streaming)

```
→  SET_PARAMETER rtsp://<receiver_ip>:<port>/<stream_id> RTSP/1.0\r\n
   CSeq: N\r\n
   User-Agent: AirBeam/1.0\r\n
   Session: <session_token>\r\n
   Content-Type: text/parameters\r\n
   Content-Length: <n>\r\n
   \r\n
   volume: <dB_value>\r\n
```

`<dB_value>` range: −144.0 (mute) to 0.0 (max). Precision: one decimal place.  
Conversion: `dB = (linearVol == 0.0f) ? -144.0f : 20.0f * log10f(linearVol)`, clamped to [−144.0, 0.0].

### Teardown

```
→  TEARDOWN rtsp://<receiver_ip>:<port>/<stream_id> RTSP/1.0\r\n
   CSeq: N\r\n
   User-Agent: AirBeam/1.0\r\n
   Session: <session_token>\r\n
   \r\n

←  RTSP/1.0 200 OK\r\n
   CSeq: N\r\n
   \r\n
```

If no response within 2 s, proceed with local cleanup regardless.

---

## RTP Audio Packets (UDP)

Sent from AirBeam to `<receiver_ip>:<rcvr_audio_udp>`.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT=96   |        Sequence Number        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Timestamp (+352)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                  SSRC (random, constant/session)              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|      AES-128-CBC encrypted ALAC payload (16-byte padded)      |
|                            ...                                |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Field | Value |
|-------|-------|
| V | 2 |
| P | 0 |
| X | 0 |
| CC | 0 |
| M | 0 (all packets) |
| PT | 96 |
| Sequence | Starts random uint16; +1 per packet; wraps 65535→0 |
| Timestamp | Starts random uint32; +352 per packet; wraps |
| SSRC | Random uint32; constant for entire session |

---

## Timing Protocol (UDP)

### Timing Request (Receiver → AirBeam, ~every 3 s)
### Timing Response (AirBeam → Receiver)

32-byte NTP-like packet format:

```
Bytes  0:   0x00 (marker)
Bytes  1-3: 0x00 0x00 0x00 (reserved)
Bytes  4-7: Originate Timestamp (NTP seconds, uint32 big-endian)
Bytes  8-11: Originate Timestamp fractional (uint32 big-endian)
Bytes 12-15: Receive Timestamp (NTP seconds)
Bytes 16-19: Receive Timestamp fractional
Bytes 20-23: Transmit Timestamp (NTP seconds)
Bytes 24-27: Transmit Timestamp fractional
Bytes 28-31: 0x00 (padding)
```

NTP epoch: seconds since 1900-01-01 00:00:00 UTC  
Win32 conversion: `GetSystemTimeAsFileTime` → FILETIME → subtract offset 9435484800 seconds (FILETIME epoch is 1601-01-01).

### Sync Packet (AirBeam → Receiver, ~every 1 s, on control port)

8-byte packet:
```
Bytes 0-1: 0x00 0xD4 (marker)
Bytes 2-3: 0x00 0x00 (reserved)
Bytes 4-7: Current RTP timestamp (uint32 big-endian)
```

Followed by 8 bytes: `<NTP_secs uint32 BE> <NTP_frac uint32 BE>` — current wall-clock NTP time corresponding to the RTP timestamp above.

---

## Retransmit Protocol (UDP, Control Port)

### Retransmit Request (Receiver → AirBeam)

4 bytes:
```
Byte 0: 0x00
Byte 1: 0x55  (retransmit request marker)
Byte 2: <missing_seq_hi>
Byte 3: <missing_seq_lo>
```

### Retransmit Response (AirBeam → Receiver)

AirBeam looks up `sequence & 511` in `RetransmitBuffer`. If found: re-sends the original RTP packet to the receiver's audio UDP port. If not in window: log and ignore (no error response sent).

---

## Error Handling

| Error Condition | AirBeam Action |
|-----------------|----------------|
| TCP connect fails | Retry with backoff (1s, 2s, 4s); balloon on 3rd failure |
| RTSP 4xx response | Log; post `WM_RAOP_CONNECT_FAILED`; backoff |
| RTSP 5xx response | Log; treat as transient; retry with backoff |
| SET_PARAMETER fails | Log; ignore (non-critical; volume mismatch only) |
| TEARDOWN fails/timeout | Log; proceed with local cleanup |
| UDP send fails | Log; continue (next packet will be attempted) |
| Timing port receive fails | Log; continue streaming (timing sync best-effort) |

---

## Security Constraints

- AES session key: 16 random bytes generated by `BCryptGenRandom` per session
- AES IV: 16 random bytes generated by `BCryptGenRandom` per session; reused for session duration
- RSA wrap: PKCS#1 v1.5, BCrypt, with hardcoded AirPlay 1 public key (2048-bit PEM)
- No TLS on the RTSP TCP connection (AirPlay 1 is unencrypted at transport layer; audio content encryption is at application layer via AES-128-CBC)
