#pragma once
#include <windows.h>
#include <vector>
#include <functional>
#include "core/Config.h"
#include "discovery/AirPlayReceiver.h"
#include "ui/CustomPopup.h"

// Builds and displays the tray context menu using the modern CustomPopup.
// The menu is rebuilt on every Show() call from the live Config / receiver state.
class TrayMenu {
public:
    TrayMenu()  = default;
    ~TrayMenu() = default;

    TrayMenu(const TrayMenu&)            = delete;
    TrayMenu& operator=(const TrayMenu&) = delete;

    void Init(HINSTANCE hInst);

    /// Show the custom popup and return the selected IDM_* command id (0 = dismissed).
    /// onVolumeChange fires live as the slider is dragged.
    UINT Show(HWND hwnd,
              const Config& config,
              bool sparkleAvailable,
              bool bonjourMissing,
              const std::vector<AirPlayReceiver>& receivers,
              int connectedReceiverIdx,
              int connectingReceiverIdx,
              std::function<void(float)> onVolumeChange = nullptr);

    /// Build and return a HMENU with all items (used by unit tests only).
    HMENU BuildMenu(const Config& config,
                    bool sparkleAvailable,
                    bool bonjourMissing,
                    const std::vector<AirPlayReceiver>& receivers,
                    int connectedReceiverIdx,
                    int connectingReceiverIdx) const;

private:
    /// Build the PopupItem list consumed by CustomPopup::Show().
    std::vector<PopupItem> BuildItems(const Config& config,
                                      bool sparkleAvailable,
                                      bool bonjourMissing,
                                      const std::vector<AirPlayReceiver>& receivers,
                                      int connectedReceiverIdx,
                                      int connectingReceiverIdx,
                                      float volume) const;

    HINSTANCE hInst_ = nullptr;
};
