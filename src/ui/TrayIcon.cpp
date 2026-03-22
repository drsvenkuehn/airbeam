#include <windows.h>
#include <shellapi.h>

#include "ui/TrayIcon.h"
#include "localization/StringLoader.h"
#include "core/Messages.h"   // WM_TRAY_CALLBACK
#include "resource_ids.h"    // IDS_TOOLTIP_*, IDI_TRAY_*

static constexpr UINT  ANIM_INTERVAL_MS = 150;
static constexpr int   ANIM_FRAME_COUNT = 8;

// ---------------------------------------------------------------------------
// Loads the branded application icon for a given tray state.
// Falls back to system stock icons if the resource is not embedded
// (e.g. running tests outside the full installer).
// ---------------------------------------------------------------------------
static HICON LoadTrayIcon(HINSTANCE hInst, TrayState state)
{
    UINT resId = IDI_TRAY_IDLE;
    switch (state) {
        case TrayState::Streaming:  resId = IDI_TRAY_STREAMING; break;
        case TrayState::Error:      resId = IDI_TRAY_ERROR;     break;
        case TrayState::Connecting: resId = IDI_TRAY_CONN_001;  break;
        default:                    resId = IDI_TRAY_IDLE;      break;
    }
    HICON hIcon = static_cast<HICON>(
        LoadImageW(hInst, MAKEINTRESOURCEW(resId), IMAGE_ICON, 16, 16, LR_SHARED));
    if (!hIcon) {
        // Fallback: system stock icons when resources are unavailable
        switch (state) {
            case TrayState::Streaming: return LoadIconW(nullptr, IDI_INFORMATION);
            case TrayState::Error:     return LoadIconW(nullptr, IDI_ERROR);
            default:                   return LoadIconW(nullptr, IDI_APPLICATION);
        }
    }
    return hIcon;
}

static HICON LoadAnimFrame(HINSTANCE hInst, int frame)
{
    static constexpr UINT kAnimIds[ANIM_FRAME_COUNT] = {
        IDI_TRAY_CONN_001, IDI_TRAY_CONN_002, IDI_TRAY_CONN_003, IDI_TRAY_CONN_004,
        IDI_TRAY_CONN_005, IDI_TRAY_CONN_006, IDI_TRAY_CONN_007, IDI_TRAY_CONN_008
    };
    HICON hIcon = static_cast<HICON>(
        LoadImageW(hInst, MAKEINTRESOURCEW(kAnimIds[frame & 7]),
                   IMAGE_ICON, 16, 16, LR_SHARED));
    return hIcon ? hIcon : LoadIconW(nullptr, IDI_APPLICATION);
}

// ---------------------------------------------------------------------------
bool TrayIcon::Create(HWND hwnd, HINSTANCE hInst)
{
    hwnd_  = hwnd;
    hInst_ = hInst;
    state_ = TrayState::Idle;
    frame_ = 0;

    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd_;
    nid.uID              = NOTIFY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_CALLBACK;
    nid.hIcon            = LoadTrayIcon(hInst_, TrayState::Idle);

    std::wstring tip = StringLoader::Load(IDS_TOOLTIP_IDLE);
    wcsncpy_s(nid.szTip, _countof(nid.szTip), tip.c_str(), _TRUNCATE);

    return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

// ---------------------------------------------------------------------------
void TrayIcon::Delete()
{
    KillTimer(hwnd_, ANIMATION_TIMER);

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = NOTIFY_UID;
    Shell_NotifyIconW(NIM_DELETE, &nid);

    hwnd_  = nullptr;
    hInst_ = nullptr;
    frame_ = 0;
}

// ---------------------------------------------------------------------------
void TrayIcon::SetState(TrayState state, const wchar_t* deviceName)
{
    const bool wasConnecting = (state_ == TrayState::Connecting);
    state_                   = state;
    frame_                   = 0;

    if (wasConnecting && state != TrayState::Connecting)
        KillTimer(hwnd_, ANIMATION_TIMER);

    if (state == TrayState::Connecting)
        SetTimer(hwnd_, ANIMATION_TIMER, ANIM_INTERVAL_MS, nullptr);

    // Build tooltip
    UINT tipId = IDS_TOOLTIP_IDLE;
    switch (state) {
        case TrayState::Idle:       tipId = IDS_TOOLTIP_IDLE;       break;
        case TrayState::Connecting: tipId = IDS_TOOLTIP_CONNECTING;  break;
        case TrayState::Streaming:  tipId = IDS_TOOLTIP_STREAMING;   break;
        case TrayState::Error:      tipId = IDS_TOOLTIP_ERROR;       break;
    }

    std::wstring tip = StringLoader::Load(tipId);
    if (deviceName && tip.find(L"%s") != std::wstring::npos) {
        wchar_t formatted[128] = {};
        if (_snwprintf_s(formatted, _countof(formatted), _TRUNCATE,
                         tip.c_str(), deviceName) >= 0) {
            tip = formatted;
        }
    } else if (deviceName && *deviceName) {
        tip += L" \x2014 ";
        tip += deviceName;
    }

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = NOTIFY_UID;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon  = LoadTrayIcon(hInst_, state);
    wcsncpy_s(nid.szTip, _countof(nid.szTip), tip.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ---------------------------------------------------------------------------
void TrayIcon::OnAnimationTick()
{
    if (state_ != TrayState::Connecting)
        return;

    frame_ = (frame_ + 1) % ANIM_FRAME_COUNT;

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = NOTIFY_UID;
    nid.uFlags = NIF_ICON;
    nid.hIcon  = LoadAnimFrame(hInst_, frame_);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}


