#include "core/Logger.h"
#include <shellapi.h>
#include <cstdarg>
#include <cstdio>

// Pull in shell32 without requiring the caller's CMakeLists.txt to list it.
#pragma comment(lib, "shell32.lib")

// ── Directory helper ──────────────────────────────────────────────────────────
// Creates every component of a directory path that doesn't already exist.
static void CreateDirTree(const std::wstring& path)
{
    if (path.empty())
        return;
    if (CreateDirectoryW(path.c_str(), nullptr))
        return;
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS)
        return;
    if (err == ERROR_PATH_NOT_FOUND) {
        const size_t pos = path.rfind(L'\\');
        if (pos != std::wstring::npos && pos > 0) {
            CreateDirTree(path.substr(0, pos));
            CreateDirectoryW(path.c_str(), nullptr);
        }
    }
}

// ── Singleton ─────────────────────────────────────────────────────────────────
Logger& Logger::Instance()
{
    static Logger s_instance; // constructed once, thread-safe (C++11 §6.7)
    return s_instance;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
Logger::Logger()
{
    InitializeCriticalSectionAndSpinCount(&m_cs, 0x400);
    m_logDir = BuildLogDir();
    CreateDirTree(m_logDir);
    PurgeOldLogs();
    m_currentDay = TodayString();
    OpenTodayFile();
}

Logger::~Logger()
{
    EnterCriticalSection(&m_cs);
    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }
    LeaveCriticalSection(&m_cs);
    DeleteCriticalSection(&m_cs);
}

// ── Helpers ───────────────────────────────────────────────────────────────────
std::wstring Logger::BuildLogDir() const
{
    wchar_t appData[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    return std::wstring(appData) + L"\\AirBeam\\logs";
}

std::wstring Logger::TodayString() const
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[16];
    swprintf_s(buf, L"%04d%02d%02d",
               static_cast<int>(st.wYear),
               static_cast<int>(st.wMonth),
               static_cast<int>(st.wDay));
    return buf;
}

void Logger::OpenTodayFile()
{
    const std::wstring path =
        m_logDir + L"\\airbeam-" + m_currentDay + L".log";
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"a"); // append — safe across date rollovers
    m_file = f;                         // nullptr silently if we cannot write
}

void Logger::PurgeOldLogs()
{
    SYSTEMTIME nowSt;
    GetLocalTime(&nowSt);
    FILETIME nowFt;
    SystemTimeToFileTime(&nowSt, &nowFt);
    ULARGE_INTEGER nowUli;
    nowUli.HighPart = nowFt.dwHighDateTime;
    nowUli.LowPart  = nowFt.dwLowDateTime;

    constexpr ULONGLONG kSevenDays =
        static_cast<ULONGLONG>(7) * 24 * 3600 * 10'000'000ULL;

    const std::wstring pattern = m_logDir + L"\\airbeam-????????.log";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        ULARGE_INTEGER writeTime;
        writeTime.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        writeTime.LowPart  = fd.ftLastWriteTime.dwLowDateTime;
        if (nowUli.QuadPart > writeTime.QuadPart &&
            nowUli.QuadPart - writeTime.QuadPart > kSevenDays)
        {
            const std::wstring full = m_logDir + L"\\" + fd.cFileName;
            DeleteFileW(full.c_str());
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ── Public API ────────────────────────────────────────────────────────────────
// NOT RT-safe — do not call from Thread 3 (WasapiCapture) or Thread 4 (AlacEncoderThread).
// Log() acquires a CRITICAL_SECTION and calls fprintf/fflush; both are forbidden
// in the audio hot path.  All logging must come from Threads 1, 2, and 5 only.
void Logger::Log(LogLevel level, const char* msg)
{
    EnterCriticalSection(&m_cs);

    // Day rollover — close old file and open a new one.
    const std::wstring today = TodayString();
    if (today != m_currentDay) {
        if (m_file) { fclose(m_file); m_file = nullptr; }
        m_currentDay = today;
        OpenTodayFile();
    }

    if (m_file) {
        static const char* const kLevels[] = {
            "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR"
        };
        SYSTEMTIME st;
        GetLocalTime(&st);
        const DWORD tid = GetCurrentThreadId();
        fprintf(m_file,
                "%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] [%04lu] %s\n",
                static_cast<int>(st.wYear),
                static_cast<int>(st.wMonth),
                static_cast<int>(st.wDay),
                static_cast<int>(st.wHour),
                static_cast<int>(st.wMinute),
                static_cast<int>(st.wSecond),
                static_cast<int>(st.wMilliseconds),
                kLevels[static_cast<int>(level)],
                static_cast<unsigned long>(tid),
                msg);
        fflush(m_file);
    }

    LeaveCriticalSection(&m_cs);
}

void Logger::Logf(LogLevel level, const char* fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    Log(level, buf);
}

void Logger::OpenLogFolder()
{
    ShellExecuteW(nullptr, L"open",
                  m_logDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring Logger::LogDirectory() const
{
    return m_logDir;
}
