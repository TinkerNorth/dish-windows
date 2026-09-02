# Changelog

All notable changes to the Dish Windows client are documented in this file. The
format is loosely based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The version number has one source, `project(Dish VERSION ...)` in
[`CMakeLists.txt`](CMakeLists.txt), which becomes the `DISH_VERSION` compile
definition the in-app About surface reads. It is mirrored by hand in
`packaging/dish.rc` (the Windows version-info resource Explorer and Task
Manager show) and in [`vcpkg.json`](vcpkg.json). Keep all three in step when it
changes: `version-consistency.yml` fails a pull request that moves one without
the others, and `release.yml` fails the tag build when the tag, the CMake
version and `dish.exe`'s own `ProductVersion` disagree.

Cross-repo coordination: changes to the wire protocol or the pairing flow that
need matching updates in `satellite`, `dish-android`, `dish-linux` or `dish-mac`
are marked `[wire-coordinated]`. Releases tagged in lockstep across the repos
share a version number.

---

## [Unreleased]

### Added

- **Controller audio, wave 1: wire + capability model** `[wire-coordinated]`
  (satellite's `MSG_MIC_AUDIO`/`MSG_SPEAKER_AUDIO`/`MSG_MIC_LED`; dish-android
  shipped the client reference). This lands the protocol-2 audio extension's
  plumbing without yet turning any audio on:
  - the wire: `MSG_MIC_AUDIO` (0x0012) send path, `MSG_SPEAKER_AUDIO` (0x0013)
    and `MSG_MIC_LED` (0x0014) dispatch, the `mic`/`speaker` descriptor caps,
    and the datagram ceilings (1500-byte receive buffer, 1472-byte inner
    payload guard) a full-size audio frame needs;
  - the pure cores: the 2-frame reorder window (`core/audio/AudioJitter.h`,
    the third mirror of satellite's — edit together) and the pinned Opus
    formats (mic mono VOIP 32 kbps DTX, speaker stereo AUDIO 96 kbps, both
    VBR + in-band FEC) behind `core/audio/AudioCodec.h`, libopus-backed in
    `source/audio/OpusAudioCodec.*` (new vcpkg dependency: `opus`);
  - the host verdict: `GET /api/server/capabilities` is now probed after every
    session PUT for the `controllerAudio` block (per-backend `audio` fallback),
    so the capability table's mic/speaker host layer reflects what the host
    will actually carry — conservative "no audio" until a probe says yes;
  - the model and UI: Microphone and Controller sound rows in the capability
    matrix and per-binding toggles (mic defaults OFF for privacy, speaker ON),
    persisted like the motion toggle; `wButtons` bit 0x0800 reserved as the
    DualSense mic-mute state.
- **Controller audio, wave 2: the audio itself** `[wire-coordinated]`. A
  Direct-claimed DualSense (or DS4 v2) now carries real audio end to end:
  - pad-to-endpoint routing: the claimed pad's HID product string is matched
    against the WASAPI endpoints SDL enumerates, with every ambiguity
    resolving to "no route" (two pads sharing a name, duplicate endpoint
    names, a name containing two pads' strings) — routes re-resolve on claim
    changes and audio hotplug, and a change re-declares the slot's descriptor;
  - the capture engine: the pad's own headset mic, windowed to exact 20 ms
    frames, Opus-encoded and sent as `MSG_MIC_AUDIO`, one seq per window
    including failed encodes. THE PRIVACY INVARIANT: muted, toggled off,
    unrouted, unstreaming or unwelcome at the host means the capture device is
    CLOSED and zero packets leave — never silence in their place;
  - the playout engine: `MSG_SPEAKER_AUDIO` through the reorder window and
    Opus FEC/PLC to the pad's own speaker endpoint, with a two-frame start
    cushion rebuilt as silence after the satellite's suppressed-silence
    stretches;
  - the DualSense mute button: decoded as an edge onto a latch that folds the
    mute STATE into `wButtons` (0x0800) on the read thread, mirrored to the
    app's mute state, stripped from Moonlight's button words; the slot card
    and Configure binding show the local truth with a click-to-toggle control,
    and the pad's mute lamp answers locally at once (a later host `MSG_MIC_LED`
    repaints it — last writer wins on the pad);
  - `MSG_MIC_LED` actuation: the DS5 lamp + mic-amp power-save bit, shadowed in
    the per-claim feedback state so rumble/lightbar/player-LED/trigger writes
    re-assert it instead of stomping it;
  - `SDL_INIT_AUDIO` is owned by the audio gateway, not the gamepad bridge, so
    gamepad re-inits never take a live stream down; the bridge's event loop
    forwards audio hotplug.
  Mute is deliberately session-scoped (not persisted): it clears when the pad
  leaves, the way the hardware's own mute does; the durable off-switch is the
  per-binding Microphone toggle, which still defaults OFF.

- **Protocol 2** `[wire-coordinated]` (satellite #86, #87; dish-android #174,
  #175). The version is now negotiated rather than assumed: the client offers 2,
  the satellite settles the session on that offer and echoes it back, and the
  *settled* version keys the wire frames. A satellite that predates versioning
  echoes 1 and keeps working on the v1 frames. A 409 whose range still overlaps
  ours is re-offered at the satellite's ceiling instead of dead-ending, and one
  that does not names which end has to update.
- **The POINTER frame** (0x000C, v2, 19 bytes). The touchpad click moved out of
  the finger flags into a buttons byte and a signed vertical wheel was appended.
  A physical pad's touch surface reports one click and no wheel, so the right,
  middle and wheel fields ride as zero rather than being synthesised from
  gestures the user never made.
- **Feedback to a Direct-claimed pad.** The USB-direct claim path gained an OUT
  report path, so a raw-HID claim now drives the pad as well as reading it:
  - rumble and the RGB lightbar, which previously fired only on the SDL path;
  - `MSG_TRIGGER_EFFECTS` (0x0010) replays the game's own DualSense
    adaptive-trigger blocks verbatim;
  - `MSG_PLAYER_LEDS` (0x0011) drives the DualSense indicator bar and the Switch
    Pro's player lights.
  The descriptor advertises the actuator, not the hardware: a capability is
  claimed only where a report would actually land, so the satellite never sends
  into a path that would drop it.
- **Moonlight touch.** A bound pad's touchpad now reaches the host as
  `CONTROLLER_TOUCH` events: the full-state frame is diffed into per-pointer
  DOWN / MOVE / UP, with a tracking-id change closing the old contact before
  opening the new one.
- **Moonlight motion is subscription-gated.** Samples go out only after the host
  asks with a `MOTION_EVENT`, per (pad, motion type) and at the requested rate.
  Previously the stream ran whether or not anything wanted it.
- **Moonlight trigger rumble** is folded onto the pad's body motors and
  advertised, so a game whose only haptics are trigger effects is no longer
  silent. No pad this client can claim has impulse-trigger motors (Linux xpad
  publishes no hidraw node for an Xbox pad; Windows XInput hides one from raw
  HID), so the fold is the honest maximum rather than a shortcut. The two host
  rumble streams mix per motor by maximum, so neither can cancel the other.

### Fixed

- Moonlight accelerometer samples were sent in g where the wire wants metres per
  second squared, so a host read every pad as nearly motionless.
- An absent `protocolVersion` in a satellite response was read as this build's
  version rather than as 1, which would have made a pre-versioning satellite
  look like it had agreed to frames it cannot decode.

## [1.1.0] - 2026-08-24

### Added

- Configurable keep-awake, under *Power* in Settings. **Never**, **While
  playing** (the default: hold only while a bound controller has actually been
  actuated inside a 1–180 minute idle window, 5 by default) or **While
  connected** (hold for as long as a slot streams, however long the pad sits
  still). The hold now asserts `ES_SYSTEM_REQUIRED` by default and adds
  `ES_DISPLAY_REQUIRED` only on request, because forwarding a pad needs the
  computer, not the panel. Activity is measured post-deadzone against a
  reference that only advances when it moves, so a drifting stick cannot pin
  the machine awake and a slow deliberate push still registers. The streaming
  pill names the reach it actually holds and carries a **Configure** button
  through to the setting.

### Fixed

- The `⋯` overflow menus open. A Menu takes its width from its background, and
  both the Connections host menu and the Home row menu restyled that background
  without restating a width, so the menu opened at zero width — it took focus
  and drew nothing, which is indistinguishable from a dead button. Both are now
  sized to their widest item over a shared `Tokens.menuMinWidth` floor, the same
  way `ComboButton` already did it.

- With two same-model pads split across transports — one claimed over USB
  Direct, one on Bluetooth — the twin-dedup picked its suppressed SDL twin by
  list order, so it could hide the Bluetooth pad and leave the claimed pad's
  own SDL twin streaming: the wireless pad went dead and the wired one
  double-streamed. Routed twins now carry their transport and USB twins are
  always hidden first; a Bluetooth instance is only suppressed as a model's
  last remaining twin (the single-pad charging-while-paired case). The
  companion rule — a Bluetooth slot never wears its USB twin's path control —
  moved from the slot rebuild into `slotPathFields` and is pinned there.
  (Windows re-derivation of dish-android's dual-presence fix; the android
  "Use wired" pill does not port — with no USB permission broker here, the
  wired path is claimed automatically or offered by the wired twin's own card.)

- Configure binding pins its Unbind/Cancel/Apply bar to the bottom of the
  window, like the wizard's footer. Only the editor above it scrolls, so the
  primary action no longer scrolls out of reach on a short window.

### Changed

- The title bar and the rail's Home entry draw the app mark (`dish-logo`, the
  window icon's identity) instead of the plain dish silhouette, which read as
  a second satellite glyph. The plain-dish states stay the wire vocabulary's.

- **The installer is now Inno Setup.** `dish-setup.exe` is compiled from
  [`installer.iss`](installer.iss); the bespoke SFX stub, Qt Quick wizard,
  install/uninstall engine and pack tool are gone (~30k lines). The defaults
  survive: per-user with no administrator prompt, all-users on request, Start
  Menu always, Desktop opt-in, Windows 10 1809+ x64. The asset names and the
  whole auto-update chain (staging, three-point SHA-256 verification, the
  two-attempt cap, old-build relaunch on a failed apply) are unchanged. New
  behaviour: a running Dish is closed and restarted through Restart Manager
  instead of blocking the install; the silent grammar is Inno's standard
  switch set; the uninstaller is `unins000.exe` (`uninstall.exe` and
  `--purge-user-data` are gone, and `PRIVACY.md` documents the manual wipe);
  the recovery path for a broken download is `innoextract` instead of
  `7z x`. The installer wizard speaks five of the app's six languages;
  Bosnian has no official Inno catalogue yet.

## [1.0.0] - 2026-08-17

First public release of the Windows client. It reaches protocol-1 parity with
the other Dish clients, so a satellite cannot tell them apart. The list below is
what exists, not a diff against a previous release, because there is no previous
release.

### Added

- **Steam Controller over USB Direct.** The wired pad (`28DE:1102`) and the
  wireless dongle (`28DE:1142`) can be claimed on the raw-HID path: quiet-mode
  feature reports switch off the firmware's stand-alone keyboard/mouse
  emulation and enable the IMU, the 64-byte vendor state packets decode
  (sticks, right trackpad as right stick, analog triggers with Valve's 26000
  full scale, motion), and every release path restores the stand-alone
  identity so the pad keeps working as a desktop mouse afterwards. Never
  auto-claimed: the claim reconfigures a device its owner may be using as a
  mouse, so it is reached only through an explicit Direct pick on the pad's
  card. Decode and config sequences are mirrored 1:1 from dish-android's
  hardware-verified port (#154) of the Linux hid-steam / SDL drivers.
- **PDP wired Switch pads decode correctly in Direct mode.** The five wired
  models (Faceoff Wired Pro, Faceoff Deluxe, Faceoff Deluxe+ Audio, Wired
  Fight Pad Pro, Rock Candy — `0E6F:0180/0181/0184/0185/0187`) declare their
  buttons in the Switch usage order; a positional remap (mirroring
  dish-android #159 and `decodeSwitchProUsb`) maps them onto the XUSB layout,
  with ZL/ZR driving the triggers and Capture unmapped. `0E6F:0186` is
  excluded on purpose (Switch-Pro-protocol pad whose USB port is charge-only).
- **Descriptor-driven decode for generic HID pads in Direct mode.** A claimed
  generic pad is now decoded through its own HID field map (HidP preparsed
  caps: real axis usages, logical ranges, button usage indices) instead of a
  fixed-offset guess that only fit the canonical packing — the Windows analog
  of dish-android #150's "classify by descriptor, not an allowlist". The
  fixed-offset decoder remains only as the fallback when a collection
  declares nothing usable.
- **Catalog prewarm on connect.** Each time a satellite link goes Live its
  type catalog is fetched once in the background (ETag-cached), so the
  Emulate picker usually resolves instantly instead of showing a loader.
  Mirrors dish-android's `CatalogPrewarmer` (#152).

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

### Fixed

- **Every in-app pointer at the Satellite download now names the same URL.**
  The Help FAQ told users to install Satellite from `tinkernorth.com/satellite`,
  a short form that was never stood up; it now names the
  `dish.tinkernorth.com/downloads/satellite` address the Welcome screen, the
  Connections page and the setup wizard already link. The Qt organisation
  domain both executables declare also moved from `tinkernorth.dev` to
  `tinkernorth.com` to match every other reference; QSettings on Windows keys
  the registry path off the organisation *name*, so nothing stored moves.
- **The app no longer exhausts its Window Manager object quota while a
  controller is attached.** SDL2's RawInput joystick backend leaks roughly
  200 USER objects per second on Windows 11 whenever any joystick is present;
  at the process's 10,000-object cap (under a minute in) Qt can no longer
  create timers or dispatcher windows, and everything that needs the event
  loop quietly stalls — surfaced as a bind stuck on "sending descriptor".
  The backend is now disabled (`SDL_HINT_JOYSTICK_RAWINPUT=0`): Xbox pads
  ride the XInput backend, whose semantics this client already assumes (the
  practical cost is the classic four-pad XInput ceiling), and everything
  else rides HIDAPI/DirectInput unchanged. The hint is normal-priority, so
  the environment variable still overrides it for diagnosis.
- **Edge-drag resize works on both frameless windows.** Qt's
  `FramelessWindowHint` creates a bare `WS_POPUP` without `WS_THICKFRAME`,
  and DefWindowProc runs the native sizing loop only for windows that carry
  that style — so the chrome filter answered every edge hit-test with a
  resize code and the drag then did nothing, on the main window and the
  installer alike. The filter now stamps `WS_THICKFRAME` at attach (visual
  no-op: `WM_NCCALCSIZE` still zeroes the frame). The installer window also
  drops its fixed-size clamp: 460×420 stays the design floor, the faces
  stretch above it, and it still offers no maximize.
- **Capabilities now follow the active path, so nothing is advertised where it
  cannot fire** (the dish-android #146 rule, path-resolved for Windows).
  `CAP_RUMBLE` is folded per slot from the SDL probe on the Standard path and
  is never advertised for a USB-direct claim (which has no output write path
  yet), so a satellite no longer offers a rumble channel that silently drops.
  In the other direction, a Direct claim's descriptor now advertises the
  motion and touchpad its decoder actually streams — the bind seams used to
  consult only the SDL device list, miss the synthetic slot, and register a
  Direct DualSense/DS4/Switch Pro with no `CAP_MOTION` and no touchpad
  render mode, so the satellite discarded every MOTION/TOUCHPAD packet the
  claim decoded. The wizard/Configure capability matrix tells the same truth:
  Standard carries everything the pad's driver exposes, Direct refuses rumble
  and lightbar at the Link layer, and the rumble chip reads the per-pad probe
  instead of a hardcoded "Present".

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
  Those paths are verified by hand. The
  [Known limitations](docs/ARCHITECTURE.md#not-yet-implemented) section of
  `docs/ARCHITECTURE.md` tracks what is landed against what is specified and
  tested but not yet wired.
- Release artifacts are not signed and carry no build provenance. The gaps are
  listed in [`SECURITY.md`](SECURITY.md). In practice this means SmartScreen
  warns the first time a freshly downloaded `dish-setup.exe` is run, and the
  SHA-256 chain from `latest.json` is the only integrity anchor the auto-update
  path has.

[Unreleased]: https://github.com/TinkerNorth/dish-windows/compare/1.1.0...main
[1.1.0]: https://github.com/TinkerNorth/dish-windows/releases/tag/1.1.0
[1.0.0]: https://github.com/TinkerNorth/dish-windows/releases/tag/1.0.0
