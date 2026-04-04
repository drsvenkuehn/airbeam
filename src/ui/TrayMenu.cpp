#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#include <string>
#include <functional>
#include "ui/TrayMenu.h"
#include "ui/MenuIds.h"
#include "ui/CustomPopup.h"
#include "localization/StringLoader.h"
#include "discovery/AirPlayReceiver.h"
#include "resource_ids.h"
#include "core/Commands.h"

void TrayMenu::Init(HINSTANCE hInst) {
    hInst_ = hInst;
    CustomPopup::RegisterWindowClass(hInst);
}

// ── PopupItem list (for CustomPopup) ─────────────────────────────────────────

std::vector<PopupItem> TrayMenu::BuildItems(
        const Config& config,
        bool sparkleAvailable,
        bool bonjourMissing,
        const std::vector<AirPlayReceiver>& receivers,
        int connectedReceiverIdx,
        int connectingReceiverIdx,
        float volume) const {

    std::vector<PopupItem> items;

    // ── Speaker section ───────────────────────────────────────────────────────
    if (bonjourMissing) {
        std::wstring label = StringLoader::Load(IDS_MENU_BONJOUR_MISSING);
        if (label.empty()) label = L"Install Bonjour to discover speakers";
        items.push_back({ PopupItemType::Text, label, 0, false, true });
    } else if (receivers.empty()) {
        std::wstring label = StringLoader::Load(IDS_MENU_SEARCHING);
        if (label.empty()) label = L"Searching for speakers\x2026";
        items.push_back({ PopupItemType::Text, label, 0, false, true });
    } else {
        for (int i = 0; i < static_cast<int>(receivers.size()); ++i) {
            const AirPlayReceiver& r = receivers[i];
            std::wstring label = r.displayName;

            if (r.supportsAirPlay2 && !r.isAirPlay1Compatible) {
                // AP2-only device — show pairing status
                const bool isPaired = (r.pairingState == PairingState::Paired);
                const bool isPairing = (r.pairingState == PairingState::Pairing);
                const bool isUnpaired = (r.pairingState == PairingState::Unpaired ||
                                         r.pairingState == PairingState::Error ||
                                         r.pairingState == PairingState::NotApplicable);

                if (isPairing) {
                    std::wstring suffix = StringLoader::Load(IDS_AP2_STATE_PAIRING);
                    if (suffix.empty()) suffix = L"Pairing...";
                    label += L" (" + suffix + L")";
                } else if (isUnpaired) {
                    std::wstring suffix = StringLoader::Load(IDS_AP2_STATE_UNPAIRED);
                    if (suffix.empty()) suffix = L"Unpaired";
                    label += L" (" + suffix + L")";
                } else if (i == connectingReceiverIdx) {
                    std::wstring suffix = StringLoader::Load(IDS_MENU_CONNECTING);
                    if (suffix.empty()) suffix = L" \x2014 Connecting\x2026";
                    label += suffix;
                }
                // AP2 devices are selectable (not greyed out) — click initiates pairing if needed
                const bool checked = (i == connectedReceiverIdx);
                items.push_back({ PopupItemType::Text, label,
                                  IDM_DEVICE_BASE + static_cast<UINT>(i),
                                  checked, false });

                // "Forget device" option for paired AP2 speakers
                if (isPaired) {
                    std::wstring forgetLabel = L"  + ";
                    std::wstring forgetStr = StringLoader::Load(IDS_AP2_FORGET_DEVICE);
                    if (forgetStr.empty()) forgetStr = L"Forget Device";
                    forgetLabel += forgetStr;
                    items.push_back({ PopupItemType::Text, forgetLabel,
                                      IDM_FORGET_DEVICE_BASE + static_cast<UINT>(i),
                                      false, false });
                }
            } else {
                // AirPlay 1 device (unchanged behaviour)
                if (i == connectingReceiverIdx) {
                    std::wstring suffix = StringLoader::Load(IDS_MENU_CONNECTING);
                    if (suffix.empty()) suffix = L" - Connecting...";
                    label += suffix;
                }
                const bool checked = (i == connectedReceiverIdx);
                items.push_back({ PopupItemType::Text, label,
                                  IDM_DEVICE_BASE + static_cast<UINT>(i),
                                  checked, false });
            }
        }
    }

    items.push_back({ PopupItemType::Separator });

    // ── Disconnect (only when connected) ─────────────────────────────────────
    if (connectedReceiverIdx >= 0) {
        std::wstring label = StringLoader::Load(IDS_MENU_DISCONNECT);
        if (label.empty()) label = L"Disconnect";
        items.push_back({ PopupItemType::Text, label, IDM_DISCONNECT });
    }

    // ── Volume slider ─────────────────────────────────────────────────────────
    {
        std::wstring label = StringLoader::Load(IDS_MENU_VOLUME);
        if (label.empty()) label = L"Volume";
        PopupItem sliderItem;
        sliderItem.type      = PopupItemType::Slider;
        sliderItem.label     = std::move(label);
        sliderItem.sliderVal = volume;
        items.push_back(std::move(sliderItem));
    }

    // ── Low-latency mode toggle ───────────────────────────────────────────────
    {
        std::wstring label = StringLoader::Load(IDS_MENU_LOW_LATENCY);
        if (label.empty()) label = L"Low-Latency Mode";
        items.push_back({ PopupItemType::Text, label,
                          IDM_LOW_LATENCY, config.lowLatency });
    }

    // ── Launch at startup toggle ──────────────────────────────────────────────
    {
        std::wstring label = StringLoader::Load(IDS_MENU_LAUNCH_AT_STARTUP);
        if (label.empty()) label = L"Launch at Startup";
        items.push_back({ PopupItemType::Text, label,
                          IDM_LAUNCH_AT_STARTUP, config.launchAtStartup,
                          config.portableMode });
    }

    // ── Open log folder ───────────────────────────────────────────────────────
    {
        std::wstring label = StringLoader::Load(IDS_MENU_OPEN_LOG_FOLDER);
        if (label.empty()) label = L"Open Log Folder";
        items.push_back({ PopupItemType::Text, label, IDM_OPEN_LOG });
    }

    // ── Check for updates ─────────────────────────────────────────────────────
    {
        std::wstring label = StringLoader::Load(IDS_MENU_CHECK_FOR_UPDATES);
        if (label.empty()) label = L"Check for Updates\x2026";
        items.push_back({ PopupItemType::Text, label,
                          IDM_CHECK_UPDATES, false, !sparkleAvailable });
    }

    items.push_back({ PopupItemType::Separator });

    // ── Quit ──────────────────────────────────────────────────────────────────
    {
        std::wstring label = StringLoader::Load(IDS_MENU_QUIT);
        if (label.empty()) label = L"Quit AirBeam";
        items.push_back({ PopupItemType::Text, label, IDM_QUIT });
    }

    return items;
}

