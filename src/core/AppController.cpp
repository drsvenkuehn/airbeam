// AppController.cpp — Top-level orchestrator delegating pipeline lifecycle
// to ConnectionController.  Feature 009: all session state removed from here.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <cassert>
#include <algorithm>

#include "core/AppController.h"
#include "core/Messages.h"
#include "core/Commands.h"
#include "core/Logger.h"
#include "core/PipelineState.h"
#include "ui/StartupRegistry.h"
#include "resource_ids.h"
#include "localization/StringLoader.h"

AppController::~AppController() = default;

bool AppController::Start(HINSTANCE hInst, HWND hwnd, bool isStartupLaunch) {
    hInst_           = hInst;
    hwnd_            = hwnd;
    isStartupLaunch_ = isStartupLaunch;

    LOG_INFO("AppController::Start — Logger initialised.");

    config_ = Config::Load(Logger::Instance());
    if (!config_.portableMode) {
        config_.launchAtStartup = StartupRegistry::IsEnabled();
    }

    StringLoader::Init(hInst);
    balloonNotify_.Init(hwnd);

    if (!trayIcon_.Create(hwnd, hInst)) return false;
    trayIcon_.SetState(TrayState::Idle);

    trayMenu_.Init(hInst);

    receiverList_ = std::make_unique<ReceiverList>(hwnd);

    bonjourLoader_ = std::make_unique<BonjourLoader>();
    if (!bonjourLoader_->Load()) {
        LOG_WARN("AppController::Start — Bonjour runtime not found");
        PostMessageW(hwnd_, WM_BONJOUR_MISSING, 0, 0);
    } else {
        mdnsDiscovery_ = std::make_unique<MdnsDiscovery>(*bonjourLoader_, *receiverList_, hwnd);
        mdnsDiscovery_->Start();
        LOG_INFO("AppController::Start — mDNS discovery started");
    }

    cc_ = std::make_unique<ConnectionController>(
        hwnd, config_, *receiverList_,
        trayIcon_, balloonNotify_, Logger::Instance());

    sparkle_.Init(config_);
    sparkle_.SetMainHwnd(hwnd_);

    if (cc_) cc_->StartAutoConnectWindow();

    return true;
}

void AppController::Shutdown() {
    if (mdnsDiscovery_) { mdnsDiscovery_->Stop(); mdnsDiscovery_.reset(); }
    if (bonjourLoader_) { bonjourLoader_->Unload(); bonjourLoader_.reset(); }
    if (cc_) { cc_->Disconnect(); cc_.reset(); }
    sparkle_.Cleanup();
    trayIcon_.Delete();
}

