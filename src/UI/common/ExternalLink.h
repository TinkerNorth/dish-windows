// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// openExternalUrl — the shared "open a URL in the browser, warn on failure"
// helper used by the donate / licenses / onboarding screens (Workstreams
// 3a/3b/3c). Mirrors the android pattern repeated in DonateActivity /
// LicensesActivity / WelcomeActivity / HelpActivity: try to open the URL, and on
// failure route a warning through the notification host keyed
// "external-url-failed" (android's notifications.warn(key="external-url-failed")).
//
// The notification sink is the UI NotificationQueue (2f). Passing nullptr opens
// the URL but silently drops the failure warning (for hosts with no queue).

#pragma once

#include <QString>

namespace dish::ui {

class NotificationQueue;

// Open `url` via QDesktopServices. On failure, post a Warn notification with
// kind "external-url-failed" to `notifications` (if non-null). Returns true iff
// the open was handed off successfully.
bool openExternalUrl(const QString& url, NotificationQueue* notifications);

} // namespace dish::ui
