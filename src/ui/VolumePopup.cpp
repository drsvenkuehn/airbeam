#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include "ui/VolumePopup.h"

static constexpr int kPopupW       = 200;
static constexpr int kPopupH       = 40;
static constexpr int kTrackMargin  = 4;

VolumePopup::~VolumePopup()
{
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool VolumePopup::Create(HINSTANCE hInst, HWND hwndParent,
                         std::function<void(float)> onVolumeChange)
{
    hInst_          = hInst;
    hwndParent_     = hwndParent;
    onVolumeChange_ = std::move(onVolumeChange);

    // Ensure trackbar control is available
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    // Register popup window class (CS_DROPSHADOW for a polished look)
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DROPSHADOW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AirBeamVolumePopup";
    RegisterClassExW(&wc); // benign failure if already registered

    hwnd_ = CreateWindowExW(
        0,
        L"AirBeamVolumePopup",
        nullptr,
        WS_POPUP | WS_BORDER,
        0, 0, kPopupW, kPopupH,
        hwndParent,
        nullptr,
        hInst,
        this); // passed to WM_NCCREATE as CREATESTRUCT::lpCreateParams

    if (!hwnd_) return false;

    // Trackbar fills the popup interior
    hwndTrack_ = CreateWindowExW(
        0,
        TRACKBAR_CLASS,
        nullptr,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        kTrackMargin,
        kTrackMargin,
        kPopupW - kTrackMargin * 2,
        kPopupH - kTrackMargin * 2,
        hwnd_,
        nullptr,
        hInst,
        nullptr);

    if (!hwndTrack_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    SendMessageW(hwndTrack_, TBM_SETRANGE,   TRUE, MAKELPARAM(0, 100));
    SendMessageW(hwndTrack_, TBM_SETTICFREQ, 10,   0);
    SendMessageW(hwndTrack_, TBM_SETPAGESIZE, 0,   5);

    return true;
}

void VolumePopup::Show(float currentVolume)
{
    if (!hwnd_) return;

    // Position above the cursor, clamped to the screen
    POINT pt{};
    GetCursorPos(&pt);

    int x = pt.x;
    int y = pt.y - kPopupH - 4;

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);

    if (x + kPopupW > screenW) x = screenW - kPopupW;
    if (x < 0)                 x = 0;
    if (y + kPopupH > screenH) y = screenH - kPopupH;
    if (y < 0)                 y = 0;

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE);

    int pos = static_cast<int>(currentVolume * 100.0f);
    if (pos < 0)   pos = 0;
    if (pos > 100) pos = 100;

    SendMessageW(hwndTrack_, TBM_SETPOS, TRUE, pos);
    ShowWindow(hwnd_, SW_SHOW);
    SetFocus(hwndTrack_);
}

void VolumePopup::Hide()
{
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

bool VolumePopup::IsVisible() const
{
    return hwnd_ && IsWindowVisible(hwnd_);
}

// static
LRESULT CALLBACK VolumePopup::WndProc(HWND hwnd, UINT msg,
                                       WPARAM wParam, LPARAM lParam)
{
    VolumePopup* self = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<VolumePopup*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<VolumePopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_HSCROLL:
    {
        const int pos = static_cast<int>(
            SendMessageW(self->hwndTrack_, TBM_GETPOS, 0, 0));
        if (self->onVolumeChange_)
            self->onVolumeChange_(pos / 100.0f);
        return 0;
    }

    case WM_KILLFOCUS:
    {
        // Focus moving to a child (e.g. the trackbar) must not hide the popup.
        HWND newFocus = reinterpret_cast<HWND>(wParam);
        if (!IsChild(hwnd, newFocus))
            ShowWindow(hwnd, SW_HIDE);
        return 0;
    }

    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
            ShowWindow(hwnd, SW_HIDE);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
