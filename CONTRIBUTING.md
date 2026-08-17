# Contributing to Dish Windows

This document covers the conventions that are not obvious from reading the
code: the style gates, the translation gate, the hot-path rules, and what CI
enforces.

The [Code of Conduct](CODE_OF_CONDUCT.md) applies in every project space:
issues, pull requests, and review threads. Reports go to
`security@tinkernorth.com`, as that file says.

## Licensing of contributions

Dish is LGPL-3.0-or-later end-to-end (`LICENSE`, `COPYING.GPL3`, source
headers). By opening a pull request you agree that your contribution ships
under that license.

There is no CLA and no DCO sign-off requirement. `git commit -s` is welcome
but not checked.

## Getting set up

You need Visual Studio 2022 Build Tools (C++ workload), CMake, Ninja, LLVM
(clang-format + clang-tidy), Qt 6.7 with the Linguist tools, and vcpkg.
`install-dependencies.bat` installs all of it; the "Build from source" section
of [`README.md`](README.md) lists the same set for the manual route.

```powershell
scripts\build.ps1 debug test   # configure build-debug, build, run ctest
scripts\setup-hooks.ps1        # point git at the in-tree pre-commit hook
```

`scripts\build.ps1` imports the MSVC environment itself, so it works from an
ordinary PowerShell window. It writes to `build-debug\` or `build-release\`
depending on the first argument, and `CMAKE_EXPORT_COMPILE_COMMANDS` is forced
on in `CMakeLists.txt`, so `build-debug\compile_commands.json` exists after the
first debug build.

### The long-form invocation

`build.ps1` is a wrapper. If you need flags it does not pass, run CMake
yourself. You must be in a shell that has the MSVC environment, which means
either a Developer PowerShell or importing `vcvars64.bat` the way the script
does:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
# -products * is load-bearing. Without it vswhere reports only Community,
# Professional and Enterprise, and a machine with just the standalone Build
# Tools looks like it has no compiler at all.
$vs = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" |
    ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { Set-Item "Env:$($Matches[1])" $Matches[2] } }

cmake -S . -B build-debug -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug `
    -DDISH_BUILD_TESTS=ON `
    -DDISH_REQUIRE_TRANSLATIONS=ON `
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure --parallel
```

That is what CI runs, modulo the build directory name. `build.ps1` leaves
`DISH_REQUIRE_TRANSLATIONS` off, which downgrades a missing Qt Linguist
installation from a configure error to a warning; everything else is the same.
`CMAKE_PREFIX_PATH` must point at the Qt prefix (`C:\Qt\6.7.3\msvc2019_64` or
similar) and `VCPKG_ROOT` at the vcpkg checkout. `install-dependencies.bat`
persists both.

The pre-commit hook (`.githooks/pre-commit`) runs `clang-format -i` on staged
C++ files and re-stages them, then runs `clang-tidy -p build-debug` in advisory
mode. It skips whichever tool is missing rather than failing. It runs under Git
for Windows' bundled bash; no WSL required.

## License headers

Every source file (`*.h`, `*.hpp`, `*.cpp`) starts with:

```cpp
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
```

New files must include both lines. Do not introduce code under a different
license.

## Style

- C++17, four-space indent, 100-column limit. `.clang-format` is
  authoritative; run `clang-format -i` if you are unsure. CI pins
  clang-format 22.1.4, and older versions produce small diffs on braced-init
  lists, so match the pin if you can.
- Warnings are errors on first-party targets. `dish_warnings` carries the lint
  set (MSVC `/W4 /permissive-`, clang-cl and MinGW get the `-Wall -Wextra
  -Wpedantic -Wshadow ...` set); `dish_strict` adds `/WX` or `-Werror` and is
  applied only to `dish_core` and `Dish` so Catch2's headers stay buildable.
  See `CMakeLists.txt` for the exact flags.
- Match the surrounding style. Include order is project, Qt, third-party, std,
  separated by blank lines.
- Comments state non-obvious constraints: why a lock order matters, what a
  magic value encodes. They do not narrate the next line.

### QML and design tokens

The UI is Qt Quick only. Two documents bind it:

- [`DESIGN.md`](DESIGN.md) is the palette and design-token reference.
- [`docs/QML_UI_KIT.md`](docs/QML_UI_KIT.md) is the component inventory and the
  library rules.

A QML file outside `src/qml/kit/**` may not write a raw colour, a bare numeric
`radius`, `font.pixelSize`, spacing, or `font.family`. Use a `Theme` or
`Tokens` name. `scripts\qml-lint-literals.ps1` enforces this and runs in CI:

```powershell
.\scripts\qml-lint-literals.ps1 -Mode error   # what CI runs
.\scripts\qml-lint-literals.ps1 -Mode warn    # report everything, fail nothing
```

`src/qml/wizard/**` and `src/qml/shared/**` are errors. Everything else outside
the kit warns, because those files predate the token surface.

Every new `.qml` file must be listed in `qt_add_qml_module(... QML_FILES ...)`
in `CMakeLists.txt` or it will not exist at runtime.

## Translations

Six catalogues live under `translations/`: English plus Bosnian, German,
Spanish, French, and Brazilian Portuguese, the same set dish-android ships. The
installer wizard shares them; `qt_add_translations` scans the app and the
installer targets together, and the installer targets are defined in every
configuration precisely so `update_translations` produces the same output for
every contributor. The installer engine, the SFX stub and the uninstall helper
carry no translatable strings at all: they emit typed enums and the QML renders
them.

**If you add, change, or delete a user-facing string, refresh the catalogues in
the same commit.** `scripts\check-translations.ps1` re-runs `lupdate` with the
same options `CMakeLists.txt` passes (`-locations none -no-obsolete`) and fails
if the tracked catalogues move. It runs in CI on every PR.

```powershell
cmake --build build-debug --target update_translations
git add translations\
```

`update_translations` comes from `qt_add_translations()` and exists in any
configured build directory that found the Qt Linguist tools. Run the check
locally the same way CI does:

```powershell
.\scripts\check-translations.ps1
```

It needs `lupdate` on `PATH`, or `QT_ROOT_DIR` / `CMAKE_PREFIX_PATH` set, and
it refuses to run if `translations/` already has uncommitted changes, because
it cannot otherwise tell your edits from `lupdate`'s.

You are not expected to translate what you extracted. An untranslated entry
falls back to English, which is what would have happened anyway; the gate
exists so the entry is visible instead of missing. Coverage is printed but
never enforced.

Three rules the unit tests enforce:

- **Counts use `%n`, never a hand-written singular/plural pair.**
  `qsTr("%n slots free", "", count)` in QML, `tr("%n slots free", "", count)`
  in C++. A pair expresses two forms and Bosnian needs three (1 / 2-4 / 5+).
  Where two counts share one line, give each its own `%n` message and join
  them; Qt substitutes one count per message.
- **English is a catalogue, not the fallback.** A new `%n` string needs its
  singular and plural written into `translations/dish_en.ts`, because the
  source text carries only one of them and the other is what ships at `n == 1`.
- **Placeholders are load-bearing.** A translation must contain exactly the
  `%1` / `%2` / `%n` markers its source does. `tests/test_translations.cpp`
  checks every message in every catalogue, against the compiled `.qm` files.

Vocabulary follows dish-android, which has the older and more reviewed
catalogues. When a string already exists there, take its wording rather than
inventing a second one for the same idea. `scripts/seed-from-android.py` copies
across every string the two apps share verbatim, given a local checkout of
[dish-android](https://github.com/TinkerNorth/dish-android):

```powershell
python scripts\seed-from-android.py --android ..\dish-android --dry-run
python scripts\seed-from-android.py --android ..\dish-android
```

Platform idiom is the documented exception: Android says "Tap", Windows says
"Click", and neither should be copied blindly into the other.

## Branching and pull requests

All changes land on `main` through a pull request. Do not push to `main`
directly.

A pull request is ready for review when:

- CI is green. Every job listed below is blocking.
- The PR template (`.github/pull_request_template.md`) is filled in, including
  the manual test matrix you actually ran.
- Commits are focused and fixup noise is squashed.
- Anything touching the wire protocol, the crypto, or the hot path says so
  explicitly in the description.
- Behaviour changes come with tests. The Catch2 suite under `tests/` is the
  place; most of `src/core/` and `src/composer/` is deliberately Qt-free so it
  is host-testable without a window.

This is a small project with no staffed review rotation. Expect a first
response in days rather than hours, and bump a pull request that has gone quiet
for a week.

If you are looking for somewhere to start, issues labelled `good first issue`
are the ones scoped for a first contribution. When none are open, the roadmap
at the end of this file is the standing backlog.

## What CI runs

`windows-ci.yml`, on every pull request and every push to `main`, in order:

1. `clang-format --dry-run --Werror` over `src/` and `tests/`.
2. Debug configure with `-DDISH_BUILD_TESTS=ON -DDISH_REQUIRE_TRANSLATIONS=ON`,
   build, and `ctest --output-on-failure --parallel`.
3. `qmllint` over every tracked `src/qml/**/*.qml`. Every category gates except
   `unqualified`, which is downgraded to info because `App` is a runtime
   context property the linter cannot see (see `docs/QML_CONTRACT.md`).
4. `scripts/qml-lint-literals.ps1 -Mode error`.
5. `scripts/check-translations.ps1`.
6. `clang-tidy -p build` over `src/**/*.cpp` excluding `src/UI/`, against the
   same Debug tree step 2 produced.
7. Release configure and build, `dish_setup_exe`, `windeployqt` staging,
   `scripts/test-installer-roundtrip.ps1` against the freshly packed
   installer, and artifact upload.

`version-consistency.yml` runs only when a version-carrying file moves
(`CMakeLists.txt`, `packaging/dish.rc`, `vcpkg.json`, or the workflow pins): it
fails the pull request when the three version declarations disagree, or when
the vcpkg baseline in `vcpkg.json` stops matching the `VCPKG_COMMITID` the
build workflows pin and the baseline `THIRD_PARTY.md` documents. Blocking, like
everything above.

Security gates:

- `security.yml`, which calls the reusable `_security.yml`: action-pin lint
  (every `uses:` must be a 40-char SHA), `.security/allowlist.yaml` expiry,
  OSV-Scanner over the worktree, and a gitleaks secret scan. All blocking. Also
  runs weekly on a schedule.
- `dependency-review-action`, in the same workflow, on pull requests only. It
  carries `continue-on-error: true` and does not block, because the action needs
  GitHub Advanced Security, which the repository did not have while it was
  private.
- `codeql.yml`: CodeQL `cpp` analysis with the `security-extended` and
  `security-and-quality` query packs, on a Windows runner so MSVC-only
  constructs are covered. Blocking.

`clang-tidy` is advisory. `.clang-tidy` sets `WarningsAsErrors: ''` on purpose,
and the CI step does not fail on findings. Everything else in the list fails
the build.

Reproduce the build and test steps locally with `scripts\build.ps1 debug test`.

## Security

Vulnerability reports go through [`SECURITY.md`](SECURITY.md), not a public
issue.

### Adding a vulnerability allowlist entry

Open a PR that adds an entry to [`.security/allowlist.yaml`](.security/allowlist.yaml)
(the schema is in the file's header comment). Required fields: `cve`, `reason`,
`owner`, `expires`. CI rejects the PR if any field is missing or empty, or if
`expires` is in the past. Renew or remove on or before `expires`; there is no
silent suppression.

### Running the security checks locally

```powershell
# Action-pin lint: every `uses:` line must carry a 40-char SHA
$bad = git ls-files .github/workflows/ |
    ForEach-Object { Select-String -Path $_ -Pattern '^\s*uses:' } |
    Where-Object { $_.Line -notmatch '@[0-9a-f]{40}\b' }
