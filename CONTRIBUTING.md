# Contributing to Dish for Windows

This document covers the conventions that are not obvious from reading the
code: the style gates, the translation gate, the hot-path rules, and what CI
enforces.

## Code of conduct

The [Code of Conduct](CODE_OF_CONDUCT.md) applies in every project space:
issues, pull requests, and review threads. Reports go to
`security@tinkernorth.com`, as that file says.

## Getting set up

You need Visual Studio 2022 Build Tools (C++ workload), CMake, Ninja 1.13.2,
Python 3.12, Qt 6.12.1 (which carries the Linguist tools the translation gate
runs), vcpkg, clang-format 23.1.5, clang-tidy 23.1.4 and Inno Setup 6.7.4. The
two LLVM tools are the PyPI wheels, picked out of PATH by version: Visual
Studio's own clang-format and clang-tidy (19.x) sit ahead of them once vcvars
has run.
`scripts\install-deps.ps1` installs all of it, at the versions CI uses: every
pin lives in `scripts/tools.ps1`, and vcpkg is checked out at the
`builtin-baseline` of `vcpkg.json`, the commit CI checks out. Qt comes from
aqtinstall at commit `4c759852b1108cc801d8e24979136b895b5155d4`, both in CI and
locally: the newest aqtinstall release (3.3.0) cannot read Qt 6.12's online
repository, and the support (miurahr/aqtinstall#1048) is not released yet. Move
`$DishAqtSource` back to a PyPI version once a release carries it.

```powershell
scripts\build.ps1 debug test   # configure the debug preset (build\), build, run ctest
scripts\setup-hooks.ps1        # point git at the in-tree pre-commit hook
scripts\ci-local.ps1           # every windows-ci.yml and version-consistency.yml gate, in CI's order
```

`scripts\build.ps1` imports the MSVC environment itself, so it works from an
ordinary PowerShell window. It drives the presets in `CMakePresets.json`,
which are the single source of configure truth for the workflows too: `debug`
writes to `build\` (CI's tree) and `release` to `build-release\`, and
`build\compile_commands.json` exists after the first debug build.

A tree configured before CMake 3.25 became the floor still carries `/Zi` in
its cached Debug flags (the debug-information format is a target property
now, CMP0141), and the configure refuses it rather than build with D9025 on
every `dish_core` and `Dish` unit. Delete that tree's `CMakeCache.txt` and
configure again; once per tree.

`scripts\ci-local.ps1` runs the version gate, the action-pin lint, the format
gate, the debug build and tests, the measurements report's tests, qmllint, the
QML literal scanner, the translation gate, clang-tidy, the release build and
the portable-bundle and install-image smoke tests, through the same scripts CI
calls and in CI's order. Its flags:

- `-Quick` stops after the translation gate (no clang-tidy, no release build,
  no bundles).
- `-WithInstaller` adds the pinned Inno Setup, the installer compile and the
  silent install/uninstall round-trip (opt-in, because it really installs Dish
  on your machine).
- `-AllowMissing` turns a missing tool into a notice; without it a missing
  tool fails the run, so a green run never hides a skipped gate.

Its header lists what only CI runs (CodeQL, OSV-Scanner, gitleaks, the
allowlist expiry check, the input-path bench) and why.

### The long-form invocation

`build.ps1` is a wrapper over `cmake --preset`. If you need flags a preset
does not pass, run CMake yourself. You must be in a shell that has the MSVC
environment, which means either a Developer PowerShell or importing
`vcvars64.bat` the way the scripts and the workflows do (`Import-MsvcEnvironment`
in `scripts/tools.ps1`, which `scripts/ci-env.ps1` hands to every CI job):

```powershell
. .\scripts\tools.ps1
Import-MsvcEnvironment

cmake --preset debug           # build\: Debug, tests, translations required
cmake --build --preset debug --parallel
ctest --preset debug --parallel
```

That is exactly what CI runs. The presets set `DISH_REQUIRE_TRANSLATIONS=ON`
(the point of a preset is local == CI), so a box without the Qt Linguist
tools fails configure the way CI would; `cmake -S . -B <dir>` without the
preset keeps CMakeLists' friendlier OFF default if you need a build while Qt
Linguist is missing. `CMAKE_PREFIX_PATH` must point at the Qt prefix
(`C:\Qt\6.12.1\msvc2022_64`, where `scripts\install-deps.ps1` puts it) and
`VCPKG_ROOT` at the vcpkg checkout. `scripts\install-deps.ps1` persists both.

The pre-commit hook (`.githooks/pre-commit`) runs `clang-format -i` on staged
C++ files and re-stages them, then runs CI's clang-tidy gate
(`scripts/check-tidy.ps1`) over the staged sources in its file set, so a
finding stops the commit before it would stop the pull request. It skips
whichever tool is missing rather than failing. It runs under Git for Windows'
bundled bash; no WSL required.

## Where code goes

[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) is the map: "The folder map"
says which folder a piece of code belongs in, "The kernel primitives" which
primitive to reach for, and "How to add a feature" the order to add things
in. The rule that catches people out is the first one there: `src/core/` is
independent of the UI, so most of it is tested without a window.

## Touching the UI

The UI is Qt Quick only. Four documents bind it:

- [`DESIGN.md`](DESIGN.md) is the palette and design-token reference.
- [`docs/QML_UI_KIT.md`](docs/QML_UI_KIT.md) is the component inventory and the
  library rules.
- [`docs/QML_CONTRACT.md`](docs/QML_CONTRACT.md) lists everything the QML tree
  may read or call on the `App` singleton; a new property or invokable is added
  there in the same change.
- [`docs/QML_MANUAL_SMOKE_CHECKLIST.md`](docs/QML_MANUAL_SMOKE_CHECKLIST.md) is
  the hand pass for what CI cannot see (a real window, a real pad, a real
  satellite); run the parts your change touches.

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
Spanish, French, and Brazilian Portuguese, the same set dish-android ships.
They cover the app only; the installer's wizard text comes from Inno Setup's
own translated `.isl` catalogues (five of the six; Bosnian has no official
`.isl` yet and is an open follow-up).

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

You are expected to translate what you extracted. Coverage is **enforced**:
the gate fails when any catalogue has an unfinished entry, because a missing
translation is not a blank: it is English shown to someone who does not read
English, and nothing else in the suite can see that.

`dish_en.ts` is the exception that proves the rule. Its non-plural entries are
filled from their own source text by `scripts/seed-source-language.py`, which
the gate runs for you, so the count means the same thing in all six catalogues.
The only English entries a person writes are the plural forms, which a source
string cannot carry.

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

## License headers

Every source file (`*.h`, `*.hpp`, `*.cpp`) starts with:

```cpp
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
```

New files must include both lines. Do not introduce code under a different
license.

### Licensing of contributions

Dish is LGPL-3.0-or-later end-to-end (`LICENSE`, `COPYING.GPL3`, source
headers). By opening a pull request you agree that your contribution ships
under that license.

There is no CLA and no DCO sign-off requirement. `git commit -s` is welcome
but not checked.

## Style

- C++20, four-space indent, 100-column limit. `.clang-format` is
  authoritative; run `clang-format -i` if you are unsure. CI pins
  clang-format 23.1.5, and other versions produce small diffs on braced-init
  lists, so match the pin if you can.
- Warnings are errors on first-party targets. `dish_warnings` carries the lint
  set (MSVC `/W4 /permissive-`, clang-cl and MinGW get the `-Wall -Wextra
  -Wpedantic -Wshadow ...` set); `dish_strict` adds `/WX` or `-Werror` and is
  applied only to `dish_core` and `Dish` so Catch2's headers stay buildable.
  See `CMakeLists.txt` for the exact flags.
- Bitwise operators take unsigned operands only (clang-tidy's
  `bugprone-signed-bitwise`). A `std::uint8_t` or `std::uint16_t` is promoted
  to `int` before an operator sees it, so bytes widen through `bits::u32()`
  first, shift counts and masks are unsigned literals (`8U`, `0xFFU`), a
  narrower result is taken explicitly (`bits::lowByte`, `bits::low16`), and a
  signed value is made last by a named conversion (`bits::asInt16`). The
  helpers, and the multi-byte loads and stores built on them, are in
  `src/core/Bits.h`:

  ```cpp
  const std::int16_t gyroX = bits::asInt16(bits::loadLe16(buf + 13));
  const std::uint32_t pressed = bits::u32(buf[5]) & 0xF0U;
  ```

- Match the surrounding style. Include order is project, Qt, third-party, std,
  separated by blank lines.
- Comments state non-obvious constraints: why a lock order matters, what a
  magic value encodes. They do not narrate the next line.

### Shape of the code

These rules are enforced in review, not by a gate. They come from the
Parchment library and are adapted where C++ or Qt make the literal form worse
than the thing it is meant to achieve. They apply to `src/` and `tests/`
alike.

- **As immutable and as static as possible.** `const` on every local and
  parameter that is not reassigned, `constexpr` for a value known at compile
  time, and file-static (or an anonymous namespace) for anything the rest of
  the translation unit does not need. A value that never changes is a named
  constant, never a literal in the middle of a function. A type that holds no
  state is a set of free functions, not a class.

- **Split values into simple, named steps.** One operation per line, with the
  result in a named `const` that says what it is, even when that reads longer:

  ```cpp
  const bool isAnotherSlot = entry.first != deviceId;
  const bool isAPlaceholder = other.transitioning || other.needsReplug;
  const bool isTheSameModel = other.vendorId == device.vendorId &&
                              other.productId == device.productId;
  return isAnotherSlot && isAPlaceholder && isTheSameModel;
  ```

  not one five-term boolean. The names are the documentation, the debugger can
  show each value, and a test can pin each step. A named `const bool` costs
  nothing at runtime.

- **One function, one flow.** When a function would hold two algorithms chosen
  by a condition, the condition dispatches to two named things that each do
  one thing, and the dispatcher does nothing else. A `switch` over an enum
  with no `default` is the preferred form, because the compiler then checks
  that every case is handled. A guard clause is not an algorithm: do not
  invent indirection where there is only one flow.

  A function that stays long because splitting it would make it worse says so
  at the top, in one or two lines. `reduceSession` and `decodeServerEvent` are
  the examples: one is an exhaustive protocol table the compiler checks, the
  other is a byte-offset decoder whose arms are its documentation. A long
  function with no such note is one nobody has looked at.

- **A chain of `if`s over one byte is a table.** A per-bit or per-index
  mapping belongs in a `constexpr` array the code reads, not in a switch the
  reader has to diff against its twin. The point is that two mappings of the
  same thing cannot drift apart.

- **A callback with a body gets a name.** A lambda is fine as a one-expression
  forward, and fine as an argument to an algorithm that consumes it
  immediately (`std::sort`, `std::find_if`, `std::visit`). A lambda that
  carries an algorithm becomes a named function, so it can be found, read and
  tested on its own. A callback that is *stored* rather than called
  immediately -- a `QObject::connect` slot, a thread body, a
  `std::function` member -- prefers a named member function and a pointer to
  it:

  ```cpp
  QObject::connect(pingTimer_, &QTimer::timeout, this, &MoonlightSession::onPingTick);
  ```

  This is Parchment's "no anonymous methods" narrowed to what C++ can
  express.

- **No singletons.** A stateless helper is a free function in the file that
  owns it. There is no `instance()` and no `Q_GLOBAL_STATIC` in this
  codebase, and there is no reason to add one: every collaborator is
  constructed by `AppModel` and handed to whoever needs it, which is also
  what makes it replaceable in a test. The mutable statics that remain are
  process-wide by definition, and each is there because something outside
  this code demands it:

  - `main.cpp`'s `QTranslator`, which `QCoreApplication` holds by pointer;
  - the hand-off pointer in `src/qml/chrome/ForeignTypes.h`, because a
    `QML_SINGLETON`'s factory is a static function Qt calls, and it can only
    return what was published to it;
  - the active palette in `src/UI/Theme.cpp` (the `Theme` colour tokens and
    the appearance they came from), because the `Theme` QML singleton that
    reads it is constructed by the engine and cannot be handed it;
  - the crash handler's state in `src/UI/CrashHandler.cpp`, because the
    exception filter Windows calls takes no context and must not allocate;
  - the ENet reference count in `src/Network/MoonlightControlChannel.cpp`,
    because `enet_initialize` is process-wide.

  A function-local `static` of any other kind is a singleton with the
  constructor hidden, and does not belong here.

- **Member naming.** Members carry a TRAILING underscore (`host_`, `probes_`,
  `mtx_`): that is what almost every file in `src/` uses, and it is the same
  "state, not scratch" signal Parchment's `m` prefix gives at the point of
  use. Two files under `src/qml/chrome/` use an `m_` prefix instead; they were
  written against a Qt sample and are the exception, not the pattern. Follow
  the file you are in and do not mix the two within one class.

- **Prefer a test to a comment.** Behaviour that needs explaining gets a test
  named for the behaviour. A comment is the last resort for a constraint that
  genuinely cannot be tested -- a platform quirk, a wire-format byte layout, a
  lock order, a declaration-order dependency -- states why in one or two
  lines, and never narrates what the next line does.

  Exempt, because the constraint is untestable by construction: the pin-map
  headers in `.github/workflows/`, the usage headers in `scripts/`, and the
  vcpkg and installer manifests, where a comment explains what a version pin
  is holding back.

## Tests

### Test-driven, every flow

The Catch2 suite under `tests/` is not a coverage exercise. It is how a flow
is known to work at all.

- **Red first.** Write the failing test, watch it fail for the reason you
  expect, then write the code. A test that has never failed has not been
  shown to test anything.
- **Cover every flow.** Each branch a function can take gets a case named for
  the behaviour it pins, not for the function it calls. `SECTION` is the right
  tool when the cases share a fixture; a separate `TEST_CASE` is right when
  they do not.
- **Assume nothing.** Where behaviour depends on a platform, a library
  version or the wire, prove it with a probe before writing the code that
  assumes it, and name the probe's finding in the test.
- **Test where the behaviour lives.** Most of `src/core/` and
  `src/composer/` is deliberately Qt-free so it is host-testable without a
  window. Keep it that way: a reducer that needs a `QGuiApplication` to be
  tested is a reducer with a dependency it should not have.
- **Tests follow the same shape rules.** A fixture is a named type, not a
  lambda that builds one; a helper with a body gets a name; a test that is
  longer than the thing it tests is usually two tests.

## Branching & PRs

All changes land on `main` through a pull request. Do not push to `main`
directly.

A pull request is ready for review when:

- CI is green. Every job listed below is blocking.
- Commits and PR descriptions name no tool. No `Co-Authored-By` trailer for an
  assistant, no "Generated with …" footer: the author of record is the person
  who opened the PR, whatever helped write it.
- The PR template (`.github/pull_request_template.md`) is filled in, including
  the manual test matrix you actually ran.
- Commits are focused and fixup noise is squashed.
- Anything touching the wire protocol, the crypto, or the hot path says so
  explicitly in the description.
- Behaviour changes come with tests. The Catch2 suite under `tests/` is the
  place; most of `src/core/` and `src/composer/` is deliberately Qt-free so it
  is host-testable without a window.
  [`Test-driven, every flow`](#test-driven-every-flow) under Tests says what
  "with tests" means here.

This is a small project with no staffed review rotation. Expect a first
response in days rather than hours, and bump a pull request that has gone quiet
for a week.

If you are looking for somewhere to start, issues labelled `good first issue`
are the ones scoped for a first contribution. When none are open, the roadmap
at the end of this file is the standing backlog.

## What CI runs

Every workflow runs on pull requests to `main` and pushes to `main`, and every
job below is blocking. The required checks are `Build, test, lint, format`,
`Analyze (cpp)` and the four `shared / ...` jobs.

`windows-ci.yml`, job `Build, test, lint, format`, in order (each step is a
shared script or preset, so `scripts/ci-local.ps1` runs the same gates from the
same sources):

1. `scripts/ci-env.ps1`: the MSVC environment (vswhere + vcvars64) and the
   pins from `scripts/tools.ps1` and `vcpkg.json`; then Qt 6.12.1 through
   `jurplel/install-qt-action`, vcpkg at the baseline, and clang-format
   23.1.5 and clang-tidy 23.1.4 through pipx; Ninja 1.13.2 comes from
   `ci-env.ps1` itself, first on PATH.
2. `scripts/check-format.ps1`: `clang-format --dry-run --Werror` over `src/`
   and `tests/`, refusing any clang-format but the pinned one.
3. The `debug` preset (`-DDISH_BUILD_TESTS=ON -DDISH_REQUIRE_TRANSLATIONS=ON`
   into `build/`), build, and `ctest --preset debug --parallel`, then the
   measurements report's tests (`python -m unittest discover -s tests -p
   'test_measurements_report.py'`).
4. `scripts/check-qml.ps1`: `qmllint` over every tracked `src/qml/**/*.qml`.
   Every category gates at its default level, nothing downgraded: `App`,
   `Theme` and `Tokens` are module singletons the generated `qmltypes`
   describes, so `unqualified` is a real error (see `docs/QML_CONTRACT.md`).
5. `scripts/qml-lint-literals.ps1 -Mode error`.
6. `scripts/check-translations.ps1`, with the build Qt's own lupdate.
7. `scripts/check-tidy.ps1`: `clang-tidy` over `src/**/*.cpp` excluding
   `src/UI/`, four wide, against the same Debug tree step 3 produced, with
   `--warnings-as-errors='*'` so a finding fails the step.
8. The `release` preset build (`Dish` + `dish_setup_image` into
   `build-release/`), `scripts/stage-bundle.ps1` (the same staging path
   `release.yml` ships), the portable-bundle smoke test, the same smoke test
   over the install image, `scripts/install-inno-setup.ps1` (Inno Setup 6.7.4,
   SHA-256 checked), `scripts/build-installer.ps1`,
   `scripts/test-installer-roundtrip.ps1` against the freshly compiled
   installer (which also starts the installed app in place and requires its
   controller database check over HTTPS), `scripts/package-bundle.ps1` and `scripts/write-sbom.ps1` (the
   zip and the components SBOM, made as `release.yml` makes them, and
   `scripts/check-sbom.ps1` over the SBOM), and artifact upload.
9. Job `Release hardening (dry run)`: the shared `_release_harden.yml` in
   dry-run mode over step 8's installer, zip and SBOM: Grype, the Syft SBOMs,
   `SHA256SUMS`, and a signature per file with a throwaway key. Keyless
   signing, the code-scanning upload and the SLSA provenance run only on a tag.

`version-consistency.yml`, job `Versions, pins and documented facts`, runs
`scripts/check-versions.ps1` when a version-carrying file moves. It fails the
pull request when the version in `CMakeLists.txt` and its mirrors
(`packaging/dish.rc`, `vcpkg.json`, the `CHANGELOG.md` heading) disagree, when
`THIRD_PARTY.md` or `assets/licenses/licenses.json` name a version other than
what the vcpkg baseline, the FetchContent tags or the Qt pin resolve, when a
tool pin in this file or the PR template drifts from `scripts/tools.ps1`, when
a workflow pins its own vcpkg commit or Qt version, or when the protocol number
here or in `docs/contract.md` differs from `src/core/model/Protocol.h`.

`measurements.yml`, job `Measure (MSVC, Release, windows-2022)`, runs on pull
requests that touch the input path (`src/`, `tools/bench/`, the lock-law test,
its report or `CMakeLists.txt`), by hand, and from `release.yml`. It builds
the input-path bench and its lock-statistics twin in the release
configuration, runs the lock laws and the benches, and fails when the SDL
input, USB reader, receive or mic capture thread takes a lock;
[`tools/bench/README.md`](tools/bench/README.md), "Release measurements", has
the rest.

Security gates:

- `security.yml`, which calls the reusable `_security.yml` (byte-identical to
  satellite's): action-pin lint (every `uses:` a 40-character SHA with its
  `# vX.Y.Z`), `.security/allowlist.yaml` expiry, OSV-Scanner 2.7.1 over the
  worktree and the libraries `scripts/osv-lockfile.sh` names, and a gitleaks
  scan of the full history. All blocking. Also runs weekly on a schedule.
- `dependency-review-action`, in the same workflow, on pull requests only:
  blocking on a high-severity finding in the dependencies a pull request
  changes.
- `codeql.yml`, job `Analyze (cpp)`: CodeQL `cpp` analysis with the
  `security-extended` query pack, on a Windows runner so MSVC-only constructs
  are covered. Blocking, and the results are uploaded to code scanning.

`.clang-tidy` sets `WarningsAsErrors: ''` on purpose: the file is the
fleet-canonical one, shared byte for byte with the other TinkerNorth repos.
The gate is on the command line instead (`--warnings-as-errors='*'`, the same
way `dish-linux` runs its sweep), so every step in the list fails the build.

Reproduce the build and test steps locally with `scripts\build.ps1 debug
test`, or the whole lane with `scripts\ci-local.ps1` (add `-WithInstaller`
for steps CI runs that install things on your machine).

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

`scripts\ci-local.ps1` runs them through the same scripts `_security.yml`
runs, with the same pinned scanners (fetched once into
`%LOCALAPPDATA%\Programs\dish-security-tools`, each checked against its
SHA-256): the pin lint `scripts/check-action-pins.sh` through Git for Windows'
bash, `python scripts/security-allowlist.py check`, OSV-Scanner 2.7.1 through
`scripts/osv-scan.sh --lockfile-script scripts/osv-lockfile.sh .`, and
`gitleaks git --config=.gitleaks.toml --redact --exit-code=1 .` (8.30.1). The
two Python scripts need PyYAML 6.0.4, which `scripts\install-deps.ps1`
installs.

`_security.yml`, `_release_harden.yml`, `scripts/check-action-pins.sh`,
`scripts/osv-scan.sh` and `scripts/security-allowlist.py` are byte-identical
copies of satellite's. They are synced with `cp`, never edited here; a change
starts in satellite.

### Verifying a release artifact

A GitHub Release carries `dish-setup.exe`, `dish-windows.zip`, `latest.json`,
`SHA256SUMS`, the components SBOM `dish-windows-components.spdx.json`, the
Syft SBOMs, one Sigstore bundle `<file>.sigstore.json` per file (releases up to
2.8.0: a `.sig` and a `.crt`), the SLSA provenance `dish-windows.intoto.jsonl`,
and the release measurements. [`SECURITY.md`](SECURITY.md), "Verifying a
release artifact", has the recipe (cosign against the harden workflow's
identity, the checksums, the provenance with `cosign verify-blob-attestation`)
and what each check is worth.

