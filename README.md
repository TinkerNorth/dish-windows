# Dish Windows

[![Windows CI](https://github.com/TinkerNorth/dish-windows/actions/workflows/windows-ci.yml/badge.svg)](https://github.com/TinkerNorth/dish-windows/actions/workflows/windows-ci.yml)

Turns the gamepads attached to a Windows PC into wireless controllers for
another machine on the same network. Dish finds
[Satellite](https://github.com/TinkerNorth/satellite) servers on the LAN, pairs
with a PIN over HTTPS, and streams encrypted controller input over UDP; the
satellite plugs a matching virtual pad into the host, so games there see a real
controller.

**Dish needs a Satellite server running on your LAN.** It is one half of a pair
and does nothing on its own. It is the Windows sibling of `dish-android`,
`dish-mac` and `dish-linux`; all four speak the same protocol to the same
server and look identical to it.

Physical controllers only. There is no on-screen touch gamepad; that belongs to
`dish-android`, where the form factor makes sense.

## Status

Pre-release, version 0.1.0. Nothing has been tagged yet, so the install section
below describes what the release pipeline produces once a `v*` tag is pushed.
The implementation is at protocol-1 parity with the other clients and the test
suite is broad, but the live session loop, the SDL input threading and the USB
claim path cannot be exercised in CI (no socket, no satellite, no controller)
and are verified by hand.
[Known limitations](docs/ARCHITECTURE.md#not-yet-implemented) tracks what is
landed versus what is a tested-but-unwired specification.

## What it does

- LAN discovery over mDNS (`_satellite._udp.local.`) with a UDP broadcast
  fallback for older satellites
- PIN pairing over HTTPS against the satellite's self-signed certificate,
  pinned trust-on-first-use so a swapped certificate aborts the request
- ChaCha20-Poly1305 input streaming over UDP, sent straight off the input
  thread
- SDL2 (XInput for Xbox-class pads) plus an opt-in USB-direct raw-HID path for
  DualSense, DualShock 4 and 8BitDo class pads
- Motion, battery and touchpad forwarded up; rumble and light bar driven back
  down by the host
- Several satellites side by side, with per-slot controller binding
- Per-device deadzones, button remapping, and a guided setup wizard
- Holds the display awake while a slot is streaming, releases it on the last
  unbind
- Light and dark themes, six UI languages
- A single-file installer, and in-place auto-update that verifies every
  download against the release manifest's SHA-256 before it runs

## Install and run

You need 64-bit Windows 10 (1809 or newer) or Windows 11, a gamepad, and a
reachable Satellite server.

**Recommended: `dish-setup.exe`** from the Releases page. It is a single file
with no prerequisites, installs per-user by default with no administrator
prompt, and carries the Visual C++ runtime, so it works on a machine where
nothing has been installed. All-users installs are offered and ask for
elevation. Removal is a normal Windows Settings, Installed apps entry.

The installer is not code-signed yet, so SmartScreen shows "Windows protected
your PC" on first run. Choose **More info**, then **Run anyway**. If you would
rather check the bytes, every release also carries a `SHA256SUMS` asset:

```powershell
$expected = (Get-Content .\SHA256SUMS |
    Where-Object { $_ -match 'dish-setup\.exe' }).Split()[0]
$actual = (Get-FileHash .\dish-setup.exe -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) { throw 'checksum mismatch' } else { 'ok' }
```

**Portable: `dish-windows.zip`.** Unzip it anywhere and run `dish.exe`. The zip
carries the Qt runtime, the QML modules and the Visual C++ runtime alongside the
executable, so there is nothing to install, no prerequisites and no admin rights
needed. A portable copy never updates itself.

Settings persist in the Windows registry under `HKEY_CURRENT_USER`; a crash
writes a minidump and a symbolized `crash.log` to `%LOCALAPPDATA%\Dish`.

### Updates

An installed copy keeps itself current. It asks GitHub for `latest.json` about
15 seconds after launch and every four hours after that, downloads the next
`dish-setup.exe` in the background, verifies it against the SHA-256 in the
manifest, and applies it the next time you start Dish. There is no background
service and no update agent: nothing runs while Dish is closed. You can also
press Restart to update in Settings or in the title-bar indicator, and the
update applies immediately.

Two switches in Settings, Updates control all of it: *Check for updates
automatically* stops every update-related network request when off, and
*Download updates automatically* leaves the check on but reduces it to a
notification. What the check sends is spelled out in
[`PRIVACY.md`](PRIVACY.md) section 2.4.

[`docs/INSTALLER.md`](docs/INSTALLER.md) documents the payload format, the
command line and exit codes for scripted installs, the staging layout, and the
apply handoff.

## Build from source

- Visual Studio 2022 with the Desktop development with C++ workload, or the
  standalone Build Tools
- CMake 3.21+ and Ninja
- Qt 6.7+ (Core, Gui, Network, Svg, Quick, Qml, QuickControls2; Linguist tools
  for the translation catalogues)
- libsodium and SDL2, resolved by vcpkg from `vcpkg.json`

The bundled installer sets all of that up in five idempotent steps. From an
elevated prompt:

```cmd
install-dependencies.bat
```

It installs the VS Build Tools, CMake and Ninja, LLVM, Python plus aqtinstall
plus Qt 6.7.3 into `C:\Qt`, and clones vcpkg to `%USERPROFILE%\vcpkg`,
persisting `VCPKG_ROOT` and `CMAKE_PREFIX_PATH`. Budget roughly 12 GB and half
an hour on a clean machine. Then, from any PowerShell window (the build script
finds MSVC through `vswhere` and imports `vcvars64.bat` itself):

```powershell
scripts\build.ps1 release
.\build-release\dish.exe
```

`scripts\build.ps1 debug` builds into `build-debug\` instead, and a second
`test` argument runs ctest after the build. `CONTRIBUTING.md` has the long-form
CMake invocation and the hook, format and lint setup.

## How it works

The app is a unidirectional-dataflow core with a Qt Quick projection on top.
Sources of truth own state, pure composers derive from it, QML binds and
renders, and QML sends commands back. `src/qml/` could be deleted and replaced
with a different front end without touching anything below it.

```
src/qml/          Qt Quick UI: AppViewModel facade, role models, frameless chrome
src/composer/     Composers (pure derive), Controllers (effects), Coordinators
src/source/       StateSources and IO gateways: discovery, HTTP, USB, stores
src/repository/   Durable keyed storage over QSettings
src/core/         Pure, Qt-free: reducers and FSMs, wire crypto, input math
src/architecture/ The kernel: Observable, StateSource, Composer, Controller, Repository
src/Input/        SDL bridge, XUSB packing, output command queue
src/Network/      Winsock UDP session, REST client, pairing, connection pool
src/UI/           Theme palettes, font probes, crash handler, license manifest
src/update/       Update check, download, staging store, boot handoff
src/installer/    dish-setup.exe: SFX stub, install and uninstall reducers, pack tool
```

The input hot path is the deliberate exception and is not routed through that
kernel. An SDL controller event runs `GamepadInputProcessor` and then
`SatelliteClient::sendReport` inline on the SDL thread: pack the XUSB report,
encrypt it, and call `sendto()` on a raw Winsock socket. No queue, no Qt event
hop, no cross-thread signal. The UI thread never
appears on the path; it only reads counters the input thread publishes
lock-free. The USB-direct read loop feeds the same `publish` entry point on its
own thread.

Layer rules, the state-capture doctrine (`AsyncState<T>` versus a reducer FSM),
the UI binding contract and the hardening roadmap are in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), the kernel primitives in
[`src/architecture/README.md`](src/architecture/README.md), the QML surface in
[`docs/QML_CONTRACT.md`](docs/QML_CONTRACT.md) and
[`docs/QML_UI_KIT.md`](docs/QML_UI_KIT.md), the design tokens in
[`DESIGN.md`](DESIGN.md), and the installer and auto-updater in
[`docs/INSTALLER.md`](docs/INSTALLER.md).

## Protocol

Ports, byte layouts and JSON shapes match the other Dish clients so all four
are interchangeable to a satellite. The authoritative contract lives in
`satellite/docs/contract.md`; the client-side mirror is
[`src/core/model/Protocol.h`](src/core/model/Protocol.h).

| | |
|---|---|
| Protocol version | 1 |
| Discovery | UDP 9879 broadcast beacons, plus mDNS `_satellite._udp.local.` |
| Pairing and REST API | HTTPS 9443, self-signed certificate, TOFU-pinned |
| Streaming | UDP 9876 |
| REST auth | `X-Device-Id` + `X-Hmac-Proof` = hex(HMAC-SHA256(pairingKey, `"satellite-proof:"` + deviceId)) |
| Topology | REST only: `PUT /api/connections` upserts the whole desired controller set |
| Session key | HKDF-SHA256(ikm = pairingKey, salt = sessionSalt, info = `"satellite-session-v1"` \|\| token) |
| AEAD | ChaCha20-Poly1305 IETF |
| Nonce | direction(1) \| 0x00 x7 \| counter(4 BE) |
| AAD | token (4 bytes, BE) |
| Packet | `token(4) \| counter(4 BE) \| ciphertext+tag` |
| Up | INPUT 0x0001, HEARTBEAT 0x0002, MOTION 0x000A, BATTERY 0x000B, TOUCHPAD 0x000C |
| Down | HEARTBEAT_ACK 0x0003, RUMBLE 0x0009, LIGHTBAR 0x000D, SESSION_CLOSE 0x000F |
| Input report | 12 bytes XUSB, little-endian |
| Heartbeat | every 2 s; not responding at 2 misses, dead at 5 |

## Translations

Six catalogues in `translations/`: English, Bosnian, German, Spanish, French
and Brazilian Portuguese. They compile to `.qm` files embedded in the binary at
`:/i18n/`, and the app picks one at startup by walking `QLocale::uiLanguages()`
so Windows' preferred UI language wins over the regional format setting.

English is a real catalogue rather than the untranslated fallback: a `%n`
message carries one source string but needs one form per plural category, and
Bosnian has three. Vocabulary is sourced from `dish-android`, whose catalogues
are older and reviewed, via `scripts/seed-from-android.py`.

`scripts/check-translations.ps1` re-runs `lupdate` in CI and fails on any diff,
so a new user-facing string cannot land without its catalogue entry. If it
fails, run `cmake --build <build-dir> --target update_translations` and commit
the result. Coverage is reported but never enforced; translating a string is a
separate act from extracting it.

## Testing

```powershell
scripts\build.ps1 debug test
# or, against an existing build tree
ctest --test-dir build-debug --output-on-failure
```

One `DishTests` executable links the `dish_core` library. It covers the pure
core exhaustively, with no mocks and no sockets: the reducer FSMs (USB path
switching, pairing, session lifecycle, capture mode, apply sequencing),
`AsyncState` transitions, the wire encoders and decoders against interop
vectors shared with the satellite and dish-android, session crypto, XUSB
mapping and deadzones, HID report parsing and transport classification, the
beacon and mDNS parsers, TOFU pinning, and every repository against a shared
contract. The design system is tested too: palette completeness, WCAG contrast
ratios in both themes, font-family probes, and placeholder integrity plus
plural-form order across all six translation catalogues.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the workflow, the LGPL header
policy, hook setup and review expectations. Changes land on `main` through a
pull request; `Windows CI`, `Security` and `CodeQL` run on every one.

## Security

Vulnerability disclosure: [`SECURITY.md`](SECURITY.md). Dish is LAN-only and
talks to no TinkerNorth-operated server. Release artifacts ship with SHA256SUMS
and an SPDX SBOM.

## License

LGPL-3.0-or-later. See [`LICENSE`](LICENSE) for the LGPL and
[`COPYING.GPL3`](COPYING.GPL3) for the GPL v3 it incorporates by reference.
