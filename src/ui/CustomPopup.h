#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#include <string>
#include <functional>

// ── Item model ───────────────────────────────────────────────────────────────

enum class PopupItemType {
    Text,       // regular clickable text item
    Separator,  // horizontal rule
    Slider,     // volume label + draggable track
};

struct PopupItem {
    PopupItemType type      = PopupItemType::Text;
    std::wstring  label;
    UINT          cmdId     = 0;
    bool          checked   = false;  // show checkmark / bullet
    bool          disabled  = false;
    float         sliderVal = 1.0f;   // [0..1], only for Slider items
};

// ── CustomPopup ──────────────────────────────────────────────────────────────

// Modern Win11-style popup that replaces TrackPopupMenu.
// Provides rounded corners (DWM), smooth ClearType text, hover effects,
// and an embedded volume slider — all without WS_EX_LAYERED complications.
class CustomPopup {
public:
    static void RegisterWindowClass(HINSTANCE hInst);

    // Show the popup at the cursor position (bottom-right aligned).
    // Blocks in a local message loop until dismissed.
    // Returns the selected IDM_* command id, or 0 if dismissed.
    // onVolumeChange fires live as the slider is dragged.
    static UINT Show(HWND hwndOwner,
                     const std::vector<PopupItem>& items,
                     std::function<void(float)> onVolumeChange = nullptr);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);

    CustomPopup(HWND hwndOwner,
                std::vector<PopupItem> items,
                std::function<void(float)> onVolumeChange);
    ~CustomPopup();

    void Create();
    void ApplyDwmStyle();

    // Painting
    void OnPaint();
    void DrawTextItem  (HDC hdc, int index, const RECT& rc);
    void DrawSliderItem(HDC hdc, int index, const RECT& rc);

    // Mouse / keyboard
    void OnMouseMove  (int x, int y);
    void OnLButtonDown(int x, int y);
    void OnLButtonUp  (int x, int y);
    void OnMouseLeave ();
    void OnKeyDown    (WPARAM vk);
    void OnFocusLost  ();

    // Helpers
    RECT GetItemRect   (int index) const;
    int  HitTestItem   (int x, int y) const;
    int  ComputeHeight () const;
    void InvalidateItem(int index);
    void UpdateSlider  (int clientX, int sliderItemIndex);
    void Close         (UINT cmd);

    static bool IsDarkMode();

    // State
    HWND                       hwnd_       = nullptr;
    HWND                       hwndOwner_  = nullptr;
    std::vector<PopupItem>     items_;
    std::function<void(float)> onVolumeChange_;
    int                        hoverIdx_   = -1;
    int                        sliderIdx_  = -1;
    bool                       dragging_   = false;
    UINT                       resultCmd_  = 0;
    bool                       dismissed_  = false;
    HFONT                      hFont_      = nullptr;

    // Layout constants (logical pixels at 96 DPI; scaled by DPI context)
    static constexpr int kWidth   = 260; // popup width
    static constexpr int kItemH   = 32;  // text item row height
    static constexpr int kSepH    = 9;   // separator row height
    static constexpr int kSliderH = 56;  // slider row height
    static constexpr int kPadY    = 4;   // top/bottom window padding
    static constexpr int kPadX    = 16;  // right-edge inner padding
    static constexpr int kCheckW  = 28;  // checkmark column width

    static HINSTANCE s_hInst;
    static bool      s_registered;
};
