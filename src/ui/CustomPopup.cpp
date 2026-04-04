#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <winreg.h>
#include <algorithm>
#include "ui/CustomPopup.h"

// ── Statics ──────────────────────────────────────────────────────────────────

HINSTANCE CustomPopup::s_hInst     = nullptr;
bool      CustomPopup::s_registered = false;

static constexpr wchar_t kWndClass[] = L"AirBeamPopup";

// ── Registration / entry point ────────────────────────────────────────────────

void CustomPopup::RegisterWindowClass(HINSTANCE hInst) {
    if (s_registered) return;
    s_hInst = hInst;

    WNDCLASSEXW wc    = {};
    wc.cbSize         = sizeof(wc);
    wc.style          = CS_DROPSHADOW;   // DWM drop shadow
    wc.lpfnWndProc    = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor        = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground  = nullptr;         // painted entirely in WM_PAINT
    wc.lpszClassName  = kWndClass;
    RegisterClassExW(&wc);
    s_registered = true;
}

UINT CustomPopup::Show(HWND hwndOwner,
                       const std::vector<PopupItem>& items,
                       std::function<void(float)> onVolumeChange) {
    auto* popup = new CustomPopup(hwndOwner, items, std::move(onVolumeChange));
    popup->Create();

    MSG msg;
    while (!popup->dismissed_) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) {
            // WM_QUIT — re-post so the outer message loop also exits cleanly
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UINT cmd = popup->resultCmd_;
    delete popup;
    return cmd;
}

// ── Construction / destruction ────────────────────────────────────────────────

CustomPopup::CustomPopup(HWND hwndOwner,
                         std::vector<PopupItem> items,
                         std::function<void(float)> onVolumeChange)
    : hwndOwner_(hwndOwner)
    , items_(std::move(items))
    , onVolumeChange_(std::move(onVolumeChange))
{
    // Find slider item index
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (items_[i].type == PopupItemType::Slider) { sliderIdx_ = i; break; }
    }

    // Derive menu font from system NONCLIENTMETRICS (DPI-aware)
    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    hFont_ = CreateFontIndirectW(&ncm.lfMenuFont);
}

CustomPopup::~CustomPopup() {
    if (hFont_) { DeleteObject(hFont_); hFont_ = nullptr; }
}

// ── Window creation ───────────────────────────────────────────────────────────

int CustomPopup::ComputeHeight() const {
    int h = kPadY * 2;
    for (const auto& item : items_) {
        switch (item.type) {
            case PopupItemType::Separator: h += kSepH;    break;
            case PopupItemType::Slider:   h += kSliderH;  break;
            default:                      h += kItemH;    break;
        }
    }
    return h;
}

void CustomPopup::Create() {
    const int winW = kWidth;
    const int winH = ComputeHeight();

    // Position: align bottom-right to cursor (same as TPM_BOTTOMALIGN | TPM_RIGHTALIGN)
    POINT pt;
    GetCursorPos(&pt);
    int x = pt.x - winW;
    int y = pt.y - winH;

    // Clamp to monitor work area
    HMONITOR   hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi  = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi)) {
        x = std::max(x, static_cast<int>(mi.rcWork.left));
        y = std::max(y, static_cast<int>(mi.rcWork.top));
        if (x + winW > mi.rcWork.right)  x = mi.rcWork.right  - winW;
        if (y + winH > mi.rcWork.bottom) y = mi.rcWork.bottom - winH;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kWndClass, nullptr,
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, winW, winH,
        hwndOwner_, nullptr, s_hInst, this);

    ApplyDwmStyle();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    UpdateWindow(hwnd_);
}

void CustomPopup::ApplyDwmStyle() {
    if (!hwnd_) return;
    // Request Win11 rounded corners (no-op on Win10, safe to call always)
    constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    constexpr DWORD DWMWCP_ROUND                   = 2;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE,
                          &DWMWCP_ROUND, sizeof(DWMWCP_ROUND));
}

// ── Dark mode ─────────────────────────────────────────────────────────────────

bool CustomPopup::IsDarkMode() {
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme",
                 RRF_RT_DWORD, nullptr, &value, &size);
    return value == 0;
}

// ── Layout helpers ────────────────────────────────────────────────────────────

