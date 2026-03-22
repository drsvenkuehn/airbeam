#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bcrypt.lib")

#include "core/AppController.h"

// Audio and protocol subsystem headers (compiled once their modules exist).
#include "audio/WasapiCapture.h"
#include "audio/AlacEncoderThread.h"
#include "protocol/RaopSession.h"

#include <shellapi.h>
#include <cassert>

#include "core/Commands.h"
#include "core/Messages.h"
#include "ui/StartupRegistry.h"
#include "resource_ids.h"
#include "discovery/MdnsDiscovery.h"

AppController::~AppController() = default;

bool AppController::Start(HINSTANCE hInst, HWND hwnd, bool isStartupLaunch) {
    hInst_           = hInst;
    hwnd_            = hwnd;
    isStartupLaunch_ = isStartupLaunch;

    // ── Logger (singleton — auto-initialises on first access) ────────────────
    LOG_INFO("AppController::Start — Logger initialised.");

    // ── Config ───────────────────────────────────────────────────────────────
    config_ = Config::Load(Logger::Instance());
    if (config_.corruptOnLoad) {
        // Corrupt config — load already reset to defaults; balloon after tray
        // icon is created so it has a notification area to attach to.
    }

    // Sync launchAtStartup with registry reality (registry is ground truth)
    if (!config_.portableMode) {
        config_.launchAtStartup = StartupRegistry::IsEnabled();
    }

    // ── Localisation ─────────────────────────────────────────────────────────
    StringLoader::Init(hInst);

    // ── Balloon notify (needs hwnd before tray icon so it can attach) ────────
    balloonNotify_.Init(hwnd);

    // ── Tray icon ────────────────────────────────────────────────────────────
    if (!trayIcon_.Create(hwnd, hInst)) {
        return false;
    }
    trayIcon_.SetState(TrayState::Idle);

    // ── Tray menu ────────────────────────────────────────────────────────────
    trayMenu_.Init(hInst);

    // ── Receiver list ─────────────────────────────────────────────────────────
    receiverList_ = std::make_unique<ReceiverList>(hwnd);

    // ── WinSparkle ───────────────────────────────────────────────────────────
    sparkle_.Init(config_);
    sparkle_.SetMainHwnd(hwnd_);

    // ── VolumePopup ───────────────────────────────────────────────────────────
    volumePopup_.Create(hInst_, hwnd_, [this](float v) {
        config_.volume = v;
        config_.Save();
        if (raopSession_) raopSession_->SetVolume(v);
    });

    // Apply saved settings (no-op if not yet connected; reconnect re-applies)
    if (raopSession_ && isConnected_) {
        raopSession_->SetVolume(config_.volume);
    }

    // ── Startup auto-reconnect ───────────────────────────────────────────────
    if (!config_.lastDevice.empty()) {
        trayIcon_.SetState(TrayState::Connecting);
        SetTimer(hwnd_, TIMER_RECONNECT_WINDOW, 5000, nullptr);
        reconnectWindowActive_ = true;
    }

    return true;
}

void AppController::Shutdown() {
#ifdef _DEBUG
    DWORD startTick = GetTickCount();
#endif

    // 1. Stop mDNS (Thread 2) — joins with 150 ms cap (one select() cycle)
    if (mdnsDiscovery_) mdnsDiscovery_->Stop();

    // 2. If streaming: send TEARDOWN (Thread 5), wait ≤1500 ms
    if (raopSession_ && raopSession_->IsRunning()) {
        raopSession_->Stop();
        raopSession_.reset();
    }

    // 3. Stop Thread 4 (AlacEncoder)
    if (alacThread_) { alacThread_->Stop(); alacThread_.reset(); }

    // 4. Stop Thread 3 (WasapiCapture)
    if (wasapi_) { wasapi_->Stop(); wasapi_.reset(); }

    // 5. WinSparkle cleanup
    sparkle_.Cleanup();

    // 6. Remove tray icon
    trayIcon_.Delete();

#ifdef _DEBUG
    DWORD elapsed = GetTickCount() - startTick;
    // Constitution §A1: total shutdown ≤1.9 s
    assert(elapsed < 1900 && "Shutdown took too long");
#endif
}

