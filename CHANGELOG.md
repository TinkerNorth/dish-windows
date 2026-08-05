# Changelog

All notable changes to the Dish Windows client are documented in this file. The
format is loosely based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

**There are no releases yet.** Nothing has been tagged, so this file has no
version history to show. Numbered sections begin with the first tagged release;
until then everything below is under `Unreleased`, and the development history is
in `git log`.

The binary currently declares version `0.1.0`. That number has one source,
`project(Dish VERSION 0.1.0)` in [`CMakeLists.txt`](CMakeLists.txt), which
becomes the `DISH_VERSION` compile definition the in-app About surface reads. It
is mirrored by hand in `packaging/dish.rc` (the Windows version-info resource
Explorer and Task Manager show) and in [`vcpkg.json`](vcpkg.json). Keep all three
in step when it changes; `release.yml` now enforces it, failing the tag build
when the tag, the CMake version and `dish.exe`'s own `ProductVersion` disagree.

Cross-repo coordination: changes to the wire protocol or the pairing flow that
need matching updates in `satellite`, `dish-android`, `dish-linux` or `dish-mac`
are marked `[wire-coordinated]`. Releases tagged in lockstep across the repos
share a version number.

---

## [Unreleased]

First public release of the Windows client. It reaches protocol-1 parity with
the other Dish clients, so a satellite cannot tell them apart. The list below is
what exists, not a diff against a previous release, because there is no previous
release.

### Added

- **Satellite discovery and pairing.** mDNS `_satellite._udp.local.` queries with
  a UDP broadcast fallback for older satellites, PIN pairing over HTTPS against
  the satellite's self-signed certificate, and trust-on-first-use certificate
  pinning that aborts the request when a pinned fingerprint changes. Both pairing
  directions are supported: entering the PIN the satellite shows, or showing a
  PIN to type there.
- **Encrypted input streaming** `[wire-coordinated]`. ChaCha20-Poly1305 over UDP
  under a per-session key derived with HKDF-SHA256 from the pairing key, with a
  direction byte in the nonce. The pairing key itself never travels over UDP.
- **Declarative control plane** `[wire-coordinated]`. `PUT /api/connections`
  carries identity, an HMAC proof of the pairing key, and the whole controller
  topology; the response is the applied state, and a re-PUT converges. Per-slot
  changes (emulated type, motion capabilities, touchpad routing) ride
  per-controller routes without rotating the session token. A coded 401 is
  terminal: the key is dropped and retries stop instead of looping.
- **Controller input via SDL2**, with XInput underneath for Xbox-class pads.
  Buttons, sticks, triggers, motion, battery and touchpad go up; rumble and
  light-bar colour come back down and are applied through SDL.
- **USB-direct raw-HID path**, opt-in per `vid:pid`, for DualSense, DualShock 4,
  Switch Pro and generic HID pads that XInput does not claim. Per-model report
  decoders are pure and allocation-free, and the path is arbitrated against the
  SDL view of the same device so a pad visible to both streams exactly once.
- **Multiple satellites side by side**, each with its own remembered address,
  pairing key, certificate pin and per-slot controller bindings.
- **Guided setup wizard**: five pages across three stages, taking a first-run
  user from no controller to a bound one (input, destination, emulated type,
  feel, review).
- **Per-device tuning**: stick and trigger deadzone profiles, and button, stick
  and trigger remapping for raw joysticks that SDL has no mapping for.
- **Emulated type picker** rendered from the satellite's localised
  `GET /api/catalog`, ETag-cached, so a controller type this build has never
  heard of is still selectable from server-provided strings.
- **Display sleep inhibition** while a slot is streaming, released on the last
  unbind, so input latency does not spike when Windows decides the machine is
  idle.
- **Frameless Win32 window chrome** with Mica, snap hit-testing, and light, dark
  and system themes.