RECT CustomPopup::GetItemRect(int index) const {
    int y = kPadY;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        int h = 0;
        switch (items_[i].type) {
            case PopupItemType::Separator: h = kSepH;    break;
            case PopupItemType::Slider:   h = kSliderH;  break;
            default:                      h = kItemH;    break;
        }
        if (i == index) return { 4, y, kWidth - 4, y + h };
        y += h;
    }
    return {};
}

int CustomPopup::HitTestItem(int x, int y) const {
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (items_[i].type == PopupItemType::Separator) continue;
        if (items_[i].disabled && items_[i].type != PopupItemType::Slider) continue;
        RECT rc = GetItemRect(i);
        if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom) return i;
    }
    return -1;
}

void CustomPopup::InvalidateItem(int index) {
    RECT rc = GetItemRect(index);
    InvalidateRect(hwnd_, &rc, FALSE);
}

// ── Painting ──────────────────────────────────────────────────────────────────

void CustomPopup::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);

    const bool dark = IsDarkMode();
    const COLORREF clrBg  = dark ? RGB( 43,  43,  43) : RGB(255, 255, 255);
    const COLORREF clrHov = dark ? RGB( 60,  60,  60) : RGB(229, 229, 229);
    const COLORREF clrSep = dark ? RGB( 70,  70,  70) : RGB(219, 219, 219);

    // Fill window background
    RECT clientRc;
    GetClientRect(hwnd_, &clientRc);
    HBRUSH hBrBg = CreateSolidBrush(clrBg);
    FillRect(hdc, &clientRc, hBrBg);
    DeleteObject(hBrBg);

    HFONT hOldFont = static_cast<HFONT>(SelectObject(hdc, hFont_));
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        RECT rc = GetItemRect(i);

        if (items_[i].type == PopupItemType::Separator) {
            int lineY = rc.top + (rc.bottom - rc.top) / 2;
            HPEN hPen = CreatePen(PS_SOLID, 1, clrSep);
            HPEN hOld = static_cast<HPEN>(SelectObject(hdc, hPen));
            MoveToEx(hdc, rc.left + 4, lineY, nullptr);
            LineTo  (hdc, rc.right - 4, lineY);
            SelectObject(hdc, hOld);
            DeleteObject(hPen);
            continue;
        }

        // Hover background (for text and slider items)
        if (i == hoverIdx_ || (i == sliderIdx_ && dragging_)) {
            HBRUSH hBr = CreateSolidBrush(clrHov);
            FillRect(hdc, &rc, hBr);
            DeleteObject(hBr);
        }

        if (items_[i].type == PopupItemType::Slider)
            DrawSliderItem(hdc, i, rc);
        else
            DrawTextItem(hdc, i, rc);
    }

    SelectObject(hdc, hOldFont);
    EndPaint(hwnd_, &ps);
}

