# Research: Feature 009 — ConnectionController

## Overview

All five clarification questions were answered on 2026-03-30 (see spec §Clarifications).
No open unknowns remain. This document records the decision rationale, alternatives
evaluated, and design implications for each resolved question.

---

## Decision 1 — Reconnect Policy

**Decision**: 3 retries with exponential backoff: 1 s → 2 s → 4 s (total max retry window: 7 s).

**Rationale**:
- A single 5-second flat delay was the initial candidate, but the spec owner rejected it in
  favour of aggressive early retry (1 s) that gracefully falls back to longer waits.
- Three attempts cover the vast majority of transient network hiccups (Wi-Fi beacon miss,
  AP roaming handoff) without tying up the UI thread for an excessive duration.
- 7-second total window is short enough to feel responsive to the user; long enough for a
  typical AP association cycle (~2–3 s) to complete between the first and second attempt.
- Implementation uses `SetTimer(hwnd, TIMER_RECONNECT, delayMs, nullptr)` on Thread 1.
  Timer fires on the UI thread, so no cross-thread coordination is needed.

**Alternatives considered**:
- Unlimited retries: rejected — would loop indefinitely if the device is permanently gone,
  with no feedback to the user.
- Single attempt with 5 s flat delay: rejected by spec owner.
- Configurable retries: rejected as out-of-scope for v1.0 (spec §Assumptions).

**Delay array** (pre-computed constant, avoids runtime calculation on hot path):
```cpp
static constexpr UINT kRetryDelaysMs[3] = { 1000, 2000, 4000 };
```

**Implementation note**: Each retry is fired from the Win32 message loop via `WM_TIMER`.
Before firing, the device's presence in `ReceiverList` is checked (FR-007). If absent,
the retry is cancelled, `KillTimer` is called, and the user receives a balloon notification.

---

## Decision 2 — Wrong-State Message Arrivals

**Decision**: Silently discard; emit one `OutputDebugString`/`TRACE()` line; leave state unchanged.
No retry, no notification, no side effect.

**Rationale**:
- Background threads (RaopSession/T5, WasapiCapture/T3) may post messages (e.g. `WM_RAOP_FAILED`,
  `WM_STREAM_STARTED`) after the controller has already moved to a different state — for example,
  a failure notification arriving after the user has already clicked "Disconnect".
- Win32 message serialisation guarantees these arrive on Thread 1, so no lock is needed.
- Silent discard is correct: the stale message describes a state that no longer applies.
  Raising a user notification would be confusing ("Failed" after a deliberate disconnect).
- The debug trace is essential for diagnosing race-condition edge cases during development.

**Implementation pattern**:
```cpp
// In every handler that checks state first:
if (state_ != PipelineState::Streaming) {
    TRACE("[CC] OnRaopFailed: ignored in state %d\n", static_cast<int>(state_));
    return;  // FR-021: silent discard + debug trace
}
```

**Alternatives considered**:
- Assert/crash on wrong-state arrival: rejected — race conditions between user action and
  background thread notification are expected and benign.
- Queue messages for later processing: rejected — unnecessary complexity; messages that arrive
  after a state change are always stale and should be discarded.

---

## Decision 3 — Device Loss: Two Distinct Messages

**Decision**: Two separate Win32 messages with distinct names and distinct handler paths.

| Message | Posted by | Triggers |
|---------|-----------|---------|
| `WM_AUDIO_DEVICE_LOST` | `WasapiCapture` (T3) when Windows capture device removed/changed | Capture-only restart (stop T3, reinit WASAPI, resume T3); RTSP session untouched |
| `WM_SPEAKER_LOST` | `MdnsDiscovery` (T2) when Bonjour entry disappears | Full RAOP disconnect + 3-retry reconnect with 1 s / 2 s / 4 s backoff (same path as `WM_RAOP_FAILED`) |

**Rationale**:
- The two failure modes have completely different severity and recovery paths:
  - Audio device change is a Windows system event; the AirPlay session can survive because
    RTSP is TCP-based and the receiver does not time out quickly. Only the capture side needs
    restarting. The ≤50 ms gap constraint (FR-010, SC-003) is achievable precisely because
    the expensive RTSP handshake is skipped.
  - Speaker disappearance from mDNS means the AirPlay receiver itself is gone; there is no
    value in keeping an RTSP session open to a ghost endpoint.
- Merging them into a single message with a payload flag would couple two unrelated code paths
  and make the state machine harder to reason about.
- `WM_AUDIO_DEVICE_LOST` replaces the existing `WM_DEFAULT_DEVICE_CHANGED` (same `WM_APP+6`
  value, renamed for clarity). `WM_SPEAKER_LOST` is a new message (`WM_APP+10`).

**Alternatives considered**:
- Single `WM_DEVICE_LOST` with WPARAM payload: rejected — different recovery paths belong in
  separate message handlers, not a switch inside one handler.