## Cutting a release

`release.yml` runs on a bare `MAJOR.MINOR.PATCH` tag (no `v` prefix, e.g.
`1.2.3`) or a `workflow_dispatch` with a tag input, run on the tag ref
(`gh workflow run release.yml --ref X.Y.Z -f tag=X.Y.Z`): the signing
certificate names the ref it ran on, and the harden job refuses anything but a
tag, because a dispatch from `main` would sign as `refs/heads/main` and break
every user's verification. It re-runs the security
gates against the tagged commit, builds Release through the same preset and
scripts as pull-request CI, uploads the debug symbols to Sentry, compiles
`dish-setup.exe` with the pinned Inno Setup, signs it when the certificate
secrets exist, runs the installer round trip against the artifact it is about
to publish, emits `latest.json`, hands everything to the harden job (Grype,
SBOMs, `SHA256SUMS`, cosign) and the SLSA provenance, uploads it all as a
**draft**, measures the input path of the tagged
commit and attaches `measurements-<version>.md` and `.json` to the draft, and
only then flips the release to published. A release whose measurements fail
their lock law gate stays a draft; "Re-run failed jobs" repeats only the
measurement. The draft step is
load-bearing: GitHub never points `releases/latest` at a draft, so every client
polling the update permalink sees the previous release until the flip makes the
new one visible atomically.

