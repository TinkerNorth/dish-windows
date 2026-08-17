// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The two manifests. Parsing is strict on purpose: manifest.json drives every
// copy, every hash check, the progress arithmetic, EstimatedSize and the
// uninstall file list, and it can arrive from a tampered image — so a manifest
// that fails ANY structural rule yields nullopt rather than a half-trusted
// object. These cases are the rule list.

#include "installer/Manifest.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QVector>

using dish::installer::InstalledManifest;
using dish::installer::isSafeRelativePath;
using dish::installer::PayloadEntry;
using dish::installer::PayloadManifest;

namespace {

const QByteArray kShaA = QByteArray(64, 'a');
const QByteArray kShaB = QByteArray(64, 'b');

PayloadEntry entry(const QString& path, qint64 size, const QByteArray& sha,
                   const QString& stagedAs = QString()) {
    PayloadEntry e;
    e.path = path;
    e.stagedAs = stagedAs.isEmpty() ? path : stagedAs;
    e.size = size;
    e.sha256Hex = sha;
    return e;
}

PayloadManifest payload() {
    PayloadManifest m;
    m.version = QStringLiteral("0.2.0");
    m.files = QVector<PayloadEntry>{
        entry(QStringLiteral("dish.exe"), 1000, kShaA),
        // The one aliased entry (spec D9): the ~1.5 MB UI exe is stored once.
        entry(QStringLiteral("uninstall.exe"), 500, kShaB, QStringLiteral("dish-setup-ui.exe")),
        entry(QStringLiteral("licenses/LICENSE.LGPL-3.0.txt"), 20, kShaA),
    };
    m.totalBytes = 1520;
    return m;
}

InstalledManifest installed() {
    InstalledManifest m;
    m.version = QStringLiteral("0.2.0");
    m.installDir = QStringLiteral("C:/Program Files/Dish");
    m.scope = QStringLiteral("machine");
    m.startMenu = true;
    m.desktop = true;
    m.shortcutPaths =
        QStringList{QStringLiteral("C:/ProgramData/Microsoft/Windows/Start Menu/Programs/Dish.lnk"),
                    QStringLiteral("C:/Users/Public/Desktop/Dish.lnk")};
    m.installedUtc = QStringLiteral("2026-08-04T09:15:00Z");
    m.files = QVector<PayloadEntry>{entry(QStringLiteral("dish.exe"), 1000, kShaA),
                                    entry(QStringLiteral("uninstall.exe"), 500, kShaB)};
    return m;
}

QByteArray payloadJson(const QByteArray& files, const QByteArray& version = "0.2.0",
                       const QByteArray& schema = "1", const QByteArray& total = "1520") {
    return "{\"schema\":" + schema + ",\"version\":\"" + version + "\",\"totalBytes\":" + total +
           ",\"files\":" + files + "}";
}

} // namespace

TEST_CASE("installer manifest: safe relative paths", "[installer][manifest]") {
    CHECK(isSafeRelativePath(QStringLiteral("dish.exe")));
    CHECK(isSafeRelativePath(QStringLiteral("licenses/LICENSE.LGPL-3.0.txt")));
    CHECK(isSafeRelativePath(QStringLiteral("qml/QtQuick/Controls/Basic/qmldir")));
    CHECK(isSafeRelativePath(QStringLiteral("a b/c d.txt"))); // spaces inside a segment are fine
}

TEST_CASE("installer manifest: hostile and unusable paths are rejected", "[installer][manifest]") {
    CHECK_FALSE(isSafeRelativePath(QString()));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("/absolute.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("trailing/")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("double//slash.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("..")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("../escape.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("a/../b.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("./here.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("back\\slash.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("C:/drive.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("stream.txt:hidden")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("wild*card.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("quo\"te.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("pipe|.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("lt<gt>.txt")));
    // Windows silently strips a trailing dot or space, so the verified name and
    // the created name would disagree.
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("trailingdot.")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("trailingspace ")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("dir./file.txt")));
    // Entry names are enforced ASCII by the format.
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("caf\u00E9.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("\u65E5\u672C.txt")));
    CHECK_FALSE(isSafeRelativePath(QStringLiteral("bell\a.txt")));
}

TEST_CASE("installer manifest: the payload manifest round-trips", "[installer][manifest]") {
    const PayloadManifest original = payload();
    const QByteArray json = original.toJson();
    const auto parsed = PayloadManifest::fromJson(json);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == original);
    CHECK(parsed->files.size() == 3);
    CHECK(parsed->totalBytes == 1520);
}

