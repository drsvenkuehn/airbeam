# Feature Specification: WASAPI Loopback Audio Capture

**Feature Branch**: `007-wasapi-loopback-capture`  
**Created**: 2025-07-16  
**Status**: Draft  
**Feature ID**: 007

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Continuous System Audio Capture (Priority: P1)

A user launches AirBeam and selects an AirPlay receiver. All audio currently playing on their Windows PC — music, video, system sounds — is immediately mirrored on the AirPlay receiver with no perceptible latency or dropouts. The user does not need to install drivers, configure audio routing, or take any manual steps to enable capture. From the user's perspective, AirBeam simply "hears" everything Windows is playing and passes it on.

**Why this priority**: This is the core value of the feature. Without reliable, uninterrupted capture, no other aspect of AirBeam can function. Every other user story depends on this foundation being solid.

**Independent Test**: Can be fully tested by starting AirBeam, connecting to an AirPlay receiver, playing a 60-second audio file in Windows Media Player, and verifying that the audio plays continuously on the receiver. Delivers the primary product value: wireless system audio mirroring.

**Acceptance Scenarios**:

1. **Given** AirBeam is streaming to an AirPlay receiver, **When** any Windows application plays audio, **Then** that audio is audible on the receiver within the normal AirPlay latency window (≤2 seconds of stream start)
2. **Given** AirBeam is capturing audio, **When** the audio stream runs continuously for 10 minutes, **Then** no audible dropouts, glitches, or distortion are detected on the receiver
3. **Given** AirBeam is streaming, **When** multiple Windows applications play audio simultaneously (e.g., a browser and a media player), **Then** all audio sources are mixed and audible on the receiver — the capture taps the system mix, not individual apps
4. **Given** no audio is currently playing on Windows, **When** the user starts a new audio source, **Then** capture resumes seamlessly with no additional user action

---

### User Story 2 — Transparent Format Compatibility (Priority: P2)

A user has their Windows audio device configured at 48000 Hz (or 96000 Hz, or any other standard rate). They launch AirBeam and stream to an AirPlay receiver. The audio plays correctly on the receiver without any artifacts caused by sample rate mismatch. The user does not need to reconfigure their Windows audio settings or know anything about sample rates.

**Why this priority**: AirPlay requires 44100 Hz stereo 16-bit PCM. Most Windows systems default to 48000 Hz. Without transparent format conversion, the product would be broken for the majority of users. This is P2 because it is a prerequisite for broad compatibility, second only to the core capture loop itself.

**Independent Test**: Can be fully tested by setting the Windows default audio device to 48000 Hz (or another non-44100 rate) in Windows Sound settings, then streaming a reference tone (e.g., 1 kHz sine wave) through AirBeam and verifying the tone plays at the correct pitch on the AirPlay receiver. Delivers correct operation on the most common Windows audio configuration.

**Acceptance Scenarios**:

1. **Given** the Windows default audio device is configured at any standard sample rate (44100, 48000, 88200, or 96000 Hz), **When** AirBeam starts capturing, **Then** audio arrives at the AirPlay receiver at the correct pitch and tempo
2. **Given** a device running at 48000 Hz, **When** a known reference recording is streamed, **Then** no audible pitch shift, speed change, or resampling artifacts are present at normal listening volume
3. **Given** a device running at a sample rate AirBeam must convert from, **When** silence transitions to audio, **Then** the transition is clean with no click or pop on the receiver

---

### User Story 3 — Automatic Device Change Recovery (Priority: P3)

A user is streaming audio through AirBeam while switching their default Windows audio output — for example, unplugging headphones causes Windows to fall back to speakers, or the user manually selects a different device in Windows Sound settings. The AirPlay stream pauses briefly (at most a fraction of a second, imperceptible in practice) and then resumes automatically from the new device. The user never has to restart AirBeam or reconnect to the AirPlay receiver.

**Why this priority**: Device changes are a common real-world scenario (headphone plugging/unplugging, docking/undocking). Without recovery, users would need to manually restart a stream on every device change, making the product frustrating to use. The brief acceptable gap means this can be handled non-invasively.

