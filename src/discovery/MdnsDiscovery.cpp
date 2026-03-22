#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "discovery/MdnsDiscovery.h"
#include "discovery/TxtRecord.h"
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

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MdnsDiscovery::MdnsDiscovery(BonjourLoader& loader, ReceiverList& list)
    : loader_(loader), list_(list)
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

    ULONGLONG lastPrune = GetTickCount64();

    while (!stopFlag_.load(std::memory_order_acquire))
    {
        // Snapshot active refs so mutations from callbacks don't invalidate iteration
        const DnsServiceRef_t snap[3] = { browseRef_, resolveRef_, addrInfoRef_ };

        fd_set  readfds;
        FD_ZERO(&readfds);
        SOCKET  maxSock   = 0;
        int     activeCnt = 0;

        for (const auto ref : snap)
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
                for (const auto ref : snap)
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
    if (addrInfoRef_) { f.RefDeallocate(addrInfoRef_); addrInfoRef_ = nullptr; }
    if (resolveRef_)  { f.RefDeallocate(resolveRef_);  resolveRef_  = nullptr; }
    if (browseRef_)   { f.RefDeallocate(browseRef_);   browseRef_   = nullptr; }

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
        // Service removed
        self->list_.Remove(instanceName);
        return;
    }

    // Service added — begin resolve pipeline
    // If a previous resolve is in flight, abandon it (single-slot design)
    const BonjourFuncs& f = self->loader_.Funcs();
    if (self->resolveRef_)
    {
        f.RefDeallocate(self->resolveRef_);
        self->resolveRef_ = nullptr;
    }

    // Seed the pending receiver with the fields we know now
    self->pendingResolve_.interfaceIndex       = interfaceIndex;
    self->pendingResolve_.receiver             = AirPlayReceiver{};
    self->pendingResolve_.receiver.instanceName = instanceName;
    self->pendingResolve_.receiver.displayName  = DisplayNameFromInstance(instanceName);

    const DnsServiceError_t err = f.Resolve(
        &self->resolveRef_,
        0, interfaceIndex,
        serviceName, "_raop._tcp", replyDomain,
        ResolveCallback, context);

    if (err != kErrNoError)
        self->resolveRef_ = nullptr;
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
    auto* self = static_cast<MdnsDiscovery*>(context);
    const BonjourFuncs& f = self->loader_.Funcs();

    // Deallocate the resolve ref from within the callback (safe per Bonjour spec)
    if (self->resolveRef_)
    {
        f.RefDeallocate(self->resolveRef_);
        self->resolveRef_ = nullptr;
    }

    if (errorCode != kErrNoError) return;

    // Fill receiver fields available at resolve time
    self->pendingResolve_.receiver.hostName = Utf8ToWide(hosttarget);
    self->pendingResolve_.receiver.port     = ntohs(port);

    TxtRecord::Parse(txtRecord, txtLen, self->pendingResolve_.receiver);

    // Abandon any in-flight address query and start a fresh one
    if (self->addrInfoRef_)
    {
        f.RefDeallocate(self->addrInfoRef_);
        self->addrInfoRef_ = nullptr;
    }

    const DnsServiceError_t err = f.GetAddrInfo(
        &self->addrInfoRef_,
        0,                                        // flags
        self->pendingResolve_.interfaceIndex,
        kProtocolIPv4,
        hosttarget,
        AddrInfoCallback, context);

    if (err != kErrNoError)
        self->addrInfoRef_ = nullptr;
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
    auto* self = static_cast<MdnsDiscovery*>(context);
    const BonjourFuncs& f = self->loader_.Funcs();

    // Deallocate the addrinfo ref (one-shot query)
    if (self->addrInfoRef_)
    {
        f.RefDeallocate(self->addrInfoRef_);
        self->addrInfoRef_ = nullptr;
    }

    if (errorCode != kErrNoError) return;
    if (!address) return;

    // Only handle IPv4 (we requested kProtocolIPv4 but guard defensively)
    if (address->sa_family != AF_INET) return;

    // Convert sockaddr_in to dotted-decimal string
    const auto* sin = reinterpret_cast<const struct sockaddr_in*>(address);
    char ipBuf[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &sin->sin_addr, ipBuf, sizeof(ipBuf)) == nullptr)
        return;

    AirPlayReceiver& r = self->pendingResolve_.receiver;
    r.ipAddress    = ipBuf;
    r.lastSeenTick = GetTickCount();

    self->list_.Update(r);
}
