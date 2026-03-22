#include <windows.h>
#include <objbase.h>  // CoInitializeEx / CoUninitialize
#include <cstring>    // strstr

#include "core/AppController.h"
#include "core/Messages.h"
#include "core/Commands.h"

// ── Constants ────────────────────────────────────────────────────────────────

static constexpr wchar_t kMutexName[]   = L"Global\\AirBeam_SingleInstance";
static constexpr wchar_t kWindowClass[] = L"AirBeamTrayWnd";
static constexpr wchar_t kWindowTitle[] = L"AirBeam";

// ── Global AppController pointer (used by WndProc) ───────────────────────────

static AppController* g_pApp = nullptr;

// ── Window procedure ─────────────────────────────────────────────────────────

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_TRAY_CALLBACK:
        // Shell_NotifyIcon callback; lParam carries the notification event.
        if (g_pApp) {
            g_pApp->HandleTrayCallback(lParam);
        }
        return 0;

    case WM_TRAY_POPUP_MENU:
        // Sent by a second instance (non-startup) to bring the menu into focus.
        if (g_pApp) {
            g_pApp->HandleCommand(IDM_SHOW_MENU);
        }
        return 0;

    case WM_COMMAND:
        if (g_pApp) {
            g_pApp->HandleCommand(LOWORD(wParam));
        }
        return 0;

    case WM_RECEIVERS_UPDATED:
        if (g_pApp) {
            g_pApp->HandleReceiversUpdated();
        }
        return 0;

    case WM_RAOP_CONNECTED:
        if (g_pApp) {
            g_pApp->HandleRaopConnected(lParam);
        }
        return 0;

    case WM_RAOP_FAILED:
        if (g_pApp) {
            g_pApp->HandleRaopFailed(lParam);
        }
        return 0;

    case WM_BONJOUR_MISSING:
        if (g_pApp) {
            g_pApp->HandleBonjourMissing();
        }
        return 0;

    case WM_DEFAULT_DEVICE_CHANGED:
        if (g_pApp) {
            g_pApp->HandleDefaultDeviceChanged();
        }
        return 0;

    case WM_UPDATE_REJECTED:
        if (g_pApp) {
            g_pApp->HandleUpdateRejected();
        }
        return 0;

    case WM_ENDSESSION:
        if ((lParam & ENDSESSION_CLOSEAPP) && g_pApp) {
            g_pApp->Shutdown();
            g_pApp = nullptr;
            ExitProcess(0);
        }
        return 0;

    case WM_DESTROY:
        // Tear down subsystems (removes tray icon, cleans up WinSparkle).
        if (g_pApp) {
            g_pApp->Shutdown();
            g_pApp = nullptr;
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── WinMain ──────────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE /*hPrevInstance*/,
                   LPSTR     lpCmdLine,
                   int       /*nCmdShow*/)
{
    // ── Parse --startup flag ─────────────────────────────────────────────────
    // Set by the HKCU\Run entry so OS-triggered startup launches don't steal
    // focus.  Must be parsed before the single-instance check (FR-013).
    const bool isStartupLaunch = (strstr(lpCmdLine, "--startup") != nullptr);

    // ── Single-instance enforcement ──────────────────────────────────────────
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Another instance is running.  If this is NOT a startup launch, tell
        // the existing instance to show its tray menu so the user sees it.
        if (!isStartupLaunch) {
            HWND hwndExisting = FindWindowW(kWindowClass, nullptr);
            if (hwndExisting) {
                PostMessageW(hwndExisting, WM_TRAY_POPUP_MENU, 0, 0);
            }
        }
        if (hMutex) {
            CloseHandle(hMutex);
        }
        ExitProcess(0);
    }

    // ── COM initialisation ───────────────────────────────────────────────────
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // ── Register window class ────────────────────────────────────────────────
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    // ── Create hidden message window ─────────────────────────────────────────
    // A regular (non-message-only) top-level window so FindWindowW in a future
    // second instance can locate it.  Never shown (ShowWindow is never called).
    HWND hwnd = CreateWindowExW(
        0,
        kWindowClass,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        nullptr, nullptr, hInstance, nullptr);

    // ── Initialise application ───────────────────────────────────────────────
    AppController app;
    g_pApp = &app;

    if (!app.Start(hInstance, hwnd, isStartupLaunch)) {
        // Fatal init failure — clean up and exit.
        g_pApp = nullptr;
        DestroyWindow(hwnd);
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    // ── Message loop ─────────────────────────────────────────────────────────
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // g_pApp was cleared inside WM_DESTROY; Shutdown() was called there too.
    g_pApp = nullptr;

    // ── Cleanup ──────────────────────────────────────────────────────────────
    CoUninitialize();
    CloseHandle(hMutex);

    return static_cast<int>(msg.wParam);
}
