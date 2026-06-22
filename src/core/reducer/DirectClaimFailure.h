// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// DirectClaimFailure — why a Direct-mode (raw-HID claim) attempt could not be
// completed. Pure, Qt-free port of dish-android source/usb/DirectClaimFailure.kt
// 1:1. Surfaced on the controller card so the user sees the real cause instead
// of a blanket "unplug and replug".

#pragma once

namespace dish::reducer {

enum class DirectClaimFailure {
    // USB permission was refused or revoked before the interface could be opened.
    // (On Windows there is no per-device USB permission prompt the way android
    // has; this maps to ERROR_ACCESS_DENIED on CreateFile / the device being
    // held by a privileged owner.)
    PermissionDenied,
    // Another app or driver holds the interface; the open/claim was rejected
    // (ERROR_SHARING_VIOLATION, or WinUSB initialize refused).
    Busy,
    // The interface was claimed but the controller never produced a decodable
    // report (the read loop could not be brought up).
    InitFailed,
    // The OS detached the device on the claim and never returned it; needs a
    // physical replug.
    Dropped,
};

} // namespace dish::reducer
