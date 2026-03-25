# AirBeam Roadmap

**Last updated**: 2026-03-25  
**Repository**: [drsvenkuehn/airbeam](https://github.com/drsvenkuehn/airbeam)

---

## v1.0 — Core Product

Native Windows system-tray application streaming system audio to AirPlay (RAOP) receivers over the local network. Single-target, AirPlay 1 only.

### Feature Status

| # | Feature | Spec | Plan | Tasks | Status |
|---|---------|------|------|-------|--------|
| 001 | AirPlay Audio Sender | ✅ | ✅ | 101/101 | ✅ Complete |
| 002 | CI / Build Hardening | ✅ | ✅ | 9/9 | ✅ Complete |
| 003 | Branded Tray Icons | ✅ | ✅ | 25/25 | 🔧 In Progress — icons generated, CTest added; manual review pending |
| 004 | WinSparkle Auto-Update | ✅ | ✅ | 21/23¹ | 🔶 Partial — 2 manual validation tests remain |
| 005 | Bonjour Install Guidance | ✅ | ✅ | 27/27 | ✅ Complete |

¹ T020 and T023 are manual sign-off tests requiring a live WinSparkle build + local appcast server.

### v1.0 Remaining Work

**004 — WinSparkle Auto-Update (2 manual tests remaining)**

T020 and T023 require a running WinSparkle build and can only be done by you:

- **T020** (tamper test): corrupt `sparkle:edSignature` in a local `appcast.xml` copy → confirm WinSparkle rejects the update
- **T023** (happy-path dialog): craft a minimal signed appcast → serve locally → confirm WinSparkle shows the update dialog

**003 — Branded Tray Icons (manual review pending)**

Icons generated and committed. CTest `icon-validation` test added. Manual acceptance work:
1. Build Release configuration and verify all 11 icons embedded (`Resource Hacker` or `sigcheck -i`)
2. Visual review at 16×16 and 32×32 on 100% + 150% DPI (SC-001)
3. Confirm idle/streaming/error states identifiable without tooltip (SC-002)
4. Verify connecting animation cycles at ~1000 ms (SC-004, fixed to 125 ms × 8)

See [`specs/003-branded-tray-icons/tasks.md`](003-branded-tray-icons/tasks.md) T009–T021 for full review checklist.

**005 — Bonjour Install Guidance**

✅ Complete (2026-03-25). URL embedded in balloon, click opens Apple download page.

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
