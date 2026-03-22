#pragma once
#include <windows.h>

// Reads / writes the HKCU\Software\Microsoft\Windows\CurrentVersion\Run\AirBeam
// registry value to control launch-at-startup behaviour.
// Hidden in portable mode (caller must check Config::IsPortableMode()).
namespace StartupRegistry {

// Returns true if the Run entry exists and points to this executable.
bool IsEnabled();

// Writes: <exe_path> --startup
void Enable();

// Deletes the Run entry if present.
void Disable();

} // namespace StartupRegistry
