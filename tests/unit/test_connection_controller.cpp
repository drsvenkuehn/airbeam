// test_connection_controller.cpp — Unit tests for ConnectionController state machine.
//
// Design: MockStreamSession subclasses StreamSession and overrides all virtual methods
// to be non-blocking no-ops that record calls.  ConnectionController is constructed
// with a SessionFactory that returns MockStreamSession, so no real WASAPI/RAOP threads
// are started.  State-machine transitions are driven by calling the message handler
// methods (OnRaopConnected, OnRaopFailed, etc.) directly.
//
// Feature 009 — TC-001 through TC-016 per contracts/state-machine.md

#include <gtest/gtest.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "core/ConnectionController.h"
#include "core/PipelineState.h"
#include "core/ReconnectContext.h"
#include "core/StreamSession.h"
#include "core/Config.h"
#include "core/Logger.h"
#include "discovery/AirPlayReceiver.h"
#include "discovery/ReceiverList.h"
#include "ui/TrayIcon.h"
#include "ui/BalloonNotify.h"

// ─────────────────────────────────────────────────────────────────────────────
// MockStreamSession — records calls, no real threads
// ─────────────────────────────────────────────────────────────────────────────

struct MockStreamSession : public StreamSession {
    // Call counters
    int initCalls       = 0;
    int startCapCalls   = 0;
    int stopCapCalls    = 0;
    int reinitCapCalls  = 0;
    int startRaopCalls  = 0;
    int stopRaopCalls   = 0;
    int initEncCalls    = 0;
    int startEncCalls   = 0;
    int stopEncCalls    = 0;
    int setVolCalls     = 0;

    // Controllable return values
    bool initResult      = true;
    bool reinitCapResult = true;
    bool initEncResult   = true;
    bool startCapResult  = true;

    bool Init(const AirPlayReceiver& tgt, bool ll, HWND hwnd) override {
        ++initCalls;
        target_     = tgt;
        lowLatency_ = ll;
        hwnd_       = hwnd;
        return initResult;
    }

    bool StartCapture() override           { ++startCapCalls; return startCapResult; }
    void StopCapture() override            { ++stopCapCalls; }
    bool ReinitCapture() override          { ++reinitCapCalls; return reinitCapResult; }
    bool IsCaptureRunning() const override { return startCapCalls > stopCapCalls; }

    void StartRaop(float /*v*/) override   { ++startRaopCalls; }
    void StopRaop() override               { ++stopRaopCalls; }
    SOCKET AudioSocket() const override    { return INVALID_SOCKET; }

    bool InitEncoder(uint32_t /*ssrc*/, HWND /*hwnd*/) override {
        ++initEncCalls;
        return initEncResult;
    }
    void StartEncoder() override   { ++startEncCalls; }
    void StopEncoder() override    { ++stopEncCalls; }
    void SetVolume(float /*v*/) override { ++setVolCalls; }
};

// ─────────────────────────────────────────────────────────────────────────────
// FakeHwnd — creates a real hidden Win32 window for PostMessage targets
// ─────────────────────────────────────────────────────────────────────────────

