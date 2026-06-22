// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Coverage for Workstream 3d: the ThemePreferenceStore (dark/light/system mode),
// the full light-token palette, and the live re-apply ThemeController. There is
// no android @Test for the store (a plain AbstractStateSource); these pin the
// RULES the android code expresses (ThemeMode.toStorageValue/fromStorageValue,
// the SYSTEM default, setMode persist + distinct emit) plus the Windows light-
// palette deliverable: palette completeness (every dark role has a light token,
// and they genuinely differ) and SYSTEM resolution through a stubbed OS reader,
// the round-trip store->controller->palette driven via the kernel ControllerProbe
// + a recording apply sink (the house pattern). No real registry is touched.

#include "UI/Theme.h"
#include "architecture/Observable.h"
#include "composer/ThemeController.h"
#include "source/store/ThemePreferenceStore.h"

#include "ControllerProbe.h"
#include "StateSourceProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <vector>

using dish::arch::Observable;
using dish::composer::ThemeController;
using dish::source::ThemeMode;
using dish::source::themeModeFromStorage;
using dish::source::themeModeLabel;
using dish::source::themeModeToStorage;
using dish::source::ThemePreferenceStore;
using dish::test::ControllerProbe;
using dish::test::StateSourceProbe;
using dish::ui::Appearance;
using dish::ui::darkPalette;
using dish::ui::lightPalette;
using dish::ui::paletteFor;
using dish::ui::ThemePalette;

namespace {

std::unique_ptr<ThemePreferenceStore> makeStore(const QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("theme.ini"));
    auto settings = std::make_unique<QSettings>(path, QSettings::IniFormat);
    return std::make_unique<ThemePreferenceStore>(std::move(settings));
}

} // namespace

// --- Pure storage mappings --------------------------------------------------

TEST_CASE("themeMode storage round-trips for all three modes", "[settings][theme]") {
    REQUIRE(themeModeFromStorage(themeModeToStorage(ThemeMode::System)) == ThemeMode::System);
    REQUIRE(themeModeFromStorage(themeModeToStorage(ThemeMode::Light)) == ThemeMode::Light);
    REQUIRE(themeModeFromStorage(themeModeToStorage(ThemeMode::Dark)) == ThemeMode::Dark);
}

TEST_CASE("themeMode storage uses the exact android tokens", "[settings][theme]") {
    // Schema pin: these strings are cross-client; renaming is a migration.
    REQUIRE(themeModeToStorage(ThemeMode::System) == QStringLiteral("system"));
    REQUIRE(themeModeToStorage(ThemeMode::Light) == QStringLiteral("light"));
    REQUIRE(themeModeToStorage(ThemeMode::Dark) == QStringLiteral("dark"));
}

TEST_CASE("themeModeFromStorage is lenient -- unknown values fall back to System",
          "[settings][theme]") {
    REQUIRE(themeModeFromStorage(QString()) == ThemeMode::System);
    REQUIRE(themeModeFromStorage(QStringLiteral("")) == ThemeMode::System);
    REQUIRE(themeModeFromStorage(QStringLiteral("garbage")) == ThemeMode::System);
    // A forward-newer value the schema doesn't know yet -> System (not a crash).
    REQUIRE(themeModeFromStorage(QStringLiteral("oled-black-2027")) == ThemeMode::System);
    // Case-exact only: "Light" is not the token.
    REQUIRE(themeModeFromStorage(QStringLiteral("Light")) == ThemeMode::System);
    REQUIRE(themeModeFromStorage(QStringLiteral("light")) == ThemeMode::Light);
}

TEST_CASE("themeModeLabel returns the locale-aware strings", "[settings][theme]") {
    constexpr const char* ctx = "dish::source::ThemePreferenceStore";
    REQUIRE(themeModeLabel(ThemeMode::Light) == QCoreApplication::translate(ctx, "Light"));
    REQUIRE(themeModeLabel(ThemeMode::Dark) == QCoreApplication::translate(ctx, "Dark"));
    REQUIRE(themeModeLabel(ThemeMode::System) == QCoreApplication::translate(ctx, "System"));
}

// --- Store behaviour --------------------------------------------------------

TEST_CASE("ThemePreferenceStore defaults to System on a fresh store", "[settings][theme]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir);
    REQUIRE(store->mode() == ThemeMode::System);
}

TEST_CASE("ThemePreferenceStore persists setMode across a relaunch", "[settings][theme]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        auto store = makeStore(dir);
        store->setMode(ThemeMode::Light);
    }
    {
        auto reloaded = makeStore(dir);
        REQUIRE(reloaded->mode() == ThemeMode::Light);
    }
}

