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
| 004 | WinSparkle Auto-Update | ✅ | ✅ | 11/23¹ | 🔶 Partial — needs developer key gen |
| 005 | Bonjour Install Guidance | ✅ | ✅ | 27/27 | ✅ Complete |

¹ T001–T005 (key generation + RC embed) and T015–T023 (validation + gh-pages) require developer
action; T006–T014 and T021–T022 (CI pipeline + docs) are implemented.

### v1.0 Remaining Work

**004 — WinSparkle Auto-Update (developer actions)**

1. Download WinSparkle 0.9.2 → run `winsparkle-tool generate-key --file <keyfile>` and `winsparkle-tool public-key --private-key-file <keyfile>`
2. Add `SPARKLE_PRIVATE_KEY` secret to GitHub Actions
3. Replace `TODO_REPLACE_WITH_ED25519_PUBLIC_KEY` in all 7 `resources/locales/strings_*.rc`
4. Update `resources/sparkle_pubkey.txt`
5. Create `gh-pages` branch; enable GitHub Pages in repo settings
6. Validate SC-001 through SC-005 (see `specs/004-winsparkle-autoupdate/tasks.md`)

See [`specs/004-winsparkle-autoupdate/quickstart.md`](004-winsparkle-autoupdate/quickstart.md) for the step-by-step procedure.

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
