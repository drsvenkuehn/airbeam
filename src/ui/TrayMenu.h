#pragma once
#include <windows.h>
#include <vector>
#include "core/Config.h"
#include "discovery/AirPlayReceiver.h"

class MenuVolumeSlider; // forward declaration

// Builds and displays the tray context menu.  The menu is created fresh on
// every Show() call from the live Config state and receiver list.
// TrackPopupMenu is called with TPM_RETURNCMD so the caller handles the result.
class TrayMenu {
public:
    TrayMenu()  = default;
    ~TrayMenu() = default;

    TrayMenu(const TrayMenu&)            = delete;
    TrayMenu& operator=(const TrayMenu&) = delete;

    void Init(HINSTANCE hInst);

    /// Returns the selected IDM_* command from the rebuilt menu.
    /// bonjourMissing: show install-Bonjour placeholder
    /// receivers: sorted, AirPlay1-filtered list
    /// connectedReceiverIdx: index with checkmark (-1 = none)
    /// connectingReceiverIdx: index showing "— Connecting…" suffix (-1 = none)
    /// slider: optional in-menu volume slider; if non-null, installs hook around TrackPopupMenu
    UINT Show(HWND hwnd, const Config& config, bool sparkleAvailable,
              bool bonjourMissing,
              const std::vector<AirPlayReceiver>& receivers,
              int connectedReceiverIdx,
              int connectingReceiverIdx,
              MenuVolumeSlider* slider = nullptr,
              float volume = 1.0f);

    /// Builds and returns a HMENU with all items populated. Caller owns the menu.
    /// Used by unit tests to inspect menu structure without TrackPopupMenu.
    /// slider: if non-null, inserts owner-drawn label+slider rows instead of plain Volume item.
    HMENU BuildMenu(const Config& config, bool sparkleAvailable,
                    bool bonjourMissing,
                    const std::vector<AirPlayReceiver>& receivers,
                    int connectedReceiverIdx,
                    int connectingReceiverIdx,
                    MenuVolumeSlider* slider = nullptr,
                    float volume = 1.0f) const;

private:
    HINSTANCE hInst_ = nullptr;
};
