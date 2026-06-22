// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PathChoice — a user's explicit per-model USB input-path override. Pure,
// Qt-free port of dish-android source/usb/PathChoice.kt 1:1.
//
// Two values: Direct (claim the raw HID device ourselves) and Standard (let the
// framework — SDL/XInput on Windows — own the pad). The ABSENCE of a stored
// value means Auto: a verified fast-lane model resolves to Direct, everything
// else to Standard (see resolvePathChoice in UsbPathMachine.h). Auto is
// therefore represented as std::nullopt at the storage boundary, never as a
// third enum value — exactly mirroring the Kotlin `PathChoice?` shape.
//
// Storage values are persisted verbatim in the per-VID:PID preference store;
// add values, never rename an existing one. An unrecognised stored value (a
// constant written by a newer build) decodes back to std::nullopt (Auto) for
// forward-compat — the PathChoiceTest pins this.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace dish::reducer {

enum class PathChoice {
    Direct,
    Standard,
};

// Persisted in the cloud-backed user preferences on android; here, in QSettings.
// Add values, never rename existing ones.
inline constexpr std::string_view kPathChoiceStorageDirect = "direct";
inline constexpr std::string_view kPathChoiceStorageStandard = "standard";

inline std::string toStorageValue(PathChoice choice) {
    switch (choice) {
    case PathChoice::Direct:
        return std::string(kPathChoiceStorageDirect);
    case PathChoice::Standard:
        return std::string(kPathChoiceStorageStandard);
    }
    return std::string(kPathChoiceStorageStandard); // unreachable; total switch
}

// Decode a stored value back to a PathChoice. An absent (nullopt in, e.g. the
// store never wrote one) or unrecognised value resolves to std::nullopt = Auto.
inline std::optional<PathChoice> pathChoiceFromStorageValue(std::string_view value) {
    if (value == kPathChoiceStorageDirect) { return PathChoice::Direct; }
    if (value == kPathChoiceStorageStandard) { return PathChoice::Standard; }
    return std::nullopt;
}

} // namespace dish::reducer
