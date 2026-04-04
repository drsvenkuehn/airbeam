# Contract: AudioFrame Data Format

**Subsystem**: Audio Capture → Encoder (Feature 007)  
**File**: `src/audio/AudioFrame.h`  
**Contract type**: In-memory data structure / wire format  
**Stability**: Frozen — changes require updates to AlacEncoderThread, SpscRingBuffer, and all tests

---

## Overview

`AudioFrame` is the fixed-size unit of PCM audio data exchanged between the capture subsystem (Thread 3, producer) and the ALAC encoder thread (Thread 4, consumer). Its layout is fixed at compile time and validated by a `static_assert`.

---

## Struct Definition

```cpp
/// One ALAC frame worth of audio: 352 stereo PCM-16 samples.
/// Interleaved: [L0, R0, L1, R1, ..., L351, R351]
/// Samples: 704 × int16_t = 1408 bytes; + frameCount: 4 bytes → sizeof(AudioFrame) = 1412 bytes (enforced by static_assert below)
struct AudioFrame {
    int16_t  samples[704];   ///< 352 stereo samples (interleaved L/R)
    uint32_t frameCount;     ///< sequence number assigned by WasapiCapture
};

static_assert(sizeof(AudioFrame) == 1412, "AudioFrame size unexpected");
```

---

## Sample Layout

```
samples[0]  = Left  channel, sample 0
samples[1]  = Right channel, sample 0
samples[2]  = Left  channel, sample 1
samples[3]  = Right channel, sample 1
...
samples[702] = Left  channel, sample 351
samples[703] = Right channel, sample 351
```

**Access pattern** for sample `i` (0-indexed):
```cpp
int16_t left  = frame.samples[i * 2];
int16_t right = frame.samples[i * 2 + 1];
```

---

## Audio Properties

| Property | Value |
|----------|-------|
| Samples per frame | 352 (fixed, matches ALAC encoder block size) |
| Channels | 2 (stereo) |
| Sample rate | 44 100 Hz |
| Bit depth | 16-bit signed integer (S16LE on little-endian x86-64) |
| Byte order | Little-endian (native x86-64; no byte-swapping required) |
| Frame duration | 352 / 44 100 ≈ **7.98 ms** |
| Frame size | 1 408 bytes (samples) + 4 bytes (frameCount) = **1 412 bytes** |

---

## frameCount Field

```cpp
uint32_t frameCount;  // assigned by WasapiCapture; wraps at UINT32_MAX
```

- Monotonically increasing unsigned counter, assigned by `WasapiCapture` before pushing to the ring buffer.
- Incremented once per emitted frame, per `Start()`/`Stop()` cycle. Resets to 0 on each `Start()`.
- Wraps naturally at `UINT32_MAX + 1` → 0 (no special handling required).
- **Not used for timing**: the ALAC encoder thread does not use `frameCount` for RTP timestamp generation (that is derived from the sample count × sample rate).
- **Used for diagnostics**: the encoder thread or a monitoring path can compare expected vs received `frameCount` values to detect drops. A gap of N in `frameCount` means N frames were dropped by the capture subsystem (FR-011: full-queue drop).

---

## Silence Representation

When the Windows audio engine signals `AUDCLNT_BUFFERFLAGS_SILENT` for a captured buffer, `WasapiCapture` emits `AudioFrame` objects with all `samples[]` elements set to zero:

```cpp
// All samples are zero for a silent frame
frame.samples[0..703] = 0x0000;
frame.frameCount = <next sequence number>;  // sequence continues normally
```

Silent frames are indistinguishable from genuinely-zero-valued audio by the encoder. The sequence number is always valid and incrementing.

---

## Producer Responsibilities (WasapiCapture / Thread 3)

- Emit exactly one `AudioFrame` per 352 stereo samples accumulated (via `FrameAccumulator`).
- Assign `frameCount` as a monotonically increasing counter starting at 0 after each `Start()`.
- Convert all audio to 44 100 Hz stereo S16LE before writing to `samples[]` — the encoder thread never sees unconverted audio.
- Attempt `SpscRingBuffer::TryPush(frame)`. On failure (full queue), drop the frame (do not block, do not retry), increment `droppedFrameCount_`, and log at TRACE level.

## Consumer Responsibilities (AlacEncoderThread / Thread 4)

- Call `SpscRingBuffer::TryPop(frame)` to retrieve frames. If the buffer is empty, spin or yield briefly — do not call blocking waits on the hot path.
- Feed `frame.samples[0..703]` directly to the ALAC encoder as a 352-frame stereo block.
- Optionally inspect `frameCount` for drop detection; gaps indicate frames dropped by the capture thread.
- Do **not** modify `AudioFrame` fields; treat the struct as immutable after `TryPop()` returns.

---

## Ring Buffer Parameters

The `SpscRingBuffer` carrying `AudioFrame` objects between Thread 3 and Thread 4 must be instantiated with capacity `kCaptureQueueFrames`:

```cpp
// Required instantiation (in ConnectionController or AppController):
SpscRingBuffer<AudioFrame, kCaptureQueueFrames> captureQueue;
//                         ^^^^^^^^^^^^^^^^^^^
//                         kCaptureQueueFrames = 512 (from WasapiCapture.h)
```

At steady-state (encoder keeping up), typical queue occupancy is 1–5 frames. The 512-frame capacity provides ~4 seconds of burst headroom before drops occur.

---

## Compatibility Notes

- `AudioFrame` is a POD (`is_trivially_copyable_v<AudioFrame> == true`). It may be `memcpy`'d freely.
- The struct has **no padding** between `samples[703]` and `frameCount` on MSVC x86-64 with default alignment (verified by the `sizeof` assert).
- `samples[]` element type is `int16_t` (exactly 16 bits, signed, two's complement on all supported platforms).
- No endian conversion is required on Windows x86-64 (little-endian native = S16LE wire format).
