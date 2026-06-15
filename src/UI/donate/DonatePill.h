// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// DonatePill — the dismissible "support Dish" pill (Workstream 3b). Mirrors
// dish-android ui/common/DonatePill.kt: a small docked pill that opens the
// donate screen when tapped and, on its dismiss button, hides for 24h by
// persisting donate_pill_dismissed_at (epoch ms) under user_preferences.
//
// SoC: the dismiss-window decision is a PURE free function
// (donatePillSuppressed) so it is unit-testable without a clock — the pattern
// the rest of the port uses for time logic. The persisted dismissedAt is trivial
// preference state behind a tiny QSettings read/write (donatePillReadDismissedAt
// / donatePillWriteDismissedAt). The widget itself is a thin renderer.

#pragma once

#include <QWidget>

#include <cstdint>
#include <memory>

class QSettings;

namespace dish::ui {

// The 24h dismissal window in milliseconds (android's
// DONATE_PILL_DISMISS_WINDOW_MS = 24 * 60 * 60 * 1000).
inline constexpr std::int64_t kDonatePillDismissWindowMs = 24LL * 60LL * 60LL * 1000LL;

// Persisted key — verbatim from android (cross-client schema continuity).
inline constexpr const char* kDonatePillDismissedAtKey = "donate_pill_dismissed_at";

// PURE: should the pill be suppressed? True while now - dismissedAt < windowMs.
// Mirrors android's `System.currentTimeMillis() - dismissedAt < WINDOW`. A
// never-dismissed pill (dismissedAt == 0) is suppressed only inside the first
// window after epoch 0, which is decades in the past — so in practice not
// suppressed. Exactly-at / past the boundary -> not suppressed (the pill
// returns). Clock-free: the caller passes `nowMs`.
bool donatePillSuppressed(std::int64_t dismissedAtMs, std::int64_t nowMs,
                          std::int64_t windowMs = kDonatePillDismissWindowMs);

// Read / write the persisted dismissedAt (epoch ms) from a QSettings store.
// Trivial preference IO — the pure decision above is what tests pin.
std::int64_t donatePillReadDismissedAt(QSettings& settings);
void donatePillWriteDismissedAt(QSettings& settings, std::int64_t dismissedAtMs);

// The pill widget. A docked, Theme-tinted button row: a heart glyph + "support
// Dish" label opens the donate screen (openRequested), a trailing × dismisses it
// for 24h (persists + hides). Construct with an injectable QSettings so tests /
// alternate stores work; nullptr -> the app's default user_preferences store.
class DonatePill : public QWidget {
    Q_OBJECT
  public:
    explicit DonatePill(std::shared_ptr<QSettings> settings = nullptr, QWidget* parent = nullptr);
    ~DonatePill() override;

    // Re-evaluate suppression against `now` and show/hide accordingly. Called on
    // construction (with the real clock) and re-callable by the host. A
    // suppressed pill hides itself; otherwise it shows.
    void refreshVisibility(std::int64_t nowMs);

  signals:
    // The body was tapped — the host opens the donate screen (DonateView).
    void openRequested();

  private:
    void onDismissClicked();

    std::shared_ptr<QSettings> settings_;
};

} // namespace dish::ui