if ($bad) { $bad; throw "unpinned actions" } else { "all pinned" }

# Allowlist expiry
python -c @"
import datetime, yaml, sys
data = yaml.safe_load(open('.security/allowlist.yaml').read()) or {}
for e in data.get('exceptions', []) or []:
    if datetime.date.fromisoformat(str(e['expires'])) < datetime.date.today():
        print('EXPIRED:', e); sys.exit(1)
"@

# OSV-Scanner (scoop install osv-scanner, or download from releases)
osv-scanner --recursive --skip-git .

# Gitleaks (scoop install gitleaks, or download from releases)
gitleaks detect --no-banner --redact --source .
```

### Checking a release artifact

A GitHub Release carries `dish-setup.exe` (the installer), `dish-windows.zip`
(the exe plus the bundled Qt runtime, with its own `SHA256SUMS` inside the zip),
`latest.json` (the update manifest), `SHA256SUMS` over those assets, and
`dish-windows.spdx.json` (an SPDX SBOM). Nothing is signed yet.

```powershell
$expected = (Get-Content .\SHA256SUMS |
    Where-Object { $_ -match 'dish-setup\.exe' }).Split()[0]
$actual = (Get-FileHash .\dish-setup.exe -Algorithm SHA256).Hash.ToLower()
if ($expected -ne $actual) { throw "checksum mismatch" } else { "ok" }
```

Neither `SHA256SUMS` is signed, so they detect a corrupted download and nothing
more. [`SECURITY.md`](SECURITY.md) says what that does and does not buy you,
and describes the update chain's own integrity guarantees.

## Cutting a release

`release.yml` runs on a `v*` tag or a `workflow_dispatch` with a tag input. It
re-runs the security gates against the tagged commit, builds Release, packs
`dish-setup.exe`, runs the installer round trip against the artifact it is about
to publish, emits `latest.json`, computes checksums, uploads everything as a
**draft**, and only then flips the release to published. The draft step is
load-bearing: GitHub never points `releases/latest` at a draft, so every client
polling the update permalink sees the previous release until the flip makes the
new one visible atomically.

Before you tag:

- **Bump the version in all three places.** `project(Dish VERSION ...)` in
  `CMakeLists.txt` is the source; `packaging/dish.rc` and `vcpkg.json` are
  hand-mirrored. `version-consistency.yml` fails any pull request that moves
  one of the three without the others, and `release.yml` fails the tag build
  when the tag, the CMake version, or `dish.exe`'s own `ProductVersion`
  disagree, so a forgotten `dish.rc` is a red workflow rather than a client
  that re-applies the same update forever.
- **Review `packaging/update-policy.json`.** Its `minimumSupportedVersion` is a
  reviewed release input, not a generated value: it goes into `latest.json`, and
  every client older than it treats the update as required and ignores a skip.
  Raise it only for a security fix or a protocol break, in its own commit, and
  never above the version being released.
- **Update `CHANGELOG.md`**, and `PRIVACY.md` first if anything about data
  collection changed.

After the workflow finishes:

- **Confirm the permalink resolves.**
  `https://github.com/TinkerNorth/dish-windows/releases/latest/download/latest.json`
  must return the manifest for the release you just published. A 404 there
  means installed copies stop seeing updates; clients treat it as a transient
  failure and back off, so it self-heals once the asset appears, but it is
  invisible from the release page.
