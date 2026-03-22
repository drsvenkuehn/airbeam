#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <functional>

/// Floating popup trackbar for volume control.
/// Shows adjacent to the system tray when IDM_VOLUME is clicked.
/// Dismisses on WM_KILLFOCUS or click outside the window.
class VolumePopup {
public:
    VolumePopup() = default;
    ~VolumePopup();

    VolumePopup(const VolumePopup&)            = delete;
    VolumePopup& operator=(const VolumePopup&) = delete;

    /// Registers window class and creates the popup (hidden).
    /// onVolumeChange: called with new linear volume [0,1] on every thumb movement.
    bool Create(HINSTANCE hInst, HWND hwndParent,
                std::function<void(float)> onVolumeChange);

    /// Shows the popup at the cursor position (adjusted to stay on-screen).
    void Show(float currentVolume);

    /// Hides the popup.
    void Hide();

    bool IsVisible() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HINSTANCE   hInst_       = nullptr;
    HWND        hwndParent_  = nullptr;
    HWND        hwnd_        = nullptr;
    HWND        hwndTrack_   = nullptr;
    std::function<void(float)> onVolumeChange_;
};