The fleet's order: the website (`dish-website`) is tagged first, and the
clients are tagged once
`https://dish.tinkernorth.com/controllers/v1/manifest.json` serves the new
controller database. Each client's version bump is a pull request of its own
("Bump the version to X.Y.Z and date the changelog"), and the tag goes on its
squash commit.

What the tag build reads, and what happens without it:

- `WIN_SIGN_CERT_BASE64` and `WIN_SIGN_PASSWORD` (secrets): the Authenticode
  certificate. Missing, the installer ships unsigned with a warning; the
  repository variable `ENFORCE_RELEASE_SIGNING=true` makes that a failure.
- `SENTRY_DSN` (secret): compiled into the release build. Missing, the build
  carries no DSN and sends no crash reports.
- `SENTRY_AUTH_TOKEN` (secret) and `SENTRY_ORG` / `SENTRY_PROJECT`
  (variables, which fall back to the ids inside the DSN): the symbol upload
  and the Sentry release. Missing, both are skipped with a notice.

These steps run only on a tag, so no pull request exercises them: the
Authenticode signing, the DSN-bearing configure, the symbol upload, the
harden job, the provenance, the draft upload, the flip to published, and the
Sentry release. Everything they consume (the release build, the staged
bundle, the install image, the compiled installer and its round trip) is built
the same way on every pull request.

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
- **Update `CHANGELOG.md`**: turn `## [Unreleased]` into
  `## [X.Y.Z] - YYYY-MM-DD` and add the compare link; `version-consistency.yml`
  fails a version with no heading. Update `PRIVACY.md` first if anything about
  data collection changed.

