// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The CLI grammar of spec section 9, parsed in exactly one place. Two things
// earn extra cases: NSIS's `/D=<dir>` (last argument, unquoted, swallows the
// rest of the line, spaces included) and toArgv(), which is what an elevation
// relaunch re-parses — a lossy serializer there would silently change the
// user's choices between the unelevated and the elevated instance.

#include "installer/CliOptions.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QStringList>
#include <QVector>

#include <initializer_list>
#include <variant>

using dish::installer::CliOptions;
using dish::installer::cliUsageText;
using dish::installer::ClosePolicy;
using dish::installer::Scope;
using Mode = dish::installer::CliOptions::Mode;

namespace {

const QString kSetup = QStringLiteral("dish-setup");
const QString kUninstall = QStringLiteral("uninstall");

CliOptions parsed(const QStringList& argv, const QString& base = kSetup) {
    const auto result = CliOptions::parse(argv, base);
    INFO("argv: " << argv.join(QLatin1Char(' ')).toStdString());
    if (const auto* error = std::get_if<QString>(&result)) {
        INFO("usage error: " << error->toStdString());
        REQUIRE(std::holds_alternative<CliOptions>(result));
    }
    return std::get<CliOptions>(result);
}

QString usageError(const QStringList& argv, const QString& base = kSetup) {
    const auto result = CliOptions::parse(argv, base);
    REQUIRE(std::holds_alternative<QString>(result));
    return std::get<QString>(result);
}

QStringList args(std::initializer_list<const char*> list) {
    QStringList argv;
    for (const char* arg : list) { argv.append(QString::fromLatin1(arg)); }
    return argv;
}

} // namespace

TEST_CASE("installer cli: no arguments is the UI wizard with the documented defaults",
          "[installer][cli]") {
    const CliOptions options = parsed(QStringList{});
    CHECK(options.mode == Mode::UiInstall);
    CHECK(options.plan.scope == Scope::PerUser);
    CHECK(options.plan.installDir.isEmpty());
    CHECK(options.plan.startMenu);
    CHECK_FALSE(options.plan.desktop);
    CHECK(options.plan.launch); // on in the UI
    CHECK(options.plan.closePolicy == ClosePolicy::Abort);
    CHECK_FALSE(options.plan.allowDowngrade);
    CHECK_FALSE(options.purgeUserData);
    CHECK(options.relaunch);
    CHECK_FALSE(options.isSilent());
    CHECK_FALSE(options.isUninstall());
}

TEST_CASE("installer cli: silent spellings and the launch default flip", "[installer][cli]") {
    for (const char* flag : {"/S", "/s", "--silent", "--SILENT"}) {
        const CliOptions options = parsed(args({flag}));
        CHECK(options.mode == Mode::SilentInstall);
        CHECK(options.isSilent());
        CHECK_FALSE(options.plan.launch); // off in every silent mode
    }
    // An explicit --launch overrides the per-mode default in both directions.
    CHECK(parsed(args({"/S", "--launch", "on"})).plan.launch);
    CHECK_FALSE(parsed(args({"--launch", "off"})).plan.launch);
}

TEST_CASE("installer cli: the NSIS /D= form takes the rest of the line", "[installer][cli]") {
    CHECK(parsed(args({"/D=C:\\Dish"})).plan.installDir == QStringLiteral("C:/Dish"));
    // The shell already split on spaces; the remainder is re-joined verbatim.
    CHECK(parsed(args({"/S", "/D=C:\\Program Files\\Dish"})).plan.installDir ==
          QStringLiteral("C:/Program Files/Dish"));
    CHECK(parsed(args({"/d=C:\\Dish"})).plan.installDir == QStringLiteral("C:/Dish"));

    // "Must be last" is not advice: anything after it becomes part of the path,
    // which is the documented NSIS quirk this form inherits.
    const CliOptions swallowed = parsed(args({"/D=C:\\Dish", "--silent"}));
    CHECK(swallowed.plan.installDir == QStringLiteral("C:/Dish --silent"));
    CHECK(swallowed.mode == Mode::UiInstall);

    CHECK(usageError(args({"/D="})).contains(QStringLiteral("directory")));
}

TEST_CASE("installer cli: /D= and --dir may not both be given", "[installer][cli]") {
    CHECK(usageError(args({"--dir", "C:\\A", "/D=C:\\B"})).contains(QStringLiteral("conflict")));
    CHECK(parsed(args({"--dir", "C:\\Program Files\\Dish"})).plan.installDir ==
          QStringLiteral("C:/Program Files/Dish"));
    CHECK(usageError(args({"--dir"})).contains(QStringLiteral("--dir")));
}

