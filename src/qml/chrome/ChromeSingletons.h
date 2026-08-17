// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Instance-registration of the Dish.Chrome singletons (ChromeBridge / Theme /
// Tokens) for binaries other than the app — today dish-setup-ui.exe. It
// REPLICATES QmlEntryPoint.cpp's LTCG-safe block (lines 57-71) instead of
// sharing it: registration by instance because under /GL the generated
// QQmlModuleRegistration static initialisers are stripped and every `Theme.*`
// reference would become a ReferenceError. Ten lines of duplication beat a
// cross-agent edit to a load-bearing app file (spec decision D4); a follow-up
// PR may de-duplicate. Compiled ONLY into dish_setup_kit, never the app.

#pragma once

namespace dish::chrome {

class ChromeBridge;
class ThemeBridge;
class TokensBridge;

// The three registered instances, returned so the caller can finish the wiring
// QmlEntryPoint does around its own block: hand the native chrome filter to
// `chromeBridge->setChrome()` inside the objectCreated hook and push
// `themeBridge->refresh()` once the window exists. Ignoring the return is
// legal — the singletons are parented to the application and reachable through
// the QML engine either way.
struct ChromeSingletons {
    ChromeBridge* chromeBridge = nullptr;
    ThemeBridge* themeBridge = nullptr;
    TokensBridge* tokensBridge = nullptr;
};

// Resolves the startup appearance ONCE via dish::ui::detectSystemAppearance()
// (the installer has no persisted theme preference and follows the OS; no live
// re-follow, a documented simplification), then instance-registers the three
// singletons into the "Dish.Chrome" module with CppOwnership. Call after the
// Q*Application exists and before the QML engine loads anything.
ChromeSingletons registerChromeSingletons();

} // namespace dish::chrome
