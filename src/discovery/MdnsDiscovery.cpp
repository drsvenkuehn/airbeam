#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include "discovery/MdnsDiscovery.h"
#include "discovery/TxtRecord.h"
#include "core/Messages.h"
#include "core/Logger.h"
#include <chrono>
#include <string>

// ---------------------------------------------------------------------------
// Constants (values from the Bonjour/DNS-SD specification)
// ---------------------------------------------------------------------------

static constexpr DnsServiceFlags_t    kFlagsAdd       = 0x2u;
static constexpr DnsServiceProtocol_t kProtocolIPv4   = 0x01u;
static constexpr DnsServiceError_t    kErrNoError      = 0;
static constexpr ULONGLONG            kPruneIntervalMs = 30'000ULL;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

std::wstring Utf8ToWide(const char* s)
{
    if (!s || !*s) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(static_cast<std::size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), len);
    return w;
}

/// Extract the human-readable display name from a _raop._tcp instance name.
/// RAOP names have the form "AA:BB:CC:DD:EE:FF@DeviceName"; return the part
/// after '@', or the whole string when no '@' is present.
std::wstring DisplayNameFromInstance(const std::wstring& instanceName)
{
    const auto at = instanceName.find(L'@');
    return (at != std::wstring::npos) ? instanceName.substr(at + 1) : instanceName;
}

