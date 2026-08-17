# The installer and the auto-updater

`dish-setup.exe` installs Dish; the app that it installs keeps itself up to
date by fetching the next `dish-setup.exe` and handing off to it at the next
start. The two halves share one payload format, one exit-code table, and one
set of translation catalogues, so they are documented together.

This file is the reference for anyone changing that code, packaging a release,
or debugging a failed install in the field. The user-facing summary is in
[`README.md`](../README.md); the trust story is in
[`SECURITY.md`](../SECURITY.md); what gets written where is in
[`PRIVACY.md`](../PRIVACY.md).

---

## 1. What a release carries

| Asset | What it is |
|---|---|
| `dish-setup.exe` | Self-extracting installer. One file, no prerequisites, per-user by default. |
| `dish-windows.zip` | The portable bundle. Unzip and run; never self-updates. |
| `latest.json` | The update manifest every installed copy polls. Immutable asset name. |
| `SHA256SUMS` | Checksums of the four assets above, as a release asset in its own right. |
| `dish-windows.spdx.json` | SPDX SBOM of the portable bundle. |

`dish-setup.exe` and `dish-windows.zip` contain the same `dish.exe` and the same
four licence texts. The installer additionally carries the wizard, the
uninstaller, the uninstall helper and the five Visual C++ runtime DLLs, which is
what lets it run on a machine where nothing has ever been installed.

---

## 2. Payload format

`dish-setup.exe` is three concatenated parts:

```
[ DishSetupStub PE ][ payload.zip ][ Trailer (32 bytes) ]
```

The trailer sits at the very end of the file and is read backwards from EOF.
It is packed, little-endian, and defined in
[`src/installer/PayloadFormat.h`](../src/installer/PayloadFormat.h):

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 8 | `magic` | `DISHSFX1`, ASCII, no terminator |
| 8 | 4 | `formatVersion` | `1` |
| 12 | 4 | `zipCrc32` | CRC-32 (zlib polynomial) over the zip bytes |
| 16 | 8 | `zipOffset` | Absolute offset of `payload.zip` in the file |
| 24 | 8 | `zipSize` | Length of `payload.zip` in bytes |

`payload.zip` is a standard ZIP written by [miniz](../third_party/miniz) at
DEFLATE level 9. That is deliberate: `7z x dish-setup.exe` recovers the whole
install image without any Dish tooling, which is the documented support path
when an install will not run at all. Entry names are validated twice, once by
the pack tool and once by the stub before extraction: ASCII only, forward
slashes, relative, no `..`, no drive letter, no leading slash.

### What is inside the zip

```
dish.exe                        the app
<windeployqt output>            Qt DLLs, platforms/, styles/, imageformats/, qml/
msvcp140.dll msvcp140_1.dll msvcp140_2.dll vcruntime140.dll vcruntime140_1.dll
dish-setup-ui.exe               the Qt Quick wizard, runs from %TEMP%
uninstall-helper.exe            the tiny /MT janitor, installed
licenses/LICENSE.LGPL-3.0.txt   from LICENSE
licenses/LICENSE.GPL-3.0.txt    from COPYING.GPL3
licenses/THIRD_PARTY.md
licenses/Inter-LICENSE.txt
manifest.json                   the payload manifest
```

`manifest.json` is the single source of truth for the copy loop, the progress
bar, the per-file SHA-256 verification, the ARP `EstimatedSize` value, and the
uninstall file list:

```json
{
  "schema": 1,
  "version": "0.1.0",
  "totalBytes": 104857600,
  "files": [
    { "path": "dish.exe", "size": 123, "sha256": "<64 lowercase hex>" },
    { "path": "uninstall.exe", "size": 123, "sha256": "...", "stagedAs": "dish-setup-ui.exe" }
  ]
}
```

`files[].path` is the path relative to the install directory.
`stagedAs` names the file inside the extracted image to copy from when it
differs; `uninstall.exe` is the only entry that uses it, so the roughly 1.5 MB
wizard binary is stored once and installed twice under two names.
`dish-setup-ui.exe` and `manifest.json` are not manifest entries: they exist in
the image but are never installed.

---

## 3. What happens when a user double-clicks it

The stub is the only binary that has to run on a machine with nothing
installed. It is `/MT` (static CRT), has no Qt, and links `kernel32 user32
shell32 comctl32 advapi32` and nothing else.

