// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/TokensBridge.h"

#include <QFontDatabase>

namespace dish::chrome {

TokensBridge::TokensBridge(QObject* parent) : QObject(parent) {}

// The design system asks for the platform-generic monospace (Consolas /
// Cascadia Mono on Windows) rather than shipping a font binary; QFontDatabase
// resolves the same generic the Widgets Theme asked Qt for.
QString TokensBridge::monoFamily() const {
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

} // namespace dish::chrome
