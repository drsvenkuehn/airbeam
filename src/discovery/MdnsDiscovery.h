#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include "discovery/BonjourLoader.h"
#include "discovery/ReceiverList.h"

/// Thread 2 — runs the Bonjour/mDNS event loop on a dedicated thread.
/// Browses for _raop._tcp services, resolves each to an IP+port, parses the
/// TXT record, then upserts the result into ReceiverList.
class MdnsDiscovery
{
public:
    MdnsDiscovery(BonjourLoader& loader, ReceiverList& list);
    ~MdnsDiscovery();

    MdnsDiscovery(const MdnsDiscovery&)            = delete;
    MdnsDiscovery& operator=(const MdnsDiscovery&) = delete;

    /// Start Thread 2.  No-op if already running.
    void Start();

    /// Signal stop and join Thread 2.  Blocks at most 150 ms; detaches on timeout.
    void Stop();

    bool IsRunning() const { return running_.load(std::memory_order_acquire); }

private:
    void ThreadProc();

    // Static Bonjour callbacks — each routes through context to the instance
    static void BrowseCallback(
        DnsServiceRef_t sdRef, DnsServiceFlags_t flags, uint32_t interfaceIndex,
        DnsServiceError_t errorCode,
        const char* serviceName, const char* regtype, const char* replyDomain,
        void* context);

    static void ResolveCallback(
        DnsServiceRef_t sdRef, DnsServiceFlags_t flags, uint32_t interfaceIndex,
        DnsServiceError_t errorCode,
        const char* fullname, const char* hosttarget,
        uint16_t port, uint16_t txtLen, const unsigned char* txtRecord,
        void* context);

    static void AddrInfoCallback(
        DnsServiceRef_t sdRef, DnsServiceFlags_t flags, uint32_t interfaceIndex,
        DnsServiceError_t errorCode,
        const char* hostname, const struct sockaddr* address,
        uint32_t ttl, void* context);

    BonjourLoader&    loader_;
    ReceiverList&     list_;
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> running_{false};
    std::thread       thread_;

    // Active service refs — only touched from Thread 2
    DnsServiceRef_t   browseRef_   = nullptr;
    DnsServiceRef_t   resolveRef_  = nullptr;
    DnsServiceRef_t   addrInfoRef_ = nullptr;

    /// Accumulates receiver fields across the Browse→Resolve→AddrInfo pipeline.
    /// Single-slot: adequate for LAN mDNS where responses arrive sequentially.
    struct PendingResolve
    {
        AirPlayReceiver receiver;
        uint32_t        interfaceIndex = 0;
    };
    PendingResolve pendingResolve_;
};
