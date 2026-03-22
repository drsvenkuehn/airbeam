#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "ui/StartupRegistry.h"

namespace {
    constexpr wchar_t kRunKey[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kValueName[] = L"AirBeam";

    std::wstring GetExePath() {
        wchar_t buf[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return buf;
    }
}

namespace StartupRegistry {

bool IsEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD type = 0, size = 0;
    bool found = (RegQueryValueExW(hKey, kValueName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return found;
}

void Enable() {
    std::wstring value = L"\"" + GetExePath() + L"\" --startup";

    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr)
        != ERROR_SUCCESS)
        return;

    RegSetValueExW(hKey, kValueName, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
}

void Disable() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;
    RegDeleteValueW(hKey, kValueName);
    RegCloseKey(hKey);
}

} // namespace StartupRegistry