1. Capture the raw command-line tail verbatim for pass-through, so
   `/D=C:\Some Dir` survives re-quoting. Scan only for `/S`, `--silent`,
   `--update-apply`, `--help` and `--version`.
2. OS gate: Windows 10 1809 (10.0.17763) or newer, x64. ARM64 is allowed and
   runs under emulation. Otherwise exit 3.
3. Read the trailer, validate magic and version, CRC-32 the payload range. Any
   mismatch is exit 7 with a "this download is damaged" dialog.
4. Preflight free space on the `%TEMP%` volume, then create
   `%TEMP%\dish-setup-<8 hex>\`.
5. Non-silent only: a `TaskDialogIndirect` marquee titled "Dish Setup". Its
   Cancel button is labelled by Windows, so it is localised even in a locale
   the stub has no string table for. Cancel stops at the next entry, deletes
   the staging directory, exits 10.
6. Extract with miniz, which verifies each entry's own CRC-32 as it goes.
7. Spawn `<staging>\dish-setup-ui.exe --staging <dir> --source-exe <original>
   -- <verbatim tail>` and wait for it.
8. Delete the staging directory (5 attempts, 300 ms apart, for antivirus
   stragglers) and exit with the child's exit code.

The wizard runs against the Qt and CRT DLLs sitting beside it in the extracted
image, which is also the image that gets installed. Nothing is copied out of
`%TEMP%` into the install directory until the user presses the install verb on
the welcome (or Options) face.

---

## 4. Installing

Fresh installs copy straight to the final path. Upgrades stage into
`<dir>\.dish-stage\` and commit with a burst of same-volume renames, keeping
`.dish-old` backups journaled, so the previous version stays launchable until
the commit and a cancel before the commit costs nothing.

Every mutation is appended to a journal file before it happens, flushed, and
replayed in reverse on failure. Exit 8 means "it failed and the rollback was
clean"; exit 9 means "it failed and something is left behind", and the log
names what.

### Registry

One key, written last, in the 64-bit view
(`KEY_WOW64_64KEY`), under HKCU for a per-user install and HKLM for an
all-users one:

`<hive>\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\TinkerNorth.Dish`

| Value | Type | Content |
|---|---|---|
| `DisplayName` | `REG_SZ` | `Dish` |
| `DisplayVersion` | `REG_SZ` | `<major>.<minor>.<patch>` |
| `VersionMajor`, `VersionMinor` | `REG_DWORD` | parsed from the version |
| `Publisher` | `REG_SZ` | `TinkerNorth` |
| `DisplayIcon` | `REG_SZ` | `<dir>\dish.exe,0` |
| `InstallLocation` | `REG_SZ` | `<dir>` |
| `InstallDate` | `REG_SZ` | `yyyyMMdd` |
| `UninstallString` | `REG_SZ` | `"<dir>\uninstall.exe"` |
| `QuietUninstallString` | `REG_SZ` | `"<dir>\uninstall.exe" --silent` |
| `NoModify`, `NoRepair` | `REG_DWORD` | `1` |
| `EstimatedSize` | `REG_DWORD` | KiB, ceil of the installed byte total |
| `URLInfoAbout`, `HelpLink` | `REG_SZ` | the GitHub repository |
| `InstallScope` | `REG_SZ` | `user` or `machine` |

There is no other registry state. The authoritative record of what was
installed, where, at which scope, with which shortcuts, is
`.dish-manifest.json` in the install directory. It travels with the install,
which is what lets a silent upgrade reproduce the original choices without the
app having to remember them.

### Shortcuts

At most two `.lnk` files, both named `Dish.lnk`, both recorded by absolute path
in `.dish-manifest.json`:

- Start Menu: `FOLDERID_Programs` or `FOLDERID_CommonPrograms`. No vendor
  subfolder.
- Desktop: `FOLDERID_Desktop` or `FOLDERID_PublicDesktop`.

A machine-scope install writes only to machine locations. It never touches the
elevating administrator's own profile.

---

## 5. Uninstalling

`uninstall.exe` is a byte copy of `dish-setup-ui.exe` running against the Qt
DLLs installed beside it. It picks uninstall mode from its own basename.

It cannot delete itself while it is running, so the last step hands off:
`uninstall-helper.exe` is copied to its own `%TEMP%` directory and spawned with
the list of leftover paths and the pid to wait on. The helper waits for the
uninstaller to exit, retry-deletes each path with backoff for up to 30 seconds,
removes the install directory if it is empty, and deletes the ARP key **last**.
That ordering is deliberate: a cleanup blocked by antivirus leaves a working
Add/Remove Programs entry pointing at a still-present `uninstall.exe`, so the
user can simply try again.

Removed always: the installed files, the shortcuts, and
`%LOCALAPPDATA%\Dish\updates\` (the updater's download cache is not user data).

Kept unless `--purge-user-data` (silent) or the confirm-page switch (UI):
`HKCU\Software\Dish\Dish`, `HKCU\Software\TinkerNorth\Dish`, and
`%LOCALAPPDATA%\Dish`. Reinstalling therefore restores the user's satellites,
pairings and preferences. A machine-scope purge only touches the invoking
user's hive; it does not walk other users' profiles, and the UI says so.

---

## 6. Command line

Flags are case-insensitive. The stub passes its tail through verbatim and
parsing happens once, in
[`src/installer/CliOptions.cpp`](../src/installer/CliOptions.cpp).

```
dish-setup.exe [options]
  /S | --silent                 silent install (no dialog, no Qt UI, no UAC)
  /D=<dir>                      NSIS-compatible install dir; MUST be last, unquoted
  --dir <dir>                   install dir override
  --scope user|machine          default user; silent machine scope needs an elevated caller
  --start-menu on|off           default on
  --desktop on|off              default off
  --launch on|off               default on in the UI, off in silent
  --closeapps                   graceful close plus a 10 s wait for a running Dish
  --forceclose                  implies --closeapps, then terminates
  --allow-downgrade             permit older-over-newer
  --lang <code>                 system|en|bs|de|es|fr|pt_BR
  --log <file>                  default %TEMP%\dish-setup-<timestamp>.log; always written
  --extract-only <dir>          unpack the install image only: no registry, no shortcuts
  --version | --help            print and exit 0
  --update-apply --waitpid <pid> --target-exe <path> --expect-version <M.m.p>
                 [--no-relaunch] [--log <file>]