- **`dish-setup.exe`, a single-file installer.** A static-CRT Win32 stub with
  the whole install image appended as a standard ZIP and a 32-byte trailer at
  EOF, so `7z x dish-setup.exe` recovers everything without Dish tooling. The
  wizard is the app's own Qt Quick design system, running from `%TEMP%` against
  the runtime it is about to install. Per-user by default with no administrator
  prompt, all-users on request, both scopes recorded in a manifest that travels
  with the install. Upgrades stage and commit so the previous version stays
  launchable until the last moment, every mutation is journaled and reversed on
  failure, and the uninstaller hands its own removal to a small helper so the
  Add/Remove Programs entry outlives a cleanup that antivirus blocks. The full
  silent command line, the exit-code table and the payload format are in
  [`docs/INSTALLER.md`](docs/INSTALLER.md); a CI round trip installs, repairs,
  upgrades, applies an update and uninstalls on every pull request.
- **Auto-update.** An installed copy asks GitHub for `latest.json` 15 seconds
  after launch and every four hours, downloads the next `dish-setup.exe` in the
  background, verifies it against the manifest's SHA-256 three times (streaming,
  off disk after the download, and again at the next boot), and applies it at
  the next start or immediately from a Restart to update button. Downgrades are
  refused at both ends, a withdrawn release un-stages itself on the next check,
  a failing update is abandoned after two attempts, and the old version is
  relaunched if an apply fails, so the user is never left with no app. No
  service, no scheduled task, and nothing runs while Dish is closed. Two
  switches in Settings, Updates control it; the portable zip detects itself and
  only notifies. See [`SECURITY.md`](SECURITY.md) for the trust model.
- **Six UI languages**: English, Bosnian, German, Spanish, French and Brazilian
  Portuguese, covering the installer wizard as well as the app. English is a
  real catalogue rather than the untranslated fallback, so `%n` plural forms are
  correct in every language including Bosnian's three.
- **Crash diagnostics written locally.** A Win32 unhandled-exception filter
  writes `crash.dmp` and a symbolized `crash.log` to `%LOCALAPPDATA%\Dish\`.
  Nothing is uploaded. See [`PRIVACY.md`](PRIVACY.md) section 3.
- **In-app licenses screen** rendered from a shipped manifest, and
  [`THIRD_PARTY.md`](THIRD_PARTY.md) describing every dependency and the LGPL
  position on Qt.
- **Bluetooth radio presence and power probe**, so the copy shown when no adapter
  exists is accurate rather than a generic failure.
- **CI gates** on every pull request: `clang-format` pinned to 22.1.4, a Debug
  build, the full `ctest` suite, `qmllint`, a QML design-token literal scanner, a
  translation-catalogue freshness check, `clang-tidy`, and a Release build whose
  artifact is staged with `windeployqt` and whose installer is exercised by the
  silent install, upgrade, update-apply and uninstall round trip.
- [`PRIVACY.md`](PRIVACY.md), [`SECURITY.md`](SECURITY.md),
  [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) and
  [`CONTRIBUTING.md`](CONTRIBUTING.md) for the public release.

### Notes

- **Privacy policy change.** The update check is the first thing this client
  sends off your own network, so [`PRIVACY.md`](PRIVACY.md) gained a section 2.4
  describing exactly what the request carries (your IP as GitHub sees it and a
  `Dish/<version> (Windows; x64)` user agent, and nothing else), when it fires,
  and how to turn it off. Section 2.1 gained the seven registry values behind
  the two switches, the `%LOCALAPPDATA%\Dish\updates\` cache, and the
  installer's own footprint. The effective date moved to 2026-08-04.
- The *Share crash reports* setting exists and persists, but it is wired to a
  no-op backend. No crash report is transmitted anywhere. This is stated in full
  in [`PRIVACY.md`](PRIVACY.md) section 3, and it will be called out here before
  any real backend ships.
- Physical controllers only. There is no on-screen touch gamepad; that belongs to
  `dish-android`.
- The live session loop, the SDL input threading and the USB claim path cannot be
  exercised in CI, because CI has no socket, no satellite and no controller.
  Those paths are verified by hand. `docs/ARCHITECTURE.md` section 11 tracks what
  is landed against what is specified and tested but not yet wired.
- Release artifacts are not signed and carry no build provenance. The gaps are
  listed in [`SECURITY.md`](SECURITY.md). In practice this means SmartScreen
  warns the first time a freshly downloaded `dish-setup.exe` is run, and the
  SHA-256 chain from `latest.json` is the only integrity anchor the auto-update
  path has.

[Unreleased]: https://github.com/TinkerNorth/dish-windows/commits/main
