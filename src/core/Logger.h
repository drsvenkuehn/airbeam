#pragma once
#include <windows.h>
#include <string>

// RT CONTRACT: Logger MUST NEVER be called from Thread 3 (WasapiCapture) or
// Thread 4 (AlacEncoderThread).  Those threads run under MMCSS real-time
// scheduling; any file I/O from them causes unbounded priority inversions and
// violates the RT budget.  Enforce by defining AIRBEAM_NO_LOGGER before
// including this header in WasapiCapture.cpp and AlacEncoderThread.cpp.

// NOTE: enum values use "k" prefix to avoid colliding with the Windows GDI
// macro  #define ERROR 0  that is emitted by <wingdi.h>.
enum class LogLevel { kTrace = 0, kDebug, kInfo, kWarn, kError };

class Logger
{
public:
    // Meyer's singleton — thread-safe initialisation under C++11 and MSVC
    // /Zc:threadSafeInit (on by default since VS 2015).
    static Logger& Instance();

    void Log(LogLevel level, const char* msg);
    void Logf(LogLevel level, const char* fmt, ...);

    // Wide-string variant — converts to UTF-8 then logs.
    // Use for callers (e.g. CC_TRACE) that build messages with wchar_t format strings.
    void LogW(LogLevel level, const wchar_t* wfmt, ...);

    // Opens %APPDATA%\AirBeam\logs\ in Windows Explorer.
    void OpenLogFolder();

    // Returns the log directory path (for callers that want to open it themselves).
    std::wstring LogDirectory() const;

    Logger(const Logger&)            = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    void         OpenTodayFile();
    void         PurgeOldLogs();
    std::wstring BuildLogDir() const;
    std::wstring TodayString() const; // "YYYYMMDD"

    CRITICAL_SECTION m_cs;
    FILE*            m_file       = nullptr;
    std::wstring     m_logDir;
    std::wstring     m_currentDay; // "YYYYMMDD" of the currently open file
};

// ── Logging macros ────────────────────────────────────────────────────────────
// Define AIRBEAM_NO_LOGGER before including this header to compile all
// LOG_* macros away (required for real-time audio threads).
#ifndef AIRBEAM_NO_LOGGER
#  define LOG_TRACE(fmt, ...) Logger::Instance().Logf(LogLevel::kTrace, fmt, ##__VA_ARGS__)
#  define LOG_DEBUG(fmt, ...) Logger::Instance().Logf(LogLevel::kDebug, fmt, ##__VA_ARGS__)
#  define LOG_INFO(fmt, ...)  Logger::Instance().Logf(LogLevel::kInfo,  fmt, ##__VA_ARGS__)
#  define LOG_WARN(fmt, ...)  Logger::Instance().Logf(LogLevel::kWarn,  fmt, ##__VA_ARGS__)
#  define LOG_ERROR(fmt, ...) Logger::Instance().Logf(LogLevel::kError, fmt, ##__VA_ARGS__)
#else
#  define LOG_TRACE(...)  ((void)0)
#  define LOG_DEBUG(...)  ((void)0)
#  define LOG_INFO(...)   ((void)0)
#  define LOG_WARN(...)   ((void)0)
#  define LOG_ERROR(...)  ((void)0)
#endif
