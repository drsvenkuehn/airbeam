// src/ui/PinDialog.cpp
// Win32 modal dialog for HAP 6-digit PIN entry.
// §III: Win32 API only — no external UI framework.
// §III-A: No green; only system colours used here.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <memory>
#include <cstdint>
#include <cstdio>

#include "ui/PinDialog.h"
#include "resource_ids.h"
#include "localization/StringLoader.h"

#pragma comment(lib, "comctl32.lib")

// ────────────────────────────────────────────────────────────────────────────
// Dialog context
// ────────────────────────────────────────────────────────────────────────────

namespace {

struct PinDlgContext {
    std::wstring  deviceName;
    std::string   pin;  // result
};

// Control IDs
static constexpr int IDC_PIN_EDIT   = 1001;
static constexpr int IDC_LABEL      = 1002;

INT_PTR CALLBACK PinDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    PinDlgContext* ctx = reinterpret_cast<PinDlgContext*>(
        GetWindowLongPtrW(hDlg, DWLP_USER));

    switch (msg) {
    case WM_INITDIALOG: {
        ctx = reinterpret_cast<PinDlgContext*>(lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, lParam);

        SetWindowTextW(hDlg, L"AirBeam - Pairing PIN");

        std::wstring prompt;
        const std::wstring fmt = StringLoader::Load(IDS_AP2_PIN_PROMPT);
        if (!fmt.empty()) {
            wchar_t buf[256];
            _snwprintf_s(buf, 256, _TRUNCATE, fmt.c_str(), ctx->deviceName.c_str());
            prompt = buf;
        } else {
            prompt = L"Enter the PIN shown on " + ctx->deviceName + L":";
        }
        SetDlgItemTextW(hDlg, IDC_LABEL, prompt.c_str());

        HWND hEdit = GetDlgItem(hDlg, IDC_PIN_EDIT);
        SendMessageW(hEdit, EM_SETLIMITTEXT, 8, 0);

        // Center on screen
        RECT rc;
        GetWindowRect(hDlg, &rc);
        const int cx = GetSystemMetrics(SM_CXSCREEN);
        const int cy = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hDlg, nullptr,
                     (cx - (rc.right - rc.left)) / 2,
                     (cy - (rc.bottom - rc.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);

        SetFocus(hEdit);
        return FALSE;
    }

    case WM_COMMAND: {
        const WORD id = LOWORD(wParam);
        if (id == IDOK) {
            if (!ctx) { EndDialog(hDlg, IDCANCEL); return TRUE; }
            wchar_t pinBuf[16] = {};
            GetDlgItemTextW(hDlg, IDC_PIN_EDIT, pinBuf, 16);
            const std::wstring pinW = pinBuf;
            bool valid = pinW.size() >= 4 && pinW.size() <= 8;
            for (wchar_t c : pinW) {
                if (!iswdigit(c)) { valid = false; break; }
            }
            if (!valid) {
                MessageBoxW(hDlg, L"Please enter a 4-8 digit PIN.",
                            L"Invalid PIN", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            const int n = WideCharToMultiByte(CP_UTF8, 0, pinW.c_str(), -1,
                                              nullptr, 0, nullptr, nullptr);
            ctx->pin.resize(static_cast<size_t>(n - 1));
            WideCharToMultiByte(CP_UTF8, 0, pinW.c_str(), -1,
                                &ctx->pin[0], n, nullptr, nullptr);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            if (ctx) ctx->pin.clear();
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// ── Build minimal DLGTEMPLATE in a fixed-size stack buffer ──────────────────
// Avoids heap allocation and std::make_unique.

#pragma pack(push, 1)
struct DlgBuf {
    DLGTEMPLATE hdr;
    WORD        menuId;
    WORD        classId;
    WCHAR       title[28];   // "AirBeam - Pairing PIN\0"
    WORD        fontSize;
    WCHAR       fontName[10]; // "Segoe UI\0"
    // Items follow (each DWORD-aligned)
    DWORD       pad1;
    DLGITEMTEMPLATE item1;
    WORD        cls1[2];     // 0xFFFF 0x0082 = Static
    WCHAR       text1[16];   // "Enter PIN:"
    WORD        cdata1;
    DWORD       pad2;
    DLGITEMTEMPLATE item2;
    WORD        cls2[2];     // 0xFFFF 0x0081 = Edit
    WORD        text2;       // 0 = empty
    WORD        cdata2;
    DWORD       pad3;
    DLGITEMTEMPLATE item3;
    WORD        cls3[2];     // 0xFFFF 0x0080 = Button
    WCHAR       text3[3];    // "OK\0"
    WORD        cdata3;
    DWORD       pad4;
    DLGITEMTEMPLATE item4;
    WORD        cls4[2];     // 0xFFFF 0x0080 = Button
    WCHAR       text4[7];    // "Cancel\0"
    WORD        cdata4;
};
#pragma pack(pop)

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────────
// PinDialog::Show
// ────────────────────────────────────────────────────────────────────────────

/*static*/ std::string PinDialog::Show(HWND parent, const std::wstring& deviceName)
{
    // Build dialog template manually in a stack buffer (C++17 aggregate init)
    static const wchar_t kTitle[]    = L"AirBeam - Pairing PIN";
    static const wchar_t kFontName[] = L"Segoe UI";

    DlgBuf buf{};
    // Header
    buf.hdr.style           = DS_CENTER | DS_MODALFRAME | DS_SETFONT |
                              WS_POPUP | WS_CAPTION | WS_SYSMENU;
    buf.hdr.dwExtendedStyle = 0;
    buf.hdr.cdit            = 4;
    buf.hdr.x               = 0; buf.hdr.y  = 0;
    buf.hdr.cx              = 200; buf.hdr.cy = 100;
    buf.menuId   = 0;
    buf.classId  = 0;
    wcsncpy_s(buf.title, kTitle, _TRUNCATE);
    buf.fontSize = 9;
    wcsncpy_s(buf.fontName, kFontName, _TRUNCATE);

    // Item 1: Static label (IDC_LABEL)
    buf.item1.style           = WS_CHILD | WS_VISIBLE | SS_LEFT;
    buf.item1.dwExtendedStyle = 0;
    buf.item1.x = 5; buf.item1.y = 8; buf.item1.cx = 190; buf.item1.cy = 12;
    buf.item1.id = static_cast<WORD>(IDC_LABEL);
    buf.cls1[0] = 0xFFFF; buf.cls1[1] = 0x0082;  // STATIC
    wcsncpy_s(buf.text1, L"Enter PIN:", _TRUNCATE);
    buf.cdata1 = 0;

    // Item 2: Edit (IDC_PIN_EDIT)
    buf.item2.style           = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP |
                                ES_CENTER | ES_NUMBER;
    buf.item2.dwExtendedStyle = 0;
    buf.item2.x = 60; buf.item2.y = 26; buf.item2.cx = 80; buf.item2.cy = 12;
    buf.item2.id = static_cast<WORD>(IDC_PIN_EDIT);
    buf.cls2[0] = 0xFFFF; buf.cls2[1] = 0x0081;  // EDIT
    buf.text2 = 0;
    buf.cdata2 = 0;

    // Item 3: OK button (IDOK)
    buf.item3.style           = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON;
    buf.item3.dwExtendedStyle = 0;
    buf.item3.x = 35; buf.item3.y = 70; buf.item3.cx = 55; buf.item3.cy = 14;
    buf.item3.id = IDOK;
    buf.cls3[0] = 0xFFFF; buf.cls3[1] = 0x0080;  // BUTTON
    wcsncpy_s(buf.text3, L"OK", _TRUNCATE);
    buf.cdata3 = 0;

    // Item 4: Cancel button (IDCANCEL)
    buf.item4.style           = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    buf.item4.dwExtendedStyle = 0;
    buf.item4.x = 110; buf.item4.y = 70; buf.item4.cx = 55; buf.item4.cy = 14;
    buf.item4.id = IDCANCEL;
    buf.cls4[0] = 0xFFFF; buf.cls4[1] = 0x0080;  // BUTTON
    wcsncpy_s(buf.text4, L"Cancel", _TRUNCATE);
    buf.cdata4 = 0;

    PinDlgContext ctx;
    ctx.deviceName = deviceName;

    const HINSTANCE hInst = GetModuleHandleW(nullptr);
    const INT_PTR result = DialogBoxIndirectParamW(
        hInst,
        reinterpret_cast<LPCDLGTEMPLATEW>(&buf),
        parent,
        PinDlgProc,
        reinterpret_cast<LPARAM>(&ctx));

    if (result != IDOK) return {};
    return ctx.pin;
}
