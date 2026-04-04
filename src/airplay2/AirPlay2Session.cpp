// src/airplay2/AirPlay2Session.cpp
// AirPlay 2 session implementation — HTTP/2 over WinHTTP + AES-GCM + PTP.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include "airplay2/AirPlay2Session.h"
#include "airplay2/CredentialStore.h"
#include "core/Messages.h"
#include "core/Logger.h"

#include <sodium.h>
#include <string>
#include <vector>
#include <sstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

namespace AirPlay2 {

// ────────────────────────────────────────────────────────────────────────────
// Construction / destruction
// ────────────────────────────────────────────────────────────────────────────

AirPlay2Session::AirPlay2Session() = default;

AirPlay2Session::~AirPlay2Session()
{
    StopRaop();
}

// ────────────────────────────────────────────────────────────────────────────
// ProbePort — T024a: TCP reachability check
// ────────────────────────────────────────────────────────────────────────────

bool AirPlay2Session::ProbePort(const std::string& ip, uint16_t port, int timeoutMs)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        closesocket(s);
        return false;
    }

    // Non-blocking connect
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    fd_set fds; FD_ZERO(&fds); FD_SET(s, &fds);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    const bool reachable = (select(0, nullptr, &fds, nullptr, &tv) > 0);

    closesocket(s);
    return reachable;
}

// ────────────────────────────────────────────────────────────────────────────
// Init — T024 + T024a
// ────────────────────────────────────────────────────────────────────────────

bool AirPlay2Session::Init(const AirPlayReceiver& target, bool lowLatency, HWND hwnd)
{
    hwnd_ = hwnd;
    target_      = target;
    lowLatency_  = lowLatency;

    // Step 1: derive hapDeviceId from public key
    hapDeviceId_ = CredentialStore::DeviceIdFromPublicKey(target.hapDevicePublicKey);
    if (hapDeviceId_.empty()) {
        LOG_WARN("AirPlay2Session: empty hapDevicePublicKey for \"%ls\"",
                 target.displayName.c_str());
    }

    // Step 2: Check if paired
    if (!hasPairing_.IsPaired(hapDeviceId_)) {
        LOG_INFO("AirPlay2Session: not paired with \"%ls\" — requesting pairing",
                 target.displayName.c_str());
        // Post WM_AP2_PAIRING_REQUIRED — do NOT post WM_AP2_FAILED
        auto* rec = new AirPlayReceiver(target);
        PostMessageW(hwnd_, WM_AP2_PAIRING_REQUIRED, 0, reinterpret_cast<LPARAM>(rec));
        return false;
    }

    // Step 3: Load credential
    const auto credOpt = CredentialStore::Read(hapDeviceId_);
    if (!credOpt.has_value()) {
        auto* rec = new AirPlayReceiver(target);
        PostMessageW(hwnd_, WM_AP2_PAIRING_REQUIRED, 0, reinterpret_cast<LPARAM>(rec));
        return false;
    }
    const PairingCredential& cred = *credOpt;

    // Step 4: HAP VERIFY
    HapPairing verifyPairing;
    const PairingResult vr = verifyPairing.Verify(target, cred);
    if (vr == PairingResult::AuthFailed) {
        LOG_WARN("AirPlay2Session: HAP VERIFY failed for \"%ls\" — credential stale",
                 target.displayName.c_str());
        auto* rec = new AirPlayReceiver(target);
        PostMessageW(hwnd_, WM_AP2_PAIRING_STALE, 0, reinterpret_cast<LPARAM>(rec));
        return false;
    }
    if (vr != PairingResult::Success) {
        LOG_WARN("AirPlay2Session: HAP VERIFY error %d for \"%ls\"",
                 static_cast<int>(vr), target.displayName.c_str());
        auto* rec = new AirPlayReceiver(target);
        PostMessageW(hwnd_, WM_AP2_FAILED,
                     static_cast<WPARAM>(0),
                     reinterpret_cast<LPARAM>(rec));
        return false;
    }

    // Step 5: Port reachability probe (T024a)
    const uint16_t ctrlPort = target.airPlay2Port ? target.airPlay2Port : 7000;
    if (!ProbePort(target.ipAddress, ctrlPort, 2000)) {
        LOG_WARN("AirPlay2Session: port %u unreachable on %s",
                 ctrlPort, target.ipAddress.c_str());
        auto* rec = new AirPlayReceiver(target);
        PostMessageW(hwnd_, WM_AP2_FAILED,
                     static_cast<WPARAM>(AP2_ERROR_PORT_UNREACHABLE),
                     reinterpret_cast<LPARAM>(rec));
        return false;
    }

    // Step 6: Initialise AES-GCM cipher using session key from VERIFY
    const auto& sk = verifyPairing.SessionKey();
    // Session salt = first 4 bytes of session key
    if (!aesGcm_.Init(sk.data(), sk.data())) {
        LOG_WARN("AirPlay2Session: AesGcmCipher init failed");
        return false;
    }

    // Step 7: Open WinHTTP session
    const std::wstring userAgent = L"AirBeam/1.0";
    hSession_ = WinHttpOpen(userAgent.c_str(),
                            WINHTTP_ACCESS_TYPE_NO_PROXY,
                            WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession_) {
        LOG_WARN("AirPlay2Session: WinHttpOpen failed (%lu)", GetLastError());
        return false;
    }

    // Wide-string IP address for WinHTTP
    const int wLen = MultiByteToWideChar(CP_UTF8, 0,
                                         target.ipAddress.c_str(), -1, nullptr, 0);
    std::wstring wIp(static_cast<size_t>(wLen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, target.ipAddress.c_str(), -1, wIp.data(), wLen);

    hConnect_ = WinHttpConnect(hSession_, wIp.c_str(),
                               static_cast<INTERNET_PORT>(ctrlPort), 0);
    if (!hConnect_) {
        LOG_WARN("AirPlay2Session: WinHttpConnect failed (%lu)", GetLastError());
        WinHttpCloseHandle(hSession_); hSession_ = nullptr;
        return false;
    }

    // Start PTP clock (non-blocking; failure is non-fatal — uses default offset 0)
    ptpClock_.Start(target.ipAddress, 319);

    LOG_INFO("AirPlay2Session: Init OK for \"%ls\" ip=%s port=%u",
             target.displayName.c_str(), target.ipAddress.c_str(), ctrlPort);
    return true;
}

// ────────────────────────────────────────────────────────────────────────────
// Post — HTTP/2 helper (Thread 5)
// ────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> AirPlay2Session::Post(
    const std::wstring& path,
    const std::string&  contentType,
    const std::vector<uint8_t>& body)
{
    if (!hConnect_) return {};

    const DWORD flags = WINHTTP_FLAG_SECURE;
    HINTERNET hReq = WinHttpOpenRequest(
        hConnect_, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) return {};

    // Convert content-type to wide
    const int ctLen = MultiByteToWideChar(CP_UTF8, 0, contentType.c_str(), -1, nullptr, 0);
    std::wstring wCt(static_cast<size_t>(ctLen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, contentType.c_str(), -1, wCt.data(), ctLen);

    const std::wstring headers = L"Content-Type: " + wCt;
    const BOOL sent = WinHttpSendRequest(
        hReq,
        headers.c_str(), static_cast<DWORD>(headers.size()),
        const_cast<void*>(static_cast<const void*>(body.data())),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0);

    if (!sent || !WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq);
        return {};
    }

    std::vector<uint8_t> responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hReq, &bytesAvailable) && bytesAvailable > 0) {
        const size_t offset = responseBody.size();
        responseBody.resize(offset + bytesAvailable);
        DWORD bytesRead = 0;
        WinHttpReadData(hReq, responseBody.data() + offset, bytesAvailable, &bytesRead);
        responseBody.resize(offset + bytesRead);
    }

    WinHttpCloseHandle(hReq);
    return responseBody;
}