- Keep `WM_DEFAULT_DEVICE_CHANGED`: renamed to `WM_AUDIO_DEVICE_LOST` for clarity. The new
  name matches the spec contract and the handler semantics (capture-only restart).

---

## Decision 4 — Real-Time Audio Path Safety

**Decision**: Hard real-time constraint on Thread 3 (WasapiCapture) and Thread 4
(AlacEncoderThread): zero heap allocation (`new`/`malloc`/`realloc`) and zero mutex/lock
acquisition during the steady-state hot loop. All buffers pre-allocated in `Init()`.

**Rationale**:
- Audio glitches caused by heap allocator latency spikes or priority inversion from mutex
  contention are user-perceptible and cannot be patched retroactively (Constitution §I).
- WASAPI event-driven capture fires at buffer periods of ~10 ms; a single allocation in the
  Windows heap during that window risks a 10–100 ms stall, violating the ≤50 ms gap guarantee.
- Pre-allocation at `Init()` time is the standard industry practice for real-time audio
  (used by JUCE, RtAudio, and PortAudio internally).

**Buffer sizing decisions**:
- ALAC frame size is fixed at 352 samples × 4 bytes/sample (stereo S16LE) = 1408 bytes per slot.
- Normal mode: 128 slots × 1408 bytes = ~176 KB ring buffer working set.
- Low-latency mode: 32 slots × 1408 bytes = ~44 KB ring buffer working set.
- Both ring buffer variants (`SpscRingBuffer<AudioFrame, 128>` and `SpscRingBuffer<AudioFrame, 32>`)
  already exist and are tested (`test_spsc_ring.cpp`).

**Connection to ConnectionController**:
- `StreamSession::Init()` is called on Thread 1 (heap alloc allowed) before any thread starts.
  It pre-allocates the ring buffer and initialises `AlacEncoderThread` with all required
  pointers so the hot loop never allocates.
- `ConnectionController::BeginConnect()` calls `StreamSession::Init()` on Thread 1, then
  starts threads in order: Thread 3 → Thread 5 → (on `WM_RAOP_CONNECTED`) Thread 4.
- Thread 3 may begin writing to the ring buffer before Thread 4 is started; the lock-free
  `TryPush` will return `false` (ring full) if Thread 4 is not yet consuming — this is safe
  and expected during the startup window. Frames dropped in this window are acceptable.

**Alternatives considered**:
- Preallocate inside `WasapiCapture` constructor: rejected — constructor runs on Thread 1 at
  app startup, not at session start. Buffer size depends on the low-latency flag which is
  per-session.
- Dynamic allocation with a custom pool allocator: rejected — unnecessary complexity when
  fixed-size pre-allocation suffices.

---

## Decision 5 — Observability / Debug Tracing

**Decision**: `OutputDebugString`-based `TRACE()` macro at:
- Every state transition (4 transitions × 2 directions = up to 8 log points)
- Every thread start and stop (Threads 3, 4, 5 × 2 events = up to 6 log points)
- Each reconnect attempt (attempt number + delay in ms)
- Each capture restart (WM_AUDIO_DEVICE_LOST handling)
- Each wrong-state message discard (FR-021)

**Explicitly NOT traced**: per-frame hot-loop activity on Threads 3 and 4.

**Rationale**:
- `OutputDebugString` is zero-overhead when no debugger is attached (the kernel call returns
  immediately). When attached (DebugView, VS Output window), it provides real-time visibility
  into state machine behaviour without log file I/O.
- Per-frame tracing on Threads 3/4 would violate the real-time constraint (FR-022): even a
  single `OutputDebugString` call per WASAPI buffer period can introduce non-deterministic
  latency spikes.
- The agreed naming convention is a `[CC]` prefix (ConnectionController) for all trace lines,
  making them filterable in DebugView.

**Macro definition**:
```cpp
// In ConnectionController.cpp (or a shared debug header):
#ifdef _DEBUG
#  define CC_TRACE(fmt, ...) \
     do { \
       wchar_t _buf[256]; \
       swprintf_s(_buf, L"[CC] " fmt L"\n", ##__VA_ARGS__); \
       OutputDebugStringW(_buf); \
     } while (0)
#else
#  define CC_TRACE(fmt, ...) do {} while (0)
#endif
```

**Alternatives considered**:
- File-based log: rejected — violates the "no blocking I/O on hot path" rule if accidentally
  called from Thread 3/4; `OutputDebugString` is already the project standard.
- `Logger` class (already in `src/core/Logger.h`): investigate whether `Logger` uses
  `OutputDebugString` internally. If so, `CC_TRACE` can delegate to it. If `Logger` has I/O
  overhead, `OutputDebugString` direct calls are preferred for controller trace lines.
  **Resolution**: `Logger` wraps `OutputDebugString`; it is safe to use for the non-hot-loop
  trace points. All CC trace calls will use `Logger::Trace(L"[CC] ...")` for consistency.

---

## Additional Research: Thread Startup Ordering

