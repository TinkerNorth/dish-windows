// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// DonateView — the donate screen (Workstream 3b). An external-link funnel, no
// in-app purchase. Mirrors dish-android ui/donate/DonateActivity.kt: a
// recommended CTA + three rails (GitHub Sponsors = recommended/recurring, Ko-fi
// = one-time, Buy Me a Coffee = either), each with name/blurb/cadence/currencies
// /visit-link, plus four "why we ask" cards (hosting / signing / Play / time).
// Each rail and the CTA open an external URL via QDesktopServices; on failure a
// warning is routed through the notification host keyed "external-url-failed".
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
