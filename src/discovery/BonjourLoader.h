#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>   // Must precede windows.h
#include <windows.h>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Opaque Bonjour types  (no dns_sd.h required — pure dynamic loading)
// ---------------------------------------------------------------------------

struct OpaqueServiceRef_t;
typedef OpaqueServiceRef_t* DnsServiceRef_t;
typedef uint32_t            DnsServiceFlags_t;
typedef int32_t             DnsServiceError_t;
typedef SOCKET              DnsSock_t;          // SOCKET on Windows (UINT_PTR)
typedef uint32_t            DnsServiceProtocol_t;

// ---------------------------------------------------------------------------
// Callback typedefs matching the Bonjour SDK ABI
// ---------------------------------------------------------------------------

typedef void (*DnsBrowseCallback)(
    DnsServiceRef_t,    DnsServiceFlags_t, uint32_t,  DnsServiceError_t,
    const char*,        const char*,       const char*, void*);

typedef void (*DnsResolveCallback)(
    DnsServiceRef_t,    DnsServiceFlags_t,  uint32_t,          DnsServiceError_t,
    const char*,        const char*,        uint16_t /*port*/,
    uint16_t /*txtLen*/, const unsigned char* /*txtRecord*/,  void*);

typedef void (*DnsAddrInfoCallback)(
    DnsServiceRef_t,    DnsServiceFlags_t,  uint32_t,          DnsServiceError_t,
    const char*,        const struct sockaddr*, uint32_t /*ttl*/, void*);

// ---------------------------------------------------------------------------
// Function-pointer table
// ---------------------------------------------------------------------------

struct BonjourFuncs
{
    DnsServiceError_t (*Browse)(
        DnsServiceRef_t*, DnsServiceFlags_t, uint32_t,
        const char*, const char*, DnsBrowseCallback, void*)           = nullptr;

    DnsServiceError_t (*Resolve)(
        DnsServiceRef_t*, DnsServiceFlags_t, uint32_t,
        const char*, const char*, const char*, DnsResolveCallback, void*) = nullptr;

    DnsServiceError_t (*GetAddrInfo)(
        DnsServiceRef_t*, DnsServiceFlags_t, uint32_t, DnsServiceProtocol_t,
        const char*, DnsAddrInfoCallback, void*)                      = nullptr;

    DnsSock_t         (*RefSockFD)(DnsServiceRef_t)                   = nullptr;
    DnsServiceError_t (*ProcessResult)(DnsServiceRef_t)               = nullptr;
    void              (*RefDeallocate)(DnsServiceRef_t)               = nullptr;

    const void*       (*TXTRecordGetValuePtr)(
        uint16_t, const void*, uint8_t, const char*, uint8_t*)        = nullptr;
};

// ---------------------------------------------------------------------------
// BonjourLoader — loads dnssd.dll at runtime
// ---------------------------------------------------------------------------

class BonjourLoader
{
public:
    BonjourLoader()  = default;
    ~BonjourLoader() = default;

    BonjourLoader(const BonjourLoader&)            = delete;
    BonjourLoader& operator=(const BonjourLoader&) = delete;

    /// Loads dnssd.dll.  On failure checks the Bonjour service registry entry
    /// to locate the DLL in the installation directory and retries.
    /// Returns true only when all six critical function pointers are resolved.
    bool Load();

    /// Unloads the DLL and zeroes all function pointers.
    void Unload();

    bool IsLoaded() const { return hDll_ != nullptr; }

    const BonjourFuncs& Funcs() const { return funcs_; }

private:
    HMODULE      hDll_  = nullptr;
    BonjourFuncs funcs_;
};
