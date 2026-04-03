#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "discovery/ReceiverList.h"
#include "core/Messages.h"
#include "core/Logger.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// File-level constants
// ---------------------------------------------------------------------------

static constexpr ULONGLONG kStaleTimeoutMs = 60000ULL;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ReceiverList::ReceiverList(HWND mainHwnd)
    : mainHwnd_(mainHwnd)
{
    InitializeCriticalSection(&cs_);
}

ReceiverList::~ReceiverList()
{
    DeleteCriticalSection(&cs_);
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void ReceiverList::Update(const AirPlayReceiver& receiver)
{
    // Accept AirPlay 1 compatible devices and AirPlay 2-only devices (shown
    // in the menu with an informational label). Silently drop anything else
    // (e.g. MFiSAP-only devices that speak neither protocol).
    if (!receiver.isAirPlay1Compatible && !receiver.isAirPlay2Only)
    {
        LOG_WARN("ReceiverList: \"%ls\" filtered (unsupported protocol)",
                 receiver.instanceName.c_str());
        return;
    }
    if (receiver.isAirPlay2Only)
    {
        LOG_INFO("ReceiverList: \"%ls\" added as AirPlay 2-only (will show with label)",
                 receiver.instanceName.c_str());
    }

    EnterCriticalSection(&cs_);

    auto it = std::find_if(receivers_.begin(), receivers_.end(),
        [&receiver](const AirPlayReceiver& r)
        { return r.instanceName == receiver.instanceName; });

    if (it != receivers_.end())
        *it = receiver;
    else
        receivers_.push_back(receiver);

    LeaveCriticalSection(&cs_);
    NotifyUpdated();
}

void ReceiverList::Remove(const std::wstring& instanceName)
{
    bool removed = false;

    EnterCriticalSection(&cs_);

    auto it = std::find_if(receivers_.begin(), receivers_.end(),
        [&instanceName](const AirPlayReceiver& r)
        { return r.instanceName == instanceName; });

    if (it != receivers_.end())
    {
        receivers_.erase(it);
        removed = true;
    }

    LeaveCriticalSection(&cs_);

    if (removed) NotifyUpdated();
}

void ReceiverList::PruneStale(ULONGLONG nowTicks)
{
    bool removed = false;

    EnterCriticalSection(&cs_);

    const std::size_t before = receivers_.size();

    receivers_.erase(
        std::remove_if(receivers_.begin(), receivers_.end(),
            [nowTicks](const AirPlayReceiver& r)
            {
                return (nowTicks - r.lastSeenTick) > kStaleTimeoutMs;
            }),
        receivers_.end());

    removed = (receivers_.size() < before);

    LeaveCriticalSection(&cs_);

    if (removed) NotifyUpdated();
}

// ---------------------------------------------------------------------------
// Read-only accessors
// ---------------------------------------------------------------------------

void ReceiverList::ForEach(
    std::function<void(const std::vector<AirPlayReceiver>&)> fn) const
{
    EnterCriticalSection(&cs_);
    fn(receivers_);
    LeaveCriticalSection(&cs_);
}

std::vector<AirPlayReceiver> ReceiverList::Snapshot() const
{
    EnterCriticalSection(&cs_);
    std::vector<AirPlayReceiver> copy = receivers_;
    LeaveCriticalSection(&cs_);
    return copy;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ReceiverList::NotifyUpdated() const
{
    PostMessage(mainHwnd_, WM_RECEIVERS_UPDATED, 0, 0);
}