<installdir>\uninstall.exe [--uninstall] [/S | --silent] [--purge-user-data]
                           [--closeapps | --forceclose] [--lang <code>] [--log <file>]
```

Both binaries are GUI-subsystem executables, so a shell does not wait on them
by default and console output is a best-effort `AttachConsole` echo. The exit
code and the log file are the interfaces a script should use:

```powershell
$p = Start-Process .\dish-setup.exe -ArgumentList '/S','--desktop','on' -Wait -PassThru
$p.ExitCode
```

### Exit codes

The table is generated from one authority,
[`src/installer/Errors.h`](../src/installer/Errors.h). The token column is what
`--update-apply` writes into `apply-result.txt`.

| Code | Meaning | Token |
|---|---|---|
| 0 | Install, repair, upgrade or uninstall completed | `ok` |
| 1 | Internal error, see the log | `internal` |
| 2 | Command-line usage error | `usage` |
| 3 | Unsupported OS: needs Windows 10 1809+ x64 | `unsupported-os` |
| 4 | Elevation required or declined | `elevation-declined` |
| 5 | Blocked by a running Dish | `app-running` |
| 6 | Insufficient disk space (target volume or `%TEMP%`) | `disk-full` |
| 7 | Payload integrity failure (trailer, CRC, zip or sha256) | `integrity-failure` |
| 8 | An operation failed; rollback completed cleanly | `rolled-back` |
| 9 | An operation failed and the rollback was incomplete | `rollback-incomplete` |
| 10 | Cancelled by the user | `cancelled` |
| 11 | Nothing to uninstall, or no managed install to upgrade | `nothing-installed` |
| 12 | Downgrade refused (silent, without `--allow-downgrade`) | `downgrade-refused` |
| 13 | Another setup instance is running (`Local\TinkerNorth.DishSetup`) | `busy` |
| 14 | `--update-apply --expect-version` disagrees with the payload | `version-mismatch` |

A silent same-version install is a repair and exits 0. A silent uninstall's exit
code covers everything except the helper's tail (the working set, the install
directory and the ARP key), which lands within seconds; poll for up to 30 s.

---

## 7. Auto-update

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
  "version": "0.2.0",
  "channel": "stable",
  "publishedAt": "2026-08-03T14:21:07Z",
  "minimumSupportedVersion": "0.1.0",
  "releaseNotesUrl": "https://github.com/TinkerNorth/dish-windows/releases/tag/v0.2.0",
  "assets": {
    "dish-setup.exe":   { "url": "...", "sha256": "<64 lowercase hex>", "size": 41943040 },
    "dish-windows.zip": { "url": "...", "sha256": "<64 lowercase hex>", "size": 52428800 }
  }
}
```