**Independent Test**: Can be fully tested by starting a stream, then changing the default audio output device in Windows Sound settings, and verifying that audio resumes on the AirPlay receiver within 50ms of the change with no user intervention. Delivers seamless "it just works" experience for users with dynamic audio setups.

**Acceptance Scenarios**:

1. **Given** AirBeam is actively streaming, **When** the Windows default audio output device is changed, **Then** audio capture automatically re-attaches to the new device and the AirPlay stream resumes within 50 milliseconds
2. **Given** a device change has just occurred, **When** the new capture session is established, **Then** audio from the new device is audible on the receiver with no user interaction required
3. **Given** the default device changes multiple times in rapid succession (e.g., 3 changes within 2 seconds), **Then** the capture subsystem coalesces the events (waiting at most 20 ms after the first), re-initialises exactly once onto the final device, and does not crash or enter a broken state
4. **Given** the device change results in a new audio format (e.g., different sample rate), **Then** the format conversion is re-applied automatically to the new device's format

---

### User Story 4 — Graceful Error Notification (Priority: P4)

A user is streaming through AirBeam when an unrecoverable audio capture failure occurs — for example, the audio device is physically removed, the Windows audio service crashes, or the device driver enters an error state. AirBeam detects the failure and displays a clear notification (e.g., a system-tray balloon or message), allowing the user to understand what went wrong and take corrective action (reconnect the device, restart AirBeam). The application does not hang, crash silently, or continue showing a "streaming" indicator when audio capture has actually failed.

**Why this priority**: Unrecoverable errors are rare but must not leave the application in a silent broken state. Clear error surfacing is a quality-of-life requirement; the primary capture and recovery paths are more critical.

**Independent Test**: Can be fully tested by starting a stream, then simulating a capture failure (e.g., disabling the audio device via Device Manager), and verifying that a notification appears in the AirBeam system-tray area and the streaming state is updated accordingly.

**Acceptance Scenarios**:

1. **Given** AirBeam is streaming, **When** an unrecoverable audio capture error occurs, **Then** the main application window receives an error notification within 1 second of the failure
2. **Given** an error notification has been sent, **When** the user inspects AirBeam's status, **Then** the application no longer indicates an active stream
3. **Given** a recoverable error (e.g., device change) vs. an unrecoverable one (e.g., device removed entirely), **When** an unrecoverable error is raised, **Then** the application stops the stream rather than looping endlessly

---

### Edge Cases

