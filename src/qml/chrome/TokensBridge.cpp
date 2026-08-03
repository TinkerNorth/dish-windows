// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/TokensBridge.h"

#include "UI/FontStacks.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace dish::chrome {

namespace {

// Without the Win32 probe, motion stays allowed: a missing probe is not a
// stated preference.
bool probeReducedMotion() {
#ifdef Q_OS_WIN
    BOOL animationsOn = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsOn, 0) == FALSE) {
        return false;
    }
    return animationsOn == FALSE;
#else
    return false;
#endif
}

} // namespace

TokensBridge::TokensBridge(QObject* parent)
    : QObject(parent), reducedMotion_(probeReducedMotion()) {}

// Probed by name rather than asking for the platform FixedFont, which resolves
// to Courier New on several Windows configurations.
QString TokensBridge::monoFamily() const { return ui::preferredMonoFamily(); }

QString TokensBridge::sansFamily() const { return ui::preferredSansFamily(); }

void TokensBridge::refreshMotionPreference() {
    const bool next = probeReducedMotion();
    if (next == reducedMotion_) { return; }
    reducedMotion_ = next;
    emit reducedMotionChanged();
}

} // namespace dish::chrome
