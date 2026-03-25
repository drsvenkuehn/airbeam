#pragma once
#include <windows.h>

// Wraps Shell_NotifyIcon with four icon states and an animated connecting state
// (150 ms timer cycling through 8 frames).  All tooltip text is loaded via
// StringLoader at each state transition.
enum class TrayState { Idle, Connecting, Streaming, Error, BonjourMissing };

class TrayIcon {
public:
    TrayIcon()  = default;
    ~TrayIcon() = default;

    TrayIcon(const TrayIcon&)            = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    // Registers the Shell_NotifyIcon.  hwnd receives WM_TRAY_CALLBACK messages.
    bool Create(HWND hwnd, HINSTANCE hInst);

    // Calls Shell_NotifyIcon(NIM_DELETE) and stops the connecting animation timer.
    void Delete();

    // Changes the icon and tooltip.  deviceName is used for the Connecting and
    // Streaming tooltip format strings; may be nullptr for Idle/Error.
    void SetState(TrayState state, const wchar_t* deviceName = nullptr);

    TrayState GetState() const { return state_; }

    // Called by the window proc for WM_TIMER with the connecting animation timer ID.
    void OnAnimationTick();

    static constexpr UINT NOTIFY_UID       = 1;
    static constexpr UINT ANIMATION_TIMER  = 100;

private:
    HWND      hwnd_  = nullptr;
    HINSTANCE hInst_ = nullptr;
    TrayState state_ = TrayState::Idle;
    int       frame_ = 0; // 0-7 for connecting animation
};
