// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The DISH_QML alternative to constructing MainWindow: load the Qt Quick
// Main.qml, attach the native frameless chrome, apply Mica, and run. Kept out of
// main.cpp so main.cpp's core init (crash handler, Winsock, libsodium) stays
// untouched and the QML headers don't leak into the Widgets build.

#pragma once

namespace dish {
class AppModel;
}

namespace dish::qml {

// Construct the QML window + chrome and return the app exec() code. `model` is
// the already-started AppModel; it is NOT yet exposed to QML at this migration
// step (the stub screen needs no model), but it is threaded through so the
// wiring point is obvious for the next step.
int runQmlApp(dish::AppModel& model);

} // namespace dish::qml
