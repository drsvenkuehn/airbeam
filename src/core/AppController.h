#pragma once
#include <windows.h>
#include <memory>
#include <vector>
#include <cstdint>
#include <thread>

#include "core/Logger.h"
#include "core/Config.h"
#include "core/ConnectionController.h"
#include "localization/StringLoader.h"
#include "ui/TrayIcon.h"
#include "ui/TrayMenu.h"
#include "ui/BalloonNotify.h"
#include "update/SparkleIntegration.h"
#include "discovery/AirPlayReceiver.h"
#include "discovery/ReceiverList.h"
#include "discovery/BonjourLoader.h"
#include "discovery/MdnsDiscovery.h"
#include "airplay2/CredentialStore.h"

// Top-level orchestrator.  Owns all subsystems and dispatches Win32 messages.
// Lifetime: created on the stack in WinMain before the message loop.
// Pipeline lifecycle is delegated to ConnectionController.
class AppController {
public:
    AppController()  = default;
    ~AppController();

    AppController(const AppController&)            = delete;
    AppController& operator=(const AppController&) = delete;

    // Initialises all subsystems.  hwnd is the hidden message window that
    // receives WM_TRAY_CALLBACK, WM_TRAY_POPUP_MENU, WM_COMMAND, etc.
    // Returns false if a fatal initialisation error occurred.
    bool Start(HINSTANCE hInst, HWND hwnd, bool isStartupLaunch);

    // Tears down all subsystems.  Called from the WM_DESTROY handler before
    // PostQuitMessage(0) so the tray icon is removed cleanly.
    void Shutdown();

    // ── Message handlers ────────────────────────────────────────────────────

    // Dispatches IDM_* menu command IDs (from WM_COMMAND or WM_TRAY_POPUP_MENU).
    void HandleCommand(UINT id);

    // Routes Shell_NotifyIcon callback messages.
    void HandleTrayCallback(LPARAM lParam);

    // Re-builds the speaker section of the tray menu.
    // Called when WM_RECEIVERS_UPDATED arrives.
    void HandleReceiversUpdated();

    // Called when WM_BONJOUR_MISSING arrives.
    void HandleBonjourMissing();

    // Called when WM_UPDATE_REJECTED arrives (WinSparkle EdDSA failure).
    void HandleUpdateRejected();

    // Called when WM_STREAM_STARTED arrives — update tray menu (show Disconnect).
    void HandleStreamStarted();

    // Called when WM_STREAM_STOPPED arrives — update tray menu (hide Disconnect).
    void HandleStreamStopped();

    // ── AirPlay 2 message handlers (Feature 010) ─────────────────────────────

    // WM_AP2_PAIRING_REQUIRED: start HapPairing worker, show "Pairing…" balloon.
    // LPARAM = heap-allocated AirPlayReceiver* (delete after use).
    void HandleAp2PairingRequired(LPARAM lParam);

    // WM_AP2_PAIRING_STALE: stale credential — delete + show "re-pair" balloon.
    // LPARAM = heap-allocated AirPlayReceiver* (delete after use).
    void HandleAp2PairingStale(LPARAM lParam);

    // WM_AP2_CONNECTED: update tray icon (blue streaming), enable volume slider.
    // LPARAM = heap-allocated AirPlayReceiver* (delete after use).
    void HandleAp2Connected(LPARAM lParam);

    // WM_AP2_FAILED: handle session failure with optional retry logic.
    // WPARAM = AP2_ERROR_* code. LPARAM = heap-allocated AirPlayReceiver*.
    void HandleAp2Failed(WPARAM wParam, LPARAM lParam);

    // WM_TIMER dispatch — forwards CC timer IDs to cc_, handles others internally.
    void HandleTimer(WPARAM wParam);

    HWND GetWindow() const { return hwnd_; }

    // Access ConnectionController for WndProc message forwarding
    ConnectionController* GetCC() { return cc_.get(); }

private:
    void ShowTrayMenu();

    HWND      hwnd_            = nullptr;
    HINSTANCE hInst_           = nullptr;
    bool      isStartupLaunch_ = false;

    // Logger is a singleton (Logger::Instance()); no member needed.
    Config             config_;
    TrayIcon           trayIcon_;
    TrayMenu           trayMenu_;
    BalloonNotify      balloonNotify_;
    SparkleIntegration sparkle_;

    std::unique_ptr<ReceiverList>         receiverList_;
    std::unique_ptr<BonjourLoader>        bonjourLoader_;
    std::unique_ptr<MdnsDiscovery>        mdnsDiscovery_;
    std::unique_ptr<ConnectionController> cc_;

    // UI state
    std::vector<AirPlayReceiver> sortedReceivers_;
    int  connectingReceiverIdx_  = -1;  // index shown as "Connecting…" in menu
    bool bonjourMissing_         = false;
    bool lastBalloonWasBonjour_  = false;
};