TEST_CASE("ThemePreferenceStore emits only on an actual transition", "[settings][theme]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir); // default System

    StateSourceProbe<ThemeMode> probe(store->state());
    REQUIRE(probe.count() == 1); // replayed current

    store->setMode(ThemeMode::System); // already System — no-op
    REQUIRE(probe.count() == 1);

    store->setMode(ThemeMode::Dark); // real change
    REQUIRE(probe.count() == 2);

    store->setMode(ThemeMode::Dark); // redundant — no re-emit
    REQUIRE(probe.count() == 2);
    REQUIRE(probe.latest() == ThemeMode::Dark);
}

// --- Palette completeness (the light-token deliverable) ---------------------

TEST_CASE("light palette defines every dark token role and differs from dark",
          "[settings][theme]") {
    const ThemePalette& dark = darkPalette();
    const ThemePalette& light = lightPalette();

    // Every role in the table has a concrete (non-zero-alpha for opaque roles)
    // value in BOTH palettes — none left dark-only / default-initialized. We
    // assert each opaque body/surface/text role is fully opaque (alpha 0xFF) in
    // both, which catches a forgotten/zeroed light token.
    const auto opaque = [](QRgb c) { return qAlpha(c) == 0xFF; };
    for (QRgb c :
         {dark.background, dark.surface, dark.surfaceDim, dark.primary, dark.primaryDark,
          dark.onPrimary, dark.onSurface, dark.muted, dark.success, dark.error, dark.warning}) {
        REQUIRE(opaque(c));
    }
    for (QRgb c : {light.background, light.surface, light.surfaceDim, light.primary,
                   light.primaryDark, light.onPrimary, light.onSurface, light.muted, light.success,
                   light.error, light.warning}) {
        REQUIRE(opaque(c));
    }

    // The two palettes must genuinely differ on the body/surface/text roles so
    // "light" is a distinct set, not a dark alias.
    REQUIRE(light.background != dark.background);
    REQUIRE(light.surface != dark.surface);
    REQUIRE(light.surfaceDim != dark.surfaceDim);
    REQUIRE(light.onSurface != dark.onSurface);
    REQUIRE(light.primary != dark.primary);
    REQUIRE(light.muted != dark.muted);

    // Light surfaces are lighter than dark surfaces (sanity: the appearance flips).
    REQUIRE(qGray(light.background) > qGray(dark.background));
    REQUIRE(qGray(light.surface) > qGray(dark.surface));
    // ...and light body text is darker than dark body text.
    REQUIRE(qGray(light.onSurface) < qGray(dark.onSurface));
}

TEST_CASE("paletteFor maps the appearance to its palette", "[settings][theme]") {
    REQUIRE(paletteFor(Appearance::Dark).background == darkPalette().background);
    REQUIRE(paletteFor(Appearance::Light).background == lightPalette().background);
}

// --- Controller: resolve SYSTEM + re-apply on a mode change -----------------

namespace {

// A recording apply sink + a stubbable system reader, the house fake pattern.
struct ThemeHarness {
    Observable<ThemeMode> mode{ThemeMode::System};
    Appearance systemAppearance = Appearance::Dark; // what SYSTEM resolves to
    std::vector<Appearance> applied;                // recorded re-themes

    ThemeController controller{mode, [this] { return systemAppearance; },
                               [this](Appearance a) { applied.push_back(a); }};
};

} // namespace

TEST_CASE("ThemeController resolves SYSTEM via the stubbed OS reader (both ways)",
          "[settings][theme]") {
    SECTION("AppsUseLightTheme = 0 -> Dark") {
        ThemeHarness h;
        h.systemAppearance = Appearance::Dark;
        REQUIRE(h.controller.resolve(ThemeMode::System) == Appearance::Dark);
    }
    SECTION("AppsUseLightTheme = 1 -> Light") {
        ThemeHarness h;
        h.systemAppearance = Appearance::Light;
        REQUIRE(h.controller.resolve(ThemeMode::System) == Appearance::Light);
    }
    // Explicit modes ignore the reader.
    ThemeHarness h;
    h.systemAppearance = Appearance::Dark;
    REQUIRE(h.controller.resolve(ThemeMode::Light) == Appearance::Light);
    REQUIRE(h.controller.resolve(ThemeMode::Dark) == Appearance::Dark);
}