After the workflow finishes:

- **Confirm the permalink resolves.**
  `https://github.com/TinkerNorth/dish-windows/releases/latest/download/latest.json`
  must return the manifest for the release you just published. A 404 there
  means installed copies stop seeing updates; clients treat it as a transient
  failure and back off, so it self-heals once the asset appears, but it is
  invisible from the release page.
- **Download `dish-setup.exe` and install it by hand** at least once. The manual
  matrix at the end of [`docs/PACKAGING.md`](docs/PACKAGING.md) is the list.

The asset names `dish-setup.exe`, `dish-windows.zip` and `latest.json` are a
permanent API. Shipped clients construct the manifest URL from them and validate
the download URL against the release-download prefix. Renaming one breaks every
installed copy in the field, silently, and there is no server-side way to fix
it.

## Touching the hot path

Three threads carry every report and every feedback message: the SDL input
thread, a Direct pad's raw-HID read thread, and each session's receive thread.
None of them takes a lock, allocates per report, or waits on the network, the
GUI or a pad, and a mic capture thread sends its frames without a lock too. If
you are modifying `SDLGamepadBridge`, `GamepadInputProcessor`,
`SlotRouter`, `UsbGamepadManager`'s publisher or its output worker, or
`SatelliteClient`:

- State another thread changes reaches these threads as an immutable snapshot
  (`SlotRouter`'s table, read inside a grace epoch; the bridge's inputs through
  `LatestValue`) or through a seqlock or a single-consumer queue
  (`core/thread/`). Nothing on the path takes a `TrackedMutex`.