The consumer rejects a body over 64 KiB before parsing it, requires
`schema == 1`, `product == "dish-windows"`, `channel == "stable"`, a strict
`MAJOR.MINOR.PATCH` version with no `v` and no prerelease suffix, an asset URL
that starts with the release-download prefix for this repository, a 64-character
lowercase hex digest, and a size between 1 byte and 500 MB.
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
certificate validation against the Windows system roots.
`net::HTTPClient` and `PairingClient` use `QSslSocket::VerifyNone` with
trust-on-first-use pinning, because a satellite presents a self-signed
certificate. Those two must never carry updater traffic, and
`src/update/HttpGateways.cpp` says so at the point of use.

### Staging layout

Rooted at `%LOCALAPPDATA%\Dish\updates\`, the same parent the crash handler
uses, built from `%LOCALAPPDATA%` directly rather than `QStandardPaths`:

```
staging\dish-setup-<version>.exe.part     in-flight bytes, never trusted, no resume
ready\<version>\dish-setup.exe            fully verified bytes
ready\<version>\manifest.json             snapshot of the latest.json that described it
ready\<version>\ready.marker              written LAST, via tmp + flush + rename
ready\<version>\apply-attempts.json       {"count":N,"lastAttemptUtc":"..."}
ready\<version>\apply-result.txt          "<token> <exit-code>", written by the installer
ready\<version>\apply.log                 the installer's log for that attempt
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
   when it relaunches the old app after a failure, and it is the documented
   troubleshooting flag. Skip-once semantics; it is not persisted.
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
5. `CreateProcessW` the staged installer with the apply arguments below, with
   the working directory set to the updates root rather than the version
   directory, so the installer's own directory stays deletable. On success
   `main` returns 0 immediately.

Two attempts per version, then the stage is quarantined: it is deleted, and
Settings shows the failure with a link to download the release by hand.

`restartToApplyUpdate()` shares one spawn function with the gate. It arms a
pending-restart flag and requests a normal window close, so every existing close
guard runs first and cancelling any of them cancels the restart with nothing
spawned. The spawn happens from `aboutToQuit`, after the guards have all
passed, waiting on the app's own pid.

### The apply contract

```
<ready>\<v>\dish-setup.exe --update-apply --waitpid <pid>
                           --target-exe "<abs path of the running dish.exe>"
                           --expect-version <M.m.p>
                           --log "<ready>\<v>\apply.log"
```

CI adds `--no-relaunch`. `/S` is accepted but redundant; the mode implies
silent.

- **Which install.** The one whose directory contains `--target-exe`, resolved
  through its `.dish-manifest.json`. Every parameter (directory, scope, Start
  Menu, Desktop) comes from that record verbatim. The app never re-supplies
  them, and an upgrade neither adds nor drops a shortcut relative to the
  record. There is no fallback to a fresh default-path install: a missing
  record is exit 11.
- **Waiting.** Up to 60 seconds for `--waitpid` to exit, before any mutation.
  On timeout, or when other processes still run from the install directory, the
  bounded lock-retry runs and then it fails with exit 5. This mode never calls
  `TerminateProcess`.
- **Gates.** Own trailer CRC before extraction and per-file SHA-256 during the
  copy (exit 7); `--expect-version` must equal the payload manifest version
  (exit 14, which is what defends against a swapped staged file); the payload
  version must be strictly greater than the installed one (exit 12). No
  downgrade override is ever passed on this path.
- **Elevation.** Scope is read from the recorded manifest, never guessed. A
  per-user install never raises UAC. A machine-scope apply self-elevates exactly
  once; that consent dialog is the only UI a silent mode may ever show, and a
  declined prompt is a failure (exit 4), never a loop.
- **Relaunch duties.** On success, start the new `dish.exe` from the install
  directory with no arguments and the install directory as the working
  directory, de-elevated to the invoking user when the apply ran elevated. On
  any failure after the waited pid exited, relaunch the **old** `dish.exe` with
  exactly one argument, `--no-update-handoff`. The user pressed restart: under
  no outcome may they end up with no app. `--no-relaunch` suppresses both.
- **Result reporting.** The invoking app is dead, so besides the exit code the
  installer atomically writes `apply-result.txt` next to the invoked exe:
  `<token> <exit-code>` and a newline, using the token column from section 6.