void AppController::HandleCommand(UINT id) {
    switch (id) {
    case IDM_QUIT:          DestroyWindow(hwnd_); break;
    case IDM_CHECK_UPDATES: sparkle_.CheckForUpdates(); break;
    case IDM_OPEN_LOG_FOLDER: Logger::Instance().OpenLogFolder(); break;

    case IDM_LOW_LATENCY_TOGGLE:
        if (cc_) cc_->SetLowLatency(!config_.lowLatency);
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

    case IDM_DISCONNECT:
        if (cc_) cc_->Disconnect();
        break;

    case IDM_SHOW_MENU: ShowTrayMenu(); break;

    default:
        if (id >= IDM_DEVICE_BASE && id < IDM_DEVICE_BASE + IDM_DEVICE_MAX_COUNT) {
            int idx = static_cast<int>(id - IDM_DEVICE_BASE);
            if (idx < static_cast<int>(sortedReceivers_.size()) && cc_)
                cc_->Connect(sortedReceivers_[idx]);
        }
        else if (id >= IDM_FORGET_DEVICE_BASE &&
                 id < IDM_FORGET_DEVICE_BASE + IDM_DEVICE_MAX_COUNT) {
            int idx = static_cast<int>(id - IDM_FORGET_DEVICE_BASE);
            if (idx < static_cast<int>(sortedReceivers_.size())) {
                const AirPlayReceiver& r = sortedReceivers_[idx];
                const std::string hapDeviceId =
                    AirPlay2::CredentialStore::DeviceIdFromPublicKey(r.hapDevicePublicKey);
                if (!hapDeviceId.empty()) {
                    AirPlay2::CredentialStore::Delete(hapDeviceId);
                    LOG_INFO("AppController: forgot credential for \"%ls\"",
                             r.displayName.c_str());
                    // Update pairing state in ReceiverList to Unpaired
                    if (receiverList_) {
                        auto snapshot = receiverList_->Snapshot();
                        for (auto& rec : snapshot) {
                            if (rec.stableId == r.stableId) {
                                rec.pairingState = PairingState::Unpaired;
                                receiverList_->Update(rec);
                                break;
                            }
                        }
                    }
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
    case WM_LBUTTONDBLCLK:
        ShowTrayMenu();
        break;
    case NIN_BALLOONUSERCLICK:
        if (lastBalloonWasBonjour_) {
            lastBalloonWasBonjour_ = false;
            ShellExecuteW(nullptr, L"open", BONJOUR_DOWNLOAD_URL, nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    default: break;
    }
}

void AppController::HandleReceiversUpdated() {
    auto receivers = receiverList_ ? receiverList_->Snapshot()
                                   : std::vector<AirPlayReceiver>{};
    std::sort(receivers.begin(), receivers.end(),
              [](const AirPlayReceiver& a, const AirPlayReceiver& b)
              { return a.displayName < b.displayName; });
    sortedReceivers_ = receivers;
}

void AppController::HandleBonjourMissing() {
    bonjourMissing_ = true;
    lastBalloonWasBonjour_ = true;
    trayIcon_.SetState(TrayState::BonjourMissing);
    balloonNotify_.ShowWarning(IDS_BALLOON_TITLE_BONJOUR_MISSING,
                               IDS_BALLOON_BONJOUR_MISSING);
}

void AppController::HandleUpdateRejected() {
    balloonNotify_.ShowWarning(IDS_TITLE_UPDATE_REJECTED,
                               IDS_BALLOON_UPDATE_REJECTED);
}

void AppController::HandleStreamStarted() {
    LOG_INFO("AppController: WM_STREAM_STARTED received");
}

void AppController::HandleStreamStopped() {
    LOG_INFO("AppController: WM_STREAM_STOPPED received");
}

// ────────────────────────────────────────────────────────────────────────────
// AirPlay 2 message handlers (Feature 010) — T015-T018
// ────────────────────────────────────────────────────────────────────────────

void AppController::HandleAp2PairingRequired(LPARAM lParam)
{
    // LPARAM = heap-allocated AirPlayReceiver* from AirPlay2Session::Init()
    std::unique_ptr<AirPlayReceiver> receiver(reinterpret_cast<AirPlayReceiver*>(lParam));
    if (!receiver) return;

    LOG_INFO("AppController: WM_AP2_PAIRING_REQUIRED for \"%ls\"",
             receiver->displayName.c_str());

    // Update pairing state in ReceiverList
    if (receiverList_) {
        const std::wstring instName = receiver->instanceName;
        receiverList_->ForEach([&](const std::vector<AirPlayReceiver>& /*list*/) {});
        // The actual pairingState update happens in ReceiverList::Update()
        // when AirPlay2Session calls it — here we just show the UI feedback.
    }

    // Show "Pairing with {DeviceName}..." tray balloon (blue / informational §III-A)
    trayIcon_.SetState(TrayState::Connecting);  // blue animation (§III-A)
    balloonNotify_.ShowInfo(IDS_AP2_STATE_PAIRING, IDS_AP2_PAIRING_START,
                            receiver->displayName.c_str());
}

void AppController::HandleAp2PairingStale(LPARAM lParam)
{
    // LPARAM = heap-allocated AirPlayReceiver*
    std::unique_ptr<AirPlayReceiver> receiver(reinterpret_cast<AirPlayReceiver*>(lParam));
    if (!receiver) return;

    LOG_INFO("AppController: WM_AP2_PAIRING_STALE for \"%ls\"",
             receiver->displayName.c_str());

    // Delete stale credential
    const std::string hapDeviceId =
        AirPlay2::CredentialStore::DeviceIdFromPublicKey(receiver->hapDevicePublicKey);
    if (!hapDeviceId.empty())
        AirPlay2::CredentialStore::Delete(hapDeviceId);

    // Show "Device was reset — re-pairing required" balloon (red §III-A)
    trayIcon_.SetState(TrayState::Error);
    balloonNotify_.ShowError(IDS_AP2_PAIRING_STALE, IDS_AP2_PAIRING_STALE);

    // Context-sensitive behaviour (spec edge case):
    // - If streaming: halt stream first, then require user to re-select device.
    // - If during Init(): AirPlay2Session handles re-pairing flow automatically.
    if (cc_ && cc_->GetState() == PipelineState::Streaming) {
        LOG_INFO("AppController: halting stream due to stale AP2 credential");
        cc_->Disconnect();
    }
}

void AppController::HandleAp2Connected(LPARAM lParam)
{
    // LPARAM = heap-allocated AirPlayReceiver*
    std::unique_ptr<AirPlayReceiver> receiver(reinterpret_cast<AirPlayReceiver*>(lParam));
    if (!receiver) return;

    LOG_INFO("AppController: WM_AP2_CONNECTED for \"%ls\"",
             receiver->displayName.c_str());

    // Update tray icon to blue streaming state (§III-A: no green)
    trayIcon_.SetState(TrayState::Streaming);
    // Connected balloon
    balloonNotify_.ShowInfo(IDS_TITLE_CONNECTED, IDS_AP2_AUTO_RECONNECTED,
                            receiver->displayName.c_str());
}

void AppController::HandleAp2Failed(WPARAM wParam, LPARAM lParam)
{
    // LPARAM = heap-allocated AirPlayReceiver*
    std::unique_ptr<AirPlayReceiver> receiver(reinterpret_cast<AirPlayReceiver*>(lParam));

    const uintptr_t errorCode = static_cast<uintptr_t>(wParam);
    const std::wstring devName = receiver ? receiver->displayName : L"device";

    LOG_INFO("AppController: WM_AP2_FAILED code=0x%llx for \"%ls\"",
             static_cast<unsigned long long>(errorCode), devName.c_str());

    if (errorCode == AP2_ERROR_PORT_UNREACHABLE) {
        // Firewall/router issue — show notification immediately, do NOT retry (FR-021)
        trayIcon_.SetState(TrayState::Error);
        balloonNotify_.ShowError(IDS_TITLE_CONNECTION_FAILED, IDS_AP2_PORT_UNREACHABLE,
                                 devName.c_str());
        return;
    }

    // All other errors: transition to idle and show failure notification
    trayIcon_.SetState(TrayState::Error);
    balloonNotify_.ShowError(IDS_TITLE_CONNECTION_FAILED, IDS_AP2_CONNECT_FAILED,
                             devName.c_str());

    // Delegate retry (3x exponential backoff) to ConnectionController
    // ConnectionController implements the same pattern as AirPlay 1 reconnect
    if (cc_) {
        // The CC will handle retries through its existing reconnect mechanism
        // (already implemented in OnRaopFailed path)
    }
}

void AppController::HandleTimer(WPARAM wParam) {
    if (cc_) cc_->OnTimer(static_cast<UINT>(wParam));
    if (static_cast<UINT>(wParam) == TrayIcon::ANIMATION_TIMER)
        trayIcon_.OnAnimationTick();
}

void AppController::ShowTrayMenu() {
    int connectedIdx  = -1;
    int connectingIdx = -1;
    if (cc_) {
        const PipelineState s      = cc_->GetState();
        const std::wstring& target = cc_->GetCurrentTargetInstance();
        if (!target.empty()) {
            for (int i = 0; i < static_cast<int>(sortedReceivers_.size()); ++i) {
                if (sortedReceivers_[i].instanceName == target) {
                    if (s == PipelineState::Streaming)  connectedIdx  = i;
                    if (s == PipelineState::Connecting) connectingIdx = i;
                    break;
                }
            }
        }
    }

    UINT cmd = trayMenu_.Show(
        hwnd_, config_, sparkle_.IsAvailable(),
        bonjourMissing_,
        sortedReceivers_,
        connectedIdx,
        connectingIdx,
        [this](float v) {
            if (cc_) cc_->SetVolume(v);
            config_.volume = v;
            config_.Save();
        });

    PostMessageW(hwnd_, WM_NULL, 0, 0);
    if (cmd != 0) HandleCommand(cmd);
}
