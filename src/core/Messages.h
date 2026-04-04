#pragma once
#include <windows.h>

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
