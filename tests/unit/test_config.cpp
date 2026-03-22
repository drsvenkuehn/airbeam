#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <fstream>

#include "core/Config.h"
#include "core/Logger.h"

// ── Test helpers ──────────────────────────────────────────────────────────────

static std::atomic<int> s_counter{ 0 };

static void CreateDirChain(const std::wstring& path)
{
    if (path.empty() || CreateDirectoryW(path.c_str(), nullptr))
        return;
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return;
    const size_t pos = path.rfind(L'\\');
    if (pos != std::wstring::npos && pos > 0) {
        CreateDirChain(path.substr(0, pos));
        CreateDirectoryW(path.c_str(), nullptr);
    }
}

static void RemoveDirRecursive(const std::wstring& dir)
{
    const std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..")
                continue;
            const std::wstring full = dir + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveDirRecursive(full);
            else
                DeleteFileW(full.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}

static bool FileExistsW(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static void WriteTextFile(const std::wstring& path, const std::string& content)
{
    CreateDirChain(path.substr(0, path.rfind(L'\\')));
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"w");
    if (f) { fputs(content.c_str(), f); fclose(f); }
}

// ── Test fixture ──────────────────────────────────────────────────────────────

class ConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        const int id = ++s_counter;
        m_testRoot   = std::wstring(tmp) + L"AirBeamCfgTest_" +
                       std::to_wstring(GetCurrentProcessId()) + L"_" +
                       std::to_wstring(id);
        m_exeDir     = m_testRoot + L"\\exe";
        m_appDataDir = m_testRoot + L"\\appdata";
        CreateDirChain(m_exeDir);
        CreateDirChain(m_appDataDir);
    }

    void TearDown() override
    {
        RemoveDirRecursive(m_testRoot);
    }

    // Returns the path where Config stores the file when not in portable mode.
    std::wstring AppDataConfigPath() const
    {
        return m_appDataDir + L"\\AirBeam\\config.json";
    }

    // Returns the path used in portable mode.
    std::wstring PortableConfigPath() const
    {
        return m_exeDir + L"\\config.json";
    }

    std::wstring m_testRoot;
    std::wstring m_exeDir;
    std::wstring m_appDataDir;
};

// ── (a) Corrupt / invalid JSON → corruptOnLoad == true, resets to defaults ───

TEST_F(ConfigTest, CorruptJson_SetsCorruptOnLoad)
{
    // Place a corrupt JSON file in the appdata location.
    WriteTextFile(AppDataConfigPath(), "{ this is not valid json !!!!");

    Config cfg = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);

    EXPECT_TRUE(cfg.corruptOnLoad);
    // All fields should be at defaults.
    const Config def = Config::Defaults();
    EXPECT_EQ(cfg.lastDevice,      def.lastDevice);
    EXPECT_FLOAT_EQ(cfg.volume,    def.volume);
    EXPECT_EQ(cfg.lowLatency,      def.lowLatency);
    EXPECT_EQ(cfg.launchAtStartup, def.launchAtStartup);
    EXPECT_EQ(cfg.autoUpdate,      def.autoUpdate);
    EXPECT_FALSE(cfg.portableMode);
}

// ── (b) Missing file → silent create with defaults, corruptOnLoad == false ───

TEST_F(ConfigTest, MissingFile_CreatesDefaultsAndCorruptOnLoadFalse)
{
    // Both directories exist but contain no config.json.
    Config cfg = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);

    EXPECT_FALSE(cfg.corruptOnLoad);
    EXPECT_FALSE(cfg.portableMode);

    const Config def = Config::Defaults();
    EXPECT_EQ(cfg.lastDevice,      def.lastDevice);
    EXPECT_FLOAT_EQ(cfg.volume,    def.volume);
    EXPECT_EQ(cfg.lowLatency,      def.lowLatency);
    EXPECT_EQ(cfg.launchAtStartup, def.launchAtStartup);
    EXPECT_EQ(cfg.autoUpdate,      def.autoUpdate);

    // The file should have been created on disk.
    EXPECT_TRUE(FileExistsW(AppDataConfigPath()));
}

// ── (c) Portable mode: config.json next to exe overrides %APPDATA% ───────────