TEST_CASE("installer cli: scope, switches and their bad values", "[installer][cli]") {
    CHECK(parsed(args({"--scope", "machine"})).plan.scope == Scope::AllUsers);
    CHECK(parsed(args({"--scope", "USER"})).plan.scope == Scope::PerUser);
    CHECK(usageError(args({"--scope", "everyone"})).contains(QStringLiteral("user|machine")));
    CHECK(usageError(args({"--scope"})).contains(QStringLiteral("user|machine")));

    CHECK_FALSE(parsed(args({"--start-menu", "off"})).plan.startMenu);
    CHECK(parsed(args({"--start-menu", "ON"})).plan.startMenu);
    CHECK(parsed(args({"--desktop", "on"})).plan.desktop);
    CHECK(usageError(args({"--desktop", "yes"})).contains(QStringLiteral("on|off")));
    CHECK(usageError(args({"--start-menu"})).contains(QStringLiteral("on|off")));
    CHECK(usageError(args({"--launch", "1"})).contains(QStringLiteral("on|off")));

    CHECK(parsed(args({"--allow-downgrade"})).plan.allowDowngrade);
}

TEST_CASE("installer cli: the close policy ladder", "[installer][cli]") {
    CHECK(parsed(QStringList{}).plan.closePolicy == ClosePolicy::Abort);
    CHECK(parsed(args({"--closeapps"})).plan.closePolicy == ClosePolicy::Graceful);
    CHECK(parsed(args({"--forceclose"})).plan.closePolicy == ClosePolicy::Force);
    // --forceclose implies --closeapps, in either order.
    CHECK(parsed(args({"--closeapps", "--forceclose"})).plan.closePolicy == ClosePolicy::Force);
    CHECK(parsed(args({"--forceclose", "--closeapps"})).plan.closePolicy == ClosePolicy::Force);
}

TEST_CASE("installer cli: the language override is canonicalized", "[installer][cli]") {
    CHECK(parsed(args({"--lang", "bs"})).langOverride == QStringLiteral("bs"));
    CHECK(parsed(args({"--lang", "PT_br"})).langOverride == QStringLiteral("pt_BR"));
    CHECK(parsed(args({"--lang", "system"})).langOverride == QStringLiteral("system"));
    CHECK(usageError(args({"--lang", "kl"})).contains(QStringLiteral("pt_BR")));
    CHECK(usageError(args({"--lang"})).contains(QStringLiteral("system")));
    CHECK(parsed(QStringList{}).langOverride.isEmpty()); // "" means "not overridden"
}

TEST_CASE("installer cli: log and extract-only take paths", "[installer][cli]") {
    CHECK(parsed(args({"--log", "C:\\Temp\\setup.log"})).logPath ==
          QStringLiteral("C:/Temp/setup.log"));
    CHECK(usageError(args({"--log"})).contains(QStringLiteral("--log")));

    const CliOptions extract = parsed(args({"--extract-only", "C:\\Temp\\image"}));
    CHECK(extract.mode == Mode::ExtractOnly);
    CHECK(extract.extractDir == QStringLiteral("C:/Temp/image"));
    CHECK(extract.isSilent()); // no UI, no registry, no shortcuts
    CHECK(usageError(args({"--extract-only"})).contains(QStringLiteral("directory")));
}

TEST_CASE("installer cli: help and version are handled locally", "[installer][cli]") {
    CHECK(parsed(args({"--version"})).mode == Mode::Version);
    CHECK(parsed(args({"--help"})).mode == Mode::Help);
    CHECK(parsed(args({"/?"})).mode == Mode::Help);
    // Help wins over everything else, so a mistyped install never runs.
    CHECK(parsed(args({"/S", "--help", "--version"})).mode == Mode::Help);
    CHECK(parsed(args({"/S", "--version"})).mode == Mode::Version);

    const QString usage = cliUsageText();
    CHECK(usage.contains(QStringLiteral("/D=<dir>")));
    CHECK(usage.contains(QStringLiteral("--update-apply")));
    CHECK(usage.contains(QStringLiteral("14 version mismatch")));
}

