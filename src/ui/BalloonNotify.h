#pragma once
#include <windows.h>

// Wraps Shell_NotifyIcon NIF_INFO balloon notifications.
// All text is loaded via StringLoader using IDS_* resource IDs.
class BalloonNotify {
public:
    BalloonNotify()  = default;
    ~BalloonNotify() = default;

    BalloonNotify(const BalloonNotify&)            = delete;
    BalloonNotify& operator=(const BalloonNotify&) = delete;

    // Attaches to the tray icon's HWND.  Must be called before any Show*().
    void Init(HWND hwnd);

    // arg is an optional printf-style substitution for the body format string
    // (e.g. the device name).  May be nullptr.
    void ShowInfo   (UINT titleId, UINT bodyId, const wchar_t* arg = nullptr);
    void ShowWarning(UINT titleId, UINT bodyId, const wchar_t* arg = nullptr);
    void ShowError  (UINT titleId, UINT bodyId, const wchar_t* arg = nullptr);

private:
    HWND hwnd_ = nullptr;

    void Show(UINT titleId, UINT bodyId, DWORD niifFlags, const wchar_t* arg);
};
