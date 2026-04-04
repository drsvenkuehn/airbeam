#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <functional>
#include <memory>

#include "core/PipelineState.h"
#include "core/ReconnectContext.h"
#include "core/StreamSession.h"
#include "discovery/AirPlayReceiver.h"
#include "discovery/ReceiverList.h"
#include "ui/TrayIcon.h"
#include "ui/BalloonNotify.h"
#include "core/Config.h"
#include "core/Logger.h"

/// Trace macro — emits a debug log entry to the Logger file AND OutputDebugString.
#define CC_TRACE(fmt, ...) \
    do { \
        Logger::Instance().LogW(LogLevel::kDebug, fmt, ##__VA_ARGS__); \
        OutputDebugStringW(L"[CC] "); \
        wchar_t _cc_buf[256]; \
        _snwprintf_s(_cc_buf, 256, _TRUNCATE, fmt, ##__VA_ARGS__); \
        OutputDebugStringW(_cc_buf); \
        OutputDebugStringW(L"\n"); \
    } while(0)

/// Central orchestrator for the audio pipeline lifecycle.
///
/// Lives entirely on Thread 1 (Win32 message loop).
/// Drives a strict four-state machine (Idle → Connecting → Streaming → Disconnecting).
/// Coordinates the three pipeline threads exclusively via Win32 posted messages and timers.
/// No mutexes or atomics are used for state management.
class ConnectionController {
public:
    /// Factory function used to create StreamSession objects.
    /// Pass nullptr to use the default factory (creates real StreamSession).
    /// Override in unit tests to inject MockStreamSession.
    using SessionFactory = std::function<std::unique_ptr<StreamSession>()>;

    ConnectionController(HWND hwnd,
                         Config&        config,
                         ReceiverList&  receivers,
                         TrayIcon&      tray,
                         BalloonNotify& balloon,
                         Logger&        logger,
                         SessionFactory factory = nullptr);

    ~ConnectionController();

    // Non-copyable, non-movable
    ConnectionController(const ConnectionController&)            = delete;
    ConnectionController& operator=(const ConnectionController&) = delete;

    // ── Command methods (called by AppController from Thread 1) ──────────────

    /// User selected a speaker (or auto-connect fired). Begins Idle→Connecting.
    /// If already Connecting/Streaming/Disconnecting, tears down current session
    /// first, then reconnects to target.
    void Connect(const AirPlayReceiver& target);

    /// User clicked "Disconnect" or application is shutting down.
    /// Cancels any pending reconnect.
    void Disconnect();

    /// User toggled Low Latency menu item.
    /// Idle: persists preference only. Streaming: full pipeline restart.
    /// Connecting/Disconnecting: deferred until stable state.
    void SetLowLatency(bool enabled);

    /// Volume slider moved.
    /// Persists to config. If Streaming, propagates immediately to RaopSession.
    void SetVolume(float linear);

    /// Begin the 5-second auto-connect discovery window on startup (FR-015).
    /// No-op if config_.lastDevice is empty.
    void StartAutoConnectWindow();

    // ── Win32 message handlers (called by AppController::WndProc on Thread 1) ─

    void OnRaopConnected(LPARAM lParam);      ///< WM_RAOP_CONNECTED
    void OnRaopFailed(LPARAM lParam);         ///< WM_RAOP_FAILED
    void OnStreamStopped();                   ///< WM_STREAM_STOPPED (teardown complete)
    void OnAudioDeviceLost();                 ///< WM_AUDIO_DEVICE_LOST
    void OnSpeakerLost(const wchar_t* devId); ///< WM_SPEAKER_LOST (deletes devId after use)
    void OnCaptureError();                    ///< WM_CAPTURE_ERROR
    void OnEncoderError();                    ///< WM_ENCODER_ERROR
    void OnDeviceDiscovered(LPARAM lParam);   ///< WM_DEVICE_DISCOVERED (auto-connect check)
    void OnTimer(UINT timerId);               ///< WM_TIMER dispatch

    // ── AirPlay 2 message handlers (Feature 010) ─────────────────────────────
    /// WM_AP2_CONNECTED — AP2 stream is live; transition Connecting → Streaming.
    void OnAp2Connected(LPARAM lParam);
    /// WM_AP2_FAILED — AP2 session failed; schedule retry or post balloon.
    void OnAp2Failed(WPARAM wParam, LPARAM lParam);

    // ── Queries ───────────────────────────────────────────────────────────────

    PipelineState GetState() const noexcept { return state_; }
    bool          IsStreaming() const noexcept { return state_ == PipelineState::Streaming; }
    bool          IsIdle() const noexcept { return state_ == PipelineState::Idle; }

    /// Instance name of the current/last connect target, or empty if none.
    const std::wstring& GetCurrentTargetInstance() const noexcept { return currentTargetInstance_; }

private:
    // ── Internal helpers ──────────────────────────────────────────────────────

    void BeginConnect(const AirPlayReceiver& target);
    void BeginDisconnect(bool forReconnect = false);
    void OnTeardownComplete();
    void ScheduleReconnect();
    void AttemptReconnect();
    void CancelReconnect();
    void TransitionTo(PipelineState next);

    // ── Win32 timer IDs — must not collide with AppController timer IDs ───────
    static constexpr UINT TIMER_RECONNECT_RETRY = 10;  ///< Exponential-backoff reconnect
    static constexpr UINT TIMER_AUTOCONNECT     = 11;  ///< 5-second startup window

    // ── Dependencies (all owned by AppController, outlive ConnectionController) ─
    HWND              hwnd_;
    Config&           config_;
    ReceiverList&     receivers_;
    TrayIcon&         tray_;
    BalloonNotify&    balloon_;
    Logger&           logger_;

    // ── State ─────────────────────────────────────────────────────────────────
    PipelineState     state_       { PipelineState::Idle };
    std::wstring      currentTargetInstance_;             ///< instanceName of last Connect() target
    ReconnectContext  reconnect_;
    bool              inAutoConnectWindow_      { false };
    bool              pendingLowLatencyToggle_  { false }; ///< deferred during transitions
    bool              pendingDisconnect_         { false }; ///< user disconnect while Connecting

    // ── Session ───────────────────────────────────────────────────────────────
    std::unique_ptr<StreamSession> session_;  ///< null when Idle
    SessionFactory                 sessionFactory_;
};
