#pragma once

// ── Tooltip strings ──────────────────────────────────────────────────────────
#define IDS_TOOLTIP_IDLE              1001
#define IDS_TOOLTIP_CONNECTING        1002
#define IDS_TOOLTIP_STREAMING         1003
#define IDS_TOOLTIP_ERROR             1004
#define IDS_TOOLTIP_BONJOUR_MISSING   1005

// ── Balloon notification strings ─────────────────────────────────────────────
#define IDS_BALLOON_BONJOUR_MISSING   1010
#define IDS_BALLOON_CONNECTED         1011
#define IDS_BALLOON_DISCONNECTED      1012
#define IDS_BALLOON_CONNECTION_FAILED 1013
#define IDS_BALLOON_CONFIG_RESET      1014
#define IDS_BALLOON_UPDATE_REJECTED   1015
#define IDS_BALLOON_TITLE_BONJOUR_MISSING 1016

// ── Notification title strings ────────────────────────────────────────────────
#define IDS_TITLE_CONNECTED           1050
#define IDS_TITLE_CONNECTION_FAILED   1051
#define IDS_TITLE_RECONNECTED         1052
#define IDS_TITLE_RECONNECT_FAILED    1053
#define IDS_TITLE_SPEAKER_UNAVAILABLE 1054
#define IDS_TITLE_AUDIO_ERROR         1055
#define IDS_TITLE_UPDATE_REJECTED     1056

// ── Menu labels ──────────────────────────────────────────────────────────────
#define IDS_LABEL_AIRPLAY2_UNSUPPORTED 1020
#define IDS_MENU_VOLUME               1021
#define IDS_MENU_LOW_LATENCY          1022
#define IDS_MENU_LAUNCH_AT_STARTUP    1023
#define IDS_MENU_OPEN_LOG_FOLDER      1024
#define IDS_MENU_CHECK_FOR_UPDATES    1025
#define IDS_MENU_QUIT                 1026
#define IDS_INSTALLER_BONJOUR_PROMPT  1027

// ── Speaker menu dynamic strings ─────────────────────────────────────────────
#define IDS_MENU_SEARCHING       1028
#define IDS_MENU_BONJOUR_MISSING 1029
#define IDS_MENU_CONNECTING      1030
#define IDS_MENU_SPEAKERS        1031

// ── WinSparkle Ed25519 public key ─────────────────────────────────────────────
#define IDS_SPARKLE_PUBKEY            1032

// ── Feature 007: WASAPI capture error balloon ─────────────────────────────────
#define IDS_CAPTURE_ERROR_BALLOON     1033

// ── Feature 009: ConnectionController notification strings ───────────────────
#define IDS_RECONNECTED               1040  // "Reconnected to %s."
#define IDS_RECONNECT_FAILED          1041  // "Could not reconnect to %s."
#define IDS_SPEAKER_UNAVAILABLE       1042  // "%s is no longer available."
#define IDS_ERROR_CAPTURE_FAILED      1043  // "Audio capture failed unexpectedly."
#define IDS_ERROR_NO_AUDIO_DEVICE     1044  // "No audio device found."
#define IDS_ENCODER_ERROR             1045  // "Audio encoder error. Please reconnect."

// ── Compile-time URL constants ─────────────────────────────────────────────
#ifdef __cplusplus
constexpr wchar_t BONJOUR_DOWNLOAD_URL[] =
    L"https://support.apple.com/downloads/bonjour-for-windows";
#endif

// ── Tray icon resources ───────────────────────────────────────────────────────
#define IDI_TRAY_IDLE                 2001
#define IDI_TRAY_STREAMING            2002
#define IDI_TRAY_ERROR                2003
#define IDI_TRAY_CONN_001             2011
#define IDI_TRAY_CONN_002             2012
#define IDI_TRAY_CONN_003             2013
#define IDI_TRAY_CONN_004             2014
#define IDI_TRAY_CONN_005             2015
#define IDI_TRAY_CONN_006             2016
#define IDI_TRAY_CONN_007             2017
#define IDI_TRAY_CONN_008             2018