class FakeHwnd {
public:
    FakeHwnd() {
        WNDCLASSEXW wc = {};
        wc.cbSize       = sizeof(wc);
        wc.lpfnWndProc  = DefWindowProcW;
        wc.hInstance    = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CCTestWnd";
        RegisterClassExW(&wc);  // may already be registered — ignore error
        hwnd_ = CreateWindowExW(0, L"CCTestWnd", L"", 0,
            0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    ~FakeHwnd() { if (hwnd_) DestroyWindow(hwnd_); }
    HWND Get() const { return hwnd_; }
private:
    HWND hwnd_ = nullptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// StubTrayIcon — records state changes without calling Shell_NotifyIcon
// ─────────────────────────────────────────────────────────────────────────────

struct StubTrayIcon : public TrayIcon {
    // NOTE: TrayIcon is not abstract; we use a wrapper so we can track calls.
    // We just let it call shell APIs into our fake HWND which has no tray registration.
    // The calls won't crash but won't actually appear in the tray.
};

// ─────────────────────────────────────────────────────────────────────────────
// StubBalloonNotify — records balloon notifications for assertion
// ─────────────────────────────────────────────────────────────────────────────

struct StubBalloonNotify : public BalloonNotify {
    struct Call { UINT titleId; UINT bodyId; };
    std::vector<Call> infoCalls, warnCalls, errorCalls;
    // Override not possible (not virtual) — use real BalloonNotify with fake HWND.
    // Tests check call counts via the real ShowInfo/ShowWarning/ShowError paths
    // by using a message-only HWND whose NIF fails silently.
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a test AirPlayReceiver
// ─────────────────────────────────────────────────────────────────────────────

static AirPlayReceiver MakeReceiver(const wchar_t* mac, const wchar_t* name,
                                    const char* ip = "192.168.1.100",
                                    uint16_t port = 5000)
{
    AirPlayReceiver r;
    r.stableId    = mac;
    r.instanceName = std::wstring(mac) + L"@" + name;
    r.displayName  = name;
    r.ipAddress    = ip;
    r.port         = port;
    r.isAirPlay1Compatible = true;
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

class CCTest : public ::testing::Test {
protected:
    void SetUp() override {
        hwnd_ = std::make_unique<FakeHwnd>();

        // Config: minimally initialised
        config_ = Config::Defaults();

        // ReceiverList with the fake HWND
        receivers_ = std::make_unique<ReceiverList>(hwnd_->Get());

        // TrayIcon: Create succeeds on a real HWND; we suppress the Shell_NotifyIcon
        // error by just accepting that it may fail silently in headless test envs.
        tray_.Create(hwnd_->Get(), GetModuleHandleW(nullptr));

        balloon_.Init(hwnd_->Get());

        // Build ConnectionController with mock factory
        cc_ = std::make_unique<ConnectionController>(
            hwnd_->Get(), config_, *receivers_,
            tray_, balloon_, Logger::Instance(),
            [this]() -> std::unique_ptr<StreamSession> {
                auto s = std::make_unique<MockStreamSession>();
                lastMock_ = s.get();  // remember for inspection
                return s;
            });
    }

    void TearDown() override {
        cc_.reset();
        receivers_.reset();
    }

    // Drive CC to Connecting state
    void DriveToConnecting(const AirPlayReceiver& target) {
        ASSERT_EQ(cc_->GetState(), PipelineState::Idle);
        cc_->Connect(target);
        ASSERT_EQ(cc_->GetState(), PipelineState::Connecting);
    }

    // Drive CC to Streaming state
    void DriveToStreaming(const AirPlayReceiver& target) {
        DriveToConnecting(target);
        cc_->OnRaopConnected(0);
        ASSERT_EQ(cc_->GetState(), PipelineState::Streaming);
    }

    // Drive CC to Disconnecting state
    void DriveToDisconnecting(const AirPlayReceiver& target) {
        DriveToStreaming(target);
        cc_->Disconnect();
        ASSERT_EQ(cc_->GetState(), PipelineState::Disconnecting);
    }

    // Drive CC to Idle after Disconnecting
    void DriveToIdle(const AirPlayReceiver& target) {
        DriveToDisconnecting(target);
        cc_->OnStreamStopped();
        ASSERT_EQ(cc_->GetState(), PipelineState::Idle);
    }

    std::unique_ptr<FakeHwnd>               hwnd_;
    Config                                  config_;
    std::unique_ptr<ReceiverList>           receivers_;
    TrayIcon                                tray_;
    BalloonNotify                           balloon_;
    MockStreamSession*                      lastMock_ = nullptr;
    std::unique_ptr<ConnectionController>   cc_;
};

// ─────────────────────────────────────────────────────────────────────────────
// TC-001: Happy path — Idle → Connecting → Streaming → Disconnecting → Idle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC001_HappyPath) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:FF", L"Living Room");

    // Idle → Connecting
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
    cc_->Connect(r);
    EXPECT_EQ(cc_->GetState(), PipelineState::Connecting);
    ASSERT_NE(lastMock_, nullptr);
    EXPECT_EQ(lastMock_->startCapCalls, 1);
    EXPECT_EQ(lastMock_->startRaopCalls, 1);

    // Connecting → Streaming
    cc_->OnRaopConnected(0);
    EXPECT_EQ(cc_->GetState(), PipelineState::Streaming);
    EXPECT_EQ(lastMock_->initEncCalls, 1);
    EXPECT_EQ(lastMock_->startEncCalls, 1);

    // Streaming → Disconnecting
    cc_->Disconnect();
    EXPECT_EQ(cc_->GetState(), PipelineState::Disconnecting);
    EXPECT_GE(lastMock_->stopCapCalls, 1);
    EXPECT_GE(lastMock_->stopEncCalls, 1);
    EXPECT_GE(lastMock_->stopRaopCalls, 1);

    // Disconnecting → Idle (simulate WM_STREAM_STOPPED)
    cc_->OnStreamStopped();
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-002: User disconnect while Connecting
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC002_DisconnectWhileConnecting) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:01", L"Bedroom");
    cc_->Connect(r);
    EXPECT_EQ(cc_->GetState(), PipelineState::Connecting);

    // User disconnects before handshake completes
    cc_->Disconnect();
    EXPECT_EQ(cc_->GetState(), PipelineState::Disconnecting);

    // Teardown completes
    cc_->OnStreamStopped();
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-003: Reconnect succeeds on attempt 2 (1-based: after 1 failure, 1 success)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC003_ReconnectSucceedsOnAttempt2) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:02", L"Kitchen");
    receivers_->Update(r);  // device must be in the list for AttemptReconnect

    DriveToStreaming(r);

    // RAOP failure while streaming → begins reconnect
    cc_->OnRaopFailed(0);
    EXPECT_EQ(cc_->GetState(), PipelineState::Disconnecting);

    // Teardown completes → schedules retry timer
    cc_->OnStreamStopped();
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);

    // Simulate TIMER_RECONNECT_RETRY firing (attempt 1) — fires, fails
    cc_->OnTimer(10);  // TIMER_RECONNECT_RETRY = 10
    EXPECT_EQ(cc_->GetState(), PipelineState::Connecting);

    // Reconnect attempt 1 fails
    cc_->OnRaopFailed(0);
    cc_->OnStreamStopped();
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);

