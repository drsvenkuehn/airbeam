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
    // Accept:
    //   - AirPlay 1 compatible devices
    //   - AirPlay 2-only devices (isAirPlay2Only=true from _raop._tcp, no RSA-AES)
    //   - AirPlay 2 capable devices (supportsAirPlay2=true from _airplay._tcp)
    // Silently drop anything else (e.g. MFiSAP-only with no usable protocol).
    if (!receiver.isAirPlay1Compatible &&
        !receiver.isAirPlay2Only &&
        !receiver.supportsAirPlay2)
    {
        LOG_WARN("ReceiverList: \"%ls\" filtered (unsupported protocol)",
                 receiver.instanceName.c_str());
        return;
    }
    if (receiver.supportsAirPlay2 && !receiver.isAirPlay1Compatible)
    {
        LOG_INFO("ReceiverList: \"%ls\" added as AirPlay 2 device (pk present)",
                 receiver.instanceName.c_str());
    }

    EnterCriticalSection(&cs_);

    // First try to match by instanceName (exact match)
    auto it = std::find_if(receivers_.begin(), receivers_.end(),
        [&receiver](const AirPlayReceiver& r)
        { return r.instanceName == receiver.instanceName; });

    if (it == receivers_.end())
    {
        // If not found by instanceName, try to match by stableId.
        // This handles the case where the same physical device is discovered
        // via both _raop._tcp and _airplay._tcp with different instance names.
        if (!receiver.stableId.empty())
        {
            it = std::find_if(receivers_.begin(), receivers_.end(),
                [&receiver](const AirPlayReceiver& r)
                { return !r.stableId.empty() && r.stableId == receiver.stableId; });
        }
    }

    if (it != receivers_.end())
    {
        // Merge: preserve pairingState and hapDevicePublicKey if already loaded.
        // Keep the richer of the two records (prefer the one with pk/AP2 info).
        AirPlayReceiver merged = receiver;
        if (merged.hapDevicePublicKey.empty() && !it->hapDevicePublicKey.empty())
            merged.hapDevicePublicKey = it->hapDevicePublicKey;
        if (!merged.supportsAirPlay2 && it->supportsAirPlay2)
            merged.supportsAirPlay2 = it->supportsAirPlay2;
        // Keep existing pairingState (loaded from CredentialStore, not from mDNS)
        if (it->pairingState != PairingState::NotApplicable &&
            it->pairingState != PairingState::Unpaired)
            merged.pairingState = it->pairingState;
        *it = merged;
    }
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