**Question**: AlacEncoderThread needs `RaopSession::AudioSocket()` (available only after
`WM_RAOP_CONNECTED`). How do we sequence startup without blocking Thread 1?

**Answer**: Use the Win32 message loop naturally:

```
Thread 1 (BeginConnect):
  1. StreamSession::Init()        ← pre-allocate all buffers
  2. WasapiCapture::Start()       ← Thread 3 starts; begins TryPush() to ring
  3. RaopSession::Start(cfg)      ← Thread 5 starts RTSP handshake (async)
  4. SetState(Connecting)
  ← returns; Win32 message loop continues

Thread 5 (async):
  5. RTSP OPTIONS→ANNOUNCE→SETUP→RECORD
  6. PostMessage(hwnd, WM_RAOP_CONNECTED, ...)   ← posts to Thread 1

Thread 1 (OnRaopConnected):
  7. AlacEncoderThread::Init(session.AudioSocket(), ...)
  8. AlacEncoderThread::Start()   ← Thread 4 starts consuming ring
  9. PostMessage(hwnd, WM_STREAM_STARTED, ...)   ← notify UI
 10. TransitionTo(Streaming)
```

Key insight: Thread 3 may be writing frames to the ring buffer for 1–3 seconds before Thread 4
starts consuming. The ring buffer is pre-sized to absorb this: 128 slots × ~23 ms/frame ≈ 2.9 s
of capacity. If the ring fills before Thread 4 starts, `TryPush` returns `false` and frames are
dropped — acceptable during the startup window only.

---

## Additional Research: Capture-Only Restart Timing

**Question**: How does the capture restart stay within ≤50 ms (FR-010)?

**Answer**: The restart path on Thread 1 is:
1. `WasapiCapture::Stop()` — signals Thread 3 to exit; joins with 30 ms timeout.
2. `WasapiCapture::Init()` — re-enumerates default audio device; all heap alloc on Thread 1.
3. `WasapiCapture::Start()` — Thread 3 restarts; first `TryPush` resumes within ~1 ms.

The WASAPI `GetDefaultAudioEndpoint` + `Activate` + `Initialize` sequence typically takes
5–15 ms on Windows 10/11. The 30 ms join timeout for Thread 3 exit, plus ~15 ms for reinit,
fits within 50 ms under normal conditions.

**Failure path** (FR-011): If `WasapiCapture::Init()` returns false (no audio device),
`ConnectionController::BeginDisconnect()` is called immediately, followed by a balloon
notification.

---

## Additional Research: Message Numbering — Avoiding Conflicts

**Existing messages** (`src/core/Messages.h`):
```
WM_APP+1  WM_TRAY_CALLBACK
WM_APP+2  WM_TRAY_POPUP_MENU
WM_APP+3  WM_RAOP_CONNECTED       (kept — posted by RaopSession)
WM_APP+4  WM_RAOP_FAILED          (kept — posted by RaopSession)
WM_APP+5  WM_RECEIVERS_UPDATED    (kept — triggers TrayMenu refresh)
WM_APP+6  WM_DEFAULT_DEVICE_CHANGED → renamed WM_AUDIO_DEVICE_LOST (same slot)
WM_APP+7  WM_BONJOUR_MISSING      (kept)
WM_APP+8  WM_TEARDOWN_COMPLETE    → renamed WM_STREAM_STOPPED (same slot)
WM_APP+9  WM_UPDATE_REJECTED      (kept)
```

**New messages** (appended):
```
WM_APP+10  WM_SPEAKER_LOST        — MdnsDiscovery: specific speaker disappeared
WM_APP+11  WM_STREAM_STARTED      — ConnectionController: all 3 threads running
WM_APP+12  WM_CAPTURE_ERROR       — WasapiCapture: non-device-loss error
WM_APP+13  WM_DEVICE_DISCOVERED   — MdnsDiscovery: specific speaker appeared (for auto-connect)
```

`WM_RECEIVERS_UPDATED` (WM_APP+5) is preserved for TrayMenu list refreshes.
`WM_DEVICE_DISCOVERED` (WM_APP+13) fires alongside it, carrying the specific new `AirPlayReceiver`
in LPARAM, enabling auto-connect checks without re-scanning the whole list.

---

## Summary of All Resolved Questions

| # | Question | Answer |
|---|----------|--------|
| 1 | Reconnect policy | 3 retries, exp. backoff 1s/2s/4s (total ≤7s) |
| 2 | Wrong-state message handling | Silent discard + one debug trace; no side effects |
| 3 | Single vs. two device-loss messages | Two distinct: WM_AUDIO_DEVICE_LOST (capture restart), WM_SPEAKER_LOST (full reconnect) |
| 4 | Real-time safety constraints | Zero heap alloc + zero mutex on T3/T4 hot loop; pre-alloc all buffers in Init() |
| 5 | Debug tracing scope | State transitions, thread start/stop, each reconnect attempt, each capture restart; NOT per-frame |
