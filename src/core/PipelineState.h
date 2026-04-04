#pragma once
#include <cstdint>

/// The four observable states of the audio pipeline.
/// All transitions occur on Thread 1 (Win32 message loop) only.
/// No state may be skipped; no concurrent states are permitted (FR-004).
enum class PipelineState : uint8_t {
    Idle,           ///< No active session. Pipeline threads not running. session_ == nullptr.
    Connecting,     ///< Pipeline starting: RaopSession RTSP handshake in progress.
    Streaming,      ///< All three pipeline threads running; audio flowing.
    Disconnecting,  ///< Pipeline shutting down: stopping threads in order.
};
