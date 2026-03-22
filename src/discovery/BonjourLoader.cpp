#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include "discovery/BonjourLoader.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Resolve all GetProcAddress calls into funcs.
/// Returns true only when all six critical pointers are non-null.
bool ResolveProcs(HMODULE hDll, BonjourFuncs& funcs)
{
    funcs.Browse = reinterpret_cast<decltype(funcs.Browse)>(
        GetProcAddress(hDll, "DNSServiceBrowse"));

    funcs.Resolve = reinterpret_cast<decltype(funcs.Resolve)>(
        GetProcAddress(hDll, "DNSServiceResolve"));

    funcs.GetAddrInfo = reinterpret_cast<decltype(funcs.GetAddrInfo)>(
        GetProcAddress(hDll, "DNSServiceGetAddrInfo"));

    funcs.RefSockFD = reinterpret_cast<decltype(funcs.RefSockFD)>(
        GetProcAddress(hDll, "DNSServiceRefSockFD"));

    funcs.ProcessResult = reinterpret_cast<decltype(funcs.ProcessResult)>(
        GetProcAddress(hDll, "DNSServiceProcessResult"));

    funcs.RefDeallocate = reinterpret_cast<decltype(funcs.RefDeallocate)>(
        GetProcAddress(hDll, "DNSServiceRefDeallocate"));

    // Non-critical: TXTRecordGetValuePtr is optional (we self-parse in TxtRecord.cpp)
    funcs.TXTRecordGetValuePtr = reinterpret_cast<decltype(funcs.TXTRecordGetValuePtr)>(
        GetProcAddress(hDll, "TXTRecordGetValuePtr"));

    return funcs.Browse      && funcs.Resolve    && funcs.GetAddrInfo &&
           funcs.RefSockFD   && funcs.ProcessResult && funcs.RefDeallocate;
}

/// Try to locate dnssd.dll via the Bonjour service's ImagePath registry value.
/// Returns an HMODULE on success, nullptr on failure.
HMODULE TryLoadViaRegistry()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Services\\Bonjour Service",
            0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return nullptr;
    }

    wchar_t imagePath[MAX_PATH] = {};
    DWORD   size = sizeof(imagePath);
    DWORD   type = REG_SZ;
    bool    ok   = (RegQueryValueExW(hKey, L"ImagePath", nullptr, &type,
                                     reinterpret_cast<LPBYTE>(imagePath),
                                     &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);

    if (!ok) return nullptr;

    // Strip the executable filename and replace with dnssd.dll
    wchar_t* lastSlash = wcsrchr(imagePath, L'\\');
    if (!lastSlash) return nullptr;

    const std::size_t dirLen  = static_cast<std::size_t>(lastSlash - imagePath + 1);
    const std::size_t dllName = 9; // "dnssd.dll"
    if (dirLen + dllName >= MAX_PATH) return nullptr;

    wcscpy_s(lastSlash + 1, MAX_PATH - dirLen, L"dnssd.dll");
    return LoadLibraryW(imagePath);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BonjourLoader::Load
// ---------------------------------------------------------------------------

bool BonjourLoader::Load()
{
    // 1. Try default search order (System32, PATH, …)
    hDll_ = LoadLibraryW(L"dnssd.dll");

    // 2. If that failed, look up the Bonjour service install directory
    if (!hDll_)
        hDll_ = TryLoadViaRegistry();

    if (!hDll_) return false;

    // 3. Resolve function pointers; unload on partial resolution
    if (!ResolveProcs(hDll_, funcs_))
    {
        Unload();
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// BonjourLoader::Unload
// ---------------------------------------------------------------------------

void BonjourLoader::Unload()
{
    if (hDll_)
    {
        FreeLibrary(hDll_);
        hDll_ = nullptr;
    }
    funcs_ = BonjourFuncs{};
}
