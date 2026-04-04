#pragma once
// src/ui/PinDialog.h — Win32 modal dialog for HAP pairing PIN entry.
// Shown when HapPairing receives kTLVError_Authentication indicating PIN required.
// §III: Win32 API only — no external UI framework.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

class PinDialog {
public:
    /// Show a modal PIN entry dialog.
    /// @param parent     Owner window (may be nullptr for taskbar-modal).
    /// @param deviceName Name of the AirPlay device requesting the PIN.
    /// @returns The 6–8 digit PIN string, or empty string if cancelled.
    static std::string Show(HWND parent, const std::wstring& deviceName);
};
