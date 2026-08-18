# The installer and the auto-updater

`dish-setup.exe` installs Dish; the app that it installs keeps itself up to
date by fetching the next `dish-setup.exe` and handing off to it at the next
start. The installer is **Inno Setup**, compiled from
[`installer.iss`](../installer.iss) at the repository root; its payload is the
install image staged by [`cmake/DishSetupImage.cmake`](../cmake/DishSetupImage.cmake),
which is the one reviewed place that decides what an installed Dish consists
of.

This file is the reference for anyone changing that code, packaging a release,
or debugging a failed install in the field. The user-facing summary is in
[`README.md`](../README.md); the trust story is in [`SECURITY.md`](../SECURITY.md);
what gets written where is in [`PRIVACY.md`](../PRIVACY.md).

---

## 1. What a release carries

| Asset | What it is |
|---|---|
| `dish-setup.exe` | Inno Setup installer. One file, no prerequisites, per-user by default. |
| `dish-windows.zip` | The portable bundle. Unzip and run; never self-updates. |
| `latest.json` | The update manifest every installed copy polls. Immutable asset name. |
| `SHA256SUMS` | Checksums of the four assets above, as a release asset in its own right. |
| `dish-windows.spdx.json` | SPDX SBOM of the portable bundle. |

The installer and the zip carry the same install image: `dish.exe`, the Qt
runtime and QML modules from `windeployqt`, `libsodium.dll` and `SDL2.dll`,
the five app-local Visual C++ runtime DLLs, the app's `Dish/Chrome/qmldir`
module entry, and the four licence texts under `licenses\`. That is what lets
either one run on a machine where nothing has ever been installed.

---

## 2. Installing

Double-clicking runs the normal Inno Setup wizard: language, licence, scope,
folder, a Desktop-shortcut checkbox, install. The defaults are the product
decisions:

- **Per-user by default** (`PrivilegesRequired=lowest`): installs under
  `%LOCALAPPDATA%\Programs\Dish` with **no UAC prompt**. "Install for all
  users" is offered through Inno's scope dialog and elevates exactly once;
  that scope installs under `C:\Program Files\Dish`.
- **Start Menu entry always; Desktop shortcut opt-in** (unchecked by default).
- **Windows 10 1809 x64 or newer** (`MinVersion=10.0.17763`,
  `ArchitecturesAllowed=x64compatible`). ARM64 runs the x64 build under
  emulation. An older machine gets a clear refusal dialog, not a broken
  install.
- **A running Dish does not block the install.** Inno closes it through
  Restart Manager (a graceful `WM_QUERYENDSESSION`, so settings writes land)
  and restarts it afterwards.
- The wizard speaks five of the app's six languages (English, German, Spanish,
  French, Brazilian Portuguese) through Inno's official catalogues. Bosnian
  has no official Inno catalogue and is an open follow-up; the app itself
  ships all six.

### What it writes

One Add/Remove Programs key, created by Inno under
`<hive>\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{0B4F3D3C-9526-4953-9CCA-BAAD2AF4A5A1}_is1`
(HKCU for per-user, HKLM for all-users), holding the display name, version,
publisher, install location, uninstall command and estimated size. The
`AppId` in `installer.iss` is how Windows and Inno recognise an in-place
upgrade; never change it without a migration plan.

The shortcuts are ordinary `.lnk` files: the Start Menu entry always, the
Desktop one when its task was ticked. Upgrades preserve the previous install's
directory and scope automatically through Inno's own recorded state.

---

## 3. Silent installs

Inno Setup's standard switches, so any deployment tooling that already speaks
Inno speaks Dish:

```
dish-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
    [/CURRENTUSER | /ALLUSERS]    scope; default per-user, /ALLUSERS needs elevation
    [/DIR="C:\Some Dir"]          install directory override
    [/TASKS="desktopicon"]        opt into the Desktop shortcut
    [/LANG=<code>]                wizard language for /SILENT runs
    [/LOG="file.log"]             full install log; without it one lands in %TEMP%
    [/OTA]                        internal: the auto-updater's boot handoff (section 5)
