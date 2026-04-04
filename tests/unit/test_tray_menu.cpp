// T028 — TrayMenu unit tests (headless Win32)
#include <gtest/gtest.h>
#include "ui/TrayMenu.h"
#include "ui/MenuIds.h"
#include "core/Config.h"
#include "discovery/AirPlayReceiver.h"
#include <windows.h>
#include <vector>
#include <string>

// ── Helpers ───────────────────────────────────────────────────────────────────

static AirPlayReceiver MakeSpeaker(const std::wstring& name)
{
    AirPlayReceiver r{};
    r.instanceName        = name;
    r.displayName         = name;
    r.isAirPlay1Compatible = true;
    return r;
}

static Config MakeConfig()
{
    Config c{};
    c.lowLatency      = false;
    c.launchAtStartup = false;
    c.portableMode    = false;
    return c;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(TrayMenu, BonjourMissing_ShowsInstallItem)
{
    TrayMenu menu;
    Config cfg = MakeConfig();
    HMENU hMenu = menu.BuildMenu(cfg, false, true, {}, -1, -1);
    ASSERT_NE(hMenu, (HMENU)nullptr);

    // First item should be grayed (Bonjour missing placeholder)
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask  = MIIM_STATE;
    BOOL ok = GetMenuItemInfoW(hMenu, 0, TRUE, &info);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(info.fState & MFS_GRAYED);

    DestroyMenu(hMenu);
}

TEST(TrayMenu, EmptyReceivers_ShowsSearching)
{
    TrayMenu menu;
    Config cfg = MakeConfig();
    HMENU hMenu = menu.BuildMenu(cfg, false, false, {}, -1, -1);
    ASSERT_NE(hMenu, (HMENU)nullptr);

    // First item should be grayed (searching placeholder)
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask  = MIIM_STATE;
    BOOL ok = GetMenuItemInfoW(hMenu, 0, TRUE, &info);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(info.fState & MFS_GRAYED);

    DestroyMenu(hMenu);
}

TEST(TrayMenu, ThreeSpeakers_Inline)
{
    TrayMenu menu;
    Config cfg = MakeConfig();
    std::vector<AirPlayReceiver> receivers = {
        MakeSpeaker(L"Alpha"),
        MakeSpeaker(L"Beta"),
        MakeSpeaker(L"Gamma")
    };
    HMENU hMenu = menu.BuildMenu(cfg, false, false, receivers, -1, -1);
    ASSERT_NE(hMenu, (HMENU)nullptr);

    // First 3 items should be speaker entries (not grayed, not submenus)
    for (int i = 0; i < 3; ++i) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask  = MIIM_STATE | MIIM_ID;
        BOOL ok = GetMenuItemInfoW(hMenu, i, TRUE, &info);
        EXPECT_TRUE(ok);
        EXPECT_EQ(info.wID, static_cast<UINT>(IDM_DEVICE_BASE + i));
    }

    DestroyMenu(hMenu);
}

TEST(TrayMenu, FourSpeakers_Submenu)
{
    TrayMenu menu;
    Config cfg = MakeConfig();
    std::vector<AirPlayReceiver> receivers = {
        MakeSpeaker(L"Alpha"),
        MakeSpeaker(L"Beta"),
        MakeSpeaker(L"Gamma"),
        MakeSpeaker(L"Delta")
    };
    HMENU hMenu = menu.BuildMenu(cfg, false, false, receivers, -1, -1);
    ASSERT_NE(hMenu, (HMENU)nullptr);

    // First item should be a popup submenu
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask  = MIIM_FTYPE | MIIM_SUBMENU;
    BOOL ok = GetMenuItemInfoW(hMenu, 0, TRUE, &info);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(info.fType & MFT_SEPARATOR);  // not a separator — it's a string item
    EXPECT_NE(info.hSubMenu, (HMENU)nullptr);

    DestroyMenu(hMenu);
}

TEST(TrayMenu, ConnectedIdx_ShowsCheckmark)
{
    TrayMenu menu;
    Config cfg = MakeConfig();
    std::vector<AirPlayReceiver> receivers = {
        MakeSpeaker(L"Alpha"),
        MakeSpeaker(L"Beta"),
    };
    HMENU hMenu = menu.BuildMenu(cfg, false, false, receivers, 1, -1);
    ASSERT_NE(hMenu, (HMENU)nullptr);

    // Item 0: no checkmark
    MENUITEMINFOW info0{};
    info0.cbSize = sizeof(info0);
    info0.fMask  = MIIM_STATE;
    GetMenuItemInfoW(hMenu, 0, TRUE, &info0);
    EXPECT_FALSE(info0.fState & MFS_CHECKED);

    // Item 1: checkmark
    MENUITEMINFOW info1{};
    info1.cbSize = sizeof(info1);
    info1.fMask  = MIIM_STATE;
    GetMenuItemInfoW(hMenu, 1, TRUE, &info1);
    EXPECT_TRUE(info1.fState & MFS_CHECKED);

    DestroyMenu(hMenu);
}

TEST(TrayMenu, ConnectingIdx_ShowsLabel)
{
    TrayMenu menu;
    Config cfg = MakeConfig();
    std::vector<AirPlayReceiver> receivers = {
        MakeSpeaker(L"Alpha"),
        MakeSpeaker(L"Beta"),
    };
    HMENU hMenu = menu.BuildMenu(cfg, false, false, receivers, -1, 0);
    ASSERT_NE(hMenu, (HMENU)nullptr);

    // Item 0 should have extra text appended
    wchar_t buf[256] = {};
    MENUITEMINFOW info{};
    info.cbSize     = sizeof(info);
    info.fMask      = MIIM_STRING;
    info.dwTypeData = buf;
    info.cch        = 255;
    BOOL ok = GetMenuItemInfoW(hMenu, 0, TRUE, &info);
    EXPECT_TRUE(ok);
    std::wstring label(buf);
    // Should contain em-dash (connecting suffix)
    EXPECT_NE(label.find(L'\x2014'), std::wstring::npos);

    DestroyMenu(hMenu);
}

TEST(TrayMenu, AlphaOrder_Preserved)
{
    TrayMenu menu;
    Config cfg = MakeConfig();
    // Receivers already sorted by caller (AppController sorts them)
    std::vector<AirPlayReceiver> receivers = {
        MakeSpeaker(L"Alpha"),
        MakeSpeaker(L"Beta"),
        MakeSpeaker(L"Gamma"),
    };
    HMENU hMenu = menu.BuildMenu(cfg, false, false, receivers, -1, -1);
    ASSERT_NE(hMenu, (HMENU)nullptr);

    for (int i = 0; i < 3; ++i) {
        wchar_t buf[256] = {};
        MENUITEMINFOW info{};
        info.cbSize     = sizeof(info);
        info.fMask      = MIIM_STRING;
        info.dwTypeData = buf;
        info.cch        = 255;
        GetMenuItemInfoW(hMenu, i, TRUE, &info);
        EXPECT_EQ(std::wstring(buf), receivers[i].displayName);
    }

    DestroyMenu(hMenu);
}
