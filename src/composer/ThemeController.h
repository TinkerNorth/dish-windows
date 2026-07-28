// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ThemeController — the EFFECT half of the theme subsystem (Workstream 3d): a
// kernel Controller that subscribes the ThemePreferenceStore's
// Observable<ThemeMode>, resolves SYSTEM to a concrete light/dark Appearance via
// an injected OS-preference reader, and applies the resolved palette live. The
// DERIVE (which mode is selected) lives in the Source; this class only actuates
// — the §4.3 rule-2 split android folds into setMode()+AppCompatDelegate.
//
// Two seams keep it testable without touching the real registry:
//   * `systemReader` resolves ThemeMode::System -> Appearance (default: the real
//     ui::detectSystemAppearance). Tests inject a stub to drive 0->Dark / 1->Light.
//   * `applySink` receives the resolved Appearance and performs the re-theme
//     (default: ui::setActiveAppearance — the QML ThemeBridge re-reads the
//     swapped tokens on its refresh). Tests inject a recording sink and assert
//     the Appearance sequence (the house pattern, cf. FakeInhibitor in
//     test_screen_wake_controller).
//
// stop() uses the kernel default (cancel the subscription) — unlike the crash
// controller, the theme controller does not need to survive teardown.

#pragma once

#include "architecture/Controller.h"
#include "source/store/ThemePreferenceStore.h"
#include "UI/Theme.h"

#include <functional>

namespace dish::composer {

class ThemeController : public arch::Controller<source::ThemeMode> {
  public:
    // Resolves ThemeMode::System to a concrete Appearance (Dark/Light).
    using SystemReader = std::function<ui::Appearance()>;
    // Applies a resolved Appearance (set the active palette + re-theme).
    using ApplySink = std::function<void(ui::Appearance)>;

    // Production ctor: swaps the active palette on each mode change; SYSTEM
    // resolves via the real OS reader.
    explicit ThemeController(const arch::Observable<source::ThemeMode>& mode);

    // Test ctor: inject both seams. `systemReader` resolves SYSTEM; `applySink`
    // receives the resolved Appearance. Neither touches the OS / a QApplication.
    ThemeController(const arch::Observable<source::ThemeMode>& mode, SystemReader systemReader,
                    ApplySink applySink);

    // The Appearance a ThemeMode resolves to right now (SYSTEM via the reader).
    // Pure helper, exposed for the picker/diagnostics and reused by apply().
    ui::Appearance resolve(source::ThemeMode mode) const;

  protected:
    void apply(const source::ThemeMode& mode) override;

  private:
    SystemReader systemReader_;
    ApplySink applySink_;
};

} // namespace dish::composer
