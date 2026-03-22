#pragma once
#include <windows.h>
#include <string>

class Logger;

// Persisted application settings stored as JSON.
//
// Portable mode: if config.json exists next to the .exe, that file is used
// (portableMode == true); otherwise %APPDATA%\AirBeam\config.json is used.
//
// Load() is a static factory that returns a fully initialised Config:
//   • Missing file  → defaults written silently, corruptOnLoad == false.
//   • Corrupt JSON  → defaults used,            corruptOnLoad == true
//                     (caller should balloon-notify the user).
//   • Partial keys  → missing keys filled with defaults; present keys kept.
//   • volume        → clamped to [0.0, 1.0] regardless of stored value.
//
// Save() performs an atomic write (temp-file + MoveFileExW rename).
class Config
{
public:
    // ── Data fields (public for direct access) ────────────────────────────────
    std::wstring lastDevice;               // last successfully connected device
    float        volume          = 1.0f;   // linear [0, 1]
    bool         lowLatency      = false;
    bool         launchAtStartup = false;
    bool         autoUpdate      = true;

    // Set by Load() when the on-disk JSON was malformed.  Cleared after Save().
    bool         corruptOnLoad   = false;
    // True when config.json lives next to the exe (portable install).
    bool         portableMode    = false;

    // ── Factory methods ───────────────────────────────────────────────────────
    static Config Defaults();

    // Loads (or creates) the config file.
    //   exeDirOverride    — overrides GetModuleFileNameW for portable-mode
    //                       detection; used by unit tests.
    //   appDataDirOverride — overrides %APPDATA% root; used by unit tests.
    static Config Load(Logger& log,
                       const std::wstring& exeDirOverride    = L"",
                       const std::wstring& appDataDirOverride = L"");

    // Atomic save: writes to <path>.tmp then renames.
    // Returns false on I/O error.  Clears corruptOnLoad on success.
    bool Save();

    const std::wstring& FilePath() const { return m_configPath; }

    // Kept for backwards-compatible access from AppController / TrayMenu.
    bool IsPortableMode() const { return portableMode; }

private:
    std::wstring m_configPath; // set by Load()
};
