// src/airplay2/CredentialStore.cpp
// Windows Credential Manager wrapper for AirPlay 2 HAP pairing credentials.
// Contract: specs/010-airplay2-support/contracts/credential-store.md

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")

#include "airplay2/CredentialStore.h"
#include "core/Logger.h"

#include <sodium.h>       // libsodium Ed25519 key generation, SHA-256
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <array>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstring>
#include <objbase.h>      // CoCreateGuid

#pragma comment(lib, "ole32.lib")

using json = nlohmann::json;

namespace AirPlay2 {

// ────────────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────────────

namespace {

/// Encode bytes as standard Base64 (no line breaks).
std::string Base64Encode(const uint8_t* data, size_t len)
{
    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v  = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) v |= static_cast<uint32_t>(data[i + 2]);
        out += kTable[(v >> 18) & 0x3F];
        out += kTable[(v >> 12) & 0x3F];
        out += (i + 1 < len) ? kTable[(v >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? kTable[(v)      & 0x3F] : '=';
    }
    return out;
}

/// Decode standard Base64.
std::vector<uint8_t> Base64Decode(const std::string& s)
{
    auto decodeChar = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<uint8_t> out;
    out.reserve((s.size() / 4) * 3);
    for (size_t i = 0; i + 3 < s.size(); i += 4) {
        const int a = decodeChar(s[i]);
        const int b = decodeChar(s[i + 1]);
        const int c = decodeChar(s[i + 2]);
        const int d = decodeChar(s[i + 3]);
        if (a < 0 || b < 0) break;
        out.push_back(static_cast<uint8_t>((a << 2) | (b >> 4)));
        if (c >= 0) out.push_back(static_cast<uint8_t>((b << 4) | (c >> 2)));
        if (d >= 0) out.push_back(static_cast<uint8_t>((c << 6) | d));
    }
    return out;
}

/// Generate a random UUID v4 string.
std::string GenerateUuidV4()
{
    GUID g{};
    CoCreateGuid(&g);
    char buf[40];
    snprintf(buf, sizeof(buf),
             "%08lx-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
             g.Data1, g.Data2, g.Data3,
             (g.Data4[0] << 8) | g.Data4[1],
             g.Data4[2], g.Data4[3], g.Data4[4],
             g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

/// UTF-8 → wide string.
std::wstring ToWide(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

/// Wide → UTF-8 string.
std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

/// Write a CRED_TYPE_GENERIC credential to Windows Credential Manager.
bool CredWrite(const std::wstring& target, const std::string& blobUtf8)
{
    CREDENTIALW cred{};
    cred.Type            = CRED_TYPE_GENERIC;
    cred.TargetName      = const_cast<LPWSTR>(target.c_str());
    cred.Comment         = const_cast<LPWSTR>(L"AirBeam AirPlay 2 credential");
    cred.CredentialBlobSize = static_cast<DWORD>(blobUtf8.size());
    cred.CredentialBlob  = reinterpret_cast<LPBYTE>(const_cast<char*>(blobUtf8.c_str()));
    cred.Persist         = CRED_PERSIST_LOCAL_MACHINE;
    const BOOL ok = CredWriteW(&cred, 0);
    if (!ok) {
        LOG_WARN("CredentialStore: CredWriteW failed for \"%ls\" (err=%lu)",
                 target.c_str(), GetLastError());
    }
    return ok == TRUE;
}

/// Read a CRED_TYPE_GENERIC credential from Windows Credential Manager.
/// Returns empty string on failure / not found.
std::string CredRead(const std::wstring& target)
{
    PCREDENTIALW p = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &p))
        return {};
    std::string blob(reinterpret_cast<const char*>(p->CredentialBlob),
                     p->CredentialBlobSize);
    CredFree(p);
    return blob;
}

} // anonymous namespace

// ────────────────────────────────────────────────────────────────────────────
// CredentialStore — target name helper
// ────────────────────────────────────────────────────────────────────────────

/*static*/ std::wstring CredentialStore::DeviceTarget(const std::string& hapDeviceId)
{
    return std::wstring(L"AirBeam:AirPlay2:") + ToWide(hapDeviceId);
}

// ────────────────────────────────────────────────────────────────────────────
// EnsureControllerIdentity
// ────────────────────────────────────────────────────────────────────────────

/*static*/ ControllerIdentity CredentialStore::EnsureControllerIdentity()
{
    // Try to load existing identity
    const std::string blob = CredRead(kIdentityTarget);
    if (!blob.empty()) {
        try {
            const json j = json::parse(blob);
            if (j.value("version", 0) == 1) {
                ControllerIdentity id;
                id.controllerId = j.at("controller_id").get<std::string>();
                const auto ltpkBytes = Base64Decode(j.at("ltpk").get<std::string>());
                const auto ltskBytes = Base64Decode(j.at("ltsk").get<std::string>());
                if (ltpkBytes.size() == 32 && ltskBytes.size() == 64) {
                    std::copy(ltpkBytes.begin(), ltpkBytes.end(), id.ltpk.begin());
                    std::copy(ltskBytes.begin(), ltskBytes.end(), id.ltsk.begin());
                    return id;
                }
            }
        }
        catch (const std::exception& e) {
            LOG_WARN("CredentialStore: corrupt controller identity (%s), regenerating", e.what());
        }
    }

    // Generate new identity
    if (sodium_init() < 0)
        throw std::runtime_error("libsodium init failed");

    ControllerIdentity id;
    id.controllerId = GenerateUuidV4();

    // Generate Ed25519 keypair
    if (crypto_sign_keypair(id.ltpk.data(), id.ltsk.data()) != 0)
        throw std::runtime_error("Ed25519 key generation failed");

    // Persist
    json j;
    j["version"]       = 1;
    j["controller_id"] = id.controllerId;
    j["ltpk"]          = Base64Encode(id.ltpk.data(), 32);
    j["ltsk"]          = Base64Encode(id.ltsk.data(), 64);

    if (!CredWrite(kIdentityTarget, j.dump()))
        throw std::runtime_error("Failed to persist controller identity to Credential Manager");

    LOG_INFO("CredentialStore: generated new controller identity %s", id.controllerId.c_str());
    return id;
}

// ────────────────────────────────────────────────────────────────────────────
// Read
// ────────────────────────────────────────────────────────────────────────────

/*static*/ std::optional<PairingCredential> CredentialStore::Read(const std::string& hapDeviceId)
{
    const std::string blob = CredRead(DeviceTarget(hapDeviceId));
    if (blob.empty()) return std::nullopt;

    try {
        const json j = json::parse(blob);
        const int ver = j.value("version", 0);
        if (ver != 1) {
            LOG_WARN("CredentialStore: unknown version %d for device %s — deleting",
                     ver, hapDeviceId.c_str());
            Delete(hapDeviceId);
            return std::nullopt;
        }

        PairingCredential cred;
        cred.controllerId = j.at("controller_id").get<std::string>();
        cred.deviceName   = ToWide(j.at("device_name").get<std::string>());

        const auto cpk = Base64Decode(j.at("controller_ltpk").get<std::string>());
        const auto csk = Base64Decode(j.at("controller_ltsk").get<std::string>());
        const auto dpk = Base64Decode(j.at("device_ltpk").get<std::string>());

        if (cpk.size() != 32 || csk.size() != 64 || dpk.size() != 32)
            throw std::runtime_error("key size mismatch");

        std::copy(cpk.begin(), cpk.end(), cred.controllerLtpk.begin());
        std::copy(csk.begin(), csk.end(), cred.controllerLtsk.begin());
        std::copy(dpk.begin(), dpk.end(), cred.deviceLtpk.begin());

        return cred;
    }
    catch (const std::exception& e) {
        LOG_WARN("CredentialStore: corrupt credential for device %s (%s) — deleting",
                 hapDeviceId.c_str(), e.what());
        Delete(hapDeviceId);
        return std::nullopt;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Write
// ────────────────────────────────────────────────────────────────────────────

/*static*/ bool CredentialStore::Write(const std::string& hapDeviceId,
                                       const PairingCredential& cred)
{
    json j;
    j["version"]          = 1;
    j["controller_id"]    = cred.controllerId;
    j["controller_ltpk"]  = Base64Encode(cred.controllerLtpk.data(), 32);
    j["controller_ltsk"]  = Base64Encode(cred.controllerLtsk.data(), 64);
    j["device_ltpk"]      = Base64Encode(cred.deviceLtpk.data(), 32);
    j["device_name"]      = ToUtf8(cred.deviceName);

    return CredWrite(DeviceTarget(hapDeviceId), j.dump());
}

// ────────────────────────────────────────────────────────────────────────────
// Delete
// ────────────────────────────────────────────────────────────────────────────

/*static*/ void CredentialStore::Delete(const std::string& hapDeviceId)
{
    const std::wstring target = DeviceTarget(hapDeviceId);
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        const DWORD err = GetLastError();
        if (err != ERROR_NOT_FOUND)
            LOG_WARN("CredentialStore: CredDeleteW failed (err=%lu)", err);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// DeviceIdFromPublicKey
// ────────────────────────────────────────────────────────────────────────────

/*static*/ std::string CredentialStore::DeviceIdFromPublicKey(
    const std::string& hapDevicePublicKeyBase64)
{
    if (hapDevicePublicKeyBase64.empty()) return {};

    const std::vector<uint8_t> pkBytes = Base64Decode(hapDevicePublicKeyBase64);
    if (pkBytes.empty()) return {};

    // SHA-256 of the raw key bytes
    uint8_t hash[crypto_hash_sha256_BYTES];
    if (sodium_init() < 0) return {};
    crypto_hash_sha256(hash, pkBytes.data(), pkBytes.size());

    // First 6 bytes → 12 uppercase hex chars
    std::ostringstream oss;
    for (int i = 0; i < 6; ++i)
        oss << std::uppercase << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<unsigned>(hash[i]);
    return oss.str();
}

} // namespace AirPlay2