TEST_CASE("installer manifest: stagedAs is written only for a real alias",
          "[installer][manifest]") {
    const QByteArray json = payload().toJson();
    CHECK(json.contains("\"stagedAs\": \"dish-setup-ui.exe\""));
    CHECK(json.count("stagedAs") == 1); // the unaliased entries stay lean

    const auto parsed = PayloadManifest::fromJson(json);
    REQUIRE(parsed.has_value());
    // An absent stagedAs defaults to the installed path.
    CHECK(parsed->files.at(0).stagedAs == QStringLiteral("dish.exe"));
    CHECK(parsed->files.at(1).stagedAs == QStringLiteral("dish-setup-ui.exe"));
    CHECK(parsed->files.at(1).path == QStringLiteral("uninstall.exe"));
}

TEST_CASE("installer manifest: serialization is stable and LF-terminated",
          "[installer][manifest]") {
    const QByteArray json = payload().toJson();
    CHECK(json.endsWith('\n'));
    CHECK_FALSE(json.contains('\r'));
    // Byte-identical on a re-serialize of the parsed form: the pack tool's
    // output and the engine's are the same document.
    const auto parsed = PayloadManifest::fromJson(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->toJson() == json);
}

TEST_CASE("installer manifest: the payload manifest rejects every structural break",
          "[installer][manifest]") {
    SECTION("not an object") {
        CHECK_FALSE(PayloadManifest::fromJson(QByteArray("[]")).has_value());
        CHECK_FALSE(PayloadManifest::fromJson(QByteArray("<html>")).has_value());
        CHECK_FALSE(PayloadManifest::fromJson(QByteArray()).has_value());
    }
    SECTION("a schema this build does not speak") {
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson("[]", "0.2.0", "2", "0")).has_value());
        CHECK_FALSE(
            PayloadManifest::fromJson(payloadJson("[]", "0.2.0", "\"1\"", "0")).has_value());
    }
    SECTION("a version that is not a strict triple") {
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson("[]", "v0.2.0", "1", "0")).has_value());
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson("[]", "0.2", "1", "0")).has_value());
        CHECK_FALSE(
            PayloadManifest::fromJson(payloadJson("[]", "0.2.0-rc1", "1", "0")).has_value());
    }
    SECTION("totals that disagree with the entries") {
        const QByteArray files = "[{\"path\":\"a.txt\",\"size\":10,\"sha256\":\"" + kShaA + "\"}]";
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson(files, "0.2.0", "1", "11")).has_value());
        CHECK(PayloadManifest::fromJson(payloadJson(files, "0.2.0", "1", "10")).has_value());
        CHECK_FALSE(
            PayloadManifest::fromJson(payloadJson(files, "0.2.0", "1", "\"10\"")).has_value());
    }
    SECTION("an unsafe or duplicated entry path") {
        const QByteArray escape =
            "[{\"path\":\"../evil.txt\",\"size\":0,\"sha256\":\"" + kShaA + "\"}]";
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson(escape, "0.2.0", "1", "0")).has_value());
        const QByteArray duplicate = "[{\"path\":\"a.txt\",\"size\":0,\"sha256\":\"" + kShaA +
                                     "\"},{\"path\":\"a.txt\",\"size\":0,\"sha256\":\"" + kShaA +
                                     "\"}]";
        CHECK_FALSE(
            PayloadManifest::fromJson(payloadJson(duplicate, "0.2.0", "1", "0")).has_value());
    }
    SECTION("an unsafe stagedAs") {
        const QByteArray alias = "[{\"path\":\"uninstall.exe\",\"stagedAs\":\"../ui.exe\","
                                 "\"size\":0,\"sha256\":\"" +
                                 kShaA + "\"}]";
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson(alias, "0.2.0", "1", "0")).has_value());
    }
    SECTION("a malformed hash") {
        for (const QByteArray& sha :
             {QByteArray(63, 'a'), QByteArray(64, 'A'), QByteArray(64, 'z'), QByteArray()}) {
            const QByteArray files = "[{\"path\":\"a.txt\",\"size\":0,\"sha256\":\"" + sha + "\"}]";
            CHECK_FALSE(
                PayloadManifest::fromJson(payloadJson(files, "0.2.0", "1", "0")).has_value());
        }
    }
    SECTION("a negative or non-numeric size") {
        const QByteArray negative =
            "[{\"path\":\"a.txt\",\"size\":-1,\"sha256\":\"" + kShaA + "\"}]";
        CHECK_FALSE(
            PayloadManifest::fromJson(payloadJson(negative, "0.2.0", "1", "-1")).has_value());
        const QByteArray text =
            "[{\"path\":\"a.txt\",\"size\":\"10\",\"sha256\":\"" + kShaA + "\"}]";
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson(text, "0.2.0", "1", "10")).has_value());
    }
    SECTION("files is not an array of objects") {
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson("{}", "0.2.0", "1", "0")).has_value());
        CHECK_FALSE(PayloadManifest::fromJson(payloadJson("[1,2]", "0.2.0", "1", "0")).has_value());
        CHECK_FALSE(PayloadManifest::fromJson("{\"schema\":1,\"version\":\"0.2.0\","
                                              "\"totalBytes\":0}")
                        .has_value());
    }
}

