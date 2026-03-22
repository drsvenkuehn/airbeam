# Quickstart: WinSparkle Key Pair Setup

**Feature**: 004-winsparkle-autoupdate  
**Date**: 2026-03-22  
**Audience**: Developer performing one-time key generation

---

## Prerequisites

- WinSparkle release zip downloaded (or extracted build output directory available)
- Access to the `drsvenkuehn/airbeam` GitHub repository settings (to add secret)

---

## Step 1 — Download `winsparkle-sign.exe`

```powershell
# Download WinSparkle 0.8.5 release zip
$ver = "0.8.5"
$url = "https://github.com/vslavik/winsparkle/releases/download/v$ver/WinSparkle-$ver.zip"
Invoke-WebRequest $url -OutFile WinSparkle.zip
Expand-Archive WinSparkle.zip -DestinationPath WinSparkle-extracted
# winsparkle-sign.exe is at the root of the extracted zip
$signTool = Get-ChildItem WinSparkle-extracted -Recurse -Filter "winsparkle-sign.exe" | Select-Object -First 1
```

---

## Step 2 — Generate the Key Pair

```powershell
& $signTool.FullName --generate-keys
```

**Output looks like:**
```
Private key: <BASE64_PRIVATE_KEY>
Public key:  <BASE64_PUBLIC_KEY>
```

⚠️ **Copy both values immediately.** The private key is shown only once and cannot be recovered.

---

## Step 3 — Store the Private Key as a GitHub Secret

1. Go to `https://github.com/drsvenkuehn/airbeam/settings/secrets/actions`
2. Click **New repository secret**
3. Name: `SPARKLE_PRIVATE_KEY`
4. Value: the `<BASE64_PRIVATE_KEY>` from Step 2
5. Click **Add secret**

---

## Step 4 — Commit the Public Key

Replace the placeholder in all 7 RC locale files with the `<BASE64_PUBLIC_KEY>` from Step 2.

```powershell
$pubkey = "<BASE64_PUBLIC_KEY>"   # from Step 2

Get-ChildItem resources\locales\strings_*.rc | ForEach-Object {
    (Get-Content $_.FullName -Raw) `
        -replace 'TODO_REPLACE_WITH_ED25519_PUBLIC_KEY', $pubkey |
    Set-Content $_.FullName -NoNewline
}
```

Also update `resources/sparkle_pubkey.txt` — replace the placeholder line:
```
Current value: placeholder (must be replaced before first release)
```
with:
```
Current value: <BASE64_PUBLIC_KEY>
Generated: <DATE>
```

---

## Step 5 — Verify the Embed

Build the project and check that the string is non-placeholder:

```powershell
cmake --preset msvc-x64-debug
cmake --build --preset msvc-x64-debug
# SC-005 check: pubkey length >= 43 chars
$key = Select-String "TODO_REPLACE" resources\locales\strings_en.rc
if ($key) { Write-Error "Placeholder still present!" } else { Write-Host "Public key committed OK" }
```

---

## Step 6 — Test the Signing Pipeline Locally

```powershell
# Requires SPARKLE_PRIVATE_KEY in environment
$env:SPARKLE_PRIVATE_KEY = "<BASE64_PRIVATE_KEY>"
$sig = & $signTool.FullName "build\Release\AirBeam.exe"
Write-Host "Signature: $sig"
# Verify
& $signTool.FullName --verify "build\Release\AirBeam.exe" --pubkey "<BASE64_PUBLIC_KEY>"
# Should print: Signature OK
```

---

## Key Rotation (if private key is compromised)

1. Revoke the `SPARKLE_PRIVATE_KEY` GitHub secret immediately (delete it).
2. Repeat Steps 1–5 with a new key pair.
3. Rebuild and release a new version — all installers from this point forward use the new key.
4. Old installers signed with the old key: existing users can still run them, but cannot update via the unsigned appcast until they install the new build manually.

See `docs/release-process.md` for the full rotation checklist.
