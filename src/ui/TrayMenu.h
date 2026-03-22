#pragma once
#include <windows.h>
#include <vector>
#include "core/Config.h"
#include "discovery/AirPlayReceiver.h"

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

    // Creates the popup menu, calls TrackPopupMenu, destroys the menu, and
    // returns the selected IDM_* command (or 0 if the user dismissed without
    // selecting).  Uses GetCursorPos() for placement.
    // config provides checkmark and enable/disable state for static items.
    // sparkleAvailable controls whether "Check for Updates" is enabled.
    UINT Show(HWND hwnd, const Config& config, bool sparkleAvailable);

    /// Returns the selected IDM_* command from the rebuilt menu including live receiver list.
    /// connectedReceiverIdx = index in receivers of currently streaming receiver, or -1.
    UINT Show(HWND hwnd, const Config& config, bool sparkleAvailable,
              const std::vector<AirPlayReceiver>& receivers, int connectedReceiverIdx);

private:
    HINSTANCE hInst_ = nullptr;
};
