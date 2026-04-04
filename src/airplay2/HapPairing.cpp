// src/airplay2/HapPairing.cpp
// HAP SRP-6a + Ed25519 + Curve25519 + ChaCha20-Poly1305 pairing ceremony.
// Implements M1→M6 per research.md §2 and HAP specification §5.6.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "airplay2/HapPairing.h"
#include "airplay2/CredentialStore.h"
#include "core/Logger.h"

#include <sodium.h>
#include "srp.h"  // csrp — SRP-6a

#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstring>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

namespace AirPlay2 {

// ────────────────────────────────────────────────────────────────────────────
// TLV8 helpers
// ────────────────────────────────────────────────────────────────────────────

TlvList TlvDecode(const std::vector<uint8_t>& data)
{
    TlvList list;
    size_t i = 0;
    while (i + 1 < data.size()) {
        const TlvType type  = static_cast<TlvType>(data[i]);
        const size_t  vlen  = data[i + 1];
        i += 2;
        if (i + vlen > data.size()) break;

        // Merge fragmented TLVs (same type, consecutive, len=255)
        if (!list.empty() && list.back().type == type) {
            list.back().value.insert(list.back().value.end(),
                                     data.begin() + static_cast<ptrdiff_t>(i),
                                     data.begin() + static_cast<ptrdiff_t>(i + vlen));
        } else {
            TlvItem item;
            item.type  = type;
            item.value.assign(data.begin() + static_cast<ptrdiff_t>(i),
                              data.begin() + static_cast<ptrdiff_t>(i + vlen));
            list.push_back(std::move(item));
        }
        i += vlen;
    }
    return list;
}

std::vector<uint8_t> TlvEncode(const TlvList& items)
{
    std::vector<uint8_t> out;
    for (const auto& item : items) {
        const auto& v = item.value;
        size_t offset = 0;
        do {
            const size_t chunk = std::min<size_t>(255, v.size() - offset);
            out.push_back(static_cast<uint8_t>(item.type));
            out.push_back(static_cast<uint8_t>(chunk));
            out.insert(out.end(), v.begin() + static_cast<ptrdiff_t>(offset),
                       v.begin() + static_cast<ptrdiff_t>(offset + chunk));
            offset += chunk;
        } while (offset < v.size());
    }
    return out;
}

const TlvItem* TlvFind(const TlvList& list, TlvType type)
{
    for (const auto& item : list)
        if (item.type == type) return &item;
    return nullptr;
}

// ────────────────────────────────────────────────────────────────────────────
// Helper: HKDF-SHA512 (used for HAP Pair-Setup M5/M6 key derivation)
// ────────────────────────────────────────────────────────────────────────────

static std::vector<uint8_t> Hkdf(
    const uint8_t* ikm,  size_t ikmLen,
    const uint8_t* salt, size_t saltLen,
    const uint8_t* info, size_t infoLen,
    size_t outputLen)
{
    // Extract: prk = HMAC-SHA512(salt, ikm)
    uint8_t prk[crypto_auth_hmacsha512_BYTES];
    crypto_auth_hmacsha512_state st;
    crypto_auth_hmacsha512_init(&st, salt, saltLen);
    crypto_auth_hmacsha512_update(&st, ikm, ikmLen);
    crypto_auth_hmacsha512_final(&st, prk);

    // Expand: T(1) = HMAC-SHA512(prk, info || 0x01)
    std::vector<uint8_t> out;
    out.reserve(outputLen);
    uint8_t prev[crypto_auth_hmacsha512_BYTES] = {};
    size_t prevLen = 0;
    uint8_t counter = 1;
    while (out.size() < outputLen) {
        uint8_t t[crypto_auth_hmacsha512_BYTES];
        crypto_auth_hmacsha512_init(&st, prk, sizeof(prk));
        if (prevLen) crypto_auth_hmacsha512_update(&st, prev, prevLen);
        crypto_auth_hmacsha512_update(&st, info, infoLen);
        crypto_auth_hmacsha512_update(&st, &counter, 1);
        crypto_auth_hmacsha512_final(&st, t);
        const size_t take = std::min<size_t>(sizeof(t), outputLen - out.size());
        out.insert(out.end(), t, t + take);
        std::memcpy(prev, t, sizeof(t));
        prevLen = sizeof(t);
        ++counter;
    }
    sodium_memzero(prk, sizeof(prk));
    return out;
}

// ────────────────────────────────────────────────────────────────────────────
// Helper: HKDF-SHA256 (used for HAP Pair-Verify and session key derivation)
// HAP spec requires SHA-256 for the Verify phase and final session keys.
// ────────────────────────────────────────────────────────────────────────────

static std::vector<uint8_t> HkdfSha256(
    const uint8_t* ikm,  size_t ikmLen,
    const uint8_t* salt, size_t saltLen,
    const uint8_t* info, size_t infoLen,
    size_t outputLen)
{
    // Extract: prk = HMAC-SHA256(salt, ikm)
    uint8_t prk[crypto_auth_hmacsha256_BYTES];
    crypto_auth_hmacsha256_state st;
    crypto_auth_hmacsha256_init(&st, salt, saltLen);
    crypto_auth_hmacsha256_update(&st, ikm, ikmLen);
    crypto_auth_hmacsha256_final(&st, prk);

    // Expand: T(i) = HMAC-SHA256(prk, T(i-1) || info || counter)
    std::vector<uint8_t> out;
    out.reserve(outputLen);
    uint8_t prev[crypto_auth_hmacsha256_BYTES] = {};
    size_t prevLen = 0;
    uint8_t counter = 1;
    while (out.size() < outputLen) {
        uint8_t t[crypto_auth_hmacsha256_BYTES];
        crypto_auth_hmacsha256_init(&st, prk, sizeof(prk));
        if (prevLen) crypto_auth_hmacsha256_update(&st, prev, prevLen);
        crypto_auth_hmacsha256_update(&st, info, infoLen);
        crypto_auth_hmacsha256_update(&st, &counter, 1);
        crypto_auth_hmacsha256_final(&st, t);
        const size_t take = std::min<size_t>(sizeof(t), outputLen - out.size());
        out.insert(out.end(), t, t + take);
        std::memcpy(prev, t, sizeof(t));
        prevLen = sizeof(t);
        ++counter;
    }
    sodium_memzero(prk, sizeof(prk));
    return out;
}

// ────────────────────────────────────────────────────────────────────────────
// TCP connect helper
// ────────────────────────────────────────────────────────────────────────────

SOCKET HapPairing::Connect(const std::string& ip, uint16_t port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    // 5-second connect timeout via non-blocking connect + select
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);

    connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    fd_set fds;
    FD_ZERO(&fds); FD_SET(s, &fds);
    timeval tv{ 5, 0 };
    if (select(0, nullptr, &fds, nullptr, &tv) <= 0) {
        closesocket(s);
        return INVALID_SOCKET;
    }

    nonblocking = 0;
    ioctlsocket(s, FIONBIO, &nonblocking);
    return s;
}

// ────────────────────────────────────────────────────────────────────────────
// HTTP POST helper
// ────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> HapPairing::HttpPost(
    SOCKET sock,
    const std::string& host,
    const std::string& path,
    const std::string& contentType,
    const std::vector<uint8_t>& body)
{
    // Build HTTP/1.1 POST request
    std::ostringstream req;
    req << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: keep-alive\r\n"
        << "\r\n";
    const std::string hdr = req.str();

    // Send header
    send(sock, hdr.c_str(), static_cast<int>(hdr.size()), 0);
    // Send body
    if (!body.empty())
        send(sock, reinterpret_cast<const char*>(body.data()),
             static_cast<int>(body.size()), 0);

    // Receive response
    std::vector<uint8_t> response;
    char buf[4096];
    int received;
    bool headersDone = false;
    size_t contentLength = 0;
    size_t bodyStart = 0;

    while (true) {
        received = recv(sock, buf, sizeof(buf), 0);
        if (received <= 0) break;
        response.insert(response.end(), buf, buf + received);

        if (!headersDone) {
            // Search for \r\n\r\n
            const std::string resp(response.begin(), response.end());
            const size_t sep = resp.find("\r\n\r\n");
            if (sep != std::string::npos) {
                headersDone = true;
                bodyStart   = sep + 4;
                // Extract Content-Length
                const std::string hdrs = resp.substr(0, sep);
                const std::string clKey = "Content-Length: ";
                const size_t clPos = hdrs.find(clKey);
                if (clPos != std::string::npos)
                    contentLength = std::stoul(hdrs.substr(clPos + clKey.size()));
            }
        }

        if (headersDone && (response.size() - bodyStart) >= contentLength)
            break;
    }

    if (!headersDone || contentLength == 0)
        return {};

    return std::vector<uint8_t>(response.begin() + static_cast<ptrdiff_t>(bodyStart),
                                response.begin() + static_cast<ptrdiff_t>(bodyStart + contentLength));
}

