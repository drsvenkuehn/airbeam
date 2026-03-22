#include "core/Config.h"
#include "core/Logger.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <string>

// ── Internal helpers ──────────────────────────────────────────────────────────

static void CreateDirTree(const std::wstring& path)
{
    if (path.empty())
        return;
    if (CreateDirectoryW(path.c_str(), nullptr))
        return;
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS)
        return;
    if (err == ERROR_PATH_NOT_FOUND) {
        const size_t pos = path.rfind(L'\\');
        if (pos != std::wstring::npos && pos > 0) {
            CreateDirTree(path.substr(0, pos));
            CreateDirectoryW(path.c_str(), nullptr);
        }
    }
}

static std::string WstrToUtf8(const std::wstring& ws)
{
    if (ws.empty())
        return {};
    const int len = WideCharToMultiByte(
        CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string s(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1,
                        s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWstr(const std::string& s)
{
    if (s.empty())
        return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring ws(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
    return ws;
}

static bool FileExistsW(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ── Config::Defaults ──────────────────────────────────────────────────────────
Config Config::Defaults()
{
    Config cfg;
    cfg.lastDevice      = L"";
    cfg.volume          = 1.0f;
    cfg.lowLatency      = false;
    cfg.launchAtStartup = false;
    cfg.autoUpdate      = true;
    cfg.corruptOnLoad   = false;
    cfg.portableMode    = false;
    return cfg;
}

// ── Config::Load ──────────────────────────────────────────────────────────────
Config Config::Load(Logger& log,
                    const std::wstring& exeDirOverride,
                    const std::wstring& appDataDirOverride)
{
    // ── 1. Determine the exe directory ────────────────────────────────────────
    std::wstring exeDir;
    if (!exeDirOverride.empty()) {
        exeDir = exeDirOverride;
    } else {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        const size_t sep = std::wstring(exePath).rfind(L'\\');
        exeDir = (sep != std::wstring::npos)
                     ? std::wstring(exePath, sep)
                     : std::wstring(exePath);
    }
    // Trim trailing backslash
    if (!exeDir.empty() && exeDir.back() == L'\\')
        exeDir.pop_back();

    // ── 2. Portable-mode detection ────────────────────────────────────────────
    const std::wstring portableCfgPath = exeDir + L"\\config.json";
    const bool         portable        = FileExistsW(portableCfgPath);

    // ── 3. Resolve config path ────────────────────────────────────────────────
    std::wstring configPath;
    if (portable) {
        configPath = portableCfgPath;
    } else {
        std::wstring appDataRoot;
        if (!appDataDirOverride.empty()) {
            appDataRoot = appDataDirOverride;
            if (!appDataRoot.empty() && appDataRoot.back() == L'\\')
                appDataRoot.pop_back();
        } else {
            wchar_t buf[MAX_PATH] = {};
            GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
            appDataRoot = buf;
        }
        configPath = appDataRoot + L"\\AirBeam\\config.json";
    }

    // ── 4. Initialise from Defaults, set path metadata ────────────────────────
    Config cfg     = Defaults();
    cfg.portableMode = portable;
    cfg.m_configPath = configPath;

    // ── 5. Attempt to read the file ───────────────────────────────────────────
    if (!FileExistsW(configPath)) {
        // Silent create — write defaults, ignoring any I/O error.
        log.Logf(LogLevel::kInfo,
                 "Config: file not found at '%s', creating defaults.",
                 WstrToUtf8(configPath).c_str());
        CreateDirTree(configPath.substr(0, configPath.rfind(L'\\')));
        cfg.Save();
        return cfg;
    }

    std::ifstream ifs(configPath.c_str());
    if (!ifs.is_open()) {
        log.Logf(LogLevel::kWarn,
                 "Config: cannot open '%s', using defaults.",
                 WstrToUtf8(configPath).c_str());
        return cfg;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(ifs);
    } catch (const nlohmann::json::parse_error& ex) {
        log.Logf(LogLevel::kWarn,
                 "Config corrupted, resetting to defaults. Error: %s",
                 ex.what());
        cfg.corruptOnLoad = true;
        return cfg;
    }

    // ── 6. Parse each field (missing keys silently default) ───────────────────
    try {
        if (j.contains("lastDevice") && j["lastDevice"].is_string())
            cfg.lastDevice = Utf8ToWstr(j["lastDevice"].get<std::string>());

        if (j.contains("volume") && j["volume"].is_number()) {
            cfg.volume = j["volume"].get<float>();
            if (cfg.volume < 0.0f || cfg.volume > 1.0f) {
                log.Logf(LogLevel::kWarn,
                         "Config: volume %.4f out of range, clamping.",
                         static_cast<double>(cfg.volume));
                cfg.volume = std::clamp(cfg.volume, 0.0f, 1.0f);
            }
        }

        if (j.contains("lowLatency") && j["lowLatency"].is_boolean())
            cfg.lowLatency = j["lowLatency"].get<bool>();

        if (j.contains("launchAtStartup") && j["launchAtStartup"].is_boolean())
            cfg.launchAtStartup = j["launchAtStartup"].get<bool>();

        if (j.contains("autoUpdate") && j["autoUpdate"].is_boolean())
            cfg.autoUpdate = j["autoUpdate"].get<bool>();

    } catch (const nlohmann::json::exception& ex) {
        log.Logf(LogLevel::kWarn,
                 "Config: unexpected JSON structure, resetting. Error: %s",
                 ex.what());
        cfg            = Defaults();
        cfg.portableMode = portable;
        cfg.m_configPath = configPath;
        cfg.corruptOnLoad = true;
    }

    return cfg;
}

// ── Config::Save ─────────────────────────────────────────────────────────────
bool Config::Save()
{
    if (m_configPath.empty())
        return false;

    // Ensure the directory exists.
    const size_t sep = m_configPath.rfind(L'\\');
    if (sep != std::wstring::npos)
        CreateDirTree(m_configPath.substr(0, sep));

    // Build JSON.
    nlohmann::json j;
    j["lastDevice"]      = WstrToUtf8(lastDevice);
    j["volume"]          = volume;
    j["lowLatency"]      = lowLatency;
    j["launchAtStartup"] = launchAtStartup;
    j["autoUpdate"]      = autoUpdate;

    // Atomic save (T096 verified): write to <configPath>.tmp then rename.
    // MoveFileExW(MOVEFILE_REPLACE_EXISTING) is atomic on the same volume,
    // so a power-loss during write cannot corrupt the live config file.
    const std::wstring tmpPath = m_configPath + L".tmp";
    {
        std::ofstream ofs(tmpPath.c_str());
        if (!ofs.is_open())
            return false;
        ofs << j.dump(2);
        if (!ofs.good())
            return false;
    }

    // Atomic rename.
    if (!MoveFileExW(tmpPath.c_str(), m_configPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING))
    {
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    corruptOnLoad = false;
    return true;
}