```

`/SILENT` shows a progress bar with no questions; `/VERYSILENT` shows nothing.
A re-run over the same version is a repair; a newer setup over an older
install is an upgrade into the same directory and scope. Exit codes are
[Inno Setup's documented set](https://jrsoftware.org/ishelp/index.php?topic=setupexitcodes):
`0` success, `2` cancelled, `5` the preflight checks failed, `8` a restart is
needed. The exe is a GUI-subsystem binary, so a script should wait explicitly:

```powershell
$p = Start-Process .\dish-setup.exe -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -Wait -PassThru
$p.ExitCode
```

Uninstall silently with the same switch set:

```powershell
& "$env:LOCALAPPDATA\Programs\Dish\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
```

---

## 4. Uninstalling

`unins000.exe` beside `dish.exe` (also reachable from Windows Settings,
Installed apps) removes the installed files, the shortcuts, the ARP entry, and
`%LOCALAPPDATA%\Dish\updates\` — the updater's download cache is not user
data.

Kept deliberately: `HKCU\Software\Dish\Dish`, `HKCU\Software\TinkerNorth\Dish`
and `%LOCALAPPDATA%\Dish`, so reinstalling restores the user's satellites,
pairings and preferences. [`PRIVACY.md`](../PRIVACY.md) documents removing
those by hand.

The uninstaller's presence is also a signal the app reads: a `dish.exe` with
no `unins*.exe` sibling knows it is the portable bundle
([`src/update/UpdateCoordinator.cpp`](../src/update/UpdateCoordinator.cpp)),
checks for updates, and only notifies — there is nothing installed to apply
an update to.

---

## 5. Auto-update

### The manifest

Every release carries `latest.json`, and the app fetches exactly one URL:

```
https://github.com/TinkerNorth/dish-windows/releases/latest/download/latest.json
```

That permalink resolves to the newest published, non-draft, non-prerelease
release. There is no moving git tag and no call to `api.github.com`.

```json
{
  "schema": 1,
  "product": "dish-windows",
  "version": "1.1.0",
  "channel": "stable",
  "publishedAt": "2026-08-03T14:21:07Z",
  "minimumSupportedVersion": "0.1.0",
  "releaseNotesUrl": "https://github.com/TinkerNorth/dish-windows/releases/tag/1.1.0",
  "assets": {
    "dish-setup.exe":   { "url": "...", "sha256": "<64 lowercase hex>", "size": 41943040 },
    "dish-windows.zip": { "url": "...", "sha256": "<64 lowercase hex>", "size": 52428800 }
  }
}
```

The consumer rejects a body over 64 KiB before parsing it, requires
`schema == 1`, `product == "dish-windows"`, `channel == "stable"`, a strict
`MAJOR.MINOR.PATCH` version with no `v` and no prerelease suffix, an asset URL
that starts with the release-download prefix for this repository, a
64-character lowercase hex digest, and a size between 1 byte and 500 MB.
`publishedAt` is display only: nothing anywhere orders by wall clock.

`minimumSupportedVersion` comes from
[`packaging/update-policy.json`](../packaging/update-policy.json), a reviewed
release input. An installed copy older than it treats the update as required,
which means the skip control disappears and the "no longer supported" copy
appears.

### Schedule

15 seconds after launch, then every 4 hours while running. The startup check is
skipped when the last check was under an hour ago, unless that timestamp is
more than 24 hours in the future, which is the clock-skew escape. Failures back
off from 10 minutes, doubling to a 6 hour cap with plus or minus 20 percent
jitter, and reset on the first success. A manual check bypasses both, rate
limited to one per 10 seconds.

`QNetworkInformation` gates the work: not-online means the failure is recorded
with no network IO at all, and a pending check fires 30 seconds after
connectivity returns. A metered connection defers the automatic download but
never a manual one.

Both `QNetworkAccessManager` instances are dedicated and use Qt's **default**
certificate validation against the Windows system roots. `net::HTTPClient` and
`PairingClient` use `QSslSocket::VerifyNone` with trust-on-first-use pinning,
because a satellite presents a self-signed certificate. Those two must never
carry updater traffic, and `src/update/HttpGateways.cpp` says so at the point
of use.

### Staging layout

Rooted at `%LOCALAPPDATA%\Dish\updates\`, the same parent the crash handler
uses, built from `%LOCALAPPDATA%` directly rather than `QStandardPaths`:

```
staging\dish-setup-<version>.exe.part     in-flight bytes, never trusted, no resume
ready\<version>\dish-setup.exe            fully verified bytes
ready\<version>\manifest.json             snapshot of the latest.json that described it
ready\<version>\ready.marker              written LAST, via tmp + flush + rename
ready\<version>\apply-attempts.json       {"count":N,"lastAttemptUtc":"..."}
ready\<version>\apply.log                 the installer's /LOG output for that attempt
```

`ready.marker` holds `schema`, `version`, `sha256`, `size` and `stagedUtc`. It
is written last and atomically, so there is no third state: a tree either has a
marker that matches the file on disk or it is swept.

The janitor runs at the boot gate, at the start of every check cycle, and after
every promote. It deletes `staging\*` older than 24 hours, any `ready\<v>` whose
name does not parse or whose marker is missing, malformed or disagrees with the
file, and any `ready\<v>` with `v <= DISH_VERSION`. That last rule is both the
post-apply loop breaker and the answer to "an installer cannot delete itself":
the just-used installer is briefly locked, and the next pass gets it. If several
valid trees survive, only the highest version is kept.

### Boot handoff

`runStartupHandoff()` is called from `main()` immediately after
`dish::crash::install()` and **before** Winsock, libsodium and
`QGuiApplication`. It uses only explicitly-constructed `QSettings`, `QFile`,
`QCryptographicHash` and Win32, because none of the app's own machinery exists
yet.

1. `--no-update-handoff` on the command line: skip. The installer passes this
   when it relaunches the old app after a failed apply, and it is the
   documented troubleshooting flag. Skip-once semantics; it is not persisted.
2. `OpenMutexW(Local\TinkerNorth.Dish.Running)` succeeds: another instance owns
   the lifecycle, so skip without consuming an attempt. After the gate, `main`
   creates and holds that mutex for the process lifetime. It is a probe, not
   single-instancing.
3. Pick the highest parsable version under `ready\`. Every guard below discards
   that stage and continues a normal startup on failure: the version parses, it
   is strictly greater than `DISH_VERSION`, its attempt count is under 2, the
   exe exists at the recorded size, and a full SHA-256 re-read matches the
   marker.
4. Write `updates_handoff_version` and the incremented
   `updates_handoff_attempts`, then `QSettings::sync()`, **before** spawning, so
   a crash during the spawn still counts against the cap.
5. `CreateProcessW` the staged installer with the switches below, with the
   working directory set to the updates root rather than the version
   directory, so the installer's own directory stays deletable. On success
   `main` returns 0 immediately.

Two attempts per version, then the stage is quarantined: it is deleted, and
Settings shows the failure with a link to download the release by hand.

`restartToApplyUpdate()` shares one spawn function with the gate. It arms a
pending-restart flag and requests a normal window close, so every existing close
guard runs first and cancelling any of them cancels the restart with nothing
spawned. The spawn happens from `aboutToQuit`, after the guards have all
passed.

### The apply contract

```
<ready>\<v>\dish-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /OTA
                           /LOG="<ready>\<v>\apply.log"