TlvList HapPairing::PairSetupPost(SOCKET sock, const std::string& host,
                                   const std::vector<uint8_t>& body)
{
    const auto resp = HttpPost(sock, host, "/pair-setup",
                               "application/pairing+tlv8", body);
    return resp.empty() ? TlvList{} : TlvDecode(resp);
}

TlvList HapPairing::PairVerifyPost(SOCKET sock, const std::string& host,
                                    const std::vector<uint8_t>& body)
{
    const auto resp = HttpPost(sock, host, "/pair-verify",
                               "application/pairing+tlv8", body);
    return resp.empty() ? TlvList{} : TlvDecode(resp);
}

// ────────────────────────────────────────────────────────────────────────────
// IsPaired
// ────────────────────────────────────────────────────────────────────────────

bool HapPairing::IsPaired(const std::string& hapDeviceId) const
{
    if (hapDeviceId.empty()) return false;
    return CredentialStore::Read(hapDeviceId).has_value();
}

// ────────────────────────────────────────────────────────────────────────────
// Pair — HAP Setup (M1 → M6)
// ────────────────────────────────────────────────────────────────────────────

PairingResult HapPairing::Pair(const AirPlayReceiver&    receiver,
                                const ControllerIdentity& identity,
                                PinCallback               pinCallback)
{
    if (sodium_init() < 0) return PairingResult::ProtocolError;

    const SOCKET sock = Connect(receiver.ipAddress, receiver.airPlay2Port);
    if (sock == INVALID_SOCKET) {
        LOG_WARN("HapPairing: connect to %s:%u failed", receiver.ipAddress.c_str(),
                 receiver.airPlay2Port);
        return PairingResult::NetworkError;
    }

    const std::string host = receiver.ipAddress + ":" +
                             std::to_string(receiver.airPlay2Port);

    // ── M1: SRP Start Request ─────────────────────────────────────────────────
    if (cancelled_) { closesocket(sock); return PairingResult::Cancelled; }

    {
        TlvList m1;
        m1.push_back({TlvType::Method, {0x00}});  // PairSetup method
        m1.push_back({TlvType::State,  {0x01}});  // M1

        const TlvList resp1 = PairSetupPost(sock, host, TlvEncode(m1));
        if (resp1.empty()) { closesocket(sock); return PairingResult::NetworkError; }

        // Check for error
        const TlvItem* errItem = TlvFind(resp1, TlvType::Error);
        if (errItem && !errItem->value.empty()) {
            closesocket(sock);
            if (errItem->value[0] == static_cast<uint8_t>(TlvError::Authentication))
                return PairingResult::AuthFailed;
            return PairingResult::ProtocolError;
        }

        const TlvItem* saltItem = TlvFind(resp1, TlvType::Salt);
        const TlvItem* pubKeyItem = TlvFind(resp1, TlvType::PublicKey);
        if (!saltItem || !pubKeyItem) { closesocket(sock); return PairingResult::ProtocolError; }

        // Get PIN from user
        const std::string pin = pinCallback ? pinCallback() : "00000000";
        if (pin.empty() || cancelled_) { closesocket(sock); return PairingResult::PinCancelled; }

        // ── M3: SRP Verify Request ────────────────────────────────────────────
        // Use SRP-6a via csrp with SHA-512 and 3072-bit group
        SRPUser* user = srp_user_new(SRP_SHA512, SRP_NG_CUSTOM,
                                     "Pair-Setup",
                                     reinterpret_cast<const unsigned char*>(pin.c_str()),
                                     static_cast<int>(pin.size()),
                                     nullptr, nullptr);
        if (!user) { closesocket(sock); return PairingResult::ProtocolError; }

        const char*    authUsername = nullptr;
        unsigned char* bytesA       = nullptr;
        int            lenA         = 0;

        srp_user_start_authentication(user, &authUsername, &bytesA, &lenA);
        if (!bytesA) {
            srp_user_delete(user);
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        // Process server challenge (s/B from M2)
        unsigned char* bytesM = nullptr;
        int            lenM   = 0;
        srp_user_process_challenge(
            user,
            saltItem->value.data(), static_cast<int>(saltItem->value.size()),
            pubKeyItem->value.data(), static_cast<int>(pubKeyItem->value.size()),
            &bytesM, &lenM);

        if (!bytesM) {
            srp_user_delete(user);
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        // Build M3 request
        TlvList m3;
        m3.push_back({TlvType::State,     {0x03}});
        m3.push_back({TlvType::PublicKey,  std::vector<uint8_t>(bytesA, bytesA + lenA)});
        m3.push_back({TlvType::Proof,      std::vector<uint8_t>(bytesM, bytesM + lenM)});
        srp_free(bytesA);

        const TlvList resp3 = PairSetupPost(sock, host, TlvEncode(m3));

        if (resp3.empty()) {
            srp_user_delete(user);
            closesocket(sock);
            return PairingResult::NetworkError;
        }

        const TlvItem* errItem3 = TlvFind(resp3, TlvType::Error);
        if (errItem3 && !errItem3->value.empty()) {
            srp_user_delete(user);
            closesocket(sock);
            if (errItem3->value[0] == static_cast<uint8_t>(TlvError::Authentication))
                return PairingResult::AuthFailed;
            return PairingResult::ProtocolError;
        }

        const TlvItem* hamkItem = TlvFind(resp3, TlvType::Proof);
        if (!hamkItem) {
            srp_user_delete(user);
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        // Verify HAMK (M4)
        srp_user_verify_session(user, hamkItem->value.data());
        if (!srp_user_is_authenticated(user)) {
            srp_user_delete(user);
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        // Get SRP shared session key (K)
        int srpKeyLen = 0;
        const unsigned char* srpKey = srp_user_get_session_key(user, &srpKeyLen);

        // ── Derive HAP session keys via HKDF-SHA512 ───────────────────────────
        // Per HAP spec: info = "Pair-Setup-Encrypt-Salt" / "Pair-Setup-Encrypt-Info"
        static const uint8_t kSetupSalt[] = "Pair-Setup-Encrypt-Salt";
        static const uint8_t kSetupInfo[] = "Pair-Setup-Encrypt-Info";
        const auto sessionEncKey = Hkdf(srpKey, static_cast<size_t>(srpKeyLen),
                                        kSetupSalt, sizeof(kSetupSalt) - 1,
                                        kSetupInfo, sizeof(kSetupInfo) - 1, 32);

        // Also derive controller sign key seed
        static const uint8_t kSignSalt[] = "Pair-Setup-Controller-Sign-Salt";
        static const uint8_t kSignInfo[] = "Pair-Setup-Controller-Sign-Info";
        const auto controllerSignSeed = Hkdf(srpKey, static_cast<size_t>(srpKeyLen),
                                             kSignSalt, sizeof(kSignSalt) - 1,
                                             kSignInfo, sizeof(kSignInfo) - 1, 32);

        srp_user_delete(user);
        if (cancelled_) { closesocket(sock); return PairingResult::Cancelled; }

        // ── M5: Exchange (Controller → Device) ───────────────────────────────
        // Build sub-TLV: identifier | ltpk | signature(controllerSignSeed | ltpk)
        // Per HAP: sign( HKDF(SRP_K, "Pair-Setup-Controller-Sign-Salt", "Pair-Setup-Controller-Sign-Info")
        //                     || controllerIdentifier || controllerLTPK )

        // Build signing material
        std::vector<uint8_t> signMaterial;
        signMaterial.insert(signMaterial.end(),
                            controllerSignSeed.begin(), controllerSignSeed.end());
        const std::string& ctrlId = identity.controllerId;
        signMaterial.insert(signMaterial.end(), ctrlId.begin(), ctrlId.end());
        signMaterial.insert(signMaterial.end(),
                            identity.ltpk.begin(), identity.ltpk.end());

        std::array<uint8_t, 64> sig{};
        if (crypto_sign_detached(sig.data(), nullptr,
                                 signMaterial.data(), signMaterial.size(),
                                 identity.ltsk.data()) != 0) {
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        // Build sub-TLV
        TlvList subTlv;
        subTlv.push_back({TlvType::Identifier,
                          std::vector<uint8_t>(ctrlId.begin(), ctrlId.end())});
        subTlv.push_back({TlvType::PublicKey,
                          std::vector<uint8_t>(identity.ltpk.begin(), identity.ltpk.end())});
        subTlv.push_back({TlvType::Signature,
                          std::vector<uint8_t>(sig.begin(), sig.end())});

        const std::vector<uint8_t> subTlvBytes = TlvEncode(subTlv);

        // Encrypt sub-TLV with ChaCha20-Poly1305 using sessionEncKey
        // Nonce for M5 = "PS-Msg05"
        static const uint8_t kNonce5[12] = "PS-Msg05";
        std::vector<uint8_t> ciphertext(subTlvBytes.size() + 16);
        crypto_aead_chacha20poly1305_ietf_encrypt(
            ciphertext.data(), nullptr,
            subTlvBytes.data(), subTlvBytes.size(),
            nullptr, 0,
            nullptr,
            kNonce5, sessionEncKey.data());

        TlvList m5;
        m5.push_back({TlvType::State,         {0x05}});
        m5.push_back({TlvType::EncryptedData,  ciphertext});
        const TlvList resp5 = PairSetupPost(sock, host, TlvEncode(m5));

        if (resp5.empty()) { closesocket(sock); return PairingResult::NetworkError; }

        const TlvItem* errItem5 = TlvFind(resp5, TlvType::Error);
        if (errItem5 && !errItem5->value.empty()) {
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        const TlvItem* encData = TlvFind(resp5, TlvType::EncryptedData);
        if (!encData) { closesocket(sock); return PairingResult::ProtocolError; }

        // Decrypt M6 response
        if (encData->value.size() < 16) { closesocket(sock); return PairingResult::ProtocolError; }
        std::vector<uint8_t> plainM6(encData->value.size() - 16);

        static const uint8_t kNonce6[12] = "PS-Msg06";
        if (crypto_aead_chacha20poly1305_ietf_decrypt(
                plainM6.data(), nullptr,
                nullptr,
                encData->value.data(), encData->value.size(),
                nullptr, 0,
                kNonce6, sessionEncKey.data()) != 0) {
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        // Parse device sub-TLV from M6
        const TlvList devSubTlv = TlvDecode(plainM6);
        const TlvItem* devId    = TlvFind(devSubTlv, TlvType::Identifier);
        const TlvItem* devLtpk  = TlvFind(devSubTlv, TlvType::PublicKey);
        const TlvItem* devSig   = TlvFind(devSubTlv, TlvType::Signature);

        if (!devId || !devLtpk || !devSig) {
            closesocket(sock);
            return PairingResult::ProtocolError;
        }
        if (devLtpk->value.size() != 32 || devSig->value.size() != 64) {
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        // Verify device signature
        // Per HAP: sign(HKDF(K, "Pair-Setup-Accessory-Sign-Salt", "Pair-Setup-Accessory-Sign-Info")
        //                    || accessoryIdentifier || accessoryLTPK)
        static const uint8_t kAccSalt[] = "Pair-Setup-Accessory-Sign-Salt";
        static const uint8_t kAccInfo[] = "Pair-Setup-Accessory-Sign-Info";
        const auto accSignSeed = Hkdf(srpKey, static_cast<size_t>(srpKeyLen),
                                      kAccSalt, sizeof(kAccSalt) - 1,
                                      kAccInfo, sizeof(kAccInfo) - 1, 32);

        std::vector<uint8_t> devSignMaterial;
        devSignMaterial.insert(devSignMaterial.end(), accSignSeed.begin(), accSignSeed.end());
        devSignMaterial.insert(devSignMaterial.end(), devId->value.begin(), devId->value.end());
        devSignMaterial.insert(devSignMaterial.end(), devLtpk->value.begin(), devLtpk->value.end());

        if (crypto_sign_verify_detached(devSig->value.data(),
                                        devSignMaterial.data(), devSignMaterial.size(),
                                        devLtpk->value.data()) != 0) {
            LOG_WARN("HapPairing: device signature verification failed");
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        // ── Store credential ──────────────────────────────────────────────────
        const std::string hapDeviceId = CredentialStore::DeviceIdFromPublicKey(
            receiver.hapDevicePublicKey);

        PairingCredential cred;
        cred.controllerId     = identity.controllerId;
        std::copy(identity.ltpk.begin(), identity.ltpk.end(), cred.controllerLtpk.begin());
        std::copy(identity.ltsk.begin(), identity.ltsk.end(), cred.controllerLtsk.begin());
        std::copy(devLtpk->value.begin(), devLtpk->value.end(), cred.deviceLtpk.begin());
        cred.deviceName       = receiver.displayName;

        if (!CredentialStore::Write(hapDeviceId, cred)) {
            closesocket(sock);
            return PairingResult::ProtocolError;
        }

        closesocket(sock);
        LOG_INFO("HapPairing: pairing with \"%ls\" successful", receiver.displayName.c_str());
        return PairingResult::Success;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Verify — HAP Verify (abbreviated 2-step exchange)
// ────────────────────────────────────────────────────────────────────────────

PairingResult HapPairing::Verify(const AirPlayReceiver&    receiver,
                                  const PairingCredential&  credential)
{
    if (sodium_init() < 0) return PairingResult::ProtocolError;

    const SOCKET sock = Connect(receiver.ipAddress, receiver.airPlay2Port);
    if (sock == INVALID_SOCKET) return PairingResult::NetworkError;

    const std::string host = receiver.ipAddress + ":" +
                             std::to_string(receiver.airPlay2Port);

    // Generate ephemeral Curve25519 keypair
    std::array<uint8_t, 32> ourPk{}, ourSk{};
    crypto_box_keypair(ourPk.data(), ourSk.data());

    // ── M1: Send our Curve25519 public key ─────────────────────────────────
    TlvList m1;
    m1.push_back({TlvType::State,     {0x01}});
    m1.push_back({TlvType::PublicKey, std::vector<uint8_t>(ourPk.begin(), ourPk.end())});
    const TlvList resp1 = PairVerifyPost(sock, host, TlvEncode(m1));

    if (resp1.empty()) { closesocket(sock); return PairingResult::NetworkError; }

    const TlvItem* errItem = TlvFind(resp1, TlvType::Error);
    if (errItem && !errItem->value.empty()) {
        closesocket(sock);
        if (errItem->value[0] == static_cast<uint8_t>(TlvError::Authentication))
            return PairingResult::AuthFailed;
        return PairingResult::ProtocolError;
    }

    const TlvItem* devPkItem  = TlvFind(resp1, TlvType::PublicKey);
    const TlvItem* encDataItem = TlvFind(resp1, TlvType::EncryptedData);
    if (!devPkItem || !encDataItem) { closesocket(sock); return PairingResult::ProtocolError; }
    if (devPkItem->value.size() != 32) { closesocket(sock); return PairingResult::ProtocolError; }

    // Derive shared secret
    std::array<uint8_t, 32> sharedSecret{};
    crypto_scalarmult_curve25519(sharedSecret.data(), ourSk.data(), devPkItem->value.data());

    // Derive session key via HKDF-SHA256 (HAP Pair-Verify requires SHA-256)
    static const uint8_t kVerifySalt[] = "Pair-Verify-Encrypt-Salt";
    static const uint8_t kVerifyInfo[] = "Pair-Verify-Encrypt-Info";
    const auto verifyKey = HkdfSha256(sharedSecret.data(), 32,
                                kVerifySalt, sizeof(kVerifySalt) - 1,
                                kVerifyInfo, sizeof(kVerifyInfo) - 1, 32);

    // Decrypt device sub-TLV
    if (encDataItem->value.size() < 16) { closesocket(sock); return PairingResult::ProtocolError; }
    std::vector<uint8_t> plain(encDataItem->value.size() - 16);
    static const uint8_t kNonceV1[12] = "PV-Msg02";
    if (crypto_aead_chacha20poly1305_ietf_decrypt(
            plain.data(), nullptr, nullptr,
            encDataItem->value.data(), encDataItem->value.size(),
            nullptr, 0,
            kNonceV1, verifyKey.data()) != 0) {
        closesocket(sock);
        return PairingResult::AuthFailed;  // Decryption failure = stale credential
    }

    const TlvList devSubTlv = TlvDecode(plain);
    const TlvItem* devSig   = TlvFind(devSubTlv, TlvType::Signature);
    if (!devSig || devSig->value.size() != 64) { closesocket(sock); return PairingResult::ProtocolError; }

    // Verify device signature: sign(ourPk || devId || devPk)
    std::vector<uint8_t> signMaterial;
    signMaterial.insert(signMaterial.end(), devPkItem->value.begin(), devPkItem->value.end());
    const TlvItem* devIdItem = TlvFind(devSubTlv, TlvType::Identifier);
    if (devIdItem)
        signMaterial.insert(signMaterial.end(), devIdItem->value.begin(), devIdItem->value.end());
    signMaterial.insert(signMaterial.end(), ourPk.begin(), ourPk.end());

    if (crypto_sign_verify_detached(devSig->value.data(),
                                    signMaterial.data(), signMaterial.size(),
                                    credential.deviceLtpk.data()) != 0) {
        closesocket(sock);
        return PairingResult::AuthFailed;
    }

    // ── M3: Send controller sub-TLV ────────────────────────────────────────
    std::vector<uint8_t> ctrlSignMaterial;
    ctrlSignMaterial.insert(ctrlSignMaterial.end(), ourPk.begin(), ourPk.end());
    const std::string& ctrlId = credential.controllerId;
    ctrlSignMaterial.insert(ctrlSignMaterial.end(), ctrlId.begin(), ctrlId.end());
    ctrlSignMaterial.insert(ctrlSignMaterial.end(),
                            credential.controllerLtpk.begin(), credential.controllerLtpk.end());

    std::array<uint8_t, 64> ctrlSig{};
    crypto_sign_detached(ctrlSig.data(), nullptr,
                         ctrlSignMaterial.data(), ctrlSignMaterial.size(),
                         credential.controllerLtsk.data());

    TlvList ctrlSubTlv;
    ctrlSubTlv.push_back({TlvType::Identifier,
                          std::vector<uint8_t>(ctrlId.begin(), ctrlId.end())});
    ctrlSubTlv.push_back({TlvType::Signature,
                          std::vector<uint8_t>(ctrlSig.begin(), ctrlSig.end())});

    const std::vector<uint8_t> ctrlTlvBytes = TlvEncode(ctrlSubTlv);
    std::vector<uint8_t> ctrlCipher(ctrlTlvBytes.size() + 16);
    static const uint8_t kNonceV3[12] = "PV-Msg03";
    crypto_aead_chacha20poly1305_ietf_encrypt(
        ctrlCipher.data(), nullptr,
        ctrlTlvBytes.data(), ctrlTlvBytes.size(),
        nullptr, 0, nullptr,
        kNonceV3, verifyKey.data());

    TlvList m3;
    m3.push_back({TlvType::State,        {0x03}});
    m3.push_back({TlvType::EncryptedData, ctrlCipher});
    const TlvList resp3 = PairVerifyPost(sock, host, TlvEncode(m3));
    closesocket(sock);

    if (resp3.empty()) return PairingResult::NetworkError;

    const TlvItem* errFinal = TlvFind(resp3, TlvType::Error);
    if (errFinal && !errFinal->value.empty()) {
        if (errFinal->value[0] == static_cast<uint8_t>(TlvError::Authentication))
            return PairingResult::AuthFailed;
        return PairingResult::ProtocolError;
    }

    // Derive final session key for audio encryption (HKDF-SHA256 per HAP spec)
    static const uint8_t kFinalSalt[] = "Control-Salt";
    static const uint8_t kFinalInfo[] = "Control-Read-Encryption-Key";
    const auto finalKey = HkdfSha256(sharedSecret.data(), 32,
                               kFinalSalt, sizeof(kFinalSalt) - 1,
                               kFinalInfo, sizeof(kFinalInfo) - 1, 32);
    std::copy(finalKey.begin(), finalKey.end(), sessionKey_.begin());

    LOG_INFO("HapPairing: VERIFY with \"%ls\" successful", receiver.displayName.c_str());
    return PairingResult::Success;
}

} // namespace AirPlay2