- **Silence / no audio playing**: The capture subsystem runs continuously during streaming even when no audio is being produced. Silence frames are captured and forwarded normally — the encoder receives a steady supply of zero-sample frames.
- **Ring buffer back-pressure**: If the encoder thread falls significantly behind, the shared queue fills. In this case, the capture subsystem drops the oldest frames to prevent unbounded memory growth. Dropped frames produce a brief audible glitch but do not crash or stall the capture loop.
- **Rapid repeated device changes**: Multiple device-change events fired in quick succession (e.g., the OS emitting two events while a device re-enumerates) must be handled without re-initialising twice concurrently. Consecutive events are coalesced via a fixed-deadline timer: the subsystem waits at most **20 ms** after the first event and then re-initialises exactly once, regardless of how many additional notifications arrive during that window.
- **System suspend / resume**: If Windows suspends while AirBeam is streaming, the capture session becomes invalid. On resume, the behaviour is equivalent to an unrecoverable device-lost error; the application notifies the user to reconnect.
- **Device format changes at runtime**: If the Windows audio engine changes the device mix format while AirBeam is running (e.g., after a driver update or settings change without a device-change event), the next capture attempt will fail; this is treated as a device-lost condition and triggers the error notification path.
- **No default render device**: If the system has no audio output device configured, `Start()` must fail immediately and notify the application rather than hanging or crashing.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The capture subsystem MUST tap the Windows system audio mix (all audio rendered to the default output device) without requiring exclusive device ownership or a virtual audio driver
- **FR-002**: The capture subsystem MUST receive audio buffer availability notifications from the OS through an event-signalling mechanism rather than polling a timer or a socket
- **FR-003**: The capture thread MUST be promoted to real-time audio scheduling priority for the full duration of active capture, using the OS-provided multimedia scheduling facility
- **FR-004**: The capture subsystem MUST package captured audio into a continuous sequence of fixed 352-sample stereo frames, using the existing FrameAccumulator component, before handing data off to the encoder pipeline
- **FR-005**: Completed audio frames MUST be delivered to the encoder pipeline through the existing lock-free single-producer/single-consumer queue (SpscRingBuffer) without acquiring any mutex or lock on the data path
- **FR-006**: If the Windows audio engine's mix format differs from 44100 Hz stereo 16-bit signed integer PCM, the capture subsystem MUST convert incoming audio to that target format prior to frame packaging; this conversion MUST be performed using **libspeexdsp** — the already-vendored compact C resampler (BSD licence, integer-friendly API)
- **FR-007**: The capture subsystem MUST monitor the Windows audio endpoint system for default device changes and automatically re-initialise capture from the newly selected device within 50 milliseconds of the change notification; consecutive change events MUST be coalesced using a fixed-deadline timer — the subsystem waits at most 20 ms after the first event before re-initialising, so that a burst of rapid notifications triggers exactly one re-attachment
- **FR-008**: The capture subsystem MUST expose a `Start` operation that accepts a reference to the encoder queue (capacity fixed at `kCaptureQueueFrames = 512` frames, defined as a named constant in the subsystem header), and a `Stop` operation that terminates capture cleanly and releases all held OS audio resources
- **FR-009**: On any unrecoverable capture failure (device lost, format negotiation failure, OS resource error), the capture subsystem MUST dispatch a `WM_CAPTURE_ERROR` message to the application's main window handle with `WPARAM` set to the HRESULT error code and `LPARAM` set to 0 (unused)
- **FR-010**: The capture subsystem's audio processing loop (the hot path: receiving a buffer, converting samples, accumulating frames, pushing to the queue) MUST NOT perform heap allocation, acquire mutex or critical-section locks, or call any blocking OS wait primitive
- **FR-011**: When the encoder queue is full, the capture subsystem MUST discard the current audio buffer rather than blocking or allocating additional memory, accepting a brief audible gap as the consequence
- **FR-012**: The `Stop` operation MUST be callable from any thread and MUST return only after the capture thread has fully exited, leaving no dangling OS handles or background activity
- **FR-013**: The capture subsystem MUST log significant lifecycle events — including session start/stop, device-change re-initialisations, buffer-drop occurrences, and unrecoverable errors — to the existing AirBeam log sink at debug/trace severity; no capture-specific log file or additional observability infrastructure is required

### Key Entities

