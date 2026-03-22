#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <vector>
#include <functional>
#include "discovery/AirPlayReceiver.h"

/// Thread-safe list of discovered AirPlay receivers backed by a CRITICAL_SECTION.
/// Every mutation posts WM_RECEIVERS_UPDATED to the main window.
class ReceiverList
{
public:
    explicit ReceiverList(HWND mainHwnd);
    ~ReceiverList();

    ReceiverList(const ReceiverList&)            = delete;
    ReceiverList& operator=(const ReceiverList&) = delete;

    /// Upsert by instanceName: replaces the entry when found, appends otherwise.
    void Update(const AirPlayReceiver& receiver);

    /// Remove entry matching instanceName. No-op when not found.
    void Remove(const std::wstring& instanceName);

    /// Evict entries whose lastSeenTick is more than 60 s before nowTicks.
    void PruneStale(ULONGLONG nowTicks);

    /// Invoke fn with a const-ref to the internal vector while holding the lock.
    /// Safe to call from any thread; fn must not call back into ReceiverList.
    void ForEach(std::function<void(const std::vector<AirPlayReceiver>&)> fn) const;

    /// Returns a snapshot copy of the receiver list. Safe from any thread.
    std::vector<AirPlayReceiver> Snapshot() const;

private:
    void NotifyUpdated() const;

    HWND                         mainHwnd_;
    mutable CRITICAL_SECTION     cs_;
    std::vector<AirPlayReceiver> receivers_;
};
