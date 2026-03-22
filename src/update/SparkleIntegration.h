#pragma once
#include <windows.h>
#include "core/Config.h"

// Dynamically loads WinSparkle.dll (must be present next to the exe).
// If the DLL cannot be loaded all public methods are safe no-ops.
class SparkleIntegration {
public:
    SparkleIntegration()  = default;
    ~SparkleIntegration() = default;

    SparkleIntegration(const SparkleIntegration&)            = delete;
    SparkleIntegration& operator=(const SparkleIntegration&) = delete;

    // Loads WinSparkle.dll, configures the appcast URL + app details from
    // compile-time constants, and calls win_sparkle_init().
    // config.autoUpdate == false disables the automatic 24-hour check.
    // Returns true if WinSparkle was loaded and initialised successfully.
    bool Init(const Config& config);

    // Calls win_sparkle_check_update_with_ui(). No-op if DLL not loaded.
    void CheckForUpdates();

    // Calls win_sparkle_cleanup(). No-op if DLL not loaded.
    void Cleanup();

    // Stores the main window handle so the update-rejected callback can post
    // WM_UPDATE_REJECTED.  Call immediately after Init().
    void SetMainHwnd(HWND hwnd);

    // True if WinSparkle.dll was loaded successfully.
    bool IsAvailable() const { return hDll_ != nullptr; }

private:
    HMODULE hDll_           = nullptr;
    void*   pfnCleanup_     = nullptr;
    void*   pfnCheckUpdate_ = nullptr;
    void*   pfnAutoCheck_   = nullptr;
};