- **Download `dish-setup.exe` and install it by hand** at least once. The manual
  matrix in [`docs/INSTALLER.md`](docs/INSTALLER.md) section 10 is the list.

The asset names `dish-setup.exe`, `dish-windows.zip` and `latest.json` are a
permanent API. Shipped clients construct the manifest URL from them and validate
the download URL against the release-download prefix. Renaming one breaks every
installed copy in the field, silently, and there is no server-side way to fix
it.

## Touching the hot path

The SDL gamepad thread runs at controller polling rate and must never block on
the UI thread or allocate. If you are modifying `SDLGamepadBridge`,
`GamepadInputProcessor`, or `SatelliteClient::sendReport`:

- No `QObject::connect` cross-thread signals on the send path.
- No lock held longer than the existing key/token/counter snapshot.
- No allocation per packet. Use the preallocated buffer.
- Preserve `IP_TOS = 0xB8` (DSCP EF) on every send. Windows strips it silently
  for non-administrator processes, but the call stays so a machine with the
  `DisableUserTOSSetting=0` registry override honours it.
- Winsock has no `MSG_NOSIGNAL` and does not need one. There is no SIGPIPE on
  Windows.

## Touching the wire protocol

The Windows, Linux, macOS, and Android clients all talk to the same `satellite`
server and must produce byte-identical traffic. Protocol 1:

- Transport split: discovery is a UDP broadcast beacon on 9879 plus an mDNS
  browse; pairing and the REST control plane are HTTPS on 9443; the data plane
  is UDP on 9876.
- The satellite presents a self-signed TLS certificate. Trust is
  trust-on-first-use, pinned on the SHA-256 of the certificate
  (`src/core/net/Tofu.*`, `src/source/http/SatelliteTlsVerifier.*`).
- Session key: `HKDF-SHA256(ikm = pairingKey, salt = sessionSalt,
  info = "satellite-session-v1" || token(4 BE))`. The pairing key itself never
  encrypts a packet.
- AEAD: ChaCha20-Poly1305 IETF. Nonce is `direction(1) | 0x00 x7 |
  counter(4 BE)`; AAD is `token(4 BE)`.
- Packet layout: `token(4) | counter(4 BE) | ciphertext+tag`.
- XUSB report: 12 bytes, little-endian.

The authoritative opcode catalogue is
[`satellite/docs/contract.md`](https://github.com/TinkerNorth/satellite/blob/main/docs/contract.md);
the client-side subset this repo needs is `src/core/model/Protocol.h`. Any
change here must be coordinated with `dish-android`, `dish-mac`, `dish-linux`,
and `satellite` in the same release cycle.

## Reporting bugs

Use the issue templates under `.github/ISSUE_TEMPLATE/`. Include:

- The Windows build number (`winver`, or
  `[System.Environment]::OSVersion.Version`).
- The dependency versions you built against. Qt's is whatever
  `CMAKE_PREFIX_PATH` resolves to (`scripts\build.ps1` echoes the prefix it
  picked); libsodium and SDL2 come from the vcpkg baseline pinned in
  `vcpkg.json`.
- `%LOCALAPPDATA%\Dish\crash.log` and the matching `.dmp`, if the app crashed.
  The crash handler writes both.

## Known gaps and roadmap

None of these are started. They are the standing backlog, and each is a
self-contained piece of work.

- **winget manifest.** Nothing is published to `microsoft/winget-pkgs`.
  `dish-setup.exe` already has the silent switches a manifest needs
  (`/S`, `/D=<dir>`, and a `QuietUninstallString` in the registry), so this is
  packaging work rather than installer work.
- **qWAVE DSCP marking.** The hot-path socket sets `IP_TOS` directly, which
  Windows drops for non-administrator processes. `QOSCreateHandle` from
  `qWAVE.dll` is the supported path and is not wired up.
- **Soak testing.** No long-run test exists. The intended shape is an 8-hour
  streaming session watching `drainTelemetry` for drops and confirming the SDL
  thread never blocks on the UI thread under load.
- **Release signing and provenance.** Authenticode signing, cosign keyless
  signatures, SLSA build provenance, and a CycloneDX SBOM alongside the SPDX
  one are all absent. This is the highest-value item on the list now that
  `dish-setup.exe` ships: unsigned means SmartScreen warns on every manual
  download, and it means the update chain's only integrity anchor is the
  SHA-256 in `latest.json`. See the "Known gaps" and "Auto-update trust model"
  sections of [`SECURITY.md`](SECURITY.md) for what that means for anyone
  downloading a release.
- **Screenshots.** `README.md` describes the UI in prose. There are no
  screenshots of the dashboard, the connections page, or the setup wizard.
