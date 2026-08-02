// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/TokensBridge.h"

#include "UI/FontStacks.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace dish::chrome {

namespace {

// The OS "animate controls and elements inside windows" setting, inverted.
// Absent the Win32 API (non-Windows builds) motion is allowed — the safe
// default is the richer one, since a missing probe is not a stated preference.
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

// Probed explicitly (Cascadia Mono -> Consolas -> Segoe UI Mono -> generic)
// rather than asking for the platform FixedFont, which resolves to Courier New
// on several Windows configurations. See ui::preferredMonoFamily.
QString TokensBridge::monoFamily() const { return ui::preferredMonoFamily(); }

// Inter is bundled (main.cpp registers :/fonts) and is the design's UI face;
// the rest of the stack is the Windows system sans.
QString TokensBridge::sansFamily() const { return ui::preferredSansFamily(); }

void TokensBridge::refreshMotionPreference() {
    const bool next = probeReducedMotion();
    if (next == reducedMotion_) { return; }
    reducedMotion_ = next;
    emit reducedMotionChanged();
}

} // namespace dish::chrome
