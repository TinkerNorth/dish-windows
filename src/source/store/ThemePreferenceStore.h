// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ThemePreferenceStore — the theme-mode StateSource (Workstream 3d). Owns the
// persisted dark/light/system choice and republishes it reactively. Mirrors
// dish-android source/store/ThemePreferenceStore.kt + its ThemeMode enum.
//
// SoC split (plan §4.3 rule 2): this class only DERIVES the mode (Source). The
// EFFECT — resolving SYSTEM against the OS preference and re-theming the live
// QApplication — lives in composer::ThemeController (a kernel Controller). Where
// android folds both into setMode() + AppCompatDelegate, the port keeps them
// apart: setMode() persists + republishes; the controller subscribes the
// Observable and applies the palette, so the persisted value and the rendered
// palette cannot drift.
//
// Storage values ("system"|"light"|"dark") and the key ("theme_mode") under the
// "user_preferences" store are kept verbatim from android — they are a
// cross-client schema; renaming is a migration. Unknown/empty -> SYSTEM
// (android's fromStorageValue).

#pragma once

#include "architecture/StateSource.h"

#include <QString>

#include <memory>

class QSettings;

namespace dish::source {

// The three appearance modes the picker offers. SYSTEM follows the OS; LIGHT /
// DARK pin it. Mirrors android ThemeMode { SYSTEM, LIGHT, DARK }.
enum class ThemeMode { System, Light, Dark };

// Pure mappings between ThemeMode and its persisted storage token / display
// label. Free functions (no QObject) so tests pin them without a store.
// `themeModeFromStorage` is lenient: null / empty / unknown / forward-newer ->
// System (matches android's fromStorageValue default).
QString themeModeToStorage(ThemeMode mode);
ThemeMode themeModeFromStorage(const QString& value);
QString themeModeLabel(ThemeMode mode);

class ThemePreferenceStore : public arch::StateSource<ThemeMode> {
  public:
    // Persisted key + store name — verbatim from android for cross-client schema
    // continuity.
    static constexpr const char* kKeyThemeMode = "theme_mode";

    // The documented default for a fresh install: follow the OS.
    static constexpr ThemeMode kDefault = ThemeMode::System;

    // Production ctor: a QSettings under the app org (the same store the other
    // user_preferences live in). Test ctor: inject a throwaway store.
    ThemePreferenceStore();
    explicit ThemePreferenceStore(std::unique_ptr<QSettings> settings);
    ~ThemePreferenceStore() override;

    ThemeMode mode() const { return state().value(); }

    // Persist the mode + republish it. Distinct-until-changed: a redundant
    // setMode is a no-op (the Observable suppresses the re-emit), matching the
    // android-test "emit only on a real transition" contract.
    void setMode(ThemeMode mode);

  private:
    static ThemeMode readInitial(QSettings& settings);

    std::unique_ptr<QSettings> settings_;
};

} // namespace dish::source
