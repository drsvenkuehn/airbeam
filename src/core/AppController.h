#pragma once
#include <windows.h>
#include <memory>
#include <cstdint>

#include "core/Logger.h"
#include "core/Config.h"
#include "localization/StringLoader.h"
#include "ui/TrayIcon.h"
#include "ui/TrayMenu.h"
#include "ui/BalloonNotify.h"
#include "ui/VolumePopup.h"
#include "update/SparkleIntegration.h"
#include "audio/SpscRingBuffer.h"
#include "discovery/AirPlayReceiver.h"
#include "discovery/ReceiverList.h"
#include "protocol/AesCbcCipher.h"
#include "protocol/RetransmitBuffer.h"

#include "audio/WasapiCapture.h"
#include "audio/AlacEncoderThread.h"
#include "protocol/RaopSession.h"
#include "discovery/MdnsDiscovery.h"

// Top-level orchestrator.  Owns all subsystems and dispatches Win32 messages.
// Lifetime: created on the stack in WinMain before the message loop.
class AppController {
public:
    AppController()  = default;
    ~AppController(); // defined in .cpp where session-object types are complete

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
    // lParam carries the mouse/notification event (e.g. WM_RBUTTONUP).
    void HandleTrayCallback(LPARAM lParam);

    // Re-builds the speaker section of the tray menu.
    // Called when WM_RECEIVERS_UPDATED arrives.
    void HandleReceiversUpdated();

    // Called when WM_RAOP_CONNECTED arrives (lParam = receiver index).
    void HandleRaopConnected(LPARAM lParam);

    // Called when WM_RAOP_FAILED arrives.
    void HandleRaopFailed(LPARAM lParam);

    // Called when WM_TIMER arrives.
    void HandleTimer(WPARAM wParam);

    // Called when WM_BONJOUR_MISSING arrives.
    void HandleBonjourMissing();

    // Called when WM_DEFAULT_DEVICE_CHANGED arrives (default audio render
    // device was switched).  Restarts WasapiCapture on the new device; ring
    // buffer absorbs the gap so Threads 4 and 5 continue uninterrupted.
    void HandleDefaultDeviceChanged();

    // Called when WM_UPDATE_REJECTED arrives (WinSparkle EdDSA failure).
    void HandleUpdateRejected();

    HWND GetWindow() const { return hwnd_; }

private:
    // Shows the tray popup menu at the current cursor position and dispatches
    // the returned command (if any).
    void ShowTrayMenu();

    void Connect(const AirPlayReceiver& receiver, int idx);
    void Disconnect();

    HWND      hwnd_            = nullptr;
    HINSTANCE hInst_           = nullptr;
    bool      isStartupLaunch_ = false;

    // Timer IDs
    static constexpr UINT TIMER_RECONNECT_WINDOW = 1;
    static constexpr UINT TIMER_RAOP_RETRY       = 2;

    // Logger is a singleton (Logger::Instance()); no member needed.
    Config             config_;
    TrayIcon           trayIcon_;
    TrayMenu           trayMenu_;
    BalloonNotify      balloonNotify_;
    VolumePopup        volumePopup_;
    SparkleIntegration sparkle_;

    std::unique_ptr<ReceiverList>  receiverList_;
    std::unique_ptr<MdnsDiscovery> mdnsDiscovery_;

    // Session objects (owned by AppController)
    std::unique_ptr<SpscRingBuffer<AudioFrame,128>> ringStd_;
    std::unique_ptr<SpscRingBuffer<AudioFrame,32>>  ringLL_;
    SpscRingBufferPtr                               ring_{static_cast<SpscRingBuffer<AudioFrame,128>*>(nullptr)};
    std::unique_ptr<RetransmitBuffer>               retransmit_;
    std::unique_ptr<AesCbcCipher>                   cipher_;
    std::unique_ptr<WasapiCapture>                  wasapi_;
    std::unique_ptr<AlacEncoderThread>              alacThread_;
    std::unique_ptr<RaopSession>                    raopSession_;
    AirPlayReceiver                                 connectedReceiver_;
    int                                             connectedReceiverIdx_ = -1;
    bool                                            isConnected_          = false;

    // Auto-reconnect / retry state
    bool reconnectWindowActive_ = false;
    int  retryCount_             = 0;
    bool wasStreaming_            = false;
};