void AppController::HandleCommand(UINT id) {
    switch (id) {
    case IDM_QUIT:
        DestroyWindow(hwnd_);
        break;

    case IDM_CHECK_UPDATES:
        sparkle_.CheckForUpdates();
        break;

    case IDM_OPEN_LOG_FOLDER:
        Logger::Instance().OpenLogFolder();
        break;

    case IDM_VOLUME:
        volumePopup_.Show(config_.volume);
        break;

    case IDM_LOW_LATENCY_TOGGLE:
        config_.lowLatency = !config_.lowLatency;
        config_.Save();
        if (isConnected_) {
            // Reconnect to apply the new buffer size
            auto receiver = connectedReceiver_;
            auto idx      = connectedReceiverIdx_;
            Disconnect();
            Connect(receiver, idx);
        }
        break;

    case IDM_LAUNCH_STARTUP_TOGGLE:
        if (!config_.portableMode) {
            if (StartupRegistry::IsEnabled()) {
                StartupRegistry::Disable();
                config_.launchAtStartup = false;
            } else {
                StartupRegistry::Enable();
                config_.launchAtStartup = true;
            }
            config_.Save();
        }
        break;

    case IDM_SHOW_MENU:
        ShowTrayMenu();
        break;

    default:
        if (id >= IDM_DEVICE_BASE && id < IDM_DEVICE_BASE + IDM_DEVICE_MAX_COUNT) {
            int idx = static_cast<int>(id - IDM_DEVICE_BASE);
            auto receivers = receiverList_ ? receiverList_->Snapshot()
                                           : std::vector<AirPlayReceiver>{};
            if (idx < static_cast<int>(receivers.size())) {
                const auto& r = receivers[idx];
                if (!r.isAirPlay1Compatible) return; // grayed item, ignore
                if (isConnected_ && connectedReceiverIdx_ == idx) {
                    Disconnect(); // toggle off
                } else {
                    Connect(r, idx);
                }
            }
        }
        break;
    }
}

void AppController::HandleTrayCallback(LPARAM lParam) {
    switch (LOWORD(lParam)) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowTrayMenu();
        break;

    case WM_LBUTTONDBLCLK:
        // Left double-click shows the menu (same as right-click for v1.0).
        ShowTrayMenu();
        break;

    default:
        break;
    }
}

void AppController::HandleReceiversUpdated() {
    if (!reconnectWindowActive_) return;

    auto receivers = receiverList_ ? receiverList_->Snapshot()
                                   : std::vector<AirPlayReceiver>{};
    for (int i = 0; i < static_cast<int>(receivers.size()); ++i) {
        const auto& r = receivers[i];
        std::wstring mac(r.macAddress.begin(), r.macAddress.end());
        if (mac == config_.lastDevice || r.instanceName == config_.lastDevice) {
            Connect(r, i);
            KillTimer(hwnd_, TIMER_RECONNECT_WINDOW);
            reconnectWindowActive_ = false;
            break;
        }
    }
}

void AppController::HandleRaopConnected(LPARAM /*lParam*/) {
    uint32_t ssrc = 0;
    BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&ssrc), 4,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    wasapi_ = std::make_unique<WasapiCapture>();
    wasapi_->Start(ring_, hwnd_);

    alacThread_ = std::make_unique<AlacEncoderThread>();

    SOCKET audioSock = raopSession_->AudioSocket();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(raopSession_->ServerAudioPort());
    inet_pton(AF_INET, connectedReceiver_.ipAddress.c_str(), &addr.sin_addr);

    alacThread_->Init(ring_, cipher_.get(), retransmit_.get(), ssrc,
                      audioSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    alacThread_->Start();

    trayIcon_.SetState(TrayState::Streaming);
    wasStreaming_ = true;
    config_.lastDevice = connectedReceiver_.macAddress.empty()
                         ? connectedReceiver_.instanceName
                         : std::wstring(connectedReceiver_.macAddress.begin(),
                                        connectedReceiver_.macAddress.end());
    config_.Save();
    balloonNotify_.ShowInfo(IDS_BALLOON_CONNECTED, IDS_BALLOON_CONNECTED);
}

void AppController::HandleRaopFailed(LPARAM /*lParam*/) {
    bool unexpected = wasStreaming_;
    wasStreaming_ = false;

    if (unexpected && retryCount_ == 0) {
        // First failure after streaming — warn user before entering retry loop
        balloonNotify_.ShowWarning(IDS_BALLOON_DISCONNECTED, IDS_BALLOON_DISCONNECTED);
    }

    if (retryCount_ < 3) {
        retryCount_++;
        DWORD backoff = (1 << (retryCount_ - 1)) * 1000;
        trayIcon_.SetState(TrayState::Connecting);
        SetTimer(hwnd_, TIMER_RAOP_RETRY, backoff, nullptr);
    } else {
        retryCount_ = 0;
        Disconnect();
        trayIcon_.SetState(TrayState::Error);
        balloonNotify_.ShowError(IDS_BALLOON_CONNECTION_FAILED,
                                 IDS_BALLOON_CONNECTION_FAILED);
    }
}

void AppController::HandleBonjourMissing() {
    balloonNotify_.ShowWarning(IDS_BALLOON_BONJOUR_MISSING,
                               IDS_BALLOON_BONJOUR_MISSING);
}

void AppController::HandleDefaultDeviceChanged() {
    if (!wasapi_ || !wasapi_->IsRunning()) return;

    // Stop Thread 3, re-init on new device, restart.
    // Target ≤1 s total gap (SC-007).
    // Thread 4 and Thread 5 continue uninterrupted — ring buffer absorbs the gap.
    wasapi_->Stop();
    wasapi_ = std::make_unique<WasapiCapture>();
    wasapi_->Start(ring_, hwnd_);
}

void AppController::HandleUpdateRejected() {
    balloonNotify_.ShowWarning(IDS_BALLOON_UPDATE_REJECTED,
                               IDS_BALLOON_UPDATE_REJECTED);
}

