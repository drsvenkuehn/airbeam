// ConnectionController.cpp — Four-state pipeline lifecycle orchestrator.
// All methods run on Thread 1 (Win32 message loop). No new mutex/atomic/lock used.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "Bcrypt.lib")

#include "core/ConnectionController.h"
#include "core/Messages.h"
#include "resource_ids.h"
#include "localization/StringLoader.h"

#include <string>
#include <cassert>

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────────────────────────────────────

ConnectionController::ConnectionController(
    HWND hwnd,
    Config&        config,
    ReceiverList&  receivers,
    TrayIcon&      tray,
    BalloonNotify& balloon,
    Logger&        logger,
    SessionFactory factory)
    : hwnd_(hwnd)
    , config_(config)
    , receivers_(receivers)
    , tray_(tray)
    , balloon_(balloon)
    , logger_(logger)
    , sessionFactory_(factory
        ? std::move(factory)
        : []() -> std::unique_ptr<StreamSession> {
              return std::make_unique<StreamSession>();
          })
{
}

ConnectionController::~ConnectionController()
{
    // Best-effort cleanup — call Disconnect() to stop any in-progress session.
    // This may be called during WM_DESTROY on Thread 1, which is safe.
    if (state_ != PipelineState::Idle) {
        reconnect_.Reset();
        if (session_) {
            session_->StopCapture();
            session_->StopEncoder();
            session_->StopRaop();
            session_.reset();
        }
        state_ = PipelineState::Idle;
    }
    // Kill any pending timers
    KillTimer(hwnd_, TIMER_RECONNECT_RETRY);
    KillTimer(hwnd_, TIMER_AUTOCONNECT);
}

