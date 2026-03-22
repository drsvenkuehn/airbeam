#pragma once

// src/ui/MenuIds.h
//
// Convenience menu-command-ID header for TrayMenu and message handlers.
// Canonical values are defined in core/Commands.h; this header re-exports them
// under the shorter names used in T014 / TrayMenu so that consumers only need
// one include.

#include "core/Commands.h"   // IDM_QUIT, IDM_CHECK_UPDATES, IDM_VOLUME, …

// Shorter aliases for names that differ between Commands.h and T014 spec
inline constexpr UINT IDM_LOW_LATENCY       = IDM_LOW_LATENCY_TOGGLE;
inline constexpr UINT IDM_LAUNCH_AT_STARTUP = IDM_LAUNCH_STARTUP_TOGGLE;
inline constexpr UINT IDM_OPEN_LOG          = IDM_OPEN_LOG_FOLDER;
inline constexpr UINT IDM_SPEAKER_BASE      = IDM_DEVICE_BASE;

// IDM_VOLUME, IDM_CHECK_UPDATES, IDM_QUIT — already defined in Commands.h
// with the same names; no aliases needed.
