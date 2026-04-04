# Contract: AirPlay2Session C++ Interface

**Feature**: 010-airplay2-support  
**Owner**: `src/airplay2/AirPlay2Session.h`

---

## Purpose

Defines the interface that `AirPlay2Session` exposes to `ConnectionController` and `MultiRoomCoordinator`. This is the boundary between the AirPlay 2 protocol layer and the rest of the application.

`AirPlay2Session` is a concrete subclass of `StreamSession` (the existing virtual base). `ConnectionController` selects the concrete type at connect time based on `AirPlayReceiver.supportsAirPlay2`.

---

## Inheritance

```
StreamSession  (virtual base — src/core/StreamSession.h)
    └── AirPlay2Session  (src/airplay2/AirPlay2Session.h)
```

All `StreamSession` virtual methods are overridden. Callers use the `StreamSession*` interface; no caller needs to cast to `AirPlay2Session`.

---

## Extended Interface (AirPlay 2-specific)

```cpp
// src/airplay2/AirPlay2Session.h

class AirPlay2Session : public StreamSession {
public:
    // ── Pairing gate ─────────────────────────────────────────────────────────

    /// Returns true if a valid PairingCredential exists for this receiver.
    /// Called before Init() — if false, ConnectionController initiates pairing.
    static bool IsPaired(const AirPlayReceiver& receiver);

    // ── Lifecycle (overrides StreamSession) ──────────────────────────────────

    /// Initialises AirPlay 2 session: loads credential, derives session key,
    /// opens WinHTTP HTTP/2 session. Does NOT start streaming.
    /// Posts WM_AP2_PAIRING_REQUIRED if device is not paired.
    bool Init(const AirPlayReceiver& target, bool lowLatency, HWND hwnd) override;

    bool StartCapture()  override;
    void StopCapture()   override;
    bool ReinitCapture() override;
    bool IsCaptureRunning() const override;

    /// Sends HTTP/2 SETUP to receiver. Posts WM_AP2_CONNECTED on success.
    /// NOTE: method name inherited from StreamSession virtual interface; in AirPlay2Session
    /// this performs an HTTP/2 SETUP POST to /audio — NOT an RTSP ANNOUNCE as in RaopSession.
    void StartRaop(float volume) override;  // name inherited; semantics = HTTP/2 SETUP

    void StopRaop()              override;
    SOCKET AudioSocket()   const override;

    bool InitEncoder(uint32_t ssrc, HWND hwndMain) override;
    void StartEncoder() override;
    void StopEncoder()  override;

    void SetVolume(float linear) override;  // HTTP/2 POST /controller

    // ── Multi-room support ───────────────────────────────────────────────────

    /// Sets the PTP reference timestamp to synchronise with other group members.
    /// Called by MultiRoomCoordinator before StartEncoder() in multi-room mode.
    void SetPtpReferenceOffset(int64_t offsetNs);

    /// Current PTP clock offset measured from the receiver (nanoseconds).
    int64_t PtpClockOffset() const;
};
```

---

## Win32 Messages Posted by AirPlay2Session

| Message constant | WParam | LParam | Meaning | Ownership |
|-----------------|--------|--------|---------|-----------|
| `WM_AP2_PAIRING_REQUIRED` | — | `AirPlayReceiver*` | Device not paired; initiates pairing flow | **Sole poster: `AirPlay2Session::Init()`** when `IsPaired()` is false. `ConnectionController` and `HapPairing` MUST NOT post this message directly. |
| `WM_AP2_PAIRING_STALE` | — | `AirPlayReceiver*` | Credential rejected by device (factory reset) | Posted by `AirPlay2Session` on HAP auth rejection |
| `WM_AP2_CONNECTED` | — | — | HTTP/2 SETUP succeeded; streaming may begin | Posted by `AirPlay2Session::StartRaop()` |
| `WM_AP2_FAILED` | error code | — | Connection failed; triggers retry/notification | Posted by `AirPlay2Session`; NOT posted on `kTLVError_Authentication` (use `WM_AP2_PAIRING_STALE` instead) |
| `WM_AP2_SPEAKER_DROPPED` | — | `AirPlayReceiver*` | One multi-room speaker disconnected | Posted by `AirPlay2Session` on connection loss |

(These constants are defined in `src/core/Messages.h` alongside existing WM_RAOP_* messages.)

---

## Invariants

- `SetPtpReferenceOffset()` MUST be called before `StartEncoder()` in multi-room mode; ignored in single-speaker mode (defaults to 0).
- `AirPlay2Session::Init()` MUST NOT be called if `IsPaired()` returns false; the pairing flow must complete first. If called when `IsPaired()` is false, `Init()` posts `WM_AP2_PAIRING_REQUIRED` and returns `false`; it is safe to call speculatively.
- All socket and WinHTTP handle cleanup happens in the destructor; callers need not explicitly close.
- Thread safety: same model as `StreamSession` — all lifecycle methods called on Thread 1; capture/encode/RTSP run on their own threads.
