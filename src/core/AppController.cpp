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

    menuSlider_.Init([this](float v) {
        if (cc_) cc_->SetVolume(v);
        config_.volume = v;
        config_.Save();
    });
    menuSlider_.SetVolume(config_.volume);

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

void AppController::HandleTimer(WPARAM wParam) {
    if (cc_) cc_->OnTimer(static_cast<UINT>(wParam));
    if (static_cast<UINT>(wParam) == TrayIcon::ANIMATION_TIMER)
        trayIcon_.OnAnimationTick();
}

bool AppController::HandleMeasureItem(MEASUREITEMSTRUCT* mis) {
    return menuSlider_.HandleMeasureItem(mis);
}

bool AppController::HandleDrawItem(DRAWITEMSTRUCT* dis) {
    return menuSlider_.HandleDrawItem(dis);
}

void AppController::ShowTrayMenu() {
    SetForegroundWindow(hwnd_);

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

    UINT cmd = trayMenu_.Show(hwnd_, config_, sparkle_.IsAvailable(),
                              bonjourMissing_,
                              sortedReceivers_,
                              connectedIdx,
                              connectingIdx,
                              &menuSlider_,
                              config_.volume);

    PostMessageW(hwnd_, WM_NULL, 0, 0);
    if (cmd != 0) HandleCommand(cmd);
}
