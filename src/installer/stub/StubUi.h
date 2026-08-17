// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The stub's three surfaces: the extraction marquee (TaskDialogIndirect, so
// DPI, theming, accessibility and the Cancel label are the OS's problem) and
// the two terminal dialogs. Qt-free by requirement; strings come from the
// 6-locale table in StubStrings.h.

#pragma once

#include <atomic>

namespace dish::installer::stub {

// Blocks the calling thread on the "Preparing Dish Setup" marquee until `done`
// turns true (the dialog closes itself via a timer poll) or the user cancels
// (stores true into `cancel`, then closes). Extraction runs on a worker thread
// meanwhile. If TaskDialogIndirect is unavailable the call returns immediately
// and the flow simply continues without UI.
void runMarqueeUntil(const std::atomic<bool>& done, std::atomic<bool>& cancel);

// "This download is damaged" — trailer/CRC/extraction integrity failures.
// TaskDialog with a MessageBox fallback.
void showDamagedDialog();

// OS gate failure: localized MessageBox (needs Windows 10 1809+ x64).
void showOsTooOldDialog();

} // namespace dish::installer::stub
