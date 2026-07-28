// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The one shared "open a link" edge. Failures are reported to the caller; the
// QML layer surfaces them through App.errorMessage → the toast host (the old
// Widgets NotificationQueue sink died with the Widgets UI).

#pragma once

#include <QString>

namespace dish::ui {

// Open `url` via QDesktopServices. Returns true iff the open was handed off
// successfully to the OS.
bool openExternalUrl(const QString& url);

} // namespace dish::ui