- One shape of waiting is allowed, and only between producers: a turn flag
  that several producers of one pad's feedback take for a fixed handful of
  stores, with no call, allocation or syscall inside, spinning with a yield
  only when two producers meet (`OutputMailbox::post`,
  `SurfaceLedger::rememberWith`). Its consumer or reader never takes it, so
  the thread that drives the pad never waits on a producer, and no producer
  waits on a reader. It is not a `TrackedMutex`, so the lock laws do not
  count it; a new one names itself here and gets a test that its reader
  takes no turn.
- Buffers live on the stack or in fixed records; nothing on the path allocates.
- No `QObject::connect` cross-thread signals on the send path.
- Both rules are enforced, not hoped for: `tests/test_lock_laws.cpp`
  (`DishLockLawTests`, built with `-DDISH_LOCK_STATS=ON`) fails when one of these
  threads takes any tracked lock, and `tools/bench` measures the path and its
  locks on every run; `measurements.yml` runs both on a pull request that
  touches the path and before every release. Run both after a change here; see
  [`tools/bench/README.md`](tools/bench/README.md).
- Preserve `IP_TOS = 0xB8` (DSCP EF) on every send. Windows strips it silently
  for non-administrator processes, but the call stays so a machine with the
  `DisableUserTOSSetting=0` registry override honours it.
