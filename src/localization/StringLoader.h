#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

// StringLoader wraps LoadStringW with locale detection.
//
// All locale string tables are compiled into the same binary with distinct
// LANGUAGE tags (via resources/locales/strings_*.rc).  Windows' LoadStringW
// automatically selects the best-matching language block based on the current
// thread UI language, so Init() sets the thread locale and Load() just calls
// LoadStringW.  If a string is not found in the detected locale (e.g. only
// the English RC was compiled in), Load() falls back to the English string.
class StringLoader
{
public:
    // Call once from WinMain before any UI is created.
    // Detects the Windows user locale and primes the thread UI language.
    static void Init(HINSTANCE hInst);

    // Load a string resource by ID.  Returns an empty wstring on total failure.
    static std::wstring Load(UINT id);

private:
    // Maps a Windows BCP-47 locale tag prefix to the supported locale suffix.
    // Returns "en" for any unmapped prefix (English fallback).
    static const wchar_t* MapLocaleTag(const wchar_t* localeName);

    static HINSTANCE s_hInst;
    static LANGID    s_langId;   // resolved LANGID used for LoadStringEx
};
