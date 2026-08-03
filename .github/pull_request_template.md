## Description

<!-- What this changes and why. If it touches the wire protocol, the crypto, or
     the input hot path, say so here explicitly. -->

## Type of change

- [ ] Bug fix (non-breaking)
- [ ] New feature (non-breaking)
- [ ] Breaking change (existing behaviour changes)
- [ ] Refactor (no behaviour change)
- [ ] Documentation, build or CI

## CI gates

`windows-ci.yml` runs these on every pull request, in this order. Tick what you
ran locally. Everything except `clang-tidy` fails the build.

- [ ] **`clang-format --dry-run --Werror`** over `src/` and `tests/`. CI pins
      clang-format **22.1.4** from PyPI. Version 18 and version 22 disagree on
      braced-init lists, so a different local version will flap.
      `pipx install clang-format==22.1.4`
- [ ] **Debug build** configured with `-DDISH_BUILD_TESTS=ON
      -DDISH_REQUIRE_TRANSLATIONS=ON`, warnings clean (`/WX` is on for
      first-party code)
- [ ] **`ctest --output-on-failure --parallel`** passes
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
- [ ] **`clang-tidy -p build`** over `src/*.cpp` excluding `src/UI/*`, against the
      same Debug tree. Advisory: `.clang-tidy` sets `WarningsAsErrors: ''` and
      the step does not fail. Read the findings anyway.
- [ ] **Release build** configures and builds

`scripts\build.ps1 debug test` reproduces the build and test steps. The security
workflows (`security.yml`, `codeql.yml`) also gate but need nothing from you.

## Manual testing

CI has no socket, no satellite and no controller, so the session loop, the SDL
input threading and the USB claim path are only ever covered by hand. Describe
what you actually exercised, and say "not applicable" rather than leaving it
blank.

- Windows build tested on:
- Controller and connection tested (make, model, USB / Bluetooth / dongle):
- Satellite version and platform tested against:
- What you did:

## Checklist

- [ ] Self-reviewed the diff
- [ ] New behaviour has tests. Most of `src/core/` and `src/composer/` is
      deliberately Qt-free so it is host-testable without a window.
- [ ] New files carry the SPDX header (`CONTRIBUTING.md`, "License headers")
- [ ] Comments explain why, not what. This repo does not narrate code.
- [ ] Documentation updated where the change makes it wrong
      (`README.md`, `docs/`, `DESIGN.md`, `PRIVACY.md`, `THIRD_PARTY.md`)
- [ ] `CHANGELOG.md` updated under `## [Unreleased]` if this is user-facing
- [ ] A new or changed dependency is reflected in `vcpkg.json`,
      `assets/licenses/licenses.json` and `THIRD_PARTY.md` together