TEST_CASE("installer cli: uninstall mode comes from the basename or the flag", "[installer][cli]") {
    CHECK(parsed(QStringList{}, kUninstall).mode == Mode::UiUninstall);
    CHECK(parsed(args({"/S"}), kUninstall).mode == Mode::SilentUninstall);
    CHECK(parsed(args({"--uninstall"})).mode == Mode::UiUninstall);
    CHECK(parsed(args({"--uninstall", "--silent"})).mode == Mode::SilentUninstall);
    CHECK(parsed(args({"--uninstall"})).isUninstall());

    const CliOptions purge = parsed(args({"--silent", "--purge-user-data"}), kUninstall);
    CHECK(purge.mode == Mode::SilentUninstall);
    CHECK(purge.purgeUserData);

    // Purging is an uninstall concept; asking for it on an install is a usage
    // error rather than a silently ignored flag.
    CHECK(usageError(args({"--purge-user-data"})).contains(QStringLiteral("uninstall option")));
    CHECK(usageError(args({"--uninstall", "--extract-only", "C:\\Temp"}))
              .contains(QStringLiteral("--extract-only")));
}

TEST_CASE("installer cli: the update-apply handoff grammar", "[installer][cli]") {
    const CliOptions options = parsed(args({"--update-apply", "--waitpid", "4242", "--target-exe",
                                            "C:\\Program Files\\Dish\\dish.exe", "--expect-version",
                                            "0.2.0", "--log", "C:\\Temp\\apply.log"}));
    CHECK(options.mode == Mode::UpdateApply);
    CHECK(options.waitPid == 4242u);
    CHECK(options.targetExe == QStringLiteral("C:/Program Files/Dish/dish.exe"));
    CHECK(options.expectVersion == QStringLiteral("0.2.0"));
    CHECK(options.logPath == QStringLiteral("C:/Temp/apply.log"));
    CHECK(options.relaunch); // relaunch is the DEFAULT in this mode
    CHECK(options.isSilent());

    // /S is accepted and redundant (the mode implies silent).
    CHECK(parsed(args({"/S", "--update-apply", "--waitpid", "1", "--target-exe", "C:\\a.exe",
                       "--expect-version", "1.0.0"}))
              .mode == Mode::UpdateApply);
    // CI passes --no-relaunch.
    CHECK_FALSE(parsed(args({"--update-apply", "--waitpid", "1", "--target-exe", "C:\\a.exe",
                             "--expect-version", "1.0.0", "--no-relaunch"}))
                    .relaunch);
}

TEST_CASE("installer cli: update-apply refuses an incomplete or conflicting invocation",
          "[installer][cli]") {
    CHECK(usageError(args({"--update-apply"})).contains(QStringLiteral("--waitpid")));
    CHECK(usageError(args({"--update-apply", "--waitpid", "1", "--target-exe", "C:\\a.exe"}))
              .contains(QStringLiteral("--expect-version")));
    CHECK(usageError(args({"--update-apply", "--waitpid", "1", "--expect-version", "1.0.0"}))
              .contains(QStringLiteral("--target-exe")));

    CHECK(usageError(args({"--waitpid", "0"})).contains(QStringLiteral("process id")));
    CHECK(usageError(args({"--waitpid", "abc"})).contains(QStringLiteral("process id")));
    CHECK(usageError(args({"--waitpid", "4294967296"})).contains(QStringLiteral("process id")));
    CHECK(usageError(args({"--waitpid"})).contains(QStringLiteral("process id")));

    CHECK(usageError(args({"--expect-version", "v1.0.0"})).contains(QStringLiteral("MAJOR")));
    CHECK(usageError(args({"--expect-version", "1.0"})).contains(QStringLiteral("MAJOR")));
    CHECK(usageError(args({"--target-exe"})).contains(QStringLiteral("--target-exe")));

    CHECK(usageError(args({"--update-apply", "--waitpid", "1", "--target-exe", "C:\\a.exe",
                           "--expect-version", "1.0.0", "--uninstall"}))
              .contains(QStringLiteral("uninstall")));
    CHECK(usageError(args({"--update-apply", "--waitpid", "1", "--target-exe", "C:\\a.exe",
                           "--expect-version", "1.0.0", "--extract-only", "C:\\Temp"}))
              .contains(QStringLiteral("--extract-only")));
}

TEST_CASE("installer cli: the internal plumbing flags", "[installer][cli]") {
    const CliOptions options =
        parsed(args({"--staging", "C:\\Temp\\dish-setup-ab12cd34", "--source-exe",
                     "D:\\Downloads\\dish-setup.exe", "--resume-install", "--elevated"}));
    CHECK(options.stagingDir == QStringLiteral("C:/Temp/dish-setup-ab12cd34"));
    CHECK(options.sourceExe == QStringLiteral("D:/Downloads/dish-setup.exe"));
    CHECK(options.resumeInstall);
    CHECK(options.elevated);
    CHECK(usageError(args({"--staging"})).contains(QStringLiteral("--staging")));
    CHECK(usageError(args({"--source-exe"})).contains(QStringLiteral("--source-exe")));
}