```

The producing half is `UpdateHandoff::applyArguments()`, pinned by
`tests/test_update_handoff.cpp`; the consuming half is Inno Setup plus the
`[Code]` section of `installer.iss`. The division of duties:

- **Which install.** Inno's own previous-install record, keyed by the `AppId`.
  The app never passes `/DIR`, `/CURRENTUSER` or `/ALLUSERS` on this path:
  re-supplying either could fork a machine-scope install into a per-user one.
  A per-user apply raises no UAC; a machine-scope apply self-elevates exactly
  once, and a declined prompt fails the install rather than looping.
- **Waiting.** `/OTA` makes `[Code]` poll the app's `Running` mutex for up to
  60 seconds before any file is touched — the spawning `dish.exe` is mid-exit,
  and its mutex dies with the process. Restart Manager remains the second
  line for anything else holding a file.
- **Relaunch duties.** The spawning app is dead by design, so `/OTA` owns
  what happens next. On success, `[Code]` starts the freshly installed
  `dish.exe` with no arguments (its janitor then discards the stale stage on
  its own). On any failure, Inno's rollback has restored the old install, and
  `[Code]` starts the **old** `dish.exe` with exactly one argument,
  `--no-update-handoff`. The user pressed restart: under no outcome may they
  end up with no app.
- **Result detection.** There is no result file. A successful apply is visible
  as the relaunched exe's own version; a failed one leaves the stage in place,
  and the attempt counter bounds retries at two before quarantine.

### Preferences

All under `HKCU\Software\TinkerNorth\Dish`, which is where Windows-desktop-only
preferences live (the cross-client schema is `Software\Dish\Dish`). The boot
gate runs before `QGuiApplication` sets the organisation name, so it constructs
`QSettings("TinkerNorth", "Dish")` explicitly and lands in the same place.

| Key | Default | Meaning |
|---|---|---|
| `updates_check_enabled` | `true` | Master toggle. No update network IO when false. |
| `updates_auto_download` | `true` | False means check and notify only. |
| `updates_skipped_version` | `""` | Exact version muted and un-staged; ignored while required |
| `updates_last_check_utc_ms` | `0` | Minimum-gap bookkeeping |
| `updates_handoff_version` | `""` | Version being applied at boot |
| `updates_handoff_attempts` | `0` | Incremented and synced before each spawn; cap 2 |
| `updates_last_run_version` | `""` | Edge detection for the "Updated to Dish X" moment |

The portable bundle is detected by the absence of an Inno uninstaller
(`unins*.exe`) beside `dish.exe`. A portable copy still checks and notifies,
but never downloads and never applies; Settings switches to the portable
wording with a link to the releases page.

---

## 6. SmartScreen, and verifying a download

`dish-setup.exe` is **not** Authenticode-signed. Microsoft Defender SmartScreen
will show "Windows protected your PC" on first run of a fresh download from a
new release, because the file has no reputation and no signature. The way
through is **More info**, then **Run anyway**. That warning is expected, and it
will keep appearing until release signing lands (it is on the roadmap in
[`CONTRIBUTING.md`](../CONTRIBUTING.md)).

If you would rather check the bytes than trust the dialog, the release carries
`SHA256SUMS` as its own asset:

```powershell
$expected = (Get-Content .\SHA256SUMS |
    Where-Object { $_ -match 'dish-setup\.exe' }).Split()[0]
