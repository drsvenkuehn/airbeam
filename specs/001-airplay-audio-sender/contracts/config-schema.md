# Contract: config.json Schema

**Scope**: The AirBeam persistent configuration file format.

---

## File Location

| Mode | Path |
|------|------|
| Normal | `%APPDATA%\AirBeam\config.json` |
| Portable | `<exe_directory>\config.json` (detected at startup if file exists next to exe) |

---

## JSON Schema

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "AirBeam Configuration",
  "type": "object",
  "properties": {
    "lastDevice": {
      "type": "string",
      "description": "Bonjour service instance name of the last successfully connected AirPlay receiver. Empty string = no last device.",
      "default": ""
    },
    "volume": {
      "type": "number",
      "minimum": 0.0,
      "maximum": 1.0,
      "description": "Linear output volume sent to AirPlay receiver. 0.0 = mute, 1.0 = max. Clamped on load if out of range.",
      "default": 1.0
    },
    "lowLatency": {
      "type": "boolean",
      "description": "When true, sender-side audio buffer is reduced from ~1s headroom (128 frames) to ~255ms headroom (32 frames). Increases dropout risk on congested networks.",
      "default": false
    },
    "launchAtStartup": {
      "type": "boolean",
      "description": "When true, AirBeam.exe is registered in HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run. Mirrored from registry state on load.",
      "default": false
    },
    "autoUpdate": {
      "type": "boolean",
      "description": "When true (default), WinSparkle performs a background update check every 24 hours. When false, automatic background checks are suppressed; the 'Check for Updates' tray item still triggers an on-demand check.",
      "default": true
    }
  },
  "additionalProperties": false
}
```

---

## Example Config File

```json
{
  "lastDevice": "Living Room HomePod._raop._tcp.local",
  "volume": 0.75,
  "lowLatency": false,
  "launchAtStartup": true,
  "autoUpdate": true
}
```

---

## Behaviour on Load

| Condition | Action |
|-----------|--------|
| File does not exist | Use all defaults; create file on first save |
| File exists, valid JSON, all keys present | Load values; clamp `volume` to [0.0, 1.0] |
| File exists, valid JSON, some keys missing | Load present keys; fill missing with defaults |
| File exists, corrupt / invalid JSON | Log `WARN "Config corrupted, resetting to defaults"`; use all defaults; overwrite file on next save |
| `volume` outside [0.0, 1.0] | Clamp to range; log `WARN "volume out of range, clamped"` |

---

## Portable Mode Detection

At application startup, before any config read:

```
1. Compute exe_dir = directory containing AirBeam.exe
2. If FileExists(exe_dir + "\\config.json"):
       configPath = exe_dir + "\\config.json"
       portableMode = true
   Else:
       configPath = %APPDATA%\AirBeam\config.json
       portableMode = false
```

In portable mode:
- Config is read/written to `exe_dir\config.json`
- Log files still go to `%APPDATA%\AirBeam\logs\` (log dir is never portable — avoid log file accumulation on USB drives)
- `launchAtStartup` toggle is **disabled** in the tray menu (silently; registry writes are inappropriate for portable installs)

---

## Save Policy

Config is saved immediately after any of these events:
- Volume slider released (`TB_ENDTRACK`)
- Low-latency mode toggled
- Launch-at-startup toggled
- Successful connection to a new device (saves `lastDevice`)
- `autoUpdate` toggled
- Application clean shutdown (`WM_DESTROY`)

Config is saved atomically: write to `config.json.tmp` then `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` to avoid partial-write corruption.