void CustomPopup::DrawTextItem(HDC hdc, int index, const RECT& rc) {
    const bool dark = IsDarkMode();
    const COLORREF clrTxt = dark ? RGB(255, 255, 255) : RGB( 26,  26,  26);
    const COLORREF clrDim = dark ? RGB(160, 160, 160) : RGB(140, 140, 140);
    const COLORREF clrChk = RGB(0, 120, 215);  // Windows accent blue

    const PopupItem& item = items_[index];

    // Checkmark / bullet in the left column
    if (item.checked) {
        SetTextColor(hdc, item.disabled ? clrDim : clrChk);
        RECT chkRc = { rc.left, rc.top, rc.left + kCheckW, rc.bottom };
        DrawTextW(hdc, L"\u2713", 1, &chkRc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Label
    SetTextColor(hdc, item.disabled ? clrDim : clrTxt);
    RECT lblRc = { rc.left + kCheckW, rc.top, rc.right - kPadX, rc.bottom };
    DrawTextW(hdc, item.label.c_str(), -1, &lblRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void CustomPopup::DrawSliderItem(HDC hdc, int index, const RECT& rc) {
    const bool dark = IsDarkMode();
    const COLORREF clrTxt  = dark ? RGB(255, 255, 255) : RGB( 26,  26,  26);
    const COLORREF clrDim  = dark ? RGB(120, 120, 120) : RGB(140, 140, 140);
    const COLORREF clrTrk  = dark ? RGB( 80,  80,  80) : RGB(210, 210, 210);
    const COLORREF clrFill = RGB(0, 120, 215);   // accent blue

    const float val = items_[index].sliderVal;

    // ── Label row ────────────────────────────────────────────────────────────
    constexpr int kLabelH = 20;
    RECT labelRc = { rc.left + kCheckW, rc.top + 6,
                     rc.right - kPadX,  rc.top + 6 + kLabelH };
    SetTextColor(hdc, clrTxt);
    DrawTextW(hdc, items_[index].label.c_str(), -1, &labelRc,
              DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

    wchar_t pct[8];
    swprintf_s(pct, L"%d%%", static_cast<int>(val * 100.f + 0.5f));
    SetTextColor(hdc, clrDim);
    DrawTextW(hdc, pct, -1, &labelRc, DT_RIGHT | DT_TOP | DT_SINGLELINE);

    // ── Slider track ─────────────────────────────────────────────────────────
    constexpr int kTrackH = 4;
    constexpr int kThumbR = 7;

    const int trackX1 = rc.left  + kCheckW;
    const int trackX2 = rc.right - kPadX;
    const int trackY  = rc.top + 6 + kLabelH + (rc.bottom - rc.top - 6 - kLabelH - kTrackH) / 2;
    const int fillX   = trackX1 + static_cast<int>((trackX2 - trackX1) * val);

    // Background track
    HBRUSH hBrTrk = CreateSolidBrush(clrTrk);
    RECT trkRc = { trackX1, trackY, trackX2, trackY + kTrackH };
    FillRect(hdc, &trkRc, hBrTrk);
    DeleteObject(hBrTrk);

    // Filled portion
    if (fillX > trackX1) {
        RECT fillRc = { trackX1, trackY, fillX, trackY + kTrackH };
        HBRUSH hBrFill = CreateSolidBrush(clrFill);
        FillRect(hdc, &fillRc, hBrFill);
        DeleteObject(hBrFill);
    }

    // Thumb circle
    const int thumbX1 = fillX - kThumbR;
    const int thumbX2 = fillX + kThumbR;
    const int thumbY1 = trackY + kTrackH / 2 - kThumbR;
    const int thumbY2 = trackY + kTrackH / 2 + kThumbR;

    HBRUSH hBrThumb = CreateSolidBrush(dark ? RGB(200, 200, 200) : RGB(255, 255, 255));
    HPEN   hPenThumb = CreatePen(PS_SOLID, 2, clrFill);
    HBRUSH hOldBr  = static_cast<HBRUSH>(SelectObject(hdc, hBrThumb));
    HPEN   hOldPen = static_cast<HPEN>  (SelectObject(hdc, hPenThumb));
    Ellipse(hdc, thumbX1, thumbY1, thumbX2, thumbY2);
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBrThumb);
    DeleteObject(hPenThumb);
}

// ── Mouse / keyboard input ────────────────────────────────────────────────────

void CustomPopup::UpdateSlider(int clientX, int sliderItemIndex) {
    const RECT rc    = GetItemRect(sliderItemIndex);
    const int trackX1 = rc.left  + kCheckW;
    const int trackX2 = rc.right - kPadX;
    float val = static_cast<float>(clientX - trackX1) /
                static_cast<float>(trackX2 - trackX1);
    val = std::max(0.0f, std::min(1.0f, val));
    items_[sliderItemIndex].sliderVal = val;
    if (onVolumeChange_) onVolumeChange_(val);
    InvalidateItem(sliderItemIndex);
}

void CustomPopup::OnMouseMove(int x, int y) {
    if (dragging_ && sliderIdx_ >= 0) {
        UpdateSlider(x, sliderIdx_);
        return;
    }

    const int newHover = HitTestItem(x, y);
    if (newHover != hoverIdx_) {
        const int oldHover = hoverIdx_;
        hoverIdx_ = newHover;
        if (oldHover  >= 0) InvalidateItem(oldHover);
        if (newHover  >= 0) InvalidateItem(newHover);
    }

    // Request WM_MOUSELEAVE notification
    TRACKMOUSEEVENT tme = {};
    tme.cbSize    = sizeof(tme);
    tme.dwFlags   = TME_LEAVE;
    tme.hwndTrack = hwnd_;
    TrackMouseEvent(&tme);
}

void CustomPopup::OnMouseLeave() {
    const int old = hoverIdx_;
    hoverIdx_ = -1;
    if (old >= 0) InvalidateItem(old);
}

void CustomPopup::OnLButtonDown(int x, int y) {
    if (sliderIdx_ >= 0) {
        const RECT rc = GetItemRect(sliderIdx_);
        if (x >= rc.left && x < rc.right && y >= rc.top && y < rc.bottom) {
            dragging_ = true;
            SetCapture(hwnd_);
            UpdateSlider(x, sliderIdx_);
            return;
        }
    }
}

void CustomPopup::OnLButtonUp(int x, int y) {
    if (dragging_) {
        dragging_ = false;
        ReleaseCapture();
        return;
    }
    const int idx = HitTestItem(x, y);
    if (idx >= 0 && items_[idx].type == PopupItemType::Text && !items_[idx].disabled)
        Close(items_[idx].cmdId);
}

void CustomPopup::OnKeyDown(WPARAM vk) {
    switch (vk) {
        case VK_ESCAPE:
            Close(0);
            break;
        case VK_RETURN:
            if (hoverIdx_ >= 0 &&
                items_[hoverIdx_].type == PopupItemType::Text &&
                !items_[hoverIdx_].disabled)
                Close(items_[hoverIdx_].cmdId);
            break;
        case VK_UP:
            for (int i = (hoverIdx_ > 0 ? hoverIdx_ : static_cast<int>(items_.size())) - 1;
                 i >= 0; --i) {
                if (items_[i].type == PopupItemType::Text && !items_[i].disabled) {
                    const int old = hoverIdx_;
                    hoverIdx_ = i;
                    if (old >= 0) InvalidateItem(old);
                    InvalidateItem(i);
                    break;
                }
            }
            break;
        case VK_DOWN:
            for (int i = hoverIdx_ + 1; i < static_cast<int>(items_.size()); ++i) {
                if (items_[i].type == PopupItemType::Text && !items_[i].disabled) {
                    const int old = hoverIdx_;
                    hoverIdx_ = i;
                    if (old >= 0) InvalidateItem(old);
                    InvalidateItem(i);
                    break;
                }
            }
            break;
    }
}

void CustomPopup::OnFocusLost() {
    if (!dragging_) Close(0);
}

// ── Close / lifetime ──────────────────────────────────────────────────────────

void CustomPopup::Close(UINT cmd) {
    if (dismissed_) return;
    resultCmd_ = cmd;
    dismissed_ = true;
    if (hwnd_) DestroyWindow(hwnd_);
    // WM_DESTROY will null hwnd_ and wake the message loop
}

// ── Window procedure ──────────────────────────────────────────────────────────

LRESULT CALLBACK CustomPopup::WndProc(HWND hwnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam) {
    CustomPopup* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<CustomPopup*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<CustomPopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_PAINT:
            self->OnPaint();
            return 0;

        case WM_ERASEBKGND:
            return 1;  // suppress default erase — OnPaint fills everything

        case WM_MOUSEMOVE: {
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            self->OnMouseMove(x, y);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            self->OnLButtonDown(x, y);
            return 0;
        }
        case WM_LBUTTONUP: {
            const int x = static_cast<short>(LOWORD(lParam));
            const int y = static_cast<short>(HIWORD(lParam));
            self->OnLButtonUp(x, y);
            return 0;
        }
        case WM_MOUSELEAVE:
            self->OnMouseLeave();
            return 0;

        case WM_KEYDOWN:
            self->OnKeyDown(wParam);
            return 0;

        case WM_KILLFOCUS:
        case WM_ACTIVATE:
            if (msg == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE) break;
            self->OnFocusLost();
            return 0;

        case WM_RBUTTONDOWN:
            self->Close(0);
            return 0;

        case WM_DESTROY:
            self->dismissed_ = true;
            self->hwnd_      = nullptr;
            // Wake the message loop in Show()
            PostMessageW(self->hwndOwner_, WM_NULL, 0, 0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
