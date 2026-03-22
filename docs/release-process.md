# AirBeam Release Process

This document covers the complete release signing procedure: initial key generation,
routine release signing (automated), and emergency key rotation.

---

## 1. Initial Key Generation (One-Time Setup)

This procedure is performed **once** per repository. It creates the Ed25519 key pair
that authenticates every AirBeam update delivered via WinSparkle.

### Prerequisites

- Windows machine with PowerShell 7+
- Access to `https://github.com/drsvenkuehn/airbeam/settings/secrets/actions`
- Git write access to the `drsvenkuehn/airbeam` repository

### Step 1 — Download `winsparkle-sign.exe`

```powershell
$ver = "0.8.5"
$url = "https://github.com/vslavik/winsparkle/releases/download/v$ver/WinSparkle-$ver.zip"
Invoke-WebRequest $url -OutFile WinSparkle.zip
Expand-Archive WinSparkle.zip -DestinationPath WinSparkle-extracted
$signTool = Get-ChildItem WinSparkle-extracted -Recurse -Filter "winsparkle-sign.exe" | Select-Object -First 1
```

### Step 2 — Generate the Key Pair

```powershell
& $signTool.FullName --generate-keys
```

Output:
```
Private key: <BASE64_PRIVATE_KEY>
Public key:  <BASE64_PUBLIC_KEY>
```

> ⚠️ **Copy both values immediately.** The private key is shown only once.

### Step 3 — Store the Private Key as a GitHub Secret

1. Go to `https://github.com/drsvenkuehn/airbeam/settings/secrets/actions`
2. Click **New repository secret**
3. Name: `SPARKLE_PRIVATE_KEY`
4. Value: the `<BASE64_PRIVATE_KEY>` from Step 2
5. Click **Add secret**

### Step 4 — Embed the Public Key

Replace the placeholder in all 7 locale RC files:

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
Generated: <YYYY-MM-DD>
```

### Step 5 — Verify the Embed (SC-005)

```powershell
cmake --preset msvc-x64-debug
cmake --build --preset msvc-x64-debug
$check = Select-String "TODO_REPLACE" resources\locales\strings_en.rc
if ($check) { Write-Error "Placeholder still present!" } else { Write-Host "OK: public key embedded" }
```

### Step 6 — Commit and Push

```powershell
git add resources\locales\strings_*.rc resources\sparkle_pubkey.txt
git commit -m "feat: embed real Ed25519 public key for WinSparkle auto-update"
git push
```

### Step 7 — Initialize the `gh-pages` Branch

```powershell
git checkout --orphan gh-pages
git rm -rf .
Copy-Item .github\appcast.xml appcast.xml
git add appcast.xml
git commit -m "chore: initialize gh-pages with appcast.xml seed"
git push origin gh-pages
git checkout main
```

Then enable GitHub Pages in repo settings:
`Settings → Pages → Source: Deploy from branch → gh-pages / (root)`

Verify the URL resolves:
```powershell
Invoke-WebRequest "https://drsvenkuehn.github.io/airbeam/appcast.xml" -UseBasicParsing
```

---

## 2. Signing a Release (Automated)

Every `v*` tag push triggers `release.yml`, which automatically:

1. Downloads WinSparkle `v0.8.5` zip and extracts `winsparkle-sign.exe` + `WinSparkle.dll`
2. Builds and packages the installer (`AirBeam-v{VERSION}-Setup.exe`)
3. Signs the installer: `winsparkle-sign.exe <installer>` reads `SPARKLE_PRIVATE_KEY` from env
4. Captures the Base64 `sparkle:edSignature` into `$GITHUB_ENV`
5. Creates the GitHub Release and uploads the installer + portable ZIP
6. Prepends a new `<item>` (with `sparkle:edSignature` on `<enclosure>`) to `appcast.xml`
7. Deploys updated `appcast.xml` to the `gh-pages` branch via `peaceiris/actions-gh-pages@v4`

### If `SPARKLE_PRIVATE_KEY` Is Not Set

- The signing step is skipped automatically (guarded by `if: env.SPARKLE_PRIVATE_KEY != ''`)
- A `::warning::` annotation is emitted in the Actions log
- The release is published without a WinSparkle signature — existing users will see the update
  but WinSparkle will refuse to install it (signature mismatch)

### Manual Override (Emergency)

If CI signing must be bypassed, sign locally and update `appcast.xml` manually:

```powershell
$env:SPARKLE_PRIVATE_KEY = "<BASE64_PRIVATE_KEY>"
$sig = & winsparkle-sign.exe "AirBeam-v1.0.1-Setup.exe"
Write-Host "Signature: $sig"
# Manually edit gh-pages/appcast.xml: add sparkle:edSignature="$sig" to <enclosure>
```

### Post-Release Validation Checklist

- [ ] SC-001: `winsparkle-sign.exe --verify <installer> --pubkey <pubkey>` exits `0`
- [ ] SC-002: AirBeam shows WinSparkle update dialog without certificate error
- [ ] SC-003: Tampered `sparkle:edSignature` → WinSparkle rejects the update
- [ ] SC-004: Release workflow completed without manual intervention
- [ ] SC-005: `resources/sparkle_pubkey.txt` contains no placeholder; key ≥ 43 chars

---

## 3. Emergency Key Rotation

Use this procedure if the private key is **compromised** or as part of a **planned rotation**.

> ⚠️ After rotation, only users who have updated to the new-public-key build can receive
> future auto-updates. Users on older builds must update manually once, then auto-update resumes.

### Compromised Key — Immediate Actions

1. **Revoke the secret immediately**:
   - Go to `https://github.com/drsvenkuehn/airbeam/settings/secrets/actions`
   - Delete `SPARKLE_PRIVATE_KEY`
   - This stops CI from signing new releases until a new key is added

