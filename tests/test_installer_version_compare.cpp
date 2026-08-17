// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The installer's version grammar and the upgrade / repair / downgrade decision
// that hangs off it. A loose grammar here is how "0.10.0" ends up ranked below
// "0.9.0" and a machine silently downgrades itself, so everything that is not
// three decimal runs is rejected rather than guessed at.

#include "installer/VersionCompare.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>

#include <optional>

using dish::installer::compareVersions;
using dish::installer::parseSemVer;
using dish::installer::SemVer;

namespace {

// What the installer does with a payload version over an installed one.
enum class Decision { Upgrade, Repair, Downgrade, Unknown };

Decision decide(const QString& installed, const QString& payload) {
    const auto cmp = compareVersions(payload, installed);
    if (!cmp.has_value()) { return Decision::Unknown; }
    if (*cmp > 0) { return Decision::Upgrade; }
    if (*cmp == 0) { return Decision::Repair; }
    return Decision::Downgrade;
}

} // namespace

TEST_CASE("installer version compare: the strict triple parses", "[installer][version]") {
    REQUIRE(parseSemVer(QStringLiteral("0.0.0")).has_value());
    CHECK(*parseSemVer(QStringLiteral("1.2.3")) == SemVer{1, 2, 3});
    CHECK(*parseSemVer(QStringLiteral("0.1.0")) == SemVer{0, 1, 0});
    // Leading zeros are allowed (they are still decimal runs).
    CHECK(*parseSemVer(QStringLiteral("01.02.03")) == SemVer{1, 2, 3});
    // Nine digits per component is the documented ceiling.
    CHECK(*parseSemVer(QStringLiteral("999999999.0.0")) == SemVer{999999999, 0, 0});
}

TEST_CASE("installer version compare: anything else is rejected", "[installer][version]") {
    for (const char* text : {"", "1", "1.2", "1.2.3.4", "1.2.", ".2.3", "1..3", "v1.2.3",
                             "1.2.3-rc1", "1.2.3+build", " 1.2.3", "1.2.3 ", "1. 2.3", "+1.2.3",
                             "-1.2.3", "1.2.x", "one.two.three", "1234567890.0.0"}) {
        CHECK_FALSE(parseSemVer(QString::fromLatin1(text)).has_value());
    }
    // Unicode digits are digits to QChar::isDigit but not to this grammar.
    CHECK_FALSE(parseSemVer(QStringLiteral("１.２.３")).has_value());
    CHECK_FALSE(parseSemVer(QStringLiteral("١.0.0")).has_value());
}

TEST_CASE("installer version compare: ordering is by triple, not by text", "[installer][version]") {
    CHECK(SemVer{0, 9, 0} < SemVer{0, 10, 0}); // the lexicographic trap
    CHECK(SemVer{1, 0, 0} > SemVer{0, 99, 99});
    CHECK(SemVer{1, 2, 3} == SemVer{1, 2, 3});
    CHECK(SemVer{1, 2, 3} != SemVer{1, 2, 4});
    CHECK(SemVer{1, 2, 3} <= SemVer{1, 2, 3});
    CHECK(SemVer{1, 2, 3} >= SemVer{1, 2, 3});
    CHECK(SemVer{1, 2, 4} > SemVer{1, 2, 3});
    CHECK_FALSE(SemVer{1, 2, 3} < SemVer{1, 2, 3});

    // Every component in precedence order.
    CHECK(SemVer{2, 0, 0} > SemVer{1, 99, 99});
    CHECK(SemVer{1, 3, 0} > SemVer{1, 2, 99});
    CHECK(SemVer{1, 2, 4} > SemVer{1, 2, 3});
}

TEST_CASE("installer version compare: compareVersions is strcmp-shaped", "[installer][version]") {
    CHECK(compareVersions(QStringLiteral("1.2.3"), QStringLiteral("1.2.3")) == 0);
    CHECK(*compareVersions(QStringLiteral("1.2.4"), QStringLiteral("1.2.3")) > 0);
    CHECK(*compareVersions(QStringLiteral("1.2.3"), QStringLiteral("1.2.4")) < 0);
    CHECK(*compareVersions(QStringLiteral("0.10.0"), QStringLiteral("0.9.0")) > 0);
    // Leading zeros do not change the value.
    CHECK(compareVersions(QStringLiteral("1.02.3"), QStringLiteral("1.2.3")) == 0);
}

TEST_CASE("installer version compare: a malformed side is nullopt, never a silent equal",
          "[installer][version]") {
    CHECK_FALSE(compareVersions(QStringLiteral("1.2"), QStringLiteral("1.2.3")).has_value());
    CHECK_FALSE(compareVersions(QStringLiteral("1.2.3"), QStringLiteral("v1.2.3")).has_value());
    CHECK_FALSE(compareVersions(QString(), QString()).has_value());
}

TEST_CASE("installer version compare: the upgrade, repair and downgrade matrix",
          "[installer][version]") {
    CHECK(decide(QStringLiteral("0.1.0"), QStringLiteral("0.2.0")) == Decision::Upgrade);
    CHECK(decide(QStringLiteral("0.1.0"), QStringLiteral("1.0.0")) == Decision::Upgrade);
    CHECK(decide(QStringLiteral("0.9.0"), QStringLiteral("0.10.0")) == Decision::Upgrade);
    // Same version is a REPAIR, and the CI round-trip relies on it exiting 0.
    CHECK(decide(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")) == Decision::Repair);
    CHECK(decide(QStringLiteral("0.10.0"), QStringLiteral("0.9.0")) == Decision::Downgrade);
    CHECK(decide(QStringLiteral("1.0.0"), QStringLiteral("0.99.99")) == Decision::Downgrade);
    CHECK(decide(QStringLiteral("1.2.3"), QStringLiteral("1.2.2")) == Decision::Downgrade);
    // An unreadable recorded version is not an excuse to overwrite an install.
    CHECK(decide(QStringLiteral("garbage"), QStringLiteral("1.0.0")) == Decision::Unknown);
    CHECK(decide(QStringLiteral("1.0.0"), QStringLiteral("garbage")) == Decision::Unknown);
}

TEST_CASE("installer version compare: the update-apply gate refuses payload <= installed",
          "[installer][version]") {
    // Spec H3: --update-apply refuses a payload at or below the installed
    // version (exit 12), with no downgrade override ever passed.
    const auto refuses = [](const char* installed, const char* payload) {
        const auto cmp =
            compareVersions(QString::fromLatin1(payload), QString::fromLatin1(installed));
        return !cmp.has_value() || *cmp <= 0;
    };
    CHECK(refuses("0.2.0", "0.1.0"));
    CHECK(refuses("0.2.0", "0.2.0"));
    CHECK_FALSE(refuses("0.2.0", "0.2.1"));
    CHECK_FALSE(refuses("0.2.0", "99.0.0"));
    CHECK(refuses("0.2.0", "not-a-version"));
}
