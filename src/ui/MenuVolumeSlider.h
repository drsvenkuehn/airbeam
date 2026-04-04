#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <functional>

/// Owner-drawn in-menu volume slider.
/// Insert two items into the popup menu (label + slider), then call
/// BeforeTrackPopup / AfterTrackPopup around TrackPopupMenu.
class MenuVolumeSlider {
public:
    void Init(std::function<void(float)> onVolumeChange);

    /// Insert "Volume: XX%" label + draggable slider into hMenu.
    /// Call this inside BuildMenu, where the old IDM_VOLUME AppendMenuW was.
    void InsertItems(HMENU hMenu, float volume);

    float GetVolume() const { return volume_; }
    void  SetVolume(float v) { volume_ = v; }

    /// Call immediately before TrackPopupMenu — installs mouse hook.
    void BeforeTrackPopup();
    /// Call immediately after TrackPopupMenu returns — uninstalls hook.
    void AfterTrackPopup();

    /// Forward from WndProc.
    bool HandleMeasureItem(MEASUREITEMSTRUCT* mis);
    bool HandleDrawItem(DRAWITEMSTRUCT* dis);

private:
    static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK CbtHookProc(int code, WPARAM wParam, LPARAM lParam);
    float XToVolume(int screenX) const;
    void  UpdateFromMouse(int screenX, int screenY);

    float    volume_      = 1.0f;
    bool     dragging_    = false;
    HHOOK    hook_        = nullptr;
    HHOOK    cbtHook_     = nullptr;
    RECT     sliderScreen_{};   // screen coords of slider item, set in DrawItem
    HWND     menuHwnd_    = nullptr;

    std::function<void(float)> onVolumeChange_;
    static MenuVolumeSlider* s_instance_;
};
