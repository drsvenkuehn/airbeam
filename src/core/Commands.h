#pragma once
#include <windows.h>

// ── Menu command IDs ─────────────────────────────────────────────────────────
// Range 1000–1999: static menu items
constexpr UINT IDM_QUIT                  = 1000;
constexpr UINT IDM_CHECK_UPDATES         = 1001;
constexpr UINT IDM_OPEN_LOG_FOLDER       = 1002;
constexpr UINT IDM_LOW_LATENCY_TOGGLE    = 1003;
constexpr UINT IDM_LAUNCH_STARTUP_TOGGLE = 1004;
constexpr UINT IDM_VOLUME                = 1005;
constexpr UINT IDM_SHOW_MENU             = 1006; // pseudo-command: show tray menu at cursor
constexpr UINT IDM_VOLUME_LABEL          = 1007; // owner-drawn "Volume  XX%" label row (non-interactive)
constexpr UINT IDM_DISCONNECT            = 1008; // disconnect from current speaker

// Range 2000–2099: dynamic receiver items (index = id - IDM_DEVICE_BASE)
constexpr UINT IDM_DEVICE_BASE           = 2000;
constexpr UINT IDM_DEVICE_MAX_COUNT      = 100;
