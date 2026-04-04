#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "discovery/AirPlayReceiver.h"

/// Tracks state for one in-progress exponential-backoff retry sequence.
/// Owned exclusively by ConnectionController; all fields written on Thread 1.
struct ReconnectContext {
    static constexpr int  kMaxAttempts            = 3;
    static constexpr UINT kDelaysMs[kMaxAttempts] = { 1000, 2000, 4000 };

    AirPlayReceiver targetDevice;  ///< Device to reconnect to (copy; safe after original lost)
    int             attempt  = 0;  ///< Next attempt index (0, 1, 2); 3 means exhausted
    bool            pending  = false; ///< True while a reconnect sequence is active

    /// Reset to default (no pending reconnect).
    void Reset() noexcept { attempt = 0; pending = false; }

    /// Returns true if more attempts remain.
    bool HasAttemptsLeft() const noexcept { return attempt < kMaxAttempts; }

    /// Delay before the current attempt, or 0 if exhausted.
    UINT CurrentDelayMs() const noexcept {
        return (attempt < kMaxAttempts) ? kDelaysMs[attempt] : 0u;
    }
};