The installer tolerates running from `%LOCALAPPDATA%\Dish\updates\ready\<v>\`
and owns no cleanup of that directory on success. The relaunched app's janitor
removes it once the staged version is no longer greater than the running one.

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

The portable bundle is detected by the absence of `uninstall.exe` beside
`dish.exe`. A portable copy still checks and notifies, but never downloads and
never applies; Settings switches to the portable wording with a link to the
releases page.

---

## 8. SmartScreen, and verifying a download

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

If an install will not start at all, the payload is still recoverable with any
ZIP tool, and `--extract-only` does the same thing without touching the
registry:

```powershell
7z x dish-setup.exe -odish-image           # any ZIP tool works
.\dish-setup.exe --extract-only .\dish-image
```

---

## 9. Working on this

### Building

The installer targets are defined in every configuration, with no opt-in
option, so `update_translations` and `scripts/check-translations.ps1` see the
same set of source files for every contributor. Only the two heavy packaging
targets are outside `all`:

```powershell
cmake --build build-release --target dish_setup_exe   # the whole chain
```

`dish_setup_exe` depends on `dish_setup_image` (which runs
`cmake/DishSetupImage.cmake` to stage the install image with `windeployqt` and
the five CRT DLLs) plus `DishSetupStub` and `DishPayloadPack`. The result is
`dish-setup.exe` in the top level of the build directory, beside `dish.exe`, so
the wizard can be run straight out of the build tree against the Qt runtime
already deployed there.

The image staging script needs `VCToolsRedistDir`, which `vcvars64.bat` sets.
It hard-errors when that variable is missing rather than shipping an image that
cannot start on a clean machine.

### The round trip

```powershell
./scripts/test-installer-roundtrip.ps1 -Setup build-release/dish-setup.exe
```

That is the same script both workflows run. It exercises a fresh install into a
path with a space, repair idempotence, an upgrade via
`dish-payload-pack --version-override`, a blocked uninstall, the whole
`--update-apply` leg including the tampered-payload and version-mismatch
failures, uninstall with the helper-tail poll, `--purge-user-data`, and
`--extract-only` diffed against the manifest. The machine-scope leg only runs
in an elevated shell; `-SkipMachineScope` and `-SkipUserDataPurge` opt out of
the two legs that touch shared state.

### Strings

The installer and the updater use the six catalogues in `translations/`. The
engine, the stub and the helper contain **zero** translatable strings: they emit
typed enums and QML renders them. The stub's three status lines are the one
documented exception, as a six-locale wide-string table in `StubStrings.h`,
because it must run before Qt exists.

The developer loop is the app's loop:

```powershell
# 1. add qsTr(...) in QML, under src/**
cmake --build build-debug --target update_translations
# 2. fill the new entries (Qt Linguist, or by hand)
git add translations/
./scripts/check-translations.ps1
ctest --test-dir build-debug -R translations
```

Counted strings use `%n`, never a hand-written pair. The design has exactly one
of them (the running-app face's counted line), and it ships both English forms
and all three Bosnian forms. Everything else that counts uses `file %1 of %2`, and
every size goes through `QLocale::formattedDataSize`.

---

## 10. Manual test matrix

The automated gates cover the engine and the silent paths. These are the checks
that need a human, and they belong in the pre-release checklist.

| Check | What good looks like |
|---|---|
| SmartScreen walkthrough | More info, Run anyway, install completes |
| UAC declined on machine scope | The "Windows didn't approve" face offers Try again / just-me / Cancel, nothing written |
| Cancel mid-copy | Rollback completes, a prior install is still launchable |
| Power-loss recovery | Kill the process mid-copy, relaunch, accept the cleanup prompt |
| Non-ASCII account name | Install and uninstall both clean under a Cyrillic or CJK profile |
| Per-monitor DPI | 100 / 125 / 150 / 200 percent, including a mixed-DPI multi-monitor drag |
| Themes | Dark and light screenshots of every face |
| Languages | All six on the Options face, switched with the language selector |
| HDD cold start | The stub marquee covers extraction; no unresponsive window |
| Update pill | idle, available, downloading, ready, in both themes |
| Restart now during a transfer | The keep-awake confirm wins; cancelling it cancels the restart |
| Portable copy | Notify-only updater, releases-page link, no download button |
| Two-version end to end | Old build stages the new release, restart applies it, the "Updated to Dish X" toast appears once |