TEST_CASE("installer manifest: an empty file list with a zero total is valid",
          "[installer][manifest]") {
    const auto parsed = PayloadManifest::fromJson(payloadJson("[]", "0.2.0", "1", "0"));
    REQUIRE(parsed.has_value());
    CHECK(parsed->files.isEmpty());
    CHECK(parsed->totalBytes == 0);
}

TEST_CASE("installer manifest: the installed manifest round-trips", "[installer][manifest]") {
    const InstalledManifest original = installed();
    const QByteArray json = original.toJson();
    const auto parsed = InstalledManifest::fromJson(json);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == original);
    CHECK(parsed->scope == QStringLiteral("machine"));
    CHECK(parsed->shortcutPaths.size() == 2);
    CHECK(parsed->installedUtc == QStringLiteral("2026-08-04T09:15:00Z"));
    CHECK(json.endsWith('\n'));
    CHECK_FALSE(json.contains('\r'));
}

TEST_CASE("installer manifest: the installed manifest carries no alias", "[installer][manifest]") {
    // The installed set is what is ON DISK; stagedAs only means something
    // inside the extracted image.
    CHECK_FALSE(installed().toJson().contains("stagedAs"));
    const QByteArray json = "{\"schema\":1,\"version\":\"0.2.0\",\"installDir\":\"C:/App\","
                            "\"scope\":\"user\",\"startMenu\":true,\"desktop\":false,"
                            "\"shortcutPaths\":[],\"installedUtc\":\"\","
                            "\"files\":[{\"path\":\"uninstall.exe\",\"stagedAs\":\"x.exe\","
                            "\"size\":1,\"sha256\":\"" +
                            kShaA + "\"}]}";
    CHECK_FALSE(InstalledManifest::fromJson(json).has_value());
}

TEST_CASE("installer manifest: the installed manifest rejects a broken record",
          "[installer][manifest]") {
    const QByteArray base = "{\"schema\":1,\"version\":\"%1\",\"installDir\":\"%2\","
                            "\"scope\":\"%3\",\"files\":[]}";
    const auto build = [&base](const QByteArray& version, const QByteArray& dir,
                               const QByteArray& scope) {
        QByteArray json = base;
        json.replace("%1", version).replace("%2", dir).replace("%3", scope);
        return json;
    };

    CHECK(InstalledManifest::fromJson(build("0.2.0", "C:/App", "user")).has_value());
    CHECK_FALSE(InstalledManifest::fromJson(build("0.2", "C:/App", "user")).has_value());
    CHECK_FALSE(InstalledManifest::fromJson(build("0.2.0", "", "user")).has_value());
    CHECK_FALSE(InstalledManifest::fromJson(build("0.2.0", "C:/App", "everyone")).has_value());
    CHECK_FALSE(InstalledManifest::fromJson(build("0.2.0", "C:/App", "")).has_value());
    CHECK_FALSE(
        InstalledManifest::fromJson("{\"schema\":2,\"version\":\"0.2.0\",\"installDir\":\"C:/App\","
                                    "\"scope\":\"user\",\"files\":[]}")
            .has_value());
    // shortcutPaths, when present, must be an array of strings.
    CHECK_FALSE(InstalledManifest::fromJson("{\"schema\":1,\"version\":\"0.2.0\","
                                            "\"installDir\":\"C:/App\",\"scope\":\"user\","
                                            "\"shortcutPaths\":\"C:/x.lnk\",\"files\":[]}")
                    .has_value());
    CHECK_FALSE(InstalledManifest::fromJson("{\"schema\":1,\"version\":\"0.2.0\","
                                            "\"installDir\":\"C:/App\",\"scope\":\"user\","
                                            "\"shortcutPaths\":[1],\"files\":[]}")
                    .has_value());
}

