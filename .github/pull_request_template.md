## What changes, and why

<!-- Two or three sentences. Name the behaviour a user or a developer sees, not
     the files. If it touches the wire protocol, the crypto, or the input hot
     path, say so here explicitly. -->

## Kind of change

- [ ] Fix
- [ ] Feature
- [ ] Refactor (behaviour unchanged)
- [ ] Docs, CI or repo plumbing
- [ ] Wire protocol or pairing flow (`[wire-coordinated]` in CHANGELOG; the satellite and the sibling clients need the matching change)

## Shape of the code (CONTRIBUTING, "Shape of the code")

- [ ] Immutable by default; one flow per function; named steps instead of comments.
- [ ] Every `switch` over an enum is exhaustive, with no `default`.
- [ ] No singleton, no mutable static, no discarded result, no lint or warning suppression.
- [ ] Words and colours reach QML only through `qsTr`, `Theme` and `Tokens`; a new `App` property or invokable is in `docs/QML_CONTRACT.md`.

## Tests

- [ ] Red first: a test failed before the change and passes after it.
- [ ] Every new flow has a test, including the empty, error and loading shapes.
- [ ] A new user-facing string is in every catalogue under `translations/`, translated.
- [ ] New files carry the SPDX header (`CONTRIBUTING.md`, "License headers").

## CI gates

`windows-ci.yml` runs these on every pull request, in this order. Tick what you
ran locally. Every one of them fails the build.

- [ ] **`clang-format --dry-run --Werror`** over `src/` and `tests/`. CI pins
      clang-format **23.1.5** from PyPI. Different major versions disagree on
      braced-init lists, so a different local version will flap.
      `pipx install clang-format==23.1.5`
- [ ] **Debug build** configured with `-DDISH_BUILD_TESTS=ON
      -DDISH_REQUIRE_TRANSLATIONS=ON`, warnings clean (`/WX` is on for
      first-party code, tests included)
- [ ] **`ctest --preset debug --parallel`** passes, and the measurements
      report's tests
- [ ] **`qmllint`** clean over every tracked `src/qml/*.qml`. Must run after the
      build, because the `Dish.Chrome` qmldir and qmltypes are generated into the
      build tree.
- [ ] **`scripts/qml-lint-literals.ps1 -Mode error`** clean. This is the gate for
      hard-coded colours, radii and spacing in QML. A literal still renders, it
      just stops tracking the design tokens, and nothing else catches it.
- [ ] **`scripts/check-translations.ps1`** clean. **This is the one contributors
      trip on.** If you added, edited or deleted any `qsTr(...)` or `tr(...)`
      string, the catalogues under `translations/` are now stale and the gate
      fails with the diff you owe. The fix is one command:
      `cmake --build <build-dir> --target update_translations`, then commit the
      changed `.ts` files. A new `%n` plural also needs its singular and plural
      written into `translations/dish_en.ts` by hand.
- [ ] **`scripts/check-tidy.ps1`** (`clang-tidy` over `src/*.cpp` excluding
      `src/UI/*`, against the same Debug tree). Findings fail the step: it
      passes `--warnings-as-errors='*'`, the way `dish-linux` gates its sweep,
      while the fleet-canonical `.clang-tidy` keeps `WarningsAsErrors: ''`.
- [ ] **Release build** configures and builds, the portable bundle and the
      install image start, and the installer compiles and round-trips
      (`scripts\ci-local.ps1 -WithInstaller`)

`scripts\ci-local.ps1` runs every one of them, in this order, through the same
scripts. The security workflows (`security.yml`, `codeql.yml`) and the version
gate (`version-consistency.yml`) also block but need nothing from you.

## Manual testing

CI has no socket, no satellite and no controller, so the session loop, the SDL
input threading and the USB claim path are only ever covered by hand. Describe
what you actually exercised, and say "not applicable" rather than leaving it
blank. `docs/QML_MANUAL_SMOKE_CHECKLIST.md` is the list.

- Windows build tested on:
- Controller and connection tested (make, model, USB / Bluetooth / dongle):
- Satellite version and platform tested against:
- What you did:

## Bookkeeping

- [ ] `CHANGELOG.md` has a line under `[Unreleased]`.
- [ ] `tests/PARITY.md` is updated if a row changed.
- [ ] `docs/ARCHITECTURE.md`, `docs/QML_CONTRACT.md` or `DESIGN.md` is updated if a rule, a surface or a token changed.
- [ ] A new or changed dependency is reflected in `vcpkg.json`,
      `assets/licenses/licenses.json` and `THIRD_PARTY.md` together
      (`scripts/check-versions.ps1` checks the versions).
