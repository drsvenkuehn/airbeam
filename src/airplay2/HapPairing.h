#pragma once
// src/airplay2/HapPairing.h — HAP SRP-6a + Ed25519 + Curve25519 + ChaCha20-Poly1305 pairing.
// Implements the M1→M6 HAP pairing ceremony per research.md §2.
// Uses libsodium for Ed25519/X25519/ChaCha20, csrp for SRP-6a.
// On success, calls CredentialStore::Write() to persist the credential.
//
// Threading: all methods called on Thread 5 (RTSP/AP2 session control).
// Does NOT post WM_AP2_PAIRING_REQUIRED — that is AirPlay2Session::Init()'s responsibility.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// winsock2.h MUST be included before windows.h to avoid redefinition of SOCKET
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <functional>

#include "discovery/AirPlayReceiver.h"
#include "airplay2/CredentialStore.h"

namespace AirPlay2 {

// ────────────────────────────────────────────────────────────────────────────
// HAP TLV type codes (subset used in M1-M6)
// ────────────────────────────────────────────────────────────────────────────

enum class TlvType : uint8_t {
    Method        = 0x00,
    Identifier    = 0x01,
    Salt          = 0x02,
    PublicKey     = 0x03,
    Proof         = 0x04,
    EncryptedData = 0x05,
    State         = 0x06,
    Error         = 0x07,
    RetryDelay    = 0x08,
    Certificate   = 0x09,
    Signature     = 0x0A,
    Permissions   = 0x0B,
    FragmentData  = 0x0C,
    FragmentLast  = 0x0D,
    SessionID     = 0x0E,
    TTL           = 0x0F,
    Separator     = 0xFF,
};

// HAP TLV error codes
enum class TlvError : uint8_t {
    None               = 0x00,
    Authentication     = 0x02,
    Backoff            = 0x03,
    MaxPeers           = 0x04,
    MaxTries           = 0x05,
    Unavailable        = 0x06,
    Busy               = 0x07,
};

// ────────────────────────────────────────────────────────────────────────────
// TLV8 encoding/decoding helpers
// ────────────────────────────────────────────────────────────────────────────

struct TlvItem {
    TlvType            type;
    std::vector<uint8_t> value;
};

using TlvList = std::vector<TlvItem>;

TlvList TlvDecode(const std::vector<uint8_t>& data);
std::vector<uint8_t> TlvEncode(const TlvList& items);
const TlvItem* TlvFind(const TlvList& list, TlvType type);

// ────────────────────────────────────────────────────────────────────────────
// HapPairing
// ────────────────────────────────────────────────────────────────────────────

/// Callback for requesting PIN from user (invoked when device requires PIN entry).
/// The callback should show a UI dialog and return the 6-digit PIN string,
/// or empty string to cancel pairing.
using PinCallback = std::function<std::string()>;

/// Result of a pairing attempt.
enum class PairingResult {
    Success,          ///< Paired successfully; credential stored
    AuthFailed,       ///< kTLVError_Authentication from VERIFY — stale credential
    PinRequired,      ///< Device requires PIN; pinCallback was called
    PinCancelled,     ///< User cancelled PIN entry
    NetworkError,     ///< TCP/HTTP error
    ProtocolError,    ///< Unexpected TLV format or SRP verification failure
    Cancelled,        ///< Pairing was aborted via Cancel()
};

class HapPairing {
public:
    HapPairing() = default;
    ~HapPairing() = default;

    HapPairing(const HapPairing&)            = delete;
    HapPairing& operator=(const HapPairing&) = delete;

    /// Check if a valid credential exists for this device (IsPaired check).
    /// Returns false if no credential or CredentialStore lookup fails.
    bool IsPaired(const std::string& hapDeviceId) const;

    /// Execute the full HAP pairing ceremony (M1 → M6) against the device.
    /// Blocks on Thread 5 until pairing completes or fails.
    /// @param receiver     Target device (uses ipAddress + airPlay2Port).
    /// @param identity     Controller identity (from CredentialStore::EnsureControllerIdentity).
    /// @param pinCallback  Called when device requires PIN entry.
    /// @returns PairingResult::Success if paired and credential stored.
    PairingResult Pair(const AirPlayReceiver&    receiver,
                       const ControllerIdentity& identity,
                       PinCallback               pinCallback);

    /// Execute the HAP VERIFY phase (abbreviated 2-step exchange to verify credential).
    /// Returns PairingResult::AuthFailed if the device no longer recognises our credential
    /// (indicating factory reset or credential mismatch → caller should post WM_AP2_PAIRING_STALE).
    PairingResult Verify(const AirPlayReceiver&    receiver,
                         const PairingCredential&  credential);

    /// Derive the shared session key after successful Verify().
    /// Valid only after Verify() returns Success.
    const std::array<uint8_t, 32>& SessionKey() const { return sessionKey_; }

    /// Signal pairing to abort at next opportunity. Thread-safe.
    void Cancel() { cancelled_ = true; }

private:
    // ── Internal helpers ─────────────────────────────────────────────────────

    /// Send TLV8 POST to /pair-setup and return response TLVs.
    /// Returns empty list on network error.
    TlvList PairSetupPost(SOCKET sock,
                          const std::string& host,
                          const std::vector<uint8_t>& body);

    /// Send TLV8 POST to /pair-verify and return response TLVs.
    TlvList PairVerifyPost(SOCKET sock,
                           const std::string& host,
                           const std::vector<uint8_t>& body);

    /// Open a blocking TCP connection to ip:port. Returns INVALID_SOCKET on failure.
    SOCKET Connect(const std::string& ip, uint16_t port);

    /// Send HTTP POST and receive full response body.
    std::vector<uint8_t> HttpPost(SOCKET sock,
                                  const std::string& host,
                                  const std::string& path,
                                  const std::string& contentType,
                                  const std::vector<uint8_t>& body);

    std::array<uint8_t, 32> sessionKey_{};
    volatile bool           cancelled_ = false;
};

} // namespace AirPlay2