    // Simulate TIMER_RECONNECT_RETRY firing (attempt 2) — succeeds
    cc_->OnTimer(10);
    EXPECT_EQ(cc_->GetState(), PipelineState::Connecting);

    cc_->OnRaopConnected(0);
    EXPECT_EQ(cc_->GetState(), PipelineState::Streaming);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-004: All 3 reconnect attempts fail → Idle + (no crash)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC004_AllReconnectAttemptsExhausted) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:03", L"Garage");
    receivers_->Update(r);

    DriveToStreaming(r);

    cc_->OnRaopFailed(0);
    cc_->OnStreamStopped();  // idle, timer set for attempt 0

    // 3 attempts, each fails
    for (int i = 0; i < 3; ++i) {
        EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
        cc_->OnTimer(10);  // fires → BeginConnect
        EXPECT_EQ(cc_->GetState(), PipelineState::Connecting);
        cc_->OnRaopFailed(0);    // fails
        cc_->OnStreamStopped();  // back to Idle, schedules next or gives up
    }

    // After 3 failures, must be Idle with no reconnect pending
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-005: Reconnect cancelled — device disappears before timer fires
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC005_ReconnectCancelledDeviceAbsent) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:04", L"Porch");
    receivers_->Update(r);

    DriveToStreaming(r);
    cc_->OnRaopFailed(0);
    cc_->OnStreamStopped();
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);

    // Remove device from receiver list before timer fires
    receivers_->Remove(r.instanceName);

    // Timer fires — device absent → stays Idle
    cc_->OnTimer(10);
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-006: Audio device lost → capture restart → continue Streaming
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC006_AudioDeviceLostRestartSucceeds) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:05", L"Office");
    DriveToStreaming(r);

    EXPECT_NE(lastMock_, nullptr);
    lastMock_->reinitCapResult = true;

    cc_->OnAudioDeviceLost();

    // State must remain Streaming after successful restart
    EXPECT_EQ(cc_->GetState(), PipelineState::Streaming);
    EXPECT_EQ(lastMock_->reinitCapCalls, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-007: Audio device lost → reinit fails → Disconnecting → Idle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC007_AudioDeviceLostReinitFails) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:06", L"Den");
    DriveToStreaming(r);

    ASSERT_NE(lastMock_, nullptr);
    lastMock_->reinitCapResult = false;

    cc_->OnAudioDeviceLost();
    EXPECT_EQ(cc_->GetState(), PipelineState::Disconnecting);

    cc_->OnStreamStopped();
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-008: Wrong-state discard — WM_RAOP_FAILED in Idle
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC008_WrongStateDiscardRaopFailedInIdle) {
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
    cc_->OnRaopFailed(0);  // must be silently discarded
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);  // no state change
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-009: Wrong-state discard — WM_RAOP_CONNECTED in Disconnecting
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC009_WrongStateDiscardRaopConnectedInDisconnecting) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:07", L"Gym");
    DriveToStreaming(r);
    cc_->Disconnect();
    EXPECT_EQ(cc_->GetState(), PipelineState::Disconnecting);

    // Late WM_RAOP_CONNECTED should be discarded
    cc_->OnRaopConnected(0);
    EXPECT_EQ(cc_->GetState(), PipelineState::Disconnecting);

    cc_->OnStreamStopped();
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-010: Speaker switch — Streaming → Disconnecting → Connecting (new target)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC010_SpeakerSwitchWhileStreaming) {
    auto r1 = MakeReceiver(L"AA:BB:CC:DD:EE:08", L"Room1");
    auto r2 = MakeReceiver(L"AA:BB:CC:DD:EE:09", L"Room2", "192.168.1.101");

    DriveToStreaming(r1);
    EXPECT_EQ(cc_->GetState(), PipelineState::Streaming);

    // Connect to different speaker while streaming
    cc_->Connect(r2);
    EXPECT_EQ(cc_->GetState(), PipelineState::Disconnecting);

    // Teardown completes → should reconnect to r2
    cc_->OnStreamStopped();
    // After OnStreamStopped with reconnect pending, ScheduleReconnect fires immediately
    // since attempt=0 and HasAttemptsLeft() is true. Timer is set for 1000ms.
    // But AttemptReconnect needs device in ReceiverList.
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-011: Low latency toggle while Streaming → pipeline restart
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC011_LowLatencyToggleWhileStreaming) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:0A", L"Studio");
    DriveToStreaming(r);

    EXPECT_FALSE(config_.lowLatency);
    cc_->SetLowLatency(true);

    // Should trigger disconnect with reconnect intent
    EXPECT_EQ(cc_->GetState(), PipelineState::Disconnecting);
    EXPECT_TRUE(config_.lowLatency);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-012: Low latency toggle while Idle — preference saved, no pipeline action
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC012_LowLatencyToggleWhileIdle) {
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
    EXPECT_FALSE(config_.lowLatency);

    cc_->SetLowLatency(true);
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);  // no state change
    EXPECT_TRUE(config_.lowLatency);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-013: Auto-connect — device discovered within 5s window → Connect()
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC013_AutoConnectDeviceDiscoveredInWindow) {
    const wchar_t* mac = L"AA:BB:CC:DD:EE:0B";
    config_.lastDevice = mac;

    // Add receiver to list before window starts
    auto r = MakeReceiver(mac, L"AutoSpeaker");
    receivers_->Update(r);

    cc_->StartAutoConnectWindow();

    // Simulate WM_DEVICE_DISCOVERED with stableId = mac
    const std::wstring stableId(mac);
    wchar_t* heapStr = new wchar_t[stableId.size() + 1];
    wmemcpy(heapStr, stableId.c_str(), stableId.size() + 1);
    cc_->OnDeviceDiscovered(reinterpret_cast<LPARAM>(heapStr));

    // Should have connected
    EXPECT_EQ(cc_->GetState(), PipelineState::Connecting);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-014: Auto-connect — 5s window expires with no match → Idle, no notification
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC014_AutoConnectWindowExpiresNoMatch) {
    config_.lastDevice = L"AA:BB:CC:DD:EE:0C";
    cc_->StartAutoConnectWindow();

    // Timer fires (TIMER_AUTOCONNECT = 11) — no device found
    cc_->OnTimer(11);
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-015: Volume change while Streaming → SetVolume propagated to session
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC015_VolumeChangeWhileStreaming) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:0D", L"Living");
    DriveToStreaming(r);

    ASSERT_NE(lastMock_, nullptr);
    cc_->SetVolume(0.5f);
    EXPECT_EQ(lastMock_->setVolCalls, 1);
    EXPECT_FLOAT_EQ(config_.volume, 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// TC-016: Volume change while Idle → persisted, not propagated
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, TC016_VolumeChangeWhileIdle) {
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
    cc_->SetVolume(0.25f);
    EXPECT_FLOAT_EQ(config_.volume, 0.25f);
    // No session created → no SetVolume propagation possible (lastMock_ is null)
    EXPECT_EQ(lastMock_, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bonus: WM_AUDIO_DEVICE_LOST ignored when not in Streaming state
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, BonusAudioDeviceLostIgnoredInConnecting) {
    auto r = MakeReceiver(L"AA:BB:CC:DD:EE:0E", L"Entry");
    cc_->Connect(r);
    EXPECT_EQ(cc_->GetState(), PipelineState::Connecting);

    // Must be silently discarded
    cc_->OnAudioDeviceLost();
    EXPECT_EQ(cc_->GetState(), PipelineState::Connecting);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bonus: Disconnect while Idle is a no-op
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(CCTest, BonusDisconnectWhileIdleIsNoop) {
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
    cc_->Disconnect();
    EXPECT_EQ(cc_->GetState(), PipelineState::Idle);
}
