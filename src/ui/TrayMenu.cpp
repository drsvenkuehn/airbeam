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
#include "localization/StringLoader.h"
#include "discovery/AirPlayReceiver.h"
#include "resource_ids.h"

// TrayMenu does not own a receiver list directly; callers pass the current
// snapshot in Show().  For the Phase 1 skeleton, receivers is always empty.
// The full receiver-list integration is wired in Phase 3 (T037 / T051).

void TrayMenu::Init(HINSTANCE hInst) {
    hInst_ = hInst;
}

UINT TrayMenu::Show(HWND hwnd, const Config& config, bool sparkleAvailable) {
    return Show(hwnd, config, sparkleAvailable, {}, -1);
}

UINT TrayMenu::Show(HWND hwnd, const Config& config, bool sparkleAvailable,
                    const std::vector<AirPlayReceiver>& receivers, int connectedReceiverIdx) {
    // Build a fresh menu on every call so state is always current.
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return 0;

    // ── Receiver entries ──────────────────────────────────────────────────────
    if (receivers.empty()) {
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"No speakers found");
    } else {
        for (int i = 0; i < static_cast<int>(receivers.size()); ++i) {
            const auto& r = receivers[i];
            if (!r.isAirPlay1Compatible) {
                std::wstring label = r.displayName + L" \u2014 " +
                                     StringLoader::Load(IDS_LABEL_AIRPLAY2_UNSUPPORTED);
                AppendMenuW(hMenu, MF_STRING | MF_GRAYED,
                            IDM_DEVICE_BASE + static_cast<UINT>(i), label.c_str());
            } else {
                UINT flags = MF_STRING;
                if (i == connectedReceiverIdx) flags |= MF_CHECKED;
                AppendMenuW(hMenu, flags,
                            IDM_DEVICE_BASE + static_cast<UINT>(i),
                            r.displayName.c_str());
            }
        }
    }
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Volume ───────────────────────────────────────────────────────────────
    std::wstring volLabel = StringLoader::Load(IDS_MENU_VOLUME);
    if (volLabel.empty()) volLabel = L"Volume";
    AppendMenuW(hMenu, MF_STRING, IDM_VOLUME, volLabel.c_str());

    // ── Low-latency mode toggle ──────────────────────────────────────────────
    std::wstring llLabel = StringLoader::Load(IDS_MENU_LOW_LATENCY);
    if (llLabel.empty()) llLabel = L"Low-Latency Mode";
    UINT llFlags = MF_STRING | (config.lowLatency ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(hMenu, llFlags, IDM_LOW_LATENCY, llLabel.c_str());

    // ── Launch at startup toggle ─────────────────────────────────────────────
    std::wstring startupLabel = StringLoader::Load(IDS_MENU_LAUNCH_AT_STARTUP);
    if (startupLabel.empty()) startupLabel = L"Launch at Startup";
    UINT startupFlags = MF_STRING | (config.launchAtStartup ? MF_CHECKED : MF_UNCHECKED);
    // Hide in portable mode (no persistent install path)
    if (config.portableMode) startupFlags |= MF_GRAYED;
    AppendMenuW(hMenu, startupFlags, IDM_LAUNCH_AT_STARTUP, startupLabel.c_str());

    // ── Open log folder ──────────────────────────────────────────────────────
    std::wstring logLabel = StringLoader::Load(IDS_MENU_OPEN_LOG_FOLDER);
    if (logLabel.empty()) logLabel = L"Open Log Folder";
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_LOG, logLabel.c_str());

    // ── Check for Updates ────────────────────────────────────────────────────
    std::wstring updateLabel = StringLoader::Load(IDS_MENU_CHECK_FOR_UPDATES);
    if (updateLabel.empty()) updateLabel = L"Check for Updates...";
    // Always present, enabled regardless of stream state (FR-018)
    UINT updateFlags = MF_STRING | (sparkleAvailable ? MF_ENABLED : MF_GRAYED);
    AppendMenuW(hMenu, updateFlags, IDM_CHECK_UPDATES, updateLabel.c_str());

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // ── Quit ─────────────────────────────────────────────────────────────────
    std::wstring quitLabel = StringLoader::Load(IDS_MENU_QUIT);
    if (quitLabel.empty()) quitLabel = L"Quit AirBeam";
    AppendMenuW(hMenu, MF_STRING, IDM_QUIT, quitLabel.c_str());

    // ── Show at cursor position ───────────────────────────────────────────────
    POINT pt = {};
    GetCursorPos(&pt);

    // Required before TrackPopupMenu so the menu dismisses on click-away
    SetForegroundWindow(hwnd);

    UINT cmd = static_cast<UINT>(TrackPopupMenu(
        hMenu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
        pt.x, pt.y,
        0,
        hwnd,
        nullptr));

    DestroyMenu(hMenu);
    return cmd;
}