void AppController::ShowTrayMenu() {
    // Notify Windows that this window is the foreground window so TrackPopupMenu
    // receives mouse messages and dismisses correctly (MSDN-recommended pattern).
    SetForegroundWindow(hwnd_);

    UINT cmd = trayMenu_.Show(hwnd_, config_, sparkle_.IsAvailable(),
                              receiverList_ ? receiverList_->Snapshot()
                                           : std::vector<AirPlayReceiver>{},
                              connectedReceiverIdx_);

    // Required: post a dummy message so the menu dismisses even if the user
    // clicks elsewhere (MSDN TrackPopupMenu documented requirement).
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    if (cmd != 0) {
        HandleCommand(cmd);
    }
}

void AppController::Connect(const AirPlayReceiver& receiver, int idx) {
    if (isConnected_) Disconnect();

    connectedReceiver_    = receiver;
    connectedReceiverIdx_ = idx;

    uint8_t aesKey[16], aesIv[16];
    BCryptGenRandom(nullptr, aesKey, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    BCryptGenRandom(nullptr, aesIv,  16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    cipher_     = std::make_unique<AesCbcCipher>(aesKey, aesIv);
    retransmit_ = std::make_unique<RetransmitBuffer>();

    if (config_.lowLatency) {
        ringLL_ = std::make_unique<SpscRingBuffer<AudioFrame,32>>();
        ring_   = ringLL_.get();
    } else {
        ringStd_ = std::make_unique<SpscRingBuffer<AudioFrame,128>>();
        ring_    = ringStd_.get();
    }

    RaopSession::Config rc;
    rc.receiverIp   = receiver.ipAddress;
    rc.receiverPort = receiver.port;
    rc.clientIp     = "0.0.0.0"; // RaopSession resolves actual local IP
    memcpy(rc.aesKey, aesKey, 16);
    memcpy(rc.aesIv,  aesIv,  16);
    rc.volume     = config_.volume;
    rc.retransmit = retransmit_.get();
    rc.hwndMain   = hwnd_;

    raopSession_ = std::make_unique<RaopSession>();
    raopSession_->Start(rc);

    // WasapiCapture and AlacEncoderThread start in HandleRaopConnected
    // once RaopSession posts WM_RAOP_CONNECTED.
    trayIcon_.SetState(TrayState::Connecting);
    isConnected_ = true;
}

void AppController::Disconnect() {
    if (!isConnected_) return;
    if (raopSession_)  { raopSession_->Stop();  raopSession_.reset(); }
    if (alacThread_)   { alacThread_->Stop();   alacThread_.reset(); }
    if (wasapi_)       { wasapi_->Stop();        wasapi_.reset(); }
    cipher_.reset();
    retransmit_.reset();
    ringStd_.reset();
    ringLL_.reset();
    connectedReceiverIdx_ = -1;
    isConnected_          = false;
    wasStreaming_         = false;
    retryCount_           = 0;
    trayIcon_.SetState(TrayState::Idle);
}

void AppController::HandleTimer(WPARAM wParam) {
    if (wParam == TIMER_RECONNECT_WINDOW) {
        KillTimer(hwnd_, TIMER_RECONNECT_WINDOW);
        reconnectWindowActive_ = false;
        // Silent expiry: go idle if no connection was established (FR-008)
        if (!isConnected_) {
            trayIcon_.SetState(TrayState::Idle);
        }
        return;
    }

    if (wParam == TIMER_RAOP_RETRY) {
        KillTimer(hwnd_, TIMER_RAOP_RETRY);
        if (connectedReceiverIdx_ < 0) return;

        // Stop any leftover session objects from the failed attempt
        if (raopSession_)  { raopSession_->Stop();  raopSession_.reset(); }
        if (alacThread_)   { alacThread_->Stop();   alacThread_.reset(); }
        if (wasapi_)       { wasapi_->Stop();        wasapi_.reset(); }

        // Rebuild crypto and ring buffer for the fresh attempt
        uint8_t aesKey[16], aesIv[16];
        BCryptGenRandom(nullptr, aesKey, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        BCryptGenRandom(nullptr, aesIv,  16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        cipher_     = std::make_unique<AesCbcCipher>(aesKey, aesIv);
        retransmit_ = std::make_unique<RetransmitBuffer>();

        ringStd_.reset();
        ringLL_.reset();
        if (config_.lowLatency) {
            ringLL_ = std::make_unique<SpscRingBuffer<AudioFrame,32>>();
            ring_   = ringLL_.get();
        } else {
            ringStd_ = std::make_unique<SpscRingBuffer<AudioFrame,128>>();
            ring_    = ringStd_.get();
        }

        RaopSession::Config rc;
        rc.receiverIp   = connectedReceiver_.ipAddress;
        rc.receiverPort = connectedReceiver_.port;
        rc.clientIp     = "0.0.0.0";
        memcpy(rc.aesKey, aesKey, 16);
        memcpy(rc.aesIv,  aesIv,  16);
        rc.volume     = config_.volume;
        rc.retransmit = retransmit_.get();
        rc.hwndMain   = hwnd_;

        raopSession_ = std::make_unique<RaopSession>();
        raopSession_->Start(rc);
    }
}