2. **Generate a new key pair** (repeat Section 1, Steps 1–2)

3. **Update the GitHub secret** (Section 1, Step 3) with the new private key

4. **Replace the public key in all RC files** (Section 1, Step 4)

5. **Update `resources/sparkle_pubkey.txt`** with the new key + updated `Generated:` date

6. **Commit and push** the RC file changes (Section 1, Step 6)

7. **Tag a new release** (e.g., `v1.0.2`) to trigger a signed release with the new key

8. **Update `appcast.xml`** on `gh-pages` to point to the new signed installer only
   (remove entries signed with the old key to prevent downgrade attacks)

### Planned Key Rotation

Follow the same steps as above, but without urgency. Communicate the rotation in release notes
so users know to update manually if they are on very old builds.

### WinSparkle Version Upgrade

If `WinSparkle` is upgraded from `v0.8.5`:

1. Update `WINSPARKLE_VERSION: "0.8.5"` in `.github/workflows/release.yml` job-level `env:` block
2. Verify the new version's `winsparkle-sign.exe` has the same CLI interface
3. Test the signing step locally before tagging a release

---

## 4. GitHub Pages Maintenance

The appcast URL `https://drsvenkuehn.github.io/airbeam/appcast.xml` is hardcoded at
compile time as `AIRBEAM_APPCAST_URL` in `CMakeLists.txt`.

### If the Repository Is Transferred to a New Org/User

1. Update `AIRBEAM_APPCAST_URL` in `CMakeLists.txt` to the new URL
2. Update the `<link>` element in `.github/appcast.xml` seed file
3. Set up GitHub Pages on the new repository (Settings → Pages → `gh-pages` branch)
4. Redirect or mirror the old URL if possible to avoid breaking existing installations

### GitHub Pages Setup Checklist

- [ ] `gh-pages` branch exists with `appcast.xml` at repository root
- [ ] GitHub Pages enabled: `Settings → Pages → Source: Deploy from branch → gh-pages / (root)`
- [ ] `https://drsvenkuehn.github.io/airbeam/appcast.xml` returns HTTP 200
- [ ] `AIRBEAM_APPCAST_URL` in `CMakeLists.txt` matches the live URL exactly
- [ ] `<link>` in `.github/appcast.xml` matches `AIRBEAM_APPCAST_URL`
