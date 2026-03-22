# Contract: appcast.xml Schema

**Feature**: 004-winsparkle-autoupdate  
**Consumer**: WinSparkle (via `win_sparkle_set_appcast_url()` → HTTP GET)  
**Producer**: `release.yml` Step 9 (GitHub Actions)  
**Served from**: `https://drsvenkuehn.github.io/airbeam/appcast.xml`

---

## Schema (normative)

```xml
<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <title>AirBeam Changelog</title>
    <link>https://drsvenkuehn.github.io/airbeam/appcast.xml</link>
    <description>Most recent changes to AirBeam</description>
    <language>en</language>

    <!-- One <item> per released version, newest first -->
    <item>
      <title>AirBeam {VERSION}</title>
      <pubDate>{RFC-822}</pubDate>                              <!-- e.g. Mon, 01 Jan 2026 12:00:00 +0000 -->
      <sparkle:version>{VERSION}</sparkle:version>             <!-- numeric: "1.0.1" -->
      <sparkle:shortVersionString>{VERSION}</sparkle:shortVersionString>
      <enclosure
        url="https://github.com/drsvenkuehn/airbeam/releases/download/v{VERSION}/AirBeam-v{VERSION}-Setup.exe"
        sparkle:edSignature="{BASE64_EDDSA_SIGNATURE}"         <!-- REQUIRED: attribute on <enclosure> -->
        sparkle:version="{VERSION}"
        type="application/octet-stream"
        length="{FILE_SIZE_IN_BYTES}"
      />
      <sparkle:releaseNotesLink>
        https://github.com/drsvenkuehn/airbeam/releases/tag/v{VERSION}
      </sparkle:releaseNotesLink>
    </item>

  </channel>
</rss>
```

---

## Constraints

| Constraint | Rule |
|-----------|------|
| `sparkle:edSignature` location | MUST be an **attribute of `<enclosure>`** — NOT a child element of `<item>` |
| `sparkle:edSignature` value | Base64-encoded Ed25519 signature of the binary at `url`; produced by `winsparkle-sign.exe` |
| `url` file naming | MUST match `AirBeam-v{VERSION}-Setup.exe` (with `v` prefix in filename) |
| `length` | MUST be the exact byte count of the installer binary |
| Version selection | WinSparkle picks the item with the highest `sparkle:version` value |
| MIME type | MUST be `application/octet-stream` for `.exe` |
| Channel URL | MUST match `AIRBEAM_APPCAST_URL` compile-time constant |

---

## Validation

SC-001: `winsparkle-sign.exe --verify <installer> --pubkey <pubkey>` exits `0`  
SC-002: WinSparkle UI shows update dialog without certificate error  
SC-003: Tampered `sparkle:edSignature` → WinSparkle rejects update  
SC-004: Release pipeline completes unattended when `SPARKLE_PRIVATE_KEY` is set  
