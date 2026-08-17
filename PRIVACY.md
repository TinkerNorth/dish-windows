# Dish for Windows: Privacy Policy

**Effective date:** 2026-08-04.
**Hosted copy:** [`https://dish.tinkernorth.com/privacy/dish-windows/`](https://dish.tinkernorth.com/privacy/dish-windows/).
The hosted copy at that URL is the canonical version; this file mirrors it
in-repo so the code and the policy ship together. The app links to the hosted
URL from Help.

This document describes what data the Dish Windows client collects, why, how
long it is retained, and the choices you have over it. The product as a whole
spans several repositories (`satellite`, `dish-android`, `dish-windows`,
`dish-linux`, `dish-mac`); this policy is specific to the Windows desktop
client. The server (`satellite`) runs on your own PC and does not transmit
data off your local network. The Android client has a separate policy and
different behaviour, so do not read one as describing the other.

---

## 1. Short version

- Dish for Windows turns a Windows PC into a wireless gamepad for a
  `satellite` server. Controller input goes from your PC, over your own
  network, to your own `satellite` host. It does not stream to any
  TinkerNorth-operated server. TinkerNorth does not operate a server for
  Dish at all.
- **Nothing is transmitted to the authors or to any third party**, except an
  optional update check against GitHub, described in section 2.4, which you
  can turn off. There is no analytics SDK, no telemetry, no advertising
  identifier, no usage reporting, and no automatic error upload in this
  client. The update check asks GitHub for one file and sends no identifier
  with the request.
- Crash diagnostics are written **to your own disk only**, under
  `%LOCALAPPDATA%\Dish\`. They are never uploaded. If you want a maintainer
  to see one, you attach it to an issue yourself.
- Settings, remembered servers, and pairing keys live in your own Windows
  registry hive under `HKEY_CURRENT_USER`. Nothing is synced to a cloud
  account by the app.
- We do not sell, share, or rent your data. We do not show ads. We do not
  profile you.

---

## 2. What data is processed

### 2.1 Stays on your PC

Persisted with `QSettings`, which on Windows writes to the current user's
registry hive. The cross-client settings schema lands under
`HKEY_CURRENT_USER\Software\Dish\Dish`. Preferences that only exist on the
Windows desktop are written through the app's default organisation name and
land under `HKEY_CURRENT_USER\Software\TinkerNorth\Dish` instead: the
window-chrome preference `ui_rail_collapsed` and the update settings listed
after the table.

| Registry value | Holds | Used for |
|---|---|---|
| `satellite_list` | JSON array of remembered satellites: display name, IP, UDP port, HTTPS port, and the server's machine id | Reconnecting to hosts you already paired with |
| `satellite_shared_key:<id>` | **The libsodium-derived pairing key for that satellite**, hex encoded | Deriving the per-session ChaCha20-Poly1305 key for the gamepad wire protocol. This is secret material. Anyone with read access to your user hive can read it. |
| `satellite_cert_pin:<ip>` | SHA-256 fingerprint of the satellite's self-signed TLS certificate | Trust-on-first-use pinning, so a later HTTPS call is talking to the same box |
| `deviceId` | A random UUID generated on first run, with the dashes stripped | A stable per-install identifier the satellite uses to recognise this client across restarts and IP changes. It is sent **only** to satellites you pair with, never to us. |
| `deadzone:<deviceId>` | Per-controller stick and trigger deadzone profile | Restoring your calibration |
| `motion_enabled:<slotId>`, `motion_preferences`, `touchpad_mode_preferences` | Per-slot motion and touchpad routing toggles | Restoring your setup |
| `usb_path_choices` | Per `vid:pid` choice between the SDL path and the USB-direct raw-HID path | Restoring your setup |
| `joystick_remaps` | Per-device button, stick, and trigger remapping | Restoring your setup |
| `feature_lightbar_mode` | `followGame` or `off` | Light-bar behaviour |
| `theme_mode` | `system`, `light`, or `dark` | Appearance |
| `onboarding_welcome_completed`, `onboarding_dashboard_hint_dismissed` | Booleans | Not showing first-run screens again |
| `crashlytics_collection_enabled` | Boolean, default `true` | Records the state of the *Share crash reports* toggle. See section 3, which explains why this currently has no external effect. The key name is inherited from the Android client for schema continuity; there is no Crashlytics in this client. |
| `ui_rail_collapsed` | Boolean | Navigation-rail width |

Legacy values `wifi_list` and `wifi_shared_key/<id>` from older builds are
migrated in place on first run so you do not have to re-pair.

These seven values live under
`HKEY_CURRENT_USER\Software\TinkerNorth\Dish` and cover the update feature
described in section 2.4. They record your choices and the app's own
bookkeeping. None of them is transmitted anywhere.

| Registry value | Holds | Used for |
|---|---|---|
| `updates_check_enabled` | Boolean, default `true` | The *Check for updates automatically* switch. When off, the app makes no update-related network request at all. |
| `updates_auto_download` | Boolean, default `true` | The *Download updates automatically* switch. When off, the app checks and tells you, and downloads nothing until you ask. |
| `updates_skipped_version` | A version string, default empty | The one version you pressed *Skip this version* on, so it stops being offered |
| `updates_last_check_utc_ms` | Timestamp | Not checking more than once an hour at startup |
| `updates_handoff_version`, `updates_handoff_attempts` | A version string and a small counter | Internal apply bookkeeping: which staged update is being installed at the next start, and how many times it has been tried, so a broken update is abandoned after two attempts instead of looping |
| `updates_last_run_version` | A version string | Noticing that the app just updated, so it can say so once |

The app also keeps a folder at `%LOCALAPPDATA%\Dish\updates\`. It holds the
downloaded `dish-setup.exe`, a copy of the release manifest that described it,
and the installer's own log from the last apply attempt. Nothing in it is
personal, nothing in it is sent anywhere, and you can delete the folder at any
time; the app recreates it only when it downloads an update. The uninstaller
removes it whether or not you ask for your settings to be purged.

**What the installer writes.** `dish-setup.exe` is a separate program from the
app, and it touches two more places on your PC. It creates the Add/Remove
Programs entry
`HKEY_CURRENT_USER\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\TinkerNorth.Dish`
(or the `HKEY_LOCAL_MACHINE` equivalent for an all-users install), holding the
display name, version, publisher, install location, install date, uninstall
command and estimated size that Windows shows you in Installed apps. And it
creates the shortcuts you asked for: `Dish.lnk` in the Start Menu programs
folder and, if you turned it on, `Dish.lnk` on the desktop. `uninstall.exe`
removes all three. The full value list is in
[`docs/INSTALLER.md`](docs/INSTALLER.md) section 4.

The app also **reads** (never writes)
`HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`
to follow your Windows light/dark setting.

Held in memory only, never written and never transmitted anywhere except to
the satellite you bound the controller to:

- Gamepad input events: buttons, sticks, triggers, motion, touchpad.
- Battery readings reported by a connected controller.
- Raw HID reports read from a controller in USB-direct mode.

The app reads input from connected game controllers, through SDL2 and, in
USB-direct mode, by reading raw HID input reports itself. It also writes back to
them: rumble and light-bar colour are sent through SDL2. The USB-direct path is
read-only. All of this is device IO on your own machine. It requires no
administrator rights and no driver install.

### 2.2 Sent to your own LAN, not to TinkerNorth

Everything the app puts on the network goes to a `satellite` server you chose,
on an address you can see in the app.

- **Discovery.** When you scan, the app sends a multicast DNS PTR query for
  `_satellite._udp.local.` to `224.0.0.251:5353`, and listens for the legacy
  UDP broadcast beacon on port 9879. The mDNS query contains the service name
  it is asking about and nothing about you. Both stay on your local network.
- **Pairing.** When you pair, the app makes an HTTPS `POST` to
  `/api/pair` on the satellite (port 9443 by default). The satellite presents
  a self-signed certificate, which the app pins on first use and checks on
  every later call. The request carries your device id, a device label, and
  either the PIN shown on the satellite or a PIN the app displays for you to
  type there. The response carries the shared pairing key, which is then
  stored as described above. `DELETE /api/pair` unpairs, which also removes
  the server-side record.
- **Control plane.** Once paired, the app calls the satellite's HTTPS API on
  the same port: `PUT`/`DELETE /api/connections` and
  `/api/connections/<id>/controllers/<slot>` to declare which controllers are
  bound and how they should be emulated, `GET /api/server/capabilities`, and
  `GET /api/catalog` for the localised list of emulatable controller types.
  These carry your device id, an HMAC proof computed from the pairing key, and
  your controller topology.
- **Gamepad stream.** The data plane is raw UDP to the satellite, port 9876
  by default, encrypted and authenticated with ChaCha20-Poly1305 under a
  session key derived from the pairing key (the pairing key itself never
  travels over UDP). Upstream frames carry controller state, motion, battery,
  and touchpad. Downstream frames carry rumble, light-bar colour, heartbeat
  acknowledgements, and session close.

Windows may prompt you once for a firewall exception so the app can send and
receive on your local network. Granting it affects your LAN only.

### 2.3 Sent to TinkerNorth or a third party

Nothing goes to TinkerNorth. There is no TinkerNorth server for this app to
talk to.

One thing leaves your network by default, and it goes to GitHub rather than to
us: the update check described in **section 2.4**. It is a plain HTTPS request
for a file on the public releases page, it carries no identifier, and you can
turn it off.

Beyond that there is no analytics library, no crash-reporting service, no
error-tracking SDK, and no advertising identifier. Apart from the updater, the
only component in the app that makes an outbound HTTPS request is the satellite
API client, and it only ever addresses the IP of a satellite you selected.

The other way this app causes a request to a TinkerNorth or third-party
address is if you click a link, which hands the URL to your default browser
and is then between you and that site. Those links are:

| Where | Opens |
|---|---|
| Help, privacy policy | `https://dish.tinkernorth.com/privacy/dish-windows/` |
| Help, project page | `https://github.com/TinkerNorth` |
| Welcome, Connections, and the setup wizard, when you have no server yet | `https://dish.tinkernorth.com/downloads/satellite` |
| Support links | `https://github.com/sponsors/TinkerNorth`, `https://ko-fi.com/tinkernorth`, `https://buymeacoffee.com/tinkernorth` |

The app does not embed a browser and does not pass any identifier along with
these URLs.

### 2.4 Update check (GitHub)

When *Check for updates automatically* is on, which is the default, the app
sends an HTTPS `GET` to `github.com` for the file `latest.json` attached to the
newest release of this project. GitHub answers with a redirect to its own
download CDN, so the request finishes against
`objects.githubusercontent.com`. The file is about a kilobyte of JSON: a
version number, a download URL, a size and a SHA-256 checksum.

**When it happens.** About 15 seconds after you launch the app, at most once
an hour; every four hours while the app stays open; and whenever you press
*Check for updates* in Settings. Nothing runs while the app is closed. There is
no service, no scheduled task, and no update agent.

**What the request carries.** Your IP address, as GitHub sees it, which is
unavoidable for any HTTPS request. A `User-Agent` header of the form
`Dish/<version> (Windows; x64)`. Nothing else. No device id, no account data,
no settings, no usage data, no cookies, no query parameters, and no
conditional-request identifiers. GitHub's own
[privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement)
governs what GitHub logs about the request.

**Downloading.** With *Download updates automatically* also on, the app then
fetches `dish-setup.exe` from the same GitHub release, to
`%LOCALAPPDATA%\Dish\updates\`. That is one more request to the same host, with
the same headers, and the downloaded file is checked against the SHA-256 in the
manifest before it is ever run. Metered connections are skipped until you are
on an unmetered one. With that switch off, the app tells you a new version
exists and downloads nothing until you ask.

**Turning it off.** *Check for updates automatically* is the master switch. Off
means the app makes no update-related network request of any kind, arms no
timer, and creates no network stack for it. The portable zip never downloads or
applies an update at all; it only tells you one exists.

The registry values behind these switches are in section 2.1, and the
`%LOCALAPPDATA%\Dish\updates\` folder is described there too.

---

## 3. Crash reports

This is the part most likely to be misread, so it is stated plainly.

**No crash report is uploaded anywhere.** The app installs a Win32 unhandled
exception filter. When the process crashes it writes two files to
`%LOCALAPPDATA%\Dish\`:

- `crash.dmp`, a minidump containing data segments, handle data, and thread
  info for the crashed process.
- `crash.log`, a text file with a UTC timestamp, the exception code, the
  faulting address and module, and a best-effort symbolised stack.

Both files stay on your disk and are overwritten by the next crash. A
minidump is a snapshot of a process, so it can contain whatever the app held
in memory at that moment, including the satellite address you were connected
to and, in principle, key material. Treat `crash.dmp` as sensitive and
consider that before you attach it to a public issue. Delete the folder at any
time; the app recreates it only if it crashes again.

Debug builds additionally route MSVC debug-CRT assertion failures into the
same `crash.log`.

**The *Share crash reports* toggle.** Settings has a *Share crash reports*
switch, on by default. Today it records your choice in the registry and hands
it to an internal no-op backend that only writes a line to the app's debug log
category `dish.crash`. It does not enable or disable any upload, because
there is no upload path to enable. The switch exists so that the preference,
its default, and the plumbing are in place before a crash backend is chosen;
the in-app description of the switch describes that intended future behaviour
rather than what ships today. If and when a real backend is added, this policy
will be updated first and the change will be called out in the release notes.

---

## 4. Windows capabilities the app uses

Windows desktop apps do not declare a permission manifest the way Android
apps do, so this is the equivalent list of what the app touches.

| Capability | Why |
|---|---|
| Network sockets: UDP multicast, UDP broadcast listen, UDP unicast, TLS over TCP | Discovery, pairing, control plane, gamepad stream. All to your LAN. |
| HTTPS to `github.com` | The update check and, if enabled, the update download. Section 2.4. Only while *Check for updates automatically* is on. |
| Game-controller device enumeration and IO | Reading controller input, and writing rumble and light-bar output back to the controller. Raw HID access is used for reading in USB-direct mode. |
| `SetThreadExecutionState` | Preventing sleep while a controller is actively streaming, so input latency stays low. Released when streaming stops. |
| `HKEY_CURRENT_USER` registry read and write | Settings and pairing state, as listed in section 2.1. |
| `%LOCALAPPDATA%\Dish\` file write | The crash dump and crash log, and the `updates\` folder described in section 2.1. |
| Default browser launch | Opening the links listed in section 2.3, only when you click one. |

The app runs as a normal user. It does not request elevation, does not
install a service or a driver, does not read your files, and does not access
the microphone, camera, location, contacts, or clipboard. The one exception to
elevation is an all-users install: applying an update to an installation under
`Program Files` needs administrator rights, so the installer asks for them once
with the standard Windows prompt, and a declined prompt simply leaves the old
version running. A per-user install, which is the default, never prompts.

---

## 5. Your choices

- **Forget a satellite.** Removing a satellite deletes its remembered row,
  its stored pairing key, and its certificate pin from the registry, and
  unpairs on the server so any live session is closed there too.
- **Turn off crash file writing.** There is no toggle for this today because
  the files never leave your machine. You can delete `%LOCALAPPDATA%\Dish\`
  whenever you like.
- **Stop the update check.** Settings, Updates, *Check for updates
  automatically*. Off means no update-related request leaves your machine, at
  any time, for any reason. Leaving it on but turning off *Download updates
  automatically* keeps the check and stops the download.
- **Delete the update cache.** `%LOCALAPPDATA%\Dish\updates\` can go at any
  time. The app recreates it only when it downloads an update, and deleting a
  partly-downloaded update simply makes it start over.
- **Uninstall.** `uninstall.exe`, or Windows Settings, Installed apps, removes
  the program files, the two shortcuts, the Add/Remove Programs entry and the
  update cache. Your settings, pairings and crash files are deliberately left
  behind so that reinstalling restores your setup; tick *Also remove my
  settings* (or pass `--purge-user-data` to a silent uninstall) to remove those
  too.
- **Wipe everything.** Delete `HKEY_CURRENT_USER\Software\Dish\Dish` and
  `HKEY_CURRENT_USER\Software\TinkerNorth\Dish`, and delete
  `%LOCALAPPDATA%\Dish\`. That removes every remembered server, pairing key,
  certificate pin, preference, update setting, and crash artifact. There is no
  server-side record to delete, because there is no TinkerNorth server.
- **Verify any of this.** The client is free software under
  [LGPL-3.0-or-later](LICENSE). Every claim above is checkable in this
  repository, and you can build the binary yourself. See
  [`CONTRIBUTING.md`](CONTRIBUTING.md).

---

## 6. Children's privacy

Dish is suitable for general audiences. The app collects nothing from anyone,
of any age, so there is no children's data for us to hold. If you believe
that is wrong in some way we have not anticipated, contact
`privacy@tinkernorth.com`.

---

## 7. International transfers

None. No personal data leaves your machine to us, so there is nothing to
transfer across a border.

---

## 8. Changes to this policy

We will update the *Effective date* at the top whenever this policy changes.
Material changes, in particular the addition of any crash-reporting or
analytics backend, will be made here before the code ships and will be called
out in that release's notes in [`CHANGELOG.md`](CHANGELOG.md). Previous
versions remain in the git history of this file.

---

## 9. Contact

- Privacy questions: `privacy@tinkernorth.com`
- Security disclosures: see [`SECURITY.md`](SECURITY.md)
- General contact and bug reports: open an issue in this repository