TEST_F(ConfigTest, PortableMode_UsesExeDir)
{
    // Write a recognisable config next to the "exe".
    WriteTextFile(PortableConfigPath(),
        R"({ "lastDevice": "PortableDevice", "volume": 0.42 })");

    // Also place different data in the appdata path to confirm it is ignored.
    WriteTextFile(AppDataConfigPath(),
        R"({ "lastDevice": "AppDataDevice", "volume": 0.99 })");

    Config cfg = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);

    EXPECT_TRUE(cfg.portableMode);
    EXPECT_EQ(cfg.lastDevice, L"PortableDevice");
    EXPECT_NEAR(cfg.volume, 0.42f, 1e-4f);
    EXPECT_FALSE(cfg.corruptOnLoad);
    EXPECT_EQ(cfg.FilePath(), PortableConfigPath());
}

// ── (d) Partial keys → missing keys filled with defaults ─────────────────────

TEST_F(ConfigTest, PartialKeys_PreservedAndMissingFilledWithDefaults)
{
    // Provide only some keys.
    WriteTextFile(AppDataConfigPath(),
        R"({ "lowLatency": true, "volume": 0.3 })");

    Config cfg = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);

    EXPECT_FALSE(cfg.corruptOnLoad);
    // Provided keys preserved.
    EXPECT_TRUE(cfg.lowLatency);
    EXPECT_NEAR(cfg.volume, 0.3f, 1e-4f);
    // Missing keys are defaults.
    const Config def = Config::Defaults();
    EXPECT_EQ(cfg.lastDevice,      def.lastDevice);
    EXPECT_EQ(cfg.launchAtStartup, def.launchAtStartup);
    EXPECT_EQ(cfg.autoUpdate,      def.autoUpdate);
}

// ── (e) UTF-8 CJK lastDevice round-trips correctly through save/load ──────────

TEST_F(ConfigTest, Utf8CJK_LastDeviceRoundTrip)
{
    // Chinese "客厅 HomePod" (living room HomePod)
    const std::wstring cjkName = L"\u5BA2\u5385 HomePod";

    // Build a config, set the CJK device name, save it.
    Config cfg1 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    cfg1.lastDevice = cjkName;
    EXPECT_TRUE(cfg1.Save());

    // Reload and verify round-trip.
    Config cfg2 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    EXPECT_EQ(cfg2.lastDevice, cjkName);
    EXPECT_FALSE(cfg2.corruptOnLoad);
}

// ── (f) volume clamped to [0, 1] on load ─────────────────────────────────────

TEST_F(ConfigTest, Volume_ClampedBelowZero)
{
    WriteTextFile(AppDataConfigPath(), R"({ "volume": -0.5 })");
    Config cfg = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    EXPECT_FLOAT_EQ(cfg.volume, 0.0f);
    EXPECT_FALSE(cfg.corruptOnLoad);
}

TEST_F(ConfigTest, Volume_ClampedAboveOne)
{
    WriteTextFile(AppDataConfigPath(), R"({ "volume": 1.5 })");
    Config cfg = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    EXPECT_FLOAT_EQ(cfg.volume, 1.0f);
    EXPECT_FALSE(cfg.corruptOnLoad);
}

// ── T060: volume / lowLatency / launchAtStartup round-trips ──────────────────

TEST_F(ConfigTest, VolumeRoundTrips)
{
    Config cfg1 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    cfg1.volume = 0.75f;
    EXPECT_TRUE(cfg1.Save());

    Config cfg2 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    EXPECT_NEAR(cfg2.volume, 0.75f, 0.001f);
}

TEST_F(ConfigTest, LowLatencyPersists)
{
    Config cfg1 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    cfg1.lowLatency = true;
    EXPECT_TRUE(cfg1.Save());

    Config cfg2 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    EXPECT_TRUE(cfg2.lowLatency);
}

TEST_F(ConfigTest, LaunchAtStartupPersists)
{
    Config cfg1 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    cfg1.launchAtStartup = true;
    EXPECT_TRUE(cfg1.Save());

    Config cfg2 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    EXPECT_TRUE(cfg2.launchAtStartup);
}

// ── T074: autoUpdate default and round-trip ───────────────────────────────────

TEST_F(ConfigTest, AutoUpdateDefaultsTrue)
{
    // JSON with no "autoUpdate" key → should default to true.
    WriteTextFile(AppDataConfigPath(), R"({ "volume": 0.5 })");

    Config cfg = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    EXPECT_TRUE(cfg.autoUpdate);
}

TEST_F(ConfigTest, AutoUpdateFalseRoundTrips)
{
    Config cfg1 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    cfg1.autoUpdate = false;
    EXPECT_TRUE(cfg1.Save());

    Config cfg2 = Config::Load(Logger::Instance(), m_exeDir, m_appDataDir);
    EXPECT_FALSE(cfg2.autoUpdate);
}
