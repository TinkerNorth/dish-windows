// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// DonateView — the donate screen (Workstream 3b). An external-link funnel, no
// in-app purchase. Mirrors dish-android ui/donate/DonateActivity.kt +
// activity_donate.xml section-for-section: a centered hero (heart badge,
// eyebrow, H1, lead, recommended CTA), three clickable rail cards (GitHub
// Sponsors = recommended/recurring, Ko-fi = one-time, Buy Me a Coffee = either)
// each with name/badge/blurb, a two-column Cadence | Pays-with meta grid, and a
// visit link, then a single "what your donation pays for" card (four dotted
// why-rows: hosting / signing / store fees / time), and a closing thanks line.
// Each rail (whole card) and the CTA open an external URL via QDesktopServices;
// on failure a warning is routed through the notification host keyed
// "external-url-failed".
//
// android's donate accent is the pulse-pink role (colorPulse). dish-windows'
// Theme has no pulse token (and this view must not edit Theme), so the heart /
// eyebrow / CTA / rail titles / visit links / why-dots use the cyan `primary`
// accent — the same substitution the rest of the port makes — which stays
// theme-correct under BOTH the dark and the light palette (it reads the active
// palette). The amber `warning` token stands in for android's `colorTertiary`
// meta labels.
//
// Hosted as a modal QDialog (like ConnectionsDialog), the dish-windows shell
// idiom. The donate URLs are localizable string resources (so a locale can
// override) defaulting to the android values.

#pragma once

#include <QDialog>

namespace dish::ui {

class NotificationQueue;

class DonateView : public QDialog {
    Q_OBJECT
  public:
    // `notifications` (borrowed, may be null) receives the external-url-failed
    // warning when a link won't open.
    explicit DonateView(NotificationQueue* notifications, QWidget* parent = nullptr);

  private:
    NotificationQueue* notifications_;
};

} // namespace dish::ui
