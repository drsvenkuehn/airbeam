# AirBeam Roadmap

**Last updated**: 2026-03-26  
**Repository**: [drsvenkuehn/airbeam](https://github.com/drsvenkuehn/airbeam)

---

## v1.0 — Core Product

Native Windows system-tray application streaming system audio to AirPlay (RAOP) receivers over the local network. Single-target, AirPlay 1 only.

### Feature Status

| # | Feature | Spec | Plan | Tasks | Status |
|---|---------|------|------|-------|--------|
| 001 | AirPlay Audio Sender | ✅ | ✅ | 101/101 | ✅ Complete |
| 002 | CI / Build Hardening | ✅ | ✅ | 9/9 | ✅ Complete |
| 003 | Branded Tray Icons | ✅ | ✅ | 25/25 | ✅ Complete |
| 004 | WinSparkle Auto-Update | ✅ | ✅ | 23/23 | ✅ Complete |
| 005 | Bonjour Install Guidance | ✅ | ✅ | 27/27 | ✅ Complete |
| 006 | Bundle Bonjour Installer | ✅ | ✅ | 20/20 | ✅ Complete |
| 007 | WASAPI Loopback Capture | ✅ | ✅ | ✅ | ✅ Complete |
| 008 | mDNS / Tray Discovery | ✅ | ✅ | ✅ | ✅ Complete |
| 009 | Connection Controller | ✅ | ✅ | ✅ | ✅ Complete |

¹ All 23 tasks complete. Key re-keyed to local pair on 2026-03-25; update GitHub Actions `SPARKLE_PRIVATE_KEY` secret with contents of `winsparkle-private.from-env.key`.

### v1.0 Remaining Work

**003 — Branded Tray Icons**: ✅ Complete (2026-04-03). Icons reviewed at 16×16 and 32×32, all states confirmed.

**004 — WinSparkle Auto-Update**: ✅ Complete (2026-03-25). Key re-keyed to local pair (`9r51oz...`). Update GitHub Actions `SPARKLE_PRIVATE_KEY` secret with the contents of `winsparkle-private.from-env.key`.

**006 — Bundle Bonjour Installer**: ✅ Complete (2026-03-26). `BonjourPSSetup.exe` fetched + SHA-256 verified at build time via `fetch-bonjour.ps1`; silently installed at NSIS install time if Bonjour absent; combined license page shown; `bonjour-fetch-smoke` CTest passes.

**007 — WASAPI Loopback Capture**: ✅ Complete. WASAPI event-driven capture, MMCSS boost, resampler, SPSC ring buffer.

**008 — mDNS / Tray Discovery**: ✅ Complete. Bonjour-based mDNS browse/resolve, TXT record parsing, AirPlay 2-only detection, tray menu with live speaker list.

**009 — Connection Controller**: ✅ Complete (2026-04-03). Full pipeline lifecycle (Idle→Connecting→Streaming→Disconnecting), auto-reconnect, AirPlay 2 detection, bold connected speaker, notification fixes.

**Open gaps before v1.0 release:**
- Disconnect menu item (not yet in tray menu)
- VolumePopup implementation (IDM_VOLUME exists, implementation TBD)

---

## v2.0 — Platform Expansion

Capabilities deferred from v1.0 per constitution. Each requires a formal version-scope
discussion and explicit agreement before work begins.

| Feature | Description | Complexity |
|---------|-------------|------------|
| Multi-room streaming | Simultaneous output to multiple AirPlay receivers with synchronized playback | High — NTP sync across multiple RTSP sessions |
| AirPlay 2 support | HomeKit pairing (SRP-6a, Ed25519, Curve25519), buffered audio mode | Very High — significant protocol engineering |
| Per-application audio capture | Capture audio from a single app rather than the default render endpoint | High — requires WASAPI session enumeration or virtual device |
| DLNA / Chromecast output | Extend to non-AirPlay receivers | Medium — separate protocol implementations |
| Kernel-mode virtual audio device | Appear as a standard audio device in Windows | Very High — driver development, signing, WHQL |

> ⚠️ Any v2.0 capability MUST NOT be partially implemented or stubbed in v1.0 in ways
> that require breaking changes later (constitution requirement).

---

## Explicit Non-Goals (all versions)

- Video streaming or screen mirroring
- No DLNA or Chromecast in v1.0
- No kernel-mode driver in v1.0
- No AirPlay 2 / HomeKit pairing in v1.0