TEST_CASE("installer cli: the stub's separator is accepted and ignored", "[installer][cli]") {
    // The stub appends `-- <verbatim original tail>`; the tail is parsed as
    // ordinary arguments, and the separator itself must not be one.
    const CliOptions options =
        parsed(args({"--staging", "C:\\Temp\\s", "--", "/S", "--desktop", "on"}));
    CHECK(options.mode == Mode::SilentInstall);
    CHECK(options.plan.desktop);
}

TEST_CASE("installer cli: an unknown argument names itself", "[installer][cli]") {
    CHECK(usageError(args({"--reinstall-everything"}))
              .contains(QStringLiteral("unknown argument: --reinstall-everything")));
    CHECK(usageError(args({"C:\\some\\stray\\path"})).contains(QStringLiteral("unknown argument")));
    CHECK(usageError(args({"-S"})).contains(QStringLiteral("unknown argument")));
}

TEST_CASE("installer cli: toArgv round-trips every CLI-expressible option", "[installer][cli]") {
    const QVector<QStringList> invocations{
        QStringList{},
        args({"/S"}),
        args({"/S", "--scope", "machine", "--dir", "C:\\Program Files\\Dish", "--start-menu", "off",
              "--desktop", "on", "--launch", "on", "--forceclose", "--allow-downgrade", "--lang",
              "bs", "--log", "C:\\Temp\\setup.log"}),
        args({"--closeapps", "--desktop", "on"}),
        args({"--extract-only", "C:\\Temp\\image", "--log", "C:\\Temp\\x.log"}),
        args({"--update-apply", "--waitpid", "4242", "--target-exe", "C:\\App\\dish.exe",
              "--expect-version", "0.2.0", "--no-relaunch", "--log", "C:\\Temp\\apply.log"}),
        args({"--uninstall", "--silent", "--purge-user-data", "--closeapps"}),
        args({"--uninstall"}),
        args({"--version"}),
        args({"--help"}),
        args({"--staging", "C:\\Temp\\s", "--source-exe", "C:\\dl\\dish-setup.exe",
              "--resume-install", "--elevated"}),
    };

    for (const QStringList& argv : invocations) {
        const CliOptions original = parsed(argv);
        const QStringList serialized = original.toArgv();
        const CliOptions again = parsed(serialized);
        CHECK(again == original);
        // Canonical form: an uninstall serializes its mode explicitly, so the
        // relaunch keeps it whichever basename it is spawned under.
        if (original.isUninstall()) { CHECK(parsed(serialized, kUninstall) == original); }
    }
}

TEST_CASE("installer cli: toArgv spells the elevation relaunch out in full", "[installer][cli]") {
    CliOptions options =
        parsed(args({"--scope", "machine", "--dir", "C:\\Program Files\\Dish", "--desktop", "on"}));
    options.elevated = true;
    const QStringList argv = options.toArgv();

    CHECK(argv.contains(QStringLiteral("--scope")));
    CHECK(argv.contains(QStringLiteral("machine")));
    CHECK(argv.contains(QStringLiteral("--dir")));
    CHECK(argv.contains(QStringLiteral("C:\\Program Files\\Dish"))); // native, for the shell
    CHECK(argv.contains(QStringLiteral("--elevated")));
    // Booleans are always explicit, so a default that changes later cannot
    // rewrite a running install's choices.
    CHECK(argv.contains(QStringLiteral("--start-menu")));
    CHECK(argv.contains(QStringLiteral("--desktop")));
    CHECK(argv.contains(QStringLiteral("--launch")));
    CHECK(parsed(argv) == options);
}

TEST_CASE("installer cli: probe-derived plan fields are not CLI surface", "[installer][cli]") {
    CliOptions options = parsed(args({"/S"}));
    options.plan.isUpgrade = true;
    options.plan.existingVersion = QStringLiteral("0.1.0");
    options.plan.existingDir = QStringLiteral("C:/App");

    // They are re-derived by the elevated instance's own probe, so the round
    // trip deliberately drops them rather than trusting a command line.
    const CliOptions again = parsed(options.toArgv());
    CHECK_FALSE(again.plan.isUpgrade);
    CHECK(again.plan.existingVersion.isEmpty());
    CHECK(again.plan.existingDir.isEmpty());
}
