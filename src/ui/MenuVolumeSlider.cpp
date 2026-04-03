#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <cwchar>

#include "ui/MenuVolumeSlider.h"
#include "core/Commands.h"

// ── Static instance (one menu open at a time) ────────────────────────────────
MenuVolumeSlider* MenuVolumeSlider::s_instance_ = nullptr;

// ── Public API ────────────────────────────────────────────────────────────────

void MenuVolumeSlider::Init(std::function<void(float)> onVolumeChange) {
    onVolumeChange_ = std::move(onVolumeChange);
}

void MenuVolumeSlider::InsertItems(HMENU hMenu, float volume) {
    volume_ = volume;

    // Label row: "Volume  XX%" — non-interactive, owner-drawn
    AppendMenuW(hMenu,
                MF_OWNERDRAW | MF_DISABLED | MF_GRAYED,
                IDM_VOLUME_LABEL,
                reinterpret_cast<LPCWSTR>(reinterpret_cast<ULONG_PTR>(this)));

    // Slider row — interactive, owner-drawn
    AppendMenuW(hMenu,
                MF_OWNERDRAW,
                IDM_VOLUME,
                reinterpret_cast<LPCWSTR>(reinterpret_cast<ULONG_PTR>(this)));
}

void MenuVolumeSlider::BeforeTrackPopup() {
    s_instance_ = this;
    menuHwnd_   = nullptr;
    dragging_   = false;
    sliderScreen_ = {};
    hook_ = SetWindowsHookExW(WH_MSGFILTER, HookProc, nullptr, GetCurrentThreadId());
}

void MenuVolumeSlider::AfterTrackPopup() {
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    dragging_  = false;
    menuHwnd_  = nullptr;
    if (s_instance_ == this) s_instance_ = nullptr;
}

bool MenuVolumeSlider::HandleMeasureItem(MEASUREITEMSTRUCT* mis) {
    if (mis->CtlType != ODT_MENU) return false;
    if (mis->itemID == IDM_VOLUME_LABEL) {
        mis->itemWidth  = 220;
        mis->itemHeight = 20;
        return true;
    }
    if (mis->itemID == IDM_VOLUME) {
        mis->itemWidth  = 220;
        mis->itemHeight = 28;
        return true;
    }
    return false;
}

bool MenuVolumeSlider::HandleDrawItem(DRAWITEMSTRUCT* dis) {
    if (dis->CtlType != ODT_MENU) return false;
    if (dis->itemID != IDM_VOLUME_LABEL && dis->itemID != IDM_VOLUME) return false;

    if (dis->itemID == IDM_VOLUME_LABEL) {
        // ── Label row ─────────────────────────────────────────────────────────
        FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_MENU));
        wchar_t buf[32];
        swprintf_s(buf, L"Volume  %d%%", static_cast<int>(volume_ * 100.0f + 0.5f));
        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, GetSysColor(COLOR_MENUTEXT));
        RECT r = dis->rcItem;
        r.left += 10;
        DrawTextW(dis->hDC, buf, -1, &r, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOCLIP);
    } else {
        // ── Slider row ────────────────────────────────────────────────────────

        // Try to capture the menu window HWND for coordinate conversion.
        // Fallback to FindWindow if the hook hasn't fired yet.
        if (!menuHwnd_) {
            menuHwnd_ = FindWindowW(L"#32768", nullptr);
        }

        // Update sliderScreen_ (screen-coord rect of this item).
        if (menuHwnd_) {
            RECT r = dis->rcItem;
            ClientToScreen(menuHwnd_, reinterpret_cast<POINT*>(&r.left));
            ClientToScreen(menuHwnd_, reinterpret_cast<POINT*>(&r.right));
            sliderScreen_ = r;
        }

        // Background
        FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_MENU));

        const int margin    = 10;
        const int trackLeft  = dis->rcItem.left  + margin;
        const int trackRight = dis->rcItem.right - margin;
        const int trackWidth = trackRight - trackLeft;
        const int trackY     = (dis->rcItem.top + dis->rcItem.bottom) / 2;
        const int thumbX     = trackLeft + static_cast<int>(volume_ * static_cast<float>(trackWidth));

        // Track: unfilled portion
        RECT trackRect = { trackLeft, trackY - 2, trackRight, trackY + 2 };
        FillRect(dis->hDC, &trackRect, GetSysColorBrush(COLOR_BTNSHADOW));

        // Track: filled portion (left of thumb)
        if (thumbX > trackLeft) {
            RECT filledRect = { trackLeft, trackY - 2, thumbX, trackY + 2 };
            FillRect(dis->hDC, &filledRect, GetSysColorBrush(COLOR_HIGHLIGHT));
        }

        // Thumb circle
        const int r = 6;
        HPEN   oldPen   = static_cast<HPEN>(SelectObject(dis->hDC, GetStockObject(NULL_PEN)));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dis->hDC, GetSysColorBrush(COLOR_HIGHLIGHT)));
        Ellipse(dis->hDC, thumbX - r, trackY - r, thumbX + r, trackY + r);
        SelectObject(dis->hDC, oldPen);
        SelectObject(dis->hDC, oldBrush);
    }
    return true;
}

// ── Hook ──────────────────────────────────────────────────────────────────────

LRESULT CALLBACK MenuVolumeSlider::HookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != MSGF_MENU || !s_instance_) {
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    MSG* msg = reinterpret_cast<MSG*>(lParam);
    if (!msg) return CallNextHookEx(nullptr, code, wParam, lParam);

    // Capture the menu window HWND from the first valid hook message.
    if (!s_instance_->menuHwnd_ && msg->hwnd) {
        wchar_t className[16] = {};
        GetClassNameW(msg->hwnd, className, _countof(className));
        if (wcscmp(className, L"#32768") == 0) {
            s_instance_->menuHwnd_ = msg->hwnd;
        }
    }

    switch (msg->message) {
    case WM_LBUTTONDOWN: {
        // msg->pt is already in screen coordinates.
        POINT pt = msg->pt;
        if (PtInRect(&s_instance_->sliderScreen_, pt)) {
            s_instance_->dragging_ = true;
            s_instance_->UpdateFromMouse(pt.x, pt.y);
            return 1; // suppress — prevent menu from treating this as a selection
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (s_instance_->dragging_) {
            POINT pt = msg->pt;
            s_instance_->UpdateFromMouse(pt.x, pt.y);
            if (s_instance_->menuHwnd_) {
                InvalidateRect(s_instance_->menuHwnd_, nullptr, FALSE);
                UpdateWindow(s_instance_->menuHwnd_);
            }
            return 1; // suppress during drag to avoid hover-state flicker
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (s_instance_->dragging_) {
            POINT pt = msg->pt;
            s_instance_->UpdateFromMouse(pt.x, pt.y);
            s_instance_->dragging_ = false;
            return 1; // suppress — keep menu open after drag ends
        }
        break;
    }
    default: break;
    }

    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// ── Private helpers ───────────────────────────────────────────────────────────

float MenuVolumeSlider::XToVolume(int screenX) const {
    const int margin     = 10;
    const int trackLeft  = sliderScreen_.left  + margin;
    const int trackRight = sliderScreen_.right - margin;
    if (trackRight <= trackLeft) return volume_;
    float v = static_cast<float>(screenX - trackLeft)
            / static_cast<float>(trackRight - trackLeft);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return v;
}

void MenuVolumeSlider::UpdateFromMouse(int screenX, int screenY) {
    (void)screenY;
    float v = XToVolume(screenX);
    volume_ = v;
    if (onVolumeChange_) onVolumeChange_(v);
}