$actual = (Get-FileHash .\dish-setup.exe -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) { throw 'checksum mismatch' } else { 'ok' }
```

Read [`SECURITY.md`](../SECURITY.md) for what that check is and is not worth:
`SHA256SUMS` is unsigned, so it proves the download is intact, not that it came
from this project.

The automatic update path is not interposed by SmartScreen: files an app writes
itself carry no mark of the web, so the staged installer runs without a
reputation prompt. That is also why the sha256 chain from `latest.json` matters
more on that path than on the manual one.

If an install will not start at all, the payload is still recoverable without
any Dish tooling: [`innoextract`](https://constexpr.org/innoextract/) unpacks
any Inno Setup installer.

```
innoextract dish-setup.exe
```

---

## 7. Working on this

### Building

```powershell
cmake --build build-release --target Dish dish_setup_image   # stage the image
pwsh scripts/build-installer.ps1                             # iscc -> dist\dish-setup.exe
```

`dish_setup_image` runs `cmake/DishSetupImage.cmake` to stage
`build-release\setup-image` with `windeployqt`, the app-local CRT and the
licence texts; the script needs `VCToolsRedistDir`, which `vcvars64.bat` sets.
`scripts/build-installer.ps1` reads the version from
`project(Dish VERSION ...)` and compiles `installer.iss` with ISCC (Inno Setup
6, `choco install innosetup` or the
[upstream installer](https://jrsoftware.org/isinfo.php)). CI does exactly
these two steps; release.yml passes the tag's version instead.

### The round trip

```powershell
./scripts/test-installer-roundtrip.ps1 -Setup dist/dish-setup.exe
```

The same script both workflows run: a fresh `/VERYSILENT` install into a path
with a space, the ARP entry's values, a repair re-run (the shape of every OTA
apply), and a silent uninstall polled to completion — files, directory and
registry key all gone. Everything happens under a scratch directory and HKCU;
no elevation, no prompts.

### Strings

The installer's wizard text comes from Inno Setup's own `.isl` catalogues, so
there is nothing to translate in this repository for it. The app-side updater
strings (the update pill, Settings, the failure notices) live in the six
`translations/` catalogues like every other app string. Bosnian installer text
is the one known gap: Inno has no official `Bosnian.isl`, and vendoring the
community translation is an open follow-up.

---

## 8. Manual test matrix

The automated gates cover the silent paths. These are the checks that need a
human, and they belong in the pre-release checklist.

| Check | What good looks like |
|---|---|
| SmartScreen walkthrough | More info, Run anyway, install completes |
| UAC declined on all-users scope | Setup reports it cleanly; nothing written; per-user still works |
| Install while Dish is running | Restart Manager closes it gracefully and restarts it after |
| Non-ASCII account name | Install and uninstall both clean under a Cyrillic or CJK profile |
| Per-monitor DPI | The wizard is crisp at 100 / 150 / 200 percent |
| Languages | Wizard follows the Windows display language for the five shipped |
| Update pill | idle, available, downloading, ready, in both themes |
| Restart now during a transfer | The keep-awake confirm wins; cancelling it cancels the restart |
| Portable copy | Notify-only updater, releases-page link, no download button |
| Two-version end to end | Old build stages the new release, restart applies it silently, the app comes back as the new version, the "Updated to Dish X" toast appears once |
| OTA failure leg | Make the apply fail (lock a file); the old version relaunches with `--no-update-handoff`; second boot quarantines and Settings shows the manual link |