// ── Show ──────────────────────────────────────────────────────────────────────

UINT TrayMenu::Show(HWND hwnd,
                    const Config& config,
                    bool sparkleAvailable,
                    bool bonjourMissing,
                    const std::vector<AirPlayReceiver>& receivers,
                    int connectedReceiverIdx,
                    int connectingReceiverIdx,
                    std::function<void(float)> onVolumeChange) {
    SetForegroundWindow(hwnd);

    auto items = BuildItems(config, sparkleAvailable, bonjourMissing,
                            receivers, connectedReceiverIdx, connectingReceiverIdx,
                            config.volume);

    return CustomPopup::Show(hwnd, items, std::move(onVolumeChange));
}

// ── BuildMenu (unit-test compatibility) ───────────────────────────────────────
// Returns a native HMENU so existing tests can inspect item count / flags.

HMENU TrayMenu::BuildMenu(const Config& config,
                           bool sparkleAvailable,
                           bool bonjourMissing,
                           const std::vector<AirPlayReceiver>& receivers,
                           int connectedReceiverIdx,
                           int connectingReceiverIdx) const {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return nullptr;

    if (bonjourMissing) {
        std::wstring label = StringLoader::Load(IDS_MENU_BONJOUR_MISSING);
        if (label.empty()) label = L"Install Bonjour to discover speakers";
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, label.c_str());
    } else if (receivers.empty()) {
        std::wstring label = StringLoader::Load(IDS_MENU_SEARCHING);
        if (label.empty()) label = L"Searching for speakers\x2026";
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, label.c_str());
    } else if (receivers.size() <= 3) {
        for (int i = 0; i < static_cast<int>(receivers.size()); ++i) {
            std::wstring label = receivers[i].displayName;
            if (receivers[i].isAirPlay2Only)  label += L" (AirPlay 2)";
            else if (i == connectingReceiverIdx) {
                std::wstring s = StringLoader::Load(IDS_MENU_CONNECTING);
                if (s.empty()) s = L" - Connecting...";
                label += s;
            }
            UINT flags = MF_STRING;
            if (i == connectedReceiverIdx) flags |= MF_CHECKED;
            AppendMenuW(hMenu, flags, IDM_DEVICE_BASE + static_cast<UINT>(i), label.c_str());
        }
        if (connectedReceiverIdx >= 0)
            SetMenuDefaultItem(hMenu, IDM_DEVICE_BASE + static_cast<UINT>(connectedReceiverIdx), FALSE);
    } else {
        HMENU hSub = CreatePopupMenu();
        if (hSub) {
            for (int i = 0; i < static_cast<int>(receivers.size()); ++i) {
                std::wstring label = receivers[i].displayName;
                if (receivers[i].isAirPlay2Only) label += L" (AirPlay 2)";
                else if (i == connectingReceiverIdx) {
                    std::wstring s = StringLoader::Load(IDS_MENU_CONNECTING);
                    if (s.empty()) s = L" - Connecting...";
                    label += s;
                }
                UINT flags = MF_STRING;
                if (i == connectedReceiverIdx) flags |= MF_CHECKED;
                AppendMenuW(hSub, flags, IDM_DEVICE_BASE + static_cast<UINT>(i), label.c_str());
            }
            if (connectedReceiverIdx >= 0)
                SetMenuDefaultItem(hSub, IDM_DEVICE_BASE + static_cast<UINT>(connectedReceiverIdx), FALSE);
            std::wstring speakersLabel = StringLoader::Load(IDS_MENU_SPEAKERS);
            if (speakersLabel.empty()) speakersLabel = L"Speakers";
            AppendMenuW(hMenu, MF_POPUP | MF_STRING,
                        reinterpret_cast<UINT_PTR>(hSub), speakersLabel.c_str());
        }
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    if (connectedReceiverIdx >= 0) {
        std::wstring label = StringLoader::Load(IDS_MENU_DISCONNECT);
        if (label.empty()) label = L"Disconnect";
        AppendMenuW(hMenu, MF_STRING, IDM_DISCONNECT, label.c_str());
    }

    {
        std::wstring label = StringLoader::Load(IDS_MENU_VOLUME);
        if (label.empty()) label = L"Volume";
        AppendMenuW(hMenu, MF_STRING, IDM_VOLUME, label.c_str());
    }

    {
        std::wstring label = StringLoader::Load(IDS_MENU_LOW_LATENCY);
        if (label.empty()) label = L"Low-Latency Mode";
        UINT flags = MF_STRING | (config.lowLatency ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(hMenu, flags, IDM_LOW_LATENCY, label.c_str());
    }

    {
        std::wstring label = StringLoader::Load(IDS_MENU_LAUNCH_AT_STARTUP);
        if (label.empty()) label = L"Launch at Startup";
        UINT flags = MF_STRING | (config.launchAtStartup ? MF_CHECKED : MF_UNCHECKED);
        if (config.portableMode) flags |= MF_GRAYED;
        AppendMenuW(hMenu, flags, IDM_LAUNCH_AT_STARTUP, label.c_str());
    }

    {
        std::wstring label = StringLoader::Load(IDS_MENU_OPEN_LOG_FOLDER);
        if (label.empty()) label = L"Open Log Folder";
        AppendMenuW(hMenu, MF_STRING, IDM_OPEN_LOG, label.c_str());
    }

    {
        std::wstring label = StringLoader::Load(IDS_MENU_CHECK_FOR_UPDATES);
        if (label.empty()) label = L"Check for Updates...";
        UINT flags = MF_STRING | (sparkleAvailable ? MF_ENABLED : MF_GRAYED);
        AppendMenuW(hMenu, flags, IDM_CHECK_UPDATES, label.c_str());
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    {
        std::wstring label = StringLoader::Load(IDS_MENU_QUIT);
        if (label.empty()) label = L"Quit AirBeam";
        AppendMenuW(hMenu, MF_STRING, IDM_QUIT, label.c_str());
    }

    return hMenu;
}