// ─────────────────────────────────────────────────────────────────────────────
// TransitionTo — the only method that writes state_
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::TransitionTo(PipelineState next)
{
    CC_TRACE(L"[CC] State %d → %d", static_cast<int>(state_), static_cast<int>(next));
    state_ = next;

    // Update tray icon state at each transition
    switch (next) {
    case PipelineState::Idle:
        tray_.SetState(TrayState::Idle);
        break;
    case PipelineState::Connecting:
        tray_.SetState(TrayState::Connecting,
                       session_ ? session_->Target().displayName.c_str() : nullptr);
        break;
    case PipelineState::Streaming:
        tray_.SetState(TrayState::Streaming,
                       session_ ? session_->Target().displayName.c_str() : nullptr);
        break;
    case PipelineState::Disconnecting:
        // Keep current tray state during teardown; tray goes Idle on WM_STREAM_STOPPED
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginConnect — Idle → Connecting
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::BeginConnect(const AirPlayReceiver& target)
{
    assert(state_ == PipelineState::Idle);
    assert(!session_);

    currentTargetInstance_ = target.instanceName;
    CC_TRACE(L"[CC] BeginConnect → %ls", target.displayName.c_str());

    // Pre-allocate all session resources on Thread 1 before any thread starts
    session_ = sessionFactory_();
    if (!session_ || !session_->Init(target, config_.lowLatency, hwnd_)) {
        CC_TRACE(L"[CC] BeginConnect: session Init failed");
        session_.reset();
        balloon_.ShowError(IDS_TITLE_CONNECTION_FAILED, IDS_BALLOON_CONNECTION_FAILED,
                           target.displayName.c_str());
        return;
    }

    // Start capture thread (Thread 3)
    session_->StartCapture();

    // Start RAOP RTSP handshake (Thread 5) — async; posts WM_RAOP_CONNECTED or WM_RAOP_FAILED
    session_->StartRaop(config_.volume);

    TransitionTo(PipelineState::Connecting);
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginDisconnect — Connecting/Streaming → Disconnecting
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::BeginDisconnect(bool forReconnect)
{
    assert(state_ == PipelineState::Connecting || state_ == PipelineState::Streaming);
    assert(session_);

    CC_TRACE(L"[CC] BeginDisconnect forReconnect=%d", forReconnect ? 1 : 0);

    TransitionTo(PipelineState::Disconnecting);

    // Stop threads in strict order (FR-002):
    // 1. Capture first — no more ring writes (≤30 ms join)
    session_->StopCapture();
    // 2. Encoder next — drains remaining ring frames before stop (≤50 ms join)
    session_->StopEncoder();
    // 3. RAOP last — sends RTSP TEARDOWN (≤200 ms join)
    session_->StopRaop();

    // Post WM_STREAM_STOPPED so OnStreamStopped() runs on Thread 1 via the message loop.
    // Store reconnect intent in reconnect_.pending before posting.
    if (forReconnect && !reconnect_.pending) {
        // pending and targetDevice are set by the caller (Connect/OnRaopFailed/etc.)
        // If not yet set, the current session target is the reconnect target.
        if (!reconnect_.pending) {
            reconnect_.pending = true;
            reconnect_.targetDevice = session_->Target();
        }
    }

    PostMessageW(hwnd_, WM_STREAM_STOPPED, 0, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnTeardownComplete — called from OnStreamStopped
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnTeardownComplete()
{
    assert(state_ == PipelineState::Disconnecting);
    session_.reset();  // destroy all pipeline objects (threads already stopped)
    TransitionTo(PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// ScheduleReconnect / AttemptReconnect / CancelReconnect
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::ScheduleReconnect()
{
    assert(state_ == PipelineState::Idle);
    assert(reconnect_.pending);

    if (!reconnect_.HasAttemptsLeft()) {
        CC_TRACE(L"[CC] ScheduleReconnect: all attempts exhausted");
        balloon_.ShowWarning(IDS_TITLE_RECONNECT_FAILED, IDS_RECONNECT_FAILED,
                             reconnect_.targetDevice.displayName.c_str());
        reconnect_.Reset();
        return;
    }

    UINT delayMs = reconnect_.CurrentDelayMs();
    CC_TRACE(L"[CC] ScheduleReconnect attempt=%d delay=%u ms", reconnect_.attempt, delayMs);
    SetTimer(hwnd_, TIMER_RECONNECT_RETRY, delayMs, nullptr);
}

void ConnectionController::AttemptReconnect()
{
    assert(state_ == PipelineState::Idle);
    assert(reconnect_.pending);

    // Check if device is still visible in the receiver list
    const std::wstring& targetStableId = reconnect_.targetDevice.stableId;
    const std::wstring& targetInstance = reconnect_.targetDevice.instanceName;

    bool devicePresent = false;
    AirPlayReceiver foundReceiver;
    auto snapshot = receivers_.Snapshot();
    for (const auto& r : snapshot) {
        if (r.stableId == targetStableId ||
            (!targetStableId.empty() && r.instanceName == targetInstance))
        {
            devicePresent = true;
            foundReceiver = r;
            break;
        }
    }

    if (!devicePresent) {
        CC_TRACE(L"[CC] AttemptReconnect: device absent — giving up");
        KillTimer(hwnd_, TIMER_RECONNECT_RETRY);
        balloon_.ShowWarning(IDS_TITLE_SPEAKER_UNAVAILABLE, IDS_SPEAKER_UNAVAILABLE,
                             reconnect_.targetDevice.displayName.c_str());
        reconnect_.Reset();
        return;
    }

    // Device is present — increment attempt counter and begin connection
    reconnect_.attempt++;
    CC_TRACE(L"[CC] AttemptReconnect attempt %d of %d → %ls",
             reconnect_.attempt, ReconnectContext::kMaxAttempts,
             foundReceiver.displayName.c_str());

    BeginConnect(foundReceiver);
}

void ConnectionController::CancelReconnect()
{
    if (reconnect_.pending) {
        KillTimer(hwnd_, TIMER_RECONNECT_RETRY);
        reconnect_.Reset();
        CC_TRACE(L"[CC] Reconnect cancelled by user");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Connect — public command from AppController
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::Connect(const AirPlayReceiver& target)
{
    // Reject AirPlay 2-only devices before attempting any connection.
    if (target.isAirPlay2Only) {
        LOG_WARN("CC: \"%ls\" is AirPlay 2-only — not supported in v1.0",
                 target.displayName.c_str());
        balloon_.ShowWarning(IDS_TITLE_AIRPLAY2_ONLY, IDS_AIRPLAY2_ONLY,
                             target.displayName.c_str());
        return;
    }

    switch (state_) {
    case PipelineState::Idle:
        CancelReconnect();
        BeginConnect(target);
        break;

    case PipelineState::Connecting:
    case PipelineState::Streaming:
        // Switching to a different speaker: cancel and reconnect to new target
        if (session_ && session_->Target().stableId == target.stableId) {
            // Same target — no-op
            CC_TRACE(L"[CC] Connect: already connected/connecting to same target");
            return;
        }
        CC_TRACE(L"[CC] Connect: switching speaker, queuing reconnect to %ls",
                 target.displayName.c_str());
        reconnect_.pending      = true;
        reconnect_.targetDevice = target;
        reconnect_.attempt      = 0;
        BeginDisconnect(/*forReconnect=*/true);
        break;

    case PipelineState::Disconnecting:
        // Teardown in progress — override reconnect target
        CC_TRACE(L"[CC] Connect: queuing reconnect to %ls (Disconnecting state)",
                 target.displayName.c_str());
        reconnect_.pending      = true;
        reconnect_.targetDevice = target;
        reconnect_.attempt      = 0;
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Disconnect — public command from AppController
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::Disconnect()
{
    switch (state_) {
    case PipelineState::Idle:
        CancelReconnect();
        return;

    case PipelineState::Connecting:
    case PipelineState::Streaming:
        CancelReconnect();
        BeginDisconnect(/*forReconnect=*/false);
        break;

    case PipelineState::Disconnecting:
        // Teardown already in progress — cancel any pending reconnect
        CancelReconnect();
        pendingDisconnect_ = true;
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SetLowLatency — public command from AppController
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::SetLowLatency(bool enabled)
{
    if (config_.lowLatency == enabled) return;

    config_.lowLatency = enabled;
    config_.Save();

    switch (state_) {
    case PipelineState::Idle:
        // Preference saved; no pipeline action needed
        CC_TRACE(L"[CC] SetLowLatency=%d saved (Idle)", enabled ? 1 : 0);
        break;

    case PipelineState::Streaming:
        // Restart pipeline with new buffer size
        CC_TRACE(L"[CC] SetLowLatency=%d — restarting pipeline", enabled ? 1 : 0);
        reconnect_.pending      = true;
        reconnect_.targetDevice = session_->Target();
        reconnect_.attempt      = 0;
        BeginDisconnect(/*forReconnect=*/true);
        break;

    case PipelineState::Connecting:
    case PipelineState::Disconnecting:
        // Defer toggle; apply when next stable state is reached
        pendingLowLatencyToggle_ = true;
        CC_TRACE(L"[CC] SetLowLatency=%d deferred (state=%d)", enabled ? 1 : 0,
                 static_cast<int>(state_));
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SetVolume — public command from AppController
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::SetVolume(float linear)
{
    config_.volume = linear;
    config_.Save();

    if (state_ == PipelineState::Streaming && session_) {
        session_->SetVolume(linear);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// StartAutoConnectWindow — startup auto-connect (FR-015)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::StartAutoConnectWindow()
{
    if (config_.lastDevice.empty()) return;

    CC_TRACE(L"[CC] StartAutoConnectWindow for device: %ls", config_.lastDevice.c_str());
    inAutoConnectWindow_ = true;
    SetTimer(hwnd_, TIMER_AUTOCONNECT, 5000, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnRaopConnected — WM_RAOP_CONNECTED (Thread 5 → Thread 1)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnRaopConnected(LPARAM /*lParam*/)
{
    if (state_ != PipelineState::Connecting) {
        CC_TRACE(L"[CC] OnRaopConnected ignored in state %d", static_cast<int>(state_));
        return;
    }

    CC_TRACE(L"[CC] OnRaopConnected — initialising encoder");

    // Generate a fresh SSRC for this session's RTP stream
    uint32_t ssrc = 0;
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&ssrc), 4,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    // Initialise AlacEncoderThread on Thread 1 (heap alloc OK before Start())
    if (!session_->InitEncoder(ssrc, hwnd_)) {
        CC_TRACE(L"[CC] OnRaopConnected: encoder init failed");
        BeginDisconnect(/*forReconnect=*/false);
        balloon_.ShowError(IDS_TITLE_CONNECTION_FAILED, IDS_BALLOON_CONNECTION_FAILED,
                           session_->Target().displayName.c_str());
        return;
    }

    // Start Thread 4
    session_->StartEncoder();

    // Notify AppController to update tray menu
    PostMessageW(hwnd_, WM_STREAM_STARTED, 0, 0);

    // Persist device ID for auto-reconnect on next launch
    const auto& target = session_->Target();
    if (!target.stableId.empty())
        config_.lastDevice = target.stableId;
    else if (!target.instanceName.empty())
        config_.lastDevice = target.instanceName;
    config_.Save();  // exactly once here (FR-003)

    // If this was a reconnect attempt, show success balloon
    if (reconnect_.pending) {
        balloon_.ShowInfo(IDS_TITLE_RECONNECTED, IDS_RECONNECTED,
                          target.displayName.c_str());
        reconnect_.Reset();
    }

    TransitionTo(PipelineState::Streaming);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnRaopFailed — WM_RAOP_FAILED (Thread 5 → Thread 1)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnRaopFailed(LPARAM /*lParam*/)
{
    if (state_ != PipelineState::Connecting && state_ != PipelineState::Streaming) {
        CC_TRACE(L"[CC] OnRaopFailed ignored in state %d", static_cast<int>(state_));
        return;
    }

    CC_TRACE(L"[CC] OnRaopFailed — beginning disconnect (forReconnect=true)");

    // Set reconnect target before BeginDisconnect so it's preserved
    if (!reconnect_.pending) {
        reconnect_.pending      = true;
        reconnect_.targetDevice = session_->Target();
        // attempt stays at 0 — will be incremented in AttemptReconnect()
    }

    BeginDisconnect(/*forReconnect=*/true);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStreamStopped — WM_STREAM_STOPPED (posted by BeginDisconnect on Thread 1)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnStreamStopped()
{
    if (state_ != PipelineState::Disconnecting) {
        CC_TRACE(L"[CC] OnStreamStopped ignored in state %d", static_cast<int>(state_));
        return;
    }

    CC_TRACE(L"[CC] OnStreamStopped — teardown complete");

    const bool hasReconnect = reconnect_.pending;
    const bool hasDisconnect = pendingDisconnect_;
    pendingDisconnect_ = false;

    OnTeardownComplete();  // destroys session_, transitions to Idle

    if (hasDisconnect) {
        // User requested disconnect; cancel any reconnect intent
        CancelReconnect();
        return;
    }

    if (hasReconnect) {
        ScheduleReconnect();  // sets timer or gives up
    }

    // Apply deferred low-latency toggle if one was pending
    if (pendingLowLatencyToggle_) {
        pendingLowLatencyToggle_ = false;
        // The toggle was already persisted; nothing more to do here since we're Idle
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnAudioDeviceLost — WM_AUDIO_DEVICE_LOST (Thread 3 → Thread 1)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnAudioDeviceLost()
{
    if (state_ != PipelineState::Streaming) {
        CC_TRACE(L"[CC] OnAudioDeviceLost ignored in state %d", static_cast<int>(state_));
        return;
    }

    CC_TRACE(L"[CC] OnAudioDeviceLost — attempting capture-only restart");

    if (!session_->ReinitCapture()) {
        CC_TRACE(L"[CC] OnAudioDeviceLost: capture reinit failed — full disconnect");
        BeginDisconnect(/*forReconnect=*/false);
        balloon_.ShowError(IDS_TITLE_AUDIO_ERROR, IDS_ERROR_NO_AUDIO_DEVICE);
        return;
    }

    CC_TRACE(L"[CC] OnAudioDeviceLost: capture restart complete");
    // State remains Streaming — no TransitionTo call
}

// ─────────────────────────────────────────────────────────────────────────────
// OnSpeakerLost — WM_SPEAKER_LOST (Thread 2 → Thread 1)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnSpeakerLost(const wchar_t* devId)
{
    // devId is heap-allocated wchar_t[] by MdnsDiscovery; we must delete[] it after reading
    if (!devId) return;

    const std::wstring id(devId);
    delete[] const_cast<wchar_t*>(devId);  // free the MdnsDiscovery-allocated array

    if (state_ != PipelineState::Streaming) {
        CC_TRACE(L"[CC] OnSpeakerLost %ls ignored in state %d",
                 id.c_str(), static_cast<int>(state_));
        return;
    }

    // Only react if this is our connected device
    if (!session_) return;
    const auto& t = session_->Target();
    if (t.instanceName != id && t.stableId != id) {
        CC_TRACE(L"[CC] OnSpeakerLost %ls — not our device, ignoring", id.c_str());
        return;
    }

    CC_TRACE(L"[CC] OnSpeakerLost: connected speaker lost — beginning reconnect");

    reconnect_.pending      = true;
    reconnect_.targetDevice = session_->Target();
    BeginDisconnect(/*forReconnect=*/true);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnCaptureError — WM_CAPTURE_ERROR (Thread 3 → Thread 1)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnCaptureError()
{
    if (state_ != PipelineState::Streaming && state_ != PipelineState::Connecting) {
        CC_TRACE(L"[CC] OnCaptureError ignored in state %d", static_cast<int>(state_));
        return;
    }

    CC_TRACE(L"[CC] OnCaptureError — full disconnect (no reconnect)");
    BeginDisconnect(/*forReconnect=*/false);
    balloon_.ShowError(IDS_TITLE_AUDIO_ERROR, IDS_ERROR_CAPTURE_FAILED);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnEncoderError — WM_ENCODER_ERROR (Thread 4 → Thread 1)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnEncoderError()
{
    if (state_ != PipelineState::Streaming) {
        CC_TRACE(L"[CC] OnEncoderError ignored in state %d", static_cast<int>(state_));
        return;
    }

    CC_TRACE(L"[CC] OnEncoderError — full disconnect (no reconnect)");
    BeginDisconnect(/*forReconnect=*/false);
    balloon_.ShowError(IDS_TITLE_AUDIO_ERROR, IDS_ENCODER_ERROR);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnDeviceDiscovered — WM_DEVICE_DISCOVERED (Thread 2 → Thread 1)
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnDeviceDiscovered(LPARAM lParam)
{
    // lParam is heap-allocated wchar_t[] containing the receiver stableId (MAC)
    if (!lParam) return;

    const wchar_t* stableIdPtr = reinterpret_cast<const wchar_t*>(lParam);
    const std::wstring stableId(stableIdPtr);
    delete[] const_cast<wchar_t*>(stableIdPtr);  // free the MdnsDiscovery-allocated array

    if (!inAutoConnectWindow_) return;
    if (config_.lastDevice.empty()) return;

    // Check if this device matches the saved last device
    if (stableId != config_.lastDevice) {
        // Also try case-insensitive comparison
        bool match = (stableId.size() == config_.lastDevice.size());
        if (match) {
            for (size_t i = 0; i < stableId.size(); ++i) {
                if (towupper(stableId[i]) != towupper(config_.lastDevice[i])) {
                    match = false;
                    break;
                }
            }
        }
        if (!match) return;
    }

    CC_TRACE(L"[CC] OnDeviceDiscovered: auto-connect match for %ls", stableId.c_str());

    // Find the full receiver in the list
    auto snapshot = receivers_.Snapshot();
    for (const auto& r : snapshot) {
        if (r.stableId == stableId ||
            (!stableId.empty() && _wcsicmp(r.stableId.c_str(), stableId.c_str()) == 0))
        {
            Connect(r);
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnTimer — WM_TIMER dispatch
// ─────────────────────────────────────────────────────────────────────────────

void ConnectionController::OnTimer(UINT timerId)
{
    if (timerId == TIMER_RECONNECT_RETRY) {
        KillTimer(hwnd_, TIMER_RECONNECT_RETRY);
        if (state_ == PipelineState::Idle && reconnect_.pending) {
            AttemptReconnect();
        } else {
            CC_TRACE(L"[CC] TIMER_RECONNECT_RETRY: unexpected state %d",
                     static_cast<int>(state_));
        }
        return;
    }

    if (timerId == TIMER_AUTOCONNECT) {
        KillTimer(hwnd_, TIMER_AUTOCONNECT);
        inAutoConnectWindow_ = false;
        CC_TRACE(L"[CC] TIMER_AUTOCONNECT: 5-second window expired (no match)");
        // FR-016: no notification on silent expiry
        return;
    }
}