/// Extract the stable device identifier from a _raop._tcp service instance name.
/// RAOP names have the form "AA:BB:CC:DD:EE:FF@DeviceName".
/// Returns the substring before '@' (the MAC address), uppercased.
std::wstring DeviceIdFromInstance(const std::wstring& instanceName)
{
    const auto at = instanceName.find(L'@');
    std::wstring id = (at != std::wstring::npos) ? instanceName.substr(0, at) : instanceName;
    std::transform(id.begin(), id.end(), id.begin(), ::towupper);
    return id;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MdnsDiscovery::MdnsDiscovery(BonjourLoader& loader, ReceiverList& list, HWND hwndMain)
    : loader_(loader), list_(list), hwndMain_(hwndMain)
{}

MdnsDiscovery::~MdnsDiscovery()
{
    Stop();
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------

void MdnsDiscovery::Start()
{
    if (running_.load(std::memory_order_acquire)) return;
    stopFlag_.store(false, std::memory_order_release);
    thread_ = std::thread(&MdnsDiscovery::ThreadProc, this);
}

void MdnsDiscovery::Stop()
{
    stopFlag_.store(true, std::memory_order_release);
    if (!thread_.joinable()) return;

    // Wait up to 150 ms for the thread to set running_ = false
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(150);
    while (running_.load(std::memory_order_acquire) &&
           steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(milliseconds(10));
    }

    if (thread_.joinable())
    {
        if (!running_.load(std::memory_order_acquire))
            thread_.join();
        else
            thread_.detach(); // timed out — let it wind down on its own
    }
}

// ---------------------------------------------------------------------------
// Thread 2 — event loop
// ---------------------------------------------------------------------------

void MdnsDiscovery::ThreadProc()
{
    running_.store(true, std::memory_order_release);

    const BonjourFuncs& f = loader_.Funcs();

    // Null-guard: BonjourLoader::Load() guarantees all-or-nothing resolution,
    // so checking Browse is sufficient to confirm the whole set is valid.
    if (!f.Browse) {
        running_.store(false, std::memory_order_release);
        return;
    }

    // Start the browse operation for _raop._tcp on all interfaces
    const DnsServiceError_t browseErr = f.Browse(
        &browseRef_,
        0, 0,                   // flags, interfaceIndex (0 = all)
        "_raop._tcp", "local.", // service type, domain
        BrowseCallback, this);

    if (browseErr != kErrNoError || !browseRef_)
    {
        running_.store(false, std::memory_order_release);
        return;
    }

    // Start second browse for _airplay._tcp (AP2-only devices — HomePod etc.)
    // Failure is non-fatal — _raop._tcp alone is sufficient for AP1 devices.
    f.Browse(
        &browseRef2_,
        0, 0,
        "_airplay._tcp", "local.",
        BrowseCallback2, this);

    ULONGLONG lastPrune = GetTickCount64();

    while (!stopFlag_.load(std::memory_order_acquire))
    {
        // Snapshot all active refs into a local vector so callbacks that
        // add/remove entries from pendingResolves_ don't invalidate iteration.
        std::vector<DnsServiceRef_t> activeRefs;
        activeRefs.push_back(browseRef_);
        if (browseRef2_) activeRefs.push_back(browseRef2_);
        for (const auto& slot : pendingResolves_)
        {
            if (slot.resolveRef)  activeRefs.push_back(slot.resolveRef);
            if (slot.addrInfoRef) activeRefs.push_back(slot.addrInfoRef);
        }

        fd_set  readfds;
        FD_ZERO(&readfds);
        SOCKET  maxSock   = 0;
        int     activeCnt = 0;

        for (const auto ref : activeRefs)
        {
            if (!ref) continue;
            const SOCKET sock = static_cast<SOCKET>(f.RefSockFD(ref));
            if (sock == INVALID_SOCKET) continue;
            FD_SET(sock, &readfds);
            if (sock > maxSock) maxSock = sock;
            ++activeCnt;
        }

        if (activeCnt == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        else
        {
            timeval tv{ 0, 100'000 }; // 100 ms
            const int ready = select(static_cast<int>(maxSock + 1),
                                     &readfds, nullptr, nullptr, &tv);
            if (ready > 0)
            {
                for (const auto ref : activeRefs)
                {
                    if (!ref) continue;
                    const SOCKET sock = static_cast<SOCKET>(f.RefSockFD(ref));
                    if (sock != INVALID_SOCKET && FD_ISSET(sock, &readfds))
                        f.ProcessResult(ref);
                }
            }
        }

        // Periodic stale-entry eviction
        const ULONGLONG now = GetTickCount64();
        if (now - lastPrune >= kPruneIntervalMs)
        {
            list_.PruneStale(now);
            lastPrune = now;
        }
    }

    // Clean up all active service refs
    for (auto& slot : pendingResolves_)
    {
        if (slot.addrInfoRef) { f.RefDeallocate(slot.addrInfoRef); slot.addrInfoRef = nullptr; }
        if (slot.resolveRef)  { f.RefDeallocate(slot.resolveRef);  slot.resolveRef  = nullptr; }
    }
    pendingResolves_.clear();
    if (browseRef2_) { f.RefDeallocate(browseRef2_); browseRef2_ = nullptr; }
    if (browseRef_)  { f.RefDeallocate(browseRef_);  browseRef_  = nullptr; }

    running_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// BrowseCallback — service added or removed
// ---------------------------------------------------------------------------

void MdnsDiscovery::BrowseCallback(
    DnsServiceRef_t /*sdRef*/, DnsServiceFlags_t flags, uint32_t interfaceIndex,
    DnsServiceError_t errorCode,
    const char* serviceName, const char* /*regtype*/, const char* replyDomain,
    void* context)
{
    if (errorCode != kErrNoError) return;

    auto* self = static_cast<MdnsDiscovery*>(context);

    const std::wstring instanceName = Utf8ToWide(serviceName);

    if ((flags & kFlagsAdd) == 0)
    {
        // Service removed — post WM_SPEAKER_LOST with heap-allocated wchar_t[]
        // so ConnectionController can reconstruct the instance name on Thread 1.
        LOG_INFO("mDNS: removed  \"%ls\"", instanceName.c_str());
        if (self->hwndMain_) {
            const size_t n = instanceName.size();
            wchar_t* heapStr = new wchar_t[n + 1];
            wmemcpy(heapStr, instanceName.c_str(), n + 1);
            PostMessageW(self->hwndMain_, WM_SPEAKER_LOST, 0,
                         reinterpret_cast<LPARAM>(heapStr));
        }
        self->list_.Remove(instanceName);
        return;
    }

    // Service added — begin resolve pipeline.
    // Push a new slot onto the pending list; std::list gives a stable address
    // that we can safely pass as the callback context to Resolve/GetAddrInfo.
    const BonjourFuncs& f = self->loader_.Funcs();
    self->pendingResolves_.push_back({});
    PendingResolve& slot = self->pendingResolves_.back();
    slot.owner                  = self;
    slot.interfaceIndex         = interfaceIndex;
    slot.receiver               = AirPlayReceiver{};
    slot.receiver.instanceName  = instanceName;
    slot.receiver.displayName   = DisplayNameFromInstance(instanceName);
    slot.receiver.stableId      = DeviceIdFromInstance(instanceName);

    LOG_DEBUG("mDNS: browse add \"%ls\" (if=%u) — resolving...",
              instanceName.c_str(), interfaceIndex);

    const DnsServiceError_t err = f.Resolve(
        &slot.resolveRef,
        0, interfaceIndex,
        serviceName, "_raop._tcp", replyDomain,
        ResolveCallback, &slot);

    if (err != kErrNoError)
    {
        self->pendingResolves_.pop_back();
    }
}

// ---------------------------------------------------------------------------
// ResolveCallback — hostname, port, and TXT record available
// ---------------------------------------------------------------------------

void MdnsDiscovery::ResolveCallback(
    DnsServiceRef_t /*sdRef*/, DnsServiceFlags_t /*flags*/, uint32_t /*ifIdx*/,
    DnsServiceError_t errorCode,
    const char* /*fullname*/, const char* hosttarget,
    uint16_t port, uint16_t txtLen, const unsigned char* txtRecord,
    void* context)
{
    auto* slot = static_cast<PendingResolve*>(context);
    MdnsDiscovery* self = slot->owner;
    const BonjourFuncs& f = self->loader_.Funcs();

    // Resolve is one-shot — deallocate the ref now (safe per Bonjour spec)
    if (slot->resolveRef)
    {
        f.RefDeallocate(slot->resolveRef);
        slot->resolveRef = nullptr;
    }

    if (errorCode != kErrNoError)
    {
        // Remove the slot on error
        self->pendingResolves_.remove_if(
            [slot](const PendingResolve& s) { return &s == slot; });
        return;
    }

    slot->receiver.hostName = Utf8ToWide(hosttarget);
    slot->receiver.port     = ntohs(port);

    TxtRecord::Parse(txtRecord, txtLen, slot->receiver);

    LOG_DEBUG("mDNS: resolved  \"%ls\" host=%s port=%u airplay1=%s%s",
              slot->receiver.instanceName.c_str(),
              hosttarget,
              static_cast<unsigned>(ntohs(port)),
              slot->receiver.isAirPlay1Compatible ? "YES" :
                  (slot->receiver.isAirPlay2Only   ? "NO (AirPlay 2 only)" : "NO (filtered)"),
              slot->receiver.supportsAes ? " aes=YES" : " aes=NO");

    // Start address resolution
    const DnsServiceError_t err = f.GetAddrInfo(
        &slot->addrInfoRef,
        0,
        slot->interfaceIndex,
        kProtocolIPv4,
        hosttarget,
        AddrInfoCallback, slot);

    if (err != kErrNoError)
    {
        self->pendingResolves_.remove_if(
            [slot](const PendingResolve& s) { return &s == slot; });
    }
}

// ---------------------------------------------------------------------------
// AddrInfoCallback — IPv4 address available; complete and publish the receiver
// ---------------------------------------------------------------------------

void MdnsDiscovery::AddrInfoCallback(
    DnsServiceRef_t /*sdRef*/, DnsServiceFlags_t flags, uint32_t /*ifIdx*/,
    DnsServiceError_t errorCode,
    const char* /*hostname*/, const struct sockaddr* address,
    uint32_t /*ttl*/, void* context)
{
    auto* slot = static_cast<PendingResolve*>(context);
    MdnsDiscovery* self = slot->owner;
    const BonjourFuncs& f = self->loader_.Funcs();

    // Deallocate the addrinfo ref (one-shot query)
    if (slot->addrInfoRef)
    {
        f.RefDeallocate(slot->addrInfoRef);
        slot->addrInfoRef = nullptr;
    }

    auto removeSlot = [&]() {
        self->pendingResolves_.remove_if(
            [slot](const PendingResolve& s) { return &s == slot; });
    };

    if (errorCode != kErrNoError) { removeSlot(); return; }
    if (!address)                 { removeSlot(); return; }
    if (address->sa_family != AF_INET) { removeSlot(); return; }

    const auto* sin = reinterpret_cast<const struct sockaddr_in*>(address);
    char ipBuf[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &sin->sin_addr, ipBuf, sizeof(ipBuf)) == nullptr)
        { removeSlot(); return; }

    AirPlayReceiver& r = slot->receiver;
    r.ipAddress    = ipBuf;
    r.lastSeenTick = GetTickCount64();

    LOG_INFO("mDNS: discovered \"%ls\" ip=%s port=%u",
             r.instanceName.c_str(), ipBuf, static_cast<unsigned>(r.port));

    self->list_.Update(r);

    // Post WM_DEVICE_DISCOVERED so ConnectionController can check auto-connect.
    if (self->hwndMain_ && !r.stableId.empty()) {
        const size_t n = r.stableId.size();
        wchar_t* heapStr = new wchar_t[n + 1];
        wmemcpy(heapStr, r.stableId.c_str(), n + 1);
        PostMessageW(self->hwndMain_, WM_DEVICE_DISCOVERED, 0,
                     reinterpret_cast<LPARAM>(heapStr));
    }

    removeSlot();
}

// ---------------------------------------------------------------------------
// BrowseCallback2 — _airplay._tcp service added or removed (AP2-only devices)
// ---------------------------------------------------------------------------
// This callback handles devices that advertise _airplay._tcp but NOT _raop._tcp
// (e.g. Apple HomePod 2nd gen, AirPlay 2-only speakers).  Resolve pipeline is
// identical to BrowseCallback.  If ReceiverList already has a matching stableId
// from a previous _raop._tcp discovery, Update() merges/updates the record.
// ---------------------------------------------------------------------------

void MdnsDiscovery::BrowseCallback2(
    DnsServiceRef_t /*sdRef*/, DnsServiceFlags_t flags, uint32_t interfaceIndex,
    DnsServiceError_t errorCode,
    const char* serviceName, const char* /*regtype*/, const char* replyDomain,
    void* context)
{
    if (errorCode != kErrNoError) return;

    auto* self = static_cast<MdnsDiscovery*>(context);
    const std::wstring instanceName = Utf8ToWide(serviceName);

    if ((flags & kFlagsAdd) == 0)
    {
        // Service removed — use same WM_SPEAKER_LOST / Remove() path.
        // Only post if not already handled by _raop._tcp callback.
        LOG_DEBUG("mDNS: _airplay removed \"%ls\"", instanceName.c_str());
        // ReceiverList::Remove is idempotent so safe to call from both paths.
        self->list_.Remove(instanceName);
        // Do NOT post WM_SPEAKER_LOST here — _raop._tcp callback already did it
        // if the device also advertised _raop._tcp.  For purely AP2-only devices
        // the _raop._tcp callback never fires, so we must post here.
        if (self->hwndMain_) {
            const size_t n = instanceName.size();
            wchar_t* heapStr = new wchar_t[n + 1];
            wmemcpy(heapStr, instanceName.c_str(), n + 1);
            PostMessageW(self->hwndMain_, WM_SPEAKER_LOST, 0,
                         reinterpret_cast<LPARAM>(heapStr));
        }
        return;
    }

    const BonjourFuncs& f = self->loader_.Funcs();
    self->pendingResolves_.push_back({});
    PendingResolve& slot = self->pendingResolves_.back();
    slot.owner                  = self;
    slot.interfaceIndex         = interfaceIndex;
    slot.receiver               = AirPlayReceiver{};
    slot.receiver.instanceName  = instanceName;
    slot.receiver.displayName   = instanceName; // will be refined by TxtRecord::Parse
    slot.receiver.stableId      = DeviceIdFromInstance(instanceName);

    LOG_DEBUG("mDNS: _airplay add \"%ls\" (if=%u) — resolving...",
              instanceName.c_str(), interfaceIndex);

    const DnsServiceError_t err = f.Resolve(
        &slot.resolveRef,
        0, interfaceIndex,
        serviceName, "_airplay._tcp", replyDomain,
        ResolveCallback, &slot);

    if (err != kErrNoError)
        self->pendingResolves_.pop_back();
}