TEST_CASE("installer manifest: a recorded shortcut path must name a Dish.lnk",
          "[installer][manifest]") {
    // Every consumer of shortcutPaths DELETES what it names, and for a machine
    // record that delete runs elevated. The record still decides WHICH directory
    // (a known folder that moved after the install keeps resolving to where the
    // record says); only the leaf is pinned.
    using dish::installer::isSafeShortcutPath;
    CHECK(isSafeShortcutPath(
        QStringLiteral("C:/ProgramData/Microsoft/Windows/Start Menu/Programs/Dish.lnk")));
    CHECK(isSafeShortcutPath(QStringLiteral("C:\\Users\\u\\Desktop\\Dish.lnk")));
    CHECK(isSafeShortcutPath(QStringLiteral("//fileserver/profiles/u/Desktop/Dish.lnk")));
    CHECK(isSafeShortcutPath(QStringLiteral("c:/x/dish.LNK"))); // NTFS folds case

    CHECK_FALSE(isSafeShortcutPath(QStringLiteral("C:/Users/u/Documents/thesis.docx")));
    CHECK_FALSE(isSafeShortcutPath(QStringLiteral("C:/Windows/System32/drivers/etc/hosts")));
    CHECK_FALSE(isSafeShortcutPath(QStringLiteral("C:/x/Dish.lnk.exe")));
    CHECK_FALSE(isSafeShortcutPath(QStringLiteral("Dish.lnk")));        // relative
    CHECK_FALSE(isSafeShortcutPath(QStringLiteral("C:Dish.lnk")));      // drive-relative
    CHECK_FALSE(isSafeShortcutPath(QStringLiteral("/x/Dish.lnk")));     // rooted, no drive
    CHECK_FALSE(isSafeShortcutPath(QStringLiteral("C:/x/*/Dish.lnk"))); // wildcard
    CHECK_FALSE(isSafeShortcutPath(QString()));

    const auto withShortcuts = [](const QByteArray& array) {
        return InstalledManifest::fromJson("{\"schema\":1,\"version\":\"0.2.0\","
                                           "\"installDir\":\"C:/App\",\"scope\":\"machine\","
                                           "\"shortcutPaths\":" +
                                           array + ",\"files\":[]}");
    };
    CHECK(withShortcuts("[\"C:/Menu/Dish.lnk\"]").has_value());
    // One bad entry rejects the whole record, exactly like an unsafe `files`
    // entry: a half-trusted uninstall record is worse than none.
    CHECK_FALSE(withShortcuts("[\"C:/Menu/Dish.lnk\",\"C:/Users/u/taxes.pdf\"]").has_value());
    CHECK_FALSE(withShortcuts("[\"C:/Windows/System32/kernel32.dll\"]").has_value());
}

TEST_CASE("installer manifest: the recorded switch defaults survive an older record",
          "[installer][manifest]") {
    // A record written before a field existed reads as the documented default
    // rather than as "off".
    const auto parsed = InstalledManifest::fromJson(
        "{\"schema\":1,\"version\":\"0.2.0\",\"installDir\":\"C:/App\",\"scope\":\"user\","
        "\"files\":[]}");
    REQUIRE(parsed.has_value());
    CHECK(parsed->startMenu);
    CHECK_FALSE(parsed->desktop);
    CHECK(parsed->shortcutPaths.isEmpty());
}

TEST_CASE("installer manifest: entry equality covers the alias and the hash",
          "[installer][manifest]") {
    const PayloadEntry base = entry(QStringLiteral("a.txt"), 1, kShaA);
    CHECK(base == entry(QStringLiteral("a.txt"), 1, kShaA));
    CHECK(base != entry(QStringLiteral("a.txt"), 2, kShaA));
    CHECK(base != entry(QStringLiteral("a.txt"), 1, kShaB));
    CHECK(base != entry(QStringLiteral("b.txt"), 1, kShaA));
    CHECK(base != entry(QStringLiteral("a.txt"), 1, kShaA, QStringLiteral("staged.txt")));
}
