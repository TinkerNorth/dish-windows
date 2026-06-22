// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CrashReportingRow — the Diagnostics opt-out toggle row (Workstream 3e). 3d
// owns the Settings column and reserves the Diagnostics slot; 3e owns this
// switch widget + its binding. Exposed as a factory so the OWNS sets stay
// disjoint (3d inserts the returned widget; it never touches the store).
//
// Mirrors dish-android SettingsActivity's crash-reporting switch: a Theme-tinted
// title + subtitle (the privacy posture — collection ON by default, toggle OFF
// to opt out, and that no crash backend is wired yet) and a checkbox bound to
// the CrashReportingStore. Observe-then-bind: it reflects the store's current
// value first so the first frame doesn't re-write the preference (android does
// exactly this).

#pragma once

#include <QWidget>

namespace dish::source {
class CrashReportingStore;
}

namespace dish::ui {

// Build the Diagnostics crash-reporting toggle row bound to `store`. The
// returned QWidget is owned by `parent` (Qt parent semantics). `store` is
// borrowed and must outlive the row. The row subscribes the store's Observable
// (RAII subscription parented to the row) so a programmatic setEnabled elsewhere
// reflects here too.
QWidget* makeCrashReportingRow(source::CrashReportingStore* store, QWidget* parent);

} // namespace dish::ui
