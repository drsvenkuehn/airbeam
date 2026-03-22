#pragma once
#include <windows.h>

constexpr UINT WM_TRAY_CALLBACK          = WM_APP + 1;
constexpr UINT WM_TRAY_POPUP_MENU        = WM_APP + 2;
constexpr UINT WM_RAOP_CONNECTED         = WM_APP + 3;
constexpr UINT WM_RAOP_FAILED            = WM_APP + 4;
constexpr UINT WM_RECEIVERS_UPDATED      = WM_APP + 5;
constexpr UINT WM_DEFAULT_DEVICE_CHANGED = WM_APP + 6;
constexpr UINT WM_BONJOUR_MISSING        = WM_APP + 7;
constexpr UINT WM_TEARDOWN_COMPLETE      = WM_APP + 8;
constexpr UINT WM_UPDATE_REJECTED        = WM_APP + 9;
