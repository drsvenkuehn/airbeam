#pragma once
#include <windows.h>
#include <cstdint>

constexpr UINT WM_TRAY_CALLBACK      = WM_APP + 1;
constexpr UINT WM_TRAY_POPUP_MENU    = WM_APP + 2;
constexpr UINT WM_RAOP_CONNECTED     = WM_APP + 3;
constexpr UINT WM_RAOP_FAILED        = WM_APP + 4;
constexpr UINT WM_RECEIVERS_UPDATED  = WM_APP + 5;

/// Posted by WasapiCapture when the default audio render device is lost/changed.
/// Triggers capture-only restart inside ConnectionController::OnAudioDeviceLost().
/// WPARAM: 0  LPARAM: 0
constexpr UINT WM_AUDIO_DEVICE_LOST  = WM_APP + 6;

// Backward-compat alias (feature 007/008 callers not yet updated)
constexpr UINT WM_DEFAULT_DEVICE_CHANGED = WM_AUDIO_DEVICE_LOST;

constexpr UINT WM_BONJOUR_MISSING    = WM_APP + 7;

/// Posted by ConnectionController after all three pipeline threads are confirmed stopped.
/// WPARAM: 0  LPARAM: 0
constexpr UINT WM_STREAM_STOPPED     = WM_APP + 8;

// Backward-compat alias
constexpr UINT WM_TEARDOWN_COMPLETE  = WM_STREAM_STOPPED;

constexpr UINT WM_UPDATE_REJECTED    = WM_APP + 9;

/// Feature 007: unrecoverable capture failure — WPARAM=HRESULT, LPARAM=0
constexpr UINT WM_CAPTURE_ERROR      = WM_APP + 10;

/// Posted by ConnectionController after AlacEncoderThread::Start() succeeds.
/// Signals AppController to refresh the tray menu (show Disconnect, disable speaker list).
/// WPARAM: 0  LPARAM: 0
constexpr UINT WM_STREAM_STARTED     = WM_APP + 11;

/// Posted by MdnsDiscovery when a specific AirPlay speaker disappears from the network.
/// LPARAM: pointer to a heap-allocated std::wstring containing the service instance name.
///         The receiver (ConnectionController) MUST delete this pointer after reading it.
constexpr UINT WM_SPEAKER_LOST       = WM_APP + 12;

/// Posted by MdnsDiscovery when a new AirPlay receiver is fully resolved and added.
/// LPARAM: pointer to a heap-allocated std::wstring containing the receiver stableId (MAC).
///         The receiver (ConnectionController) MUST delete this pointer after reading it.
constexpr UINT WM_DEVICE_DISCOVERED  = WM_APP + 13;

/// Posted by AlacEncoderThread on unexpected thread exit (not clean Stop()).
/// WPARAM: HRESULT error code  LPARAM: 0
constexpr UINT WM_ENCODER_ERROR      = WM_APP + 14;

// ── AirPlay 2 messages (Feature 010) ─────────────────────────────────────────

/// HAP pairing ceremony required for this receiver.
/// LPARAM: heap-allocated AirPlayReceiver* — caller must delete after handling.
constexpr UINT WM_AP2_PAIRING_REQUIRED = WM_APP + 15;

/// Stored credential is stale (device was factory-reset or key mismatch).
/// LPARAM: heap-allocated AirPlayReceiver* — caller must delete after handling.
constexpr UINT WM_AP2_PAIRING_STALE    = WM_APP + 16;

/// AirPlay 2 stream is live and audio is flowing.
/// LPARAM: heap-allocated AirPlayReceiver* — caller must delete after handling.
constexpr UINT WM_AP2_CONNECTED        = WM_APP + 17;

/// AirPlay 2 session failed. WPARAM = error code (see AP2_ERROR_*).
/// LPARAM: heap-allocated AirPlayReceiver* — caller must delete after handling.
constexpr UINT WM_AP2_FAILED           = WM_APP + 18;

/// AirPlay 2 speaker dropped mid-stream (network loss, device restart).
/// LPARAM: heap-allocated AirPlayReceiver* — caller must delete after handling.
constexpr UINT WM_AP2_SPEAKER_DROPPED  = WM_APP + 19;

/// Error code for WM_AP2_FAILED: AP2 control port is unreachable.
/// AppController shows firewall notification; does NOT retry (FR-021).
constexpr uintptr_t AP2_ERROR_PORT_UNREACHABLE = 0xAB200001UL;