- Winsock has no `MSG_NOSIGNAL` and does not need one. There is no SIGPIPE on
  Windows.

## Touching the wire protocol

Dish for Windows, Android, iOS, macOS and Linux all talk to the same Satellite
server and must produce byte-identical traffic. This client and its siblings
speak protocol 4 (and fall back to 1 against an older satellite); the
contract lives in one place, `satellite/docs/contract.md`, and
[`docs/contract.md`](docs/contract.md) maps it onto this repository. The parts
a change here most often touches:

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
- INPUT (0x0001): ctrlIdx + the XUSB report, and on a session settled at
  protocol 4 the extended-buttons word (u32 LE) behind it. The frame shape
  keys on the SETTLED version; the receiver reads it by length.

The authoritative opcode catalogue is
[`satellite/docs/contract.md`](https://github.com/TinkerNorth/satellite/blob/main/docs/contract.md);
the client-side subset this repo needs is `src/core/model/Protocol.h`. Any
change here must be coordinated with `dish-android`, `dish-ios`, `dish-mac`,
`dish-linux` and `satellite` in the same release cycle, and is marked
`[wire-coordinated]` in `CHANGELOG.md`.

## Reporting bugs

Use the issue templates under `.github/ISSUE_TEMPLATE/`. Include:

- The Windows build number (`winver`, or
  `[System.Environment]::OSVersion.Version`).
- The dependency versions you built against. Qt's is whatever
  `CMAKE_PREFIX_PATH` resolves to (`scripts\build.ps1` echoes the prefix it
  picked); libsodium, OpenSSL, Opus, SDL2 and sentry-native come from the
  vcpkg baseline pinned in `vcpkg.json`.
- `%LOCALAPPDATA%\Dish\crash.log` and the matching `.dmp`, if the app crashed.
  The crash handler writes both.
- The diagnostics file from Settings, *Report a problem*, when the problem is
  a controller or a connection.

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
- **An Authenticode certificate.** `release.yml` signs `dish-setup.exe` once
  the `WIN_SIGN_*` secrets exist, and none is configured, so SmartScreen warns
  on every manual download. cosign signatures, SLSA provenance and both SBOM
  formats already ship; [`SECURITY.md`](SECURITY.md), "Known gaps", says what
  each is worth today.
- **Screenshots.** `README.md` describes the UI in prose. There are no
  screenshots of the dashboard, the connections page, or the setup wizard.