TEST_CASE("ThemeController re-themes on each mode change (store->controller->palette)",
          "[settings][theme]") {
    ThemeHarness h;
    h.systemAppearance = Appearance::Light; // SYSTEM resolves to Light here

    ControllerProbe<ThemeController> probe(h.controller);
    probe.start();
    // start() applies the current value immediately: SYSTEM -> Light.
    REQUIRE(h.applied.size() == 1);
    REQUIRE(h.applied.back() == Appearance::Light);

    h.mode.set(ThemeMode::Dark); // LIGHT/DARK flip
    REQUIRE(h.applied.size() == 2);
    REQUIRE(h.applied.back() == Appearance::Dark);

    h.mode.set(ThemeMode::Light);
    REQUIRE(h.applied.size() == 3);
    REQUIRE(h.applied.back() == Appearance::Light);

    // The recorded sequence is the round-trip, not store-only.
    REQUIRE(h.applied ==
            std::vector<Appearance>{Appearance::Light, Appearance::Dark, Appearance::Light});
}

TEST_CASE("ThemeController: switching System resolution re-themes when re-emitted",
          "[settings][theme]") {
    ThemeHarness h;
    h.systemAppearance = Appearance::Dark;
    ControllerProbe<ThemeController> probe(h.controller);
    probe.start();
    REQUIRE(h.applied.back() == Appearance::Dark);
    // Move to an explicit mode and back to System with the OS now reading Light.
    h.mode.set(ThemeMode::Light);
    h.systemAppearance = Appearance::Light;
    h.mode.set(ThemeMode::System);
    REQUIRE(h.applied.back() == Appearance::Light);
}

// --- Active-token state after the startup resolution ------------------------
// The QML ThemeBridge reads the live dish::ui::Theme::* statics (surface,
// background, ...) at render time. The "body renders LIGHT under System+OS-dark"
// bug was that the rendered tokens did not match the resolved appearance. These
// pin the invariant that drives those statics: applying the appearance a mode +
// OS reading resolves to leaves Theme::* equal to that appearance's palette — so
// whatever the bridge reads matches what was resolved. Pure global-state checks
// (no QML engine); the statics are process-global, so each section re-applies.

namespace {

// The production startup decision, distilled: resolve the mode (System via the
// reader) and apply it to the active palette — exactly what the ThemeController's
// real apply sink and the QmlEntryPoint System re-resolve both do.
void applyResolved(ThemeMode mode, Appearance systemReading) {
    const Appearance resolved = mode == ThemeMode::Light  ? Appearance::Light
                                : mode == ThemeMode::Dark ? Appearance::Dark
                                                          : systemReading;
    dish::ui::setActiveAppearance(resolved);
}

} // namespace

TEST_CASE("active Theme tokens match the resolved appearance after startup", "[settings][theme]") {
    SECTION("System + OS dark -> dark tokens (the reported repro)") {
        applyResolved(ThemeMode::System, Appearance::Dark);
        REQUIRE(dish::ui::activeAppearance() == Appearance::Dark);
        REQUIRE(dish::ui::Theme::surface == darkPalette().surface);
        REQUIRE(dish::ui::Theme::background == darkPalette().background);
        // The exact seam the QML Card binds (color: Theme.surface) — must be dark,
        // not the light 0xFFFFFFFF the bug rendered.
        REQUIRE(dish::ui::Theme::surface != lightPalette().surface);
    }
    SECTION("System + OS light -> light tokens") {
        applyResolved(ThemeMode::System, Appearance::Light);
        REQUIRE(dish::ui::activeAppearance() == Appearance::Light);
        REQUIRE(dish::ui::Theme::surface == lightPalette().surface);
        REQUIRE(dish::ui::Theme::background == lightPalette().background);
    }
    SECTION("explicit Light ignores an OS-dark reading -> light tokens") {
        applyResolved(ThemeMode::Light, Appearance::Dark);
        REQUIRE(dish::ui::activeAppearance() == Appearance::Light);
        REQUIRE(dish::ui::Theme::surface == lightPalette().surface);
    }
    SECTION("explicit Dark ignores an OS-light reading -> dark tokens") {
        applyResolved(ThemeMode::Dark, Appearance::Light);
        REQUIRE(dish::ui::activeAppearance() == Appearance::Dark);
        REQUIRE(dish::ui::Theme::surface == darkPalette().surface);
    }
    // Restore the process-global default so a later test reading the statics is
    // not perturbed by whichever section ran last.
    dish::ui::setActiveAppearance(Appearance::Dark);
}
