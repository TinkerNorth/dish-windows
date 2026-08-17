// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Strict MAJOR.MINOR.PATCH parsing and ordering for the installer side (the
// updater carries its own copy in src/core/update/UpdateVersion.h by design:
// the two trees must stay independently buildable). Anything that is not three
// dot-separated decimal runs is rejected rather than guessed at, because a
// mis-parsed version silently defeats the downgrade gate.

#pragma once

#include <QString>

#include <optional>

namespace dish::installer {

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;

    bool operator==(const SemVer& o) const {
        return major == o.major && minor == o.minor && patch == o.patch;
    }
    bool operator!=(const SemVer& o) const { return !(*this == o); }
    bool operator<(const SemVer& o) const {
        if (major != o.major) { return major < o.major; }
        if (minor != o.minor) { return minor < o.minor; }
        return patch < o.patch;
    }
    bool operator<=(const SemVer& o) const { return *this < o || *this == o; }
    bool operator>(const SemVer& o) const { return o < *this; }
    bool operator>=(const SemVer& o) const { return o <= *this; }
};

// Grammar: ^\d+\.\d+\.\d+$ (leading zeros allowed, no sign, no "v" prefix, no
// prerelease or build suffix). Components longer than nine digits are rejected
// to keep the int conversion overflow-free.
std::optional<SemVer> parseSemVer(const QString& text);

// strcmp-shaped: negative, zero or positive. nullopt when either side fails the
// strict grammar, so callers must decide what a malformed version means for
// them instead of inheriting a silent "equal".
std::optional<int> compareVersions(const QString& lhs, const QString& rhs);

} // namespace dish::installer
