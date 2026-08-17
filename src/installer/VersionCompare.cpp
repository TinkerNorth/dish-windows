// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/VersionCompare.h"

#include <QStringList>

namespace dish::installer {

namespace {

// A run of 1..9 ASCII digits; anything else (including Unicode digits, which
// QChar::isDigit would accept) fails.
std::optional<int> parseComponent(const QString& part) {
    if (part.isEmpty() || part.size() > 9) { return std::nullopt; }
    int value = 0;
    for (const QChar ch : part) {
        if (ch < QLatin1Char('0') || ch > QLatin1Char('9')) { return std::nullopt; }
        value = value * 10 + (ch.unicode() - u'0');
    }
    return value;
}

} // namespace

std::optional<SemVer> parseSemVer(const QString& text) {
    const QStringList parts = text.split(QLatin1Char('.'));
    if (parts.size() != 3) { return std::nullopt; }
    const auto major = parseComponent(parts[0]);
    const auto minor = parseComponent(parts[1]);
    const auto patch = parseComponent(parts[2]);
    if (!major || !minor || !patch) { return std::nullopt; }
    return SemVer{*major, *minor, *patch};
}

std::optional<int> compareVersions(const QString& lhs, const QString& rhs) {
    const auto a = parseSemVer(lhs);
    const auto b = parseSemVer(rhs);
    if (!a || !b) { return std::nullopt; }
    if (*a < *b) { return -1; }
    if (*b < *a) { return 1; }
    return 0;
}

} // namespace dish::installer
