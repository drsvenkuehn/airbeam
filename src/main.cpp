#include <windows.h>
#include <objbase.h>  // CoInitializeEx / CoUninitialize
#include <commctrl.h> // InitCommonControlsEx
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
        if (g_pApp) g_pApp->HandleTrayCallback(lParam);
        return 0;

    case WM_TRAY_POPUP_MENU:
        if (g_pApp) g_pApp->HandleCommand(IDM_SHOW_MENU);
        return 0;

    case WM_COMMAND:
        if (g_pApp) g_pApp->HandleCommand(LOWORD(wParam));
        return 0;

    case WM_RECEIVERS_UPDATED:
        if (g_pApp) g_pApp->HandleReceiversUpdated();
        return 0;

    // ── ConnectionController messages ─────────────────────────────────────────
    case WM_RAOP_CONNECTED:
        if (g_pApp && g_pApp->GetCC())
            g_pApp->GetCC()->OnRaopConnected(lParam);
        return 0;

    case WM_RAOP_FAILED:
        if (g_pApp && g_pApp->GetCC())
            g_pApp->GetCC()->OnRaopFailed(lParam);
        return 0;

    case WM_STREAM_STOPPED:
        if (g_pApp && g_pApp->GetCC()) {
            g_pApp->GetCC()->OnStreamStopped();
            g_pApp->HandleStreamStopped();
        }
        return 0;

    case WM_AUDIO_DEVICE_LOST:
        if (g_pApp && g_pApp->GetCC())
            g_pApp->GetCC()->OnAudioDeviceLost();
        return 0;

    case WM_SPEAKER_LOST:
        if (g_pApp && g_pApp->GetCC())
            g_pApp->GetCC()->OnSpeakerLost(reinterpret_cast<const wchar_t*>(lParam));
        return 0;

    case WM_CAPTURE_ERROR:
        if (g_pApp && g_pApp->GetCC())
            g_pApp->GetCC()->OnCaptureError();
        return 0;

    case WM_ENCODER_ERROR:
        if (g_pApp && g_pApp->GetCC())
            g_pApp->GetCC()->OnEncoderError();
        return 0;

    case WM_DEVICE_DISCOVERED:
        if (g_pApp && g_pApp->GetCC())
            g_pApp->GetCC()->OnDeviceDiscovered(lParam);
        return 0;

    case WM_STREAM_STARTED:
        if (g_pApp) g_pApp->HandleStreamStarted();
        return 0;

    // ── AppController-only messages ───────────────────────────────────────────
    case WM_BONJOUR_MISSING:
        if (g_pApp) g_pApp->HandleBonjourMissing();
        return 0;

    case WM_UPDATE_REJECTED:
        if (g_pApp) g_pApp->HandleUpdateRejected();
        return 0;

    case WM_TIMER:
        if (g_pApp) g_pApp->HandleTimer(wParam);
        return 0;

    case WM_ENDSESSION:
        if ((lParam & ENDSESSION_CLOSEAPP) && g_pApp) {
            g_pApp->Shutdown();
            g_pApp = nullptr;
            ExitProcess(0);
        }
        return 0;

    case WM_DESTROY:
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
    const bool isStartupLaunch = (strstr(lpCmdLine, "--startup") != nullptr);

    HANDLE hMutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!isStartupLaunch) {
            HWND hwndExisting = FindWindowW(kWindowClass, nullptr);
            if (hwndExisting) PostMessageW(hwndExisting, WM_TRAY_POPUP_MENU, 0, 0);
        }
        if (hMutex) CloseHandle(hMutex);
        ExitProcess(0);
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Activate Common Controls v6 (visual styles)
    INITCOMMONCONTROLSEX icc{ sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kWindowClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kWindowClass, kWindowTitle,
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        nullptr, nullptr, hInstance, nullptr);

    AppController app;
    g_pApp = &app;

    if (!app.Start(hInstance, hwnd, isStartupLaunch)) {
        g_pApp = nullptr;
        DestroyWindow(hwnd);
        CoUninitialize();
        CloseHandle(hMutex);
        return 1;
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_pApp = nullptr;
    CoUninitialize();
    CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
