# AirBeam Roadmap

**Last updated**: 2026-03-22  
**Repository**: [drsvenkuehn/airbeam](https://github.com/drsvenkuehn/airbeam)

---

## v1.0 — Core Product

Native Windows system-tray application streaming system audio to AirPlay (RAOP) receivers over the local network. Single-target, AirPlay 1 only.

### Feature Status

| # | Feature | Spec | Plan | Tasks | Status |
|---|---------|------|------|-------|--------|
| 001 | AirPlay Audio Sender | ✅ | ✅ | 101/101 | ✅ Complete |
| 002 | CI / Build Hardening | ✅ | ✅ | 9/9 | ✅ Complete |
| 003 | Branded Tray Icons | ✅ | — | — | 🔲 Needs plan + tasks |
| 004 | WinSparkle Auto-Update | ✅ | ✅ | 11/23¹ | 🔶 Partial — needs developer key gen |
| 005 | Bonjour Install Guidance | ✅ | — | — | 🔲 Needs plan + tasks |

¹ T001–T005 (key generation + RC embed) and T015–T023 (validation + gh-pages) require developer
action; T006–T014 and T021–T022 (CI pipeline + docs) are implemented.

### v1.0 Remaining Work

**004 — WinSparkle Auto-Update (developer actions)**

1. Download WinSparkle 0.8.5 → run `winsparkle-sign --generate-keys`
2. Add `SPARKLE_PRIVATE_KEY` secret to GitHub Actions
3. Replace `TODO_REPLACE_WITH_ED25519_PUBLIC_KEY` in all 7 `resources/locales/strings_*.rc`
4. Update `resources/sparkle_pubkey.txt`
5. Create `gh-pages` branch; enable GitHub Pages in repo settings
6. Validate SC-001 through SC-005 (see `specs/004-winsparkle-autoupdate/tasks.md`)

See [`specs/004-winsparkle-autoupdate/quickstart.md`](004-winsparkle-autoupdate/quickstart.md) for the step-by-step procedure.

**003 — Branded Tray Icons**

Run `/speckit.plan` then `/speckit.tasks` to generate implementation plan and tasks.

**005 — Bonjour Install Guidance**

Run `/speckit.plan` then `/speckit.tasks` to generate implementation plan and tasks.

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