- **Capture Thread**: The OS-level thread that owns the audio session with Windows, waits for buffer-ready events, performs format conversion, and feeds the encoder pipeline. Runs at elevated real-time priority for its entire lifetime.
- **Audio Session**: The connection between AirBeam and the Windows audio engine for a specific render endpoint. Created on `Start` or device re-attachment; destroyed on `Stop` or unrecoverable error. Holds the event handle used for buffer notifications.
- **Default Render Endpoint**: The Windows audio output device currently designated as the user's default playback device. Can change at any time due to user action or hardware events (plug/unplug).
- **Audio Frame**: A discrete unit of 352 stereo PCM samples at 44100 Hz / 16-bit signed integer — the fixed-size chunk consumed by the ALAC encoder thread. Produced by the FrameAccumulator from variable-length capture buffers.
- **Encoder Queue**: The shared lock-free ring buffer (SpscRingBuffer) connecting the capture thread (producer) to the ALAC encoder thread (consumer). Has a fixed capacity of `kCaptureQueueFrames = 512` frames (compile-time named constant declared in the capture subsystem header); frames are dropped when full.
- **Device Change Notification**: An asynchronous signal from the Windows audio endpoint manager informing AirBeam that the user's default render device has changed. Triggers a controlled teardown and re-initialisation of the audio session. Rapid consecutive notifications are coalesced: a fixed-deadline timer waits up to 20 ms after the first event, then performs a single re-attachment.
- **Format Converter**: The component (backed by **libspeexdsp**, the vendored compact C resampler with BSD licence and integer-friendly API) that converts captured PCM from the device's native sample rate and bit depth to the AirPlay-required 44100 Hz stereo S16LE format. Instantiated only when the native format differs from the target.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All audio playing on Windows is audible on the connected AirPlay receiver with no perceptible dropout during a 10-minute continuous playback session under normal system load
- **SC-002**: When the default audio output device is changed, the AirPlay stream resumes automatically within 50 milliseconds of the device-change event, with no user interaction required
- **SC-003**: The capture thread's CPU utilisation remains below 3% on representative consumer hardware (a mid-range quad-core CPU at normal clock speed) during continuous stereo audio streaming
- **SC-004**: Audio playing at a device-native sample rate other than 44100 Hz (e.g., 48000 Hz) is reproduced on the AirPlay receiver at the correct pitch — no audible pitch shift detectable under a standard listening test
- **SC-005**: Unrecoverable capture errors are communicated to the application within 1 second of the failure event, measured from the OS error to the point the error notification is visible to the user
- **SC-006**: The capture subsystem can be started and stopped 50 consecutive times in a test harness with no resource leaks (handles, memory, or audio sessions remaining open after Stop returns)
- **SC-007**: No heap allocations occur on the audio hot path during steady-state capture, verifiable via an instrumented allocator or a memory profiler attached during a capture session

## Assumptions

- The Windows audio engine always exposes a loopback capture endpoint for the default render device on Windows 10 and later; no special hardware or driver support is required beyond what ships with the OS
- The default render device always produces stereo PCM audio at one of the well-known Windows sample rates (44100, 48000, 88200, or 96000 Hz); exotic multi-channel or compressed formats are out of scope for this feature
- The `SpscRingBuffer` and `FrameAccumulator` components are already implemented, tested, and available for use by this feature without modification
- **libspeexdsp** (the compact C resampler with BSD licence and integer-friendly API) is already vendored, compiled, and linked into the project; no additional dependency management is required
- `WM_CAPTURE_ERROR` is already defined in `Messages.h` and the main window's message pump is already prepared to receive it
- AirBeam targets 44100 Hz stereo 16-bit signed integer PCM as its sole AirPlay stream format, as mandated by the RAOP protocol; no other output formats need to be supported
- When the encoder queue is full, silent frame dropping is the correct and accepted behaviour; the encoder is expected to keep up under normal operating conditions
- Silence (no audio playing on Windows) is a valid and expected steady state; the capture thread must run and produce silence frames without special handling
- Format conversion quality need only be perceptually transparent at normal listening volume; audiophile-grade (e.g., noise-shaped dithering) accuracy is not required
- System suspend/resume is treated as an unrecoverable device-loss event; automatic recovery from suspend is out of scope for this feature

## Clarifications

### Session 2026-03-26

- Q: What is the observability/diagnostics strategy for the capture subsystem? → A: Debug logging — log capture events to the existing AirBeam log sink at debug/trace severity
- Q: What is the `WM_CAPTURE_ERROR` message payload? → A: `WPARAM` = HRESULT error code; `LPARAM` = 0 (unused)
- Q: What is the ring buffer (encoder queue) capacity, and how is it expressed? → A: Fixed named constant `kCaptureQueueFrames = 512`, declared in the capture subsystem header
- Q: How should consecutive device-change events be coalesced? → A: Fixed-deadline timer — wait at most 20 ms after the first event, then re-initialise exactly once
- Q: Which resampling library should be used for format conversion (FR-006)? → A: libspeexdsp — compact C resampler, integer-friendly API, BSD licence