// ────────────────────────────────────────────────────────────────────────────
// StartRaop — SETUP POST to /audio
// ────────────────────────────────────────────────────────────────────────────

void AirPlay2Session::StartRaop(float volume)
{
    if (!hConnect_) {
        auto* rec = new AirPlayReceiver(target_);
        PostMessageW(hwnd_, WM_AP2_FAILED, 0, reinterpret_cast<LPARAM>(rec));
        return;
    }

    // Build minimal SETUP body (AirPlay 2 RTSP-over-HTTP/2)
    const std::string setupBody =
        "a=rtpmap:96 AppleLossless/44100\r\n"
        "a=fmtp:96 4096 0 16 40 10 14 2 255 0 0 44100\r\n";

    const auto resp = Post(L"/audio", "application/sdp",
                           {setupBody.begin(), setupBody.end()});

    if (resp.empty()) {
        LOG_WARN("AirPlay2Session: SETUP failed for \"%ls\"",
                 target_.displayName.c_str());
        auto* rec = new AirPlayReceiver(target_);
        PostMessageW(hwnd_, WM_AP2_FAILED, 0, reinterpret_cast<LPARAM>(rec));
        return;
    }

    streaming_.store(true, std::memory_order_release);

    // Set initial volume
    SetVolume(volume);

    // Post WM_AP2_CONNECTED
    auto* rec = new AirPlayReceiver(target_);
    PostMessageW(hwnd_, WM_AP2_CONNECTED, 0, reinterpret_cast<LPARAM>(rec));

    LOG_INFO("AirPlay2Session: streaming to \"%ls\"", target_.displayName.c_str());
}

// ────────────────────────────────────────────────────────────────────────────
// StopRaop — TEARDOWN + cleanup
// ────────────────────────────────────────────────────────────────────────────

void AirPlay2Session::StopRaop()
{
    streaming_.store(false, std::memory_order_release);
    ptpClock_.Stop();

    if (hConnect_) {
        // Send TEARDOWN (best-effort)
        Post(L"/audio", "application/x-apple-binary-plist", {});
        WinHttpCloseHandle(hConnect_); hConnect_ = nullptr;
    }
    if (hSession_) {
        WinHttpCloseHandle(hSession_); hSession_ = nullptr;
    }
    LOG_INFO("AirPlay2Session: stopped for \"%ls\"", target_.displayName.c_str());
}

// ────────────────────────────────────────────────────────────────────────────
// SetVolume — HTTP/2 POST to /controller
// ────────────────────────────────────────────────────────────────────────────

void AirPlay2Session::SetVolume(float linear)
{
    if (!hConnect_ || !streaming_.load(std::memory_order_acquire)) return;

    // Convert linear [0..1] to dB [-144..0]
    const float dB = (linear <= 0.0f) ? -144.0f : 20.0f * std::log10f(linear);

    std::ostringstream oss;
    oss << "volume: " << dB << "\r\n";
    const std::string body = oss.str();
    Post(L"/controller", "text/parameters", {body.begin(), body.end()});
}

} // namespace AirPlay2
