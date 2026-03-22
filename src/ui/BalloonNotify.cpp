#include <windows.h>
#include <shellapi.h>

#include "ui/BalloonNotify.h"
#include "localization/StringLoader.h"

// ---------------------------------------------------------------------------
void BalloonNotify::Init(HWND hwnd)
{
    hwnd_ = hwnd;
}

// ---------------------------------------------------------------------------
void BalloonNotify::ShowInfo(UINT titleId, UINT bodyId, const wchar_t* arg)
{
    Show(titleId, bodyId, NIIF_INFO, arg);
}

void BalloonNotify::ShowWarning(UINT titleId, UINT bodyId, const wchar_t* arg)
{
    Show(titleId, bodyId, NIIF_WARNING, arg);
}

void BalloonNotify::ShowError(UINT titleId, UINT bodyId, const wchar_t* arg)
{
    Show(titleId, bodyId, NIIF_ERROR, arg);
}

// ---------------------------------------------------------------------------
void BalloonNotify::Show(UINT titleId, UINT bodyId, DWORD niifFlags, const wchar_t* arg)
{
    std::wstring title = StringLoader::Load(titleId);
    std::wstring body  = StringLoader::Load(bodyId);

    // Substitute the optional format argument (e.g. device name) into body
    // strings that contain a %s placeholder such as "Connected to %s".
    if (arg && !body.empty()) {
        wchar_t formatted[512] = {};
        if (_snwprintf_s(formatted, _countof(formatted), _TRUNCATE,
                         body.c_str(), arg) >= 0) {
            body = formatted;
        }
    }

    NOTIFYICONDATAW nid = {};
    nid.cbSize      = sizeof(nid);
    nid.hWnd        = hwnd_;
    nid.uID         = 1;        // must match TrayIcon::NOTIFY_UID
    nid.uFlags      = NIF_INFO;
    nid.dwInfoFlags = niifFlags;

    wcsncpy_s(nid.szInfoTitle, _countof(nid.szInfoTitle),
              title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo,      _countof(nid.szInfo),
              body.c_str(),  _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
