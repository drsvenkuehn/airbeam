#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "localization/StringLoader.h"

// Static member definitions
HINSTANCE StringLoader::s_hInst  = nullptr;
LANGID    StringLoader::s_langId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

// ---------------------------------------------------------------------------
// Locale tag → supported suffix mapping
//   Supported: de, fr, es, ja, zh-Hans, ko
//   Everything else (including en) → en
// ---------------------------------------------------------------------------
const wchar_t* StringLoader::MapLocaleTag(const wchar_t* localeName)
{
    struct Mapping { const wchar_t* prefix; const wchar_t* suffix; LANGID langId; };
    static constexpr Mapping kMappings[] = {
        { L"de",      L"de",      MAKELANGID(LANG_GERMAN,   SUBLANG_GERMAN)             },
        { L"fr",      L"fr",      MAKELANGID(LANG_FRENCH,   SUBLANG_FRENCH)             },
        { L"es",      L"es",      MAKELANGID(LANG_SPANISH,  SUBLANG_SPANISH)            },
        { L"ja",      L"ja",      MAKELANGID(LANG_JAPANESE, SUBLANG_DEFAULT)            },
        { L"zh-Hans", L"zh-Hans", MAKELANGID(LANG_CHINESE,  SUBLANG_CHINESE_SIMPLIFIED) },
        { L"ko",      L"ko",      MAKELANGID(LANG_KOREAN,   SUBLANG_DEFAULT)            },
    };

    if (!localeName) return L"en";

    for (const auto& m : kMappings)
    {
        // Match prefix: "de" matches "de-DE", "de-AT", etc.
        // "zh-Hans" must be tested before "zh" (longer prefix wins).
        size_t prefixLen = wcslen(m.prefix);
        if (wcsncmp(localeName, m.prefix, prefixLen) == 0)
        {
            // Ensure the match is at a tag boundary (end of string or '-')
            wchar_t next = localeName[prefixLen];
            if (next == L'\0' || next == L'-')
            {
                s_langId = m.langId;
                return m.suffix;
            }
        }
    }

    s_langId = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    return L"en";
}

// ---------------------------------------------------------------------------
void StringLoader::Init(HINSTANCE hInst)
{
    s_hInst = hInst;

    wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0)
    {
        MapLocaleTag(localeName);   // sets s_langId as a side-effect
    }

    // Prime the thread UI language so that LoadStringW picks the right
    // LANGUAGE-tagged STRINGTABLE block when multiple locales are compiled in.
    ULONG numLangs = 1;
    wchar_t langBuffer[LOCALE_NAME_MAX_LENGTH * 2] = {};
    // Build a multi-string: "<locale-name>\0\0"
    if (GetUserDefaultLocaleName(langBuffer, LOCALE_NAME_MAX_LENGTH) > 0)
    {
        SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, langBuffer, &numLangs);
    }
}

// ---------------------------------------------------------------------------
std::wstring StringLoader::Load(UINT id)
{
    if (!s_hInst) return {};

    // LoadStringW returns a pointer into the resource section; the buffer
    // trick (passing nullptr with length 0) is not portable to all runtimes,
    // so we use a reasonably large on-stack buffer.
    wchar_t buf[1024] = {};
    int len = LoadStringW(s_hInst, id, buf, static_cast<int>(std::size(buf)));
    if (len > 0)
        return std::wstring(buf, static_cast<size_t>(len));

    // Fallback: if the string was not found (non-English locale compiled in
    // without that language block), retry with the neutral English LANGID.
    // We do this by temporarily overriding the thread language.
    if (s_langId != MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US))
    {
        wchar_t enTag[] = L"en-US\0";
        ULONG numLangs = 1;
        SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, enTag, &numLangs);

        len = LoadStringW(s_hInst, id, buf, static_cast<int>(std::size(buf)));

        // Restore preferred language
        wchar_t restore[LOCALE_NAME_MAX_LENGTH * 2] = {};
        GetUserDefaultLocaleName(restore, LOCALE_NAME_MAX_LENGTH);
        SetThreadPreferredUILanguages(MUI_LANGUAGE_NAME, restore, &numLangs);

        if (len > 0)
            return std::wstring(buf, static_cast<size_t>(len));
    }

    return {};
}
