#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#include <string>
#include "ui/TrayMenu.h"
#include "ui/MenuIds.h"
#include "ui/MenuVolumeSlider.h"
#include "localization/StringLoader.h"
#include "discovery/AirPlayReceiver.h"
#include "resource_ids.h"

void TrayMenu::Init(HINSTANCE hInst) {
    hInst_ = hInst;
}

HMENU TrayMenu::BuildMenu(const Config& config, bool sparkleAvailable,
                          bool bonjourMissing,
                          const std::vector<AirPlayReceiver>& receivers,
                          int connectedReceiverIdx,
                          int connectingReceiverIdx,
                          MenuVolumeSlider* slider,
                          float volume) const {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return nullptr;

    // ── Speaker section ───────────────────────────────────────────────────────
    if (bonjourMissing) {
        std::wstring label = StringLoader::Load(IDS_MENU_BONJOUR_MISSING);
        if (label.empty()) label = L"Install Bonjour to discover speakers";
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, label.c_str());
    } else if (receivers.empty()) {
        std::wstring label = StringLoader::Load(IDS_MENU_SEARCHING);
        if (label.empty()) label = L"Searching for speakers\x2026";
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, label.c_str());
    } else if (receivers.size() <= 3) {
        // Inline items
        for (int i = 0; i < static_cast<int>(receivers.size()); ++i) {
            std::wstring label = receivers[i].displayName;
            if (receivers[i].isAirPlay2Only) {
                label += L" (AirPlay 2)";
            } else if (i == connectingReceiverIdx) {
                std::wstring suffix = StringLoader::Load(IDS_MENU_CONNECTING);
                if (suffix.empty()) suffix = L" \x2014 Connecting\x2026";
                label += suffix;
            }
            UINT flags = MF_STRING;
            if (i == connectedReceiverIdx) flags |= MF_CHECKED;
            AppendMenuW(hMenu, flags,
                        IDM_DEVICE_BASE + static_cast<UINT>(i),
                        label.c_str());
        }
        if (connectedReceiverIdx >= 0)
            SetMenuDefaultItem(hMenu, IDM_DEVICE_BASE + static_cast<UINT>(connectedReceiverIdx), FALSE);
    } else {
        // Submenu
        HMENU hSub = CreatePopupMenu();
        if (hSub) {
            for (int i = 0; i < static_cast<int>(receivers.size()); ++i) {
                std::wstring label = receivers[i].displayName;
                if (receivers[i].isAirPlay2Only) {
                    label += L" (AirPlay 2)";
                } else if (i == connectingReceiverIdx) {
                    std::wstring suffix = StringLoader::Load(IDS_MENU_CONNECTING);
                    if (suffix.empty()) suffix = L" \x2014 Connecting\x2026";
                    label += suffix;
                }
                UINT flags = MF_STRING;
                if (i == connectedReceiverIdx) flags |= MF_CHECKED;
                AppendMenuW(hSub, flags,
                            IDM_DEVICE_BASE + static_cast<UINT>(i),
                            label.c_str());
            }
            if (connectedReceiverIdx >= 0)
                SetMenuDefaultItem(hSub, IDM_DEVICE_BASE + static_cast<UINT>(connectedReceiverIdx), FALSE);
            std::wstring speakersLabel = StringLoader::Load(IDS_MENU_SPEAKERS);
            if (speakersLabel.empty()) speakersLabel = L"Speakers";
            AppendMenuW(hMenu, MF_POPUP | MF_STRING,
                        reinterpret_cast<UINT_PTR>(hSub),
                        speakersLabel.c_str());
        }
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Volume ────────────────────────────────────────────────────────────────
    if (slider) {
        slider->InsertItems(hMenu, volume);
    } else {
        std::wstring volLabel = StringLoader::Load(IDS_MENU_VOLUME);
        if (volLabel.empty()) volLabel = L"Volume";
        AppendMenuW(hMenu, MF_STRING, IDM_VOLUME, volLabel.c_str());
    }

    // ── Low-latency mode toggle ───────────────────────────────────────────────
    std::wstring llLabel = StringLoader::Load(IDS_MENU_LOW_LATENCY);
    if (llLabel.empty()) llLabel = L"Low-Latency Mode";
    UINT llFlags = MF_STRING | (config.lowLatency ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, llFlags, IDM_LOW_LATENCY, llLabel.c_str());

    // ── Launch at startup toggle ──────────────────────────────────────────────
    std::wstring startupLabel = StringLoader::Load(IDS_MENU_LAUNCH_AT_STARTUP);
    if (startupLabel.empty()) startupLabel = L"Launch at Startup";
    UINT startupFlags = MF_STRING | (config.launchAtStartup ? MF_CHECKED : MF_UNCHECKED);
    if (config.portableMode) startupFlags |= MF_GRAYED;
    AppendMenuW(hMenu, startupFlags, IDM_LAUNCH_AT_STARTUP, startupLabel.c_str());

    // ── Open log folder ───────────────────────────────────────────────────────
    std::wstring logLabel = StringLoader::Load(IDS_MENU_OPEN_LOG_FOLDER);
    if (logLabel.empty()) logLabel = L"Open Log Folder";
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_LOG, logLabel.c_str());

    // ── Check for Updates ─────────────────────────────────────────────────────
    std::wstring updateLabel = StringLoader::Load(IDS_MENU_CHECK_FOR_UPDATES);
    if (updateLabel.empty()) updateLabel = L"Check for Updates...";
    UINT updateFlags = MF_STRING | (sparkleAvailable ? MF_ENABLED : MF_GRAYED);
    AppendMenuW(hMenu, updateFlags, IDM_CHECK_UPDATES, updateLabel.c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Quit ──────────────────────────────────────────────────────────────────
    std::wstring quitLabel = StringLoader::Load(IDS_MENU_QUIT);
    if (quitLabel.empty()) quitLabel = L"Quit AirBeam";
    AppendMenuW(hMenu, MF_STRING, IDM_QUIT, quitLabel.c_str());

    return hMenu;
}

UINT TrayMenu::Show(HWND hwnd, const Config& config, bool sparkleAvailable,
                    bool bonjourMissing,
                    const std::vector<AirPlayReceiver>& receivers,
                    int connectedReceiverIdx,
                    int connectingReceiverIdx,
                    MenuVolumeSlider* slider,
                    float volume) {
    HMENU hMenu = BuildMenu(config, sparkleAvailable, bonjourMissing, receivers,
                            connectedReceiverIdx, connectingReceiverIdx,
                            slider, volume);
    if (!hMenu) return 0;

    // ── Show at cursor position ───────────────────────────────────────────────
    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);

    if (slider) slider->BeforeTrackPopup();

    UINT cmd = static_cast<UINT>(TrackPopupMenu(
        hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
        pt.x, pt.y, 0, hwnd, nullptr));

    if (slider) slider->AfterTrackPopup();

    DestroyMenu(hMenu);
    return cmd;
}
