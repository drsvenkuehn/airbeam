#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include "update/SparkleIntegration.h"
#include "localization/StringLoader.h"
#include "resource_ids.h"
#include "core/Logger.h"
#include "core/Messages.h"

// WinSparkle C API function pointer types
using PFN_set_appcast_url                   = void(*)(const char*);
using PFN_set_eddsa_pub_key                 = void(*)(const char*);
using PFN_set_app_details                   = void(*)(const wchar_t*, const wchar_t*, const wchar_t*);
using PFN_init                              = void(*)();
using PFN_cleanup                           = void(*)();
using PFN_check_update_with_ui              = void(*)();
using PFN_set_automatic_check_for_updates   = void(*)(int);
using PFN_set_did_not_install_update_callback = void(*)(void(*)());

namespace {
    // Helper: convert wstring to UTF-8
    std::string WstrToUtf8(const std::wstring& ws) {
        if (ws.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len <= 0) return {};
        std::string s(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), len, nullptr, nullptr);
        return s;
    }
}

static HWND s_hwndMain = nullptr;

void SparkleIntegration::SetMainHwnd(HWND hwnd) {
    s_hwndMain = hwnd;
}

bool SparkleIntegration::Init(const Config& config) {
    hDll_ = LoadLibraryW(L"WinSparkle.dll");
    if (!hDll_) {
        LOG_WARN("SparkleIntegration: WinSparkle.dll not found — auto-update disabled");
        return false;
    }

    // Resolve function pointers
    auto set_appcast  = reinterpret_cast<PFN_set_appcast_url>(
        GetProcAddress(hDll_, "win_sparkle_set_appcast_url"));
    auto set_eddsa    = reinterpret_cast<PFN_set_eddsa_pub_key>(
        GetProcAddress(hDll_, "win_sparkle_set_eddsa_pub_key"));
    auto set_details  = reinterpret_cast<PFN_set_app_details>(
        GetProcAddress(hDll_, "win_sparkle_set_app_details"));
    auto ws_init      = reinterpret_cast<PFN_init>(
        GetProcAddress(hDll_, "win_sparkle_init"));
    auto ws_cleanup   = reinterpret_cast<PFN_cleanup>(
        GetProcAddress(hDll_, "win_sparkle_cleanup"));
    auto ws_check     = reinterpret_cast<PFN_check_update_with_ui>(
        GetProcAddress(hDll_, "win_sparkle_check_update_with_ui"));
    auto ws_auto      = reinterpret_cast<PFN_set_automatic_check_for_updates>(
        GetProcAddress(hDll_, "win_sparkle_set_automatic_check_for_updates"));

    // Require the critical init functions
    if (!set_appcast || !ws_init || !ws_cleanup || !ws_check) {
        LOG_WARN("SparkleIntegration: WinSparkle.dll missing required exports — auto-update disabled");
        FreeLibrary(hDll_);
        hDll_ = nullptr;
        return false;
    }

    // Store cleanup/check pointers for later
    pfnCleanup_      = reinterpret_cast<void*>(ws_cleanup);
    pfnCheckUpdate_  = reinterpret_cast<void*>(ws_check);
    pfnAutoCheck_    = reinterpret_cast<void*>(ws_auto);

    // 1. Appcast URL (compile-time constant)
    set_appcast(AIRBEAM_APPCAST_URL);

    // 2. Ed25519 public key from string resource
    if (set_eddsa) {
        std::wstring pubkey = StringLoader::Load(IDS_SPARKLE_PUBKEY);
        std::string  pubkeyUtf8 = WstrToUtf8(pubkey);
        if (!pubkeyUtf8.empty()) {
            set_eddsa(pubkeyUtf8.c_str());
        }
    }

    // 3. App details
    if (set_details) {
        set_details(L"AirBeam Contributors", L"AirBeam", L"1.0.0");
    }

    // 4. Initialize WinSparkle
    ws_init();

    // 5. Register update-rejected callback if the export is available
    {
        auto set_did_not_install = reinterpret_cast<PFN_set_did_not_install_update_callback>(
            GetProcAddress(hDll_, "win_sparkle_set_did_not_install_update_callback"));
        if (set_did_not_install) {
            set_did_not_install([]() {
                if (s_hwndMain) PostMessage(s_hwndMain, WM_UPDATE_REJECTED, 0, 0);
            });
        }
    }

    // 6. Respect autoUpdate preference
    if (!config.autoUpdate && ws_auto) {
        ws_auto(0);
    }

    LOG_INFO("SparkleIntegration: WinSparkle initialised successfully");
    return true;
}

void SparkleIntegration::CheckForUpdates() {
    if (!hDll_ || !pfnCheckUpdate_) return;
    reinterpret_cast<PFN_check_update_with_ui>(pfnCheckUpdate_)();
}

// T096 verified: Cleanup() calls win_sparkle_cleanup() then FreeLibrary.
// It is called from AppController::Shutdown(), which is invoked from WM_DESTROY
// in main.cpp, guaranteeing orderly teardown before the process exits.
void SparkleIntegration::Cleanup() {
    if (!hDll_) return;
    if (pfnCleanup_) {
        reinterpret_cast<PFN_cleanup>(pfnCleanup_)();
    }
    FreeLibrary(hDll_);
    hDll_           = nullptr;
    pfnCleanup_     = nullptr;
    pfnCheckUpdate_ = nullptr;
    pfnAutoCheck_   = nullptr;
}
