// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/CliOptions.h"

#include "installer/VersionCompare.h"

#include <QDir>

namespace dish::installer {

namespace {

// The catalogue codes SetupMain can actually load, plus "system".
const char* const kLangCodes[] = {"system", "en", "bs", "de", "es", "fr", "pt_BR"};

bool matches(const QString& arg, const char* flag) {
    return arg.compare(QLatin1String(flag), Qt::CaseInsensitive) == 0;
}

std::optional<bool> parseOnOff(const QString& value) {
    if (value.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0) { return true; }
    if (value.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0) { return false; }
    return std::nullopt;
}

QString onOff(bool value) { return value ? QStringLiteral("on") : QStringLiteral("off"); }

std::optional<QString> canonicalLang(const QString& value) {
    for (const char* code : kLangCodes) {
        if (value.compare(QLatin1String(code), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(code);
        }
    }
    return std::nullopt;
}

} // namespace

std::variant<CliOptions, QString> CliOptions::parse(const QStringList& argv,
                                                    const QString& exeBaseName) {
    CliOptions options;
    bool silent = false;
    bool uninstallFlag = false;
    bool updateApplyFlag = false;
    bool helpFlag = false;
    bool versionFlag = false;
    bool launchSet = false;
    bool launchValue = false;
    bool closeApps = false;
    bool forceClose = false;
    bool sawSlashD = false;
    bool sawDirFlag = false;

    const auto usage = [](const QString& text) -> std::variant<CliOptions, QString> {
        return text;
    };

    for (int i = 0; i < argv.size(); ++i) {
        const QString arg = argv.at(i);
        const auto value = [&]() -> std::optional<QString> {
            if (i + 1 >= argv.size()) { return std::nullopt; }
            ++i;
            return argv.at(i);
        };

        if (matches(arg, "/S") || matches(arg, "--silent")) {
            silent = true;
        } else if (arg.startsWith(QLatin1String("/D="), Qt::CaseInsensitive)) {
            // NSIS compat: must be last; the unquoted remainder of the line is
            // the directory. The shell already split on spaces, so re-join
            // every remaining token with single spaces (runs of spaces cannot
            // survive argv — a documented /D= limitation NSIS shares).
            QString dir = arg.mid(3);
            for (int j = i + 1; j < argv.size(); ++j) {
                dir += QLatin1Char(' ');
                dir += argv.at(j);
            }
            i = argv.size();
            if (dir.isEmpty()) { return usage(QStringLiteral("/D= needs a directory")); }
            options.plan.installDir = QDir::fromNativeSeparators(dir);
            sawSlashD = true;
        } else if (matches(arg, "--dir")) {
            const auto v = value();
            if (!v) { return usage(QStringLiteral("--dir needs a directory")); }
            options.plan.installDir = QDir::fromNativeSeparators(*v);
            sawDirFlag = true;
        } else if (matches(arg, "--scope")) {
            const auto v = value();
            const auto scope = v ? scopeFromToken(*v) : std::nullopt;
            if (!scope) { return usage(QStringLiteral("--scope needs user|machine")); }
            options.plan.scope = *scope;
        } else if (matches(arg, "--start-menu")) {
            const auto v = value();
            const auto onOffValue = v ? parseOnOff(*v) : std::nullopt;
            if (!onOffValue) { return usage(QStringLiteral("--start-menu needs on|off")); }
            options.plan.startMenu = *onOffValue;
        } else if (matches(arg, "--desktop")) {
            const auto v = value();
            const auto onOffValue = v ? parseOnOff(*v) : std::nullopt;
            if (!onOffValue) { return usage(QStringLiteral("--desktop needs on|off")); }
            options.plan.desktop = *onOffValue;
        } else if (matches(arg, "--launch")) {
            const auto v = value();
            const auto onOffValue = v ? parseOnOff(*v) : std::nullopt;
            if (!onOffValue) { return usage(QStringLiteral("--launch needs on|off")); }
            launchSet = true;
            launchValue = *onOffValue;
        } else if (matches(arg, "--closeapps")) {
            closeApps = true;
        } else if (matches(arg, "--forceclose")) {
            forceClose = true;
        } else if (matches(arg, "--allow-downgrade")) {
            options.plan.allowDowngrade = true;
        } else if (matches(arg, "--lang")) {
            const auto v = value();
            const auto lang = v ? canonicalLang(*v) : std::nullopt;
            if (!lang) { return usage(QStringLiteral("--lang needs system|en|bs|de|es|fr|pt_BR")); }
            options.langOverride = *lang;
        } else if (matches(arg, "--log")) {
            const auto v = value();
            if (!v || v->isEmpty()) { return usage(QStringLiteral("--log needs a file path")); }
            options.logPath = QDir::fromNativeSeparators(*v);
        } else if (matches(arg, "--extract-only")) {
            const auto v = value();
            if (!v || v->isEmpty()) {
                return usage(QStringLiteral("--extract-only needs a directory"));
            }
            options.extractDir = QDir::fromNativeSeparators(*v);
        } else if (matches(arg, "--version")) {
            versionFlag = true;
        } else if (matches(arg, "--help") || matches(arg, "/?")) {
            helpFlag = true;
        } else if (matches(arg, "--update-apply")) {
            updateApplyFlag = true;
        } else if (matches(arg, "--waitpid")) {
            const auto v = value();
            bool ok = false;
            const qulonglong pid = v ? v->toULongLong(&ok) : 0;
            if (!ok || pid == 0 || pid > 0xFFFFFFFFull) {
                return usage(QStringLiteral("--waitpid needs a process id"));
            }
            options.waitPid = static_cast<quint32>(pid);
        } else if (matches(arg, "--target-exe")) {
            const auto v = value();
            if (!v || v->isEmpty()) { return usage(QStringLiteral("--target-exe needs a path")); }
            options.targetExe = QDir::fromNativeSeparators(*v);
        } else if (matches(arg, "--expect-version")) {
            const auto v = value();
            if (!v || !parseSemVer(*v)) {
                return usage(QStringLiteral("--expect-version needs MAJOR.MINOR.PATCH"));
            }
            options.expectVersion = *v;
        } else if (matches(arg, "--no-relaunch")) {
            options.relaunch = false;
        } else if (matches(arg, "--uninstall")) {
            uninstallFlag = true;
        } else if (matches(arg, "--purge-user-data")) {
            options.purgeUserData = true;
        } else if (matches(arg, "--staging")) {
            const auto v = value();
            if (!v || v->isEmpty()) { return usage(QStringLiteral("--staging needs a directory")); }
            options.stagingDir = QDir::fromNativeSeparators(*v);
        } else if (matches(arg, "--source-exe")) {
            const auto v = value();
            if (!v || v->isEmpty()) { return usage(QStringLiteral("--source-exe needs a path")); }
            options.sourceExe = QDir::fromNativeSeparators(*v);
        } else if (matches(arg, "--resume-install")) {
            options.resumeInstall = true;
        } else if (matches(arg, "--elevated")) {
            options.elevated = true;
        } else if (arg == QLatin1String("--")) {
            // The stub's separator before the verbatim original tail; the tail
            // itself was already appended as ordinary arguments.
        } else {
            return usage(QStringLiteral("unknown argument: ") + arg);
        }
    }

    if (sawSlashD && sawDirFlag) {
        return usage(QStringLiteral("/D= and --dir conflict; pass one"));
    }
    if (forceClose) { closeApps = true; }
    options.plan.closePolicy = forceClose  ? ClosePolicy::Force
                               : closeApps ? ClosePolicy::Graceful
                                           : ClosePolicy::Abort;

    const bool uninstall = uninstallFlag || exeBaseName == QLatin1String("uninstall");
    if (helpFlag) {
        options.mode = Mode::Help;
    } else if (versionFlag) {
        options.mode = Mode::Version;
    } else if (updateApplyFlag) {
        if (uninstall) { return usage(QStringLiteral("--update-apply conflicts with uninstall")); }
        if (!options.extractDir.isEmpty()) {
            return usage(QStringLiteral("--update-apply conflicts with --extract-only"));
        }
        if (options.waitPid == 0 || options.targetExe.isEmpty() ||
            options.expectVersion.isEmpty()) {
            return usage(QStringLiteral(
                "--update-apply needs --waitpid, --target-exe and --expect-version"));
        }
        options.mode = Mode::UpdateApply;
    } else if (uninstall) {
        if (!options.extractDir.isEmpty()) {
            return usage(QStringLiteral("--extract-only conflicts with uninstall"));
        }
        options.mode = silent ? Mode::SilentUninstall : Mode::UiUninstall;
    } else if (!options.extractDir.isEmpty()) {
        options.mode = Mode::ExtractOnly;
    } else {
        options.mode = silent ? Mode::SilentInstall : Mode::UiInstall;
    }

    if (options.purgeUserData && !options.isUninstall() && options.mode != Mode::Help &&
        options.mode != Mode::Version) {
        return usage(QStringLiteral("--purge-user-data is an uninstall option"));
    }

    // Launch default: on in the UI wizard, off in every silent mode (spec 9).
    options.plan.launch = launchSet ? launchValue : options.mode == Mode::UiInstall;
    return options;
}

QStringList CliOptions::toArgv() const {
    QStringList argv;
    switch (mode) {
    case Mode::Help:
        argv << QStringLiteral("--help");
        return argv;
    case Mode::Version:
        argv << QStringLiteral("--version");
        return argv;
    case Mode::UpdateApply:
        argv << QStringLiteral("--update-apply");
        break;
    case Mode::ExtractOnly:
        argv << QStringLiteral("--extract-only") << QDir::toNativeSeparators(extractDir);
        break;
    case Mode::SilentInstall:
        argv << QStringLiteral("--silent");
        break;
    case Mode::UiUninstall:
        argv << QStringLiteral("--uninstall");
        break;
    case Mode::SilentUninstall:
        argv << QStringLiteral("--uninstall") << QStringLiteral("--silent");
        break;
    case Mode::UiInstall:
        break;
    }

    if (mode == Mode::UpdateApply) {
        argv << QStringLiteral("--waitpid") << QString::number(waitPid);
        argv << QStringLiteral("--target-exe") << QDir::toNativeSeparators(targetExe);
        argv << QStringLiteral("--expect-version") << expectVersion;
        if (!relaunch) { argv << QStringLiteral("--no-relaunch"); }
    } else if (mode != Mode::ExtractOnly) {
        argv << QStringLiteral("--scope") << scopeToken(plan.scope);
        if (!plan.installDir.isEmpty()) {
            argv << QStringLiteral("--dir") << QDir::toNativeSeparators(plan.installDir);
        }
        if (!isUninstall()) {
            argv << QStringLiteral("--start-menu") << onOff(plan.startMenu);
            argv << QStringLiteral("--desktop") << onOff(plan.desktop);
            argv << QStringLiteral("--launch") << onOff(plan.launch);
            if (plan.allowDowngrade) { argv << QStringLiteral("--allow-downgrade"); }
        }
        if (plan.closePolicy == ClosePolicy::Force) {
            argv << QStringLiteral("--forceclose");
        } else if (plan.closePolicy == ClosePolicy::Graceful) {
            argv << QStringLiteral("--closeapps");
        }
        if (purgeUserData) { argv << QStringLiteral("--purge-user-data"); }
    }

    if (!langOverride.isEmpty()) { argv << QStringLiteral("--lang") << langOverride; }
    if (!logPath.isEmpty()) {
        argv << QStringLiteral("--log") << QDir::toNativeSeparators(logPath);
    }
    if (!stagingDir.isEmpty()) {
        argv << QStringLiteral("--staging") << QDir::toNativeSeparators(stagingDir);
    }
    if (!sourceExe.isEmpty()) {
        argv << QStringLiteral("--source-exe") << QDir::toNativeSeparators(sourceExe);
    }
    if (resumeInstall) { argv << QStringLiteral("--resume-install"); }
    if (elevated) { argv << QStringLiteral("--elevated"); }
    return argv;
}

QString cliUsageText() {
    return QStringLiteral(
        "dish-setup [options]\n"
        "  /S | --silent                 silent install (no UI, no UAC)\n"
        "  /D=<dir>                      NSIS-compat install dir; must be last, unquoted\n"
        "  --dir <dir>                   install dir override\n"
        "  --scope user|machine          default user; silent machine scope needs an\n"
        "                                elevated caller\n"
        "  --start-menu on|off           default on\n"
        "  --desktop on|off              default off\n"
        "  --launch on|off               default on in UI, off in silent\n"
        "  --closeapps                   graceful close + 10 s wait for running Dish\n"
        "  --forceclose                  implies --closeapps, then terminates\n"
        "  --allow-downgrade             permit older-over-newer\n"
        "  --lang <code>                 system|en|bs|de|es|fr|pt_BR\n"
        "  --log <file>                  redirect the log file\n"
        "  --extract-only <dir>          unpack the install image only\n"
        "  --uninstall                   uninstall mode (default for uninstall.exe)\n"
        "  --purge-user-data             uninstall: also remove settings and data\n"
        "  --update-apply --waitpid <pid> --target-exe <path> --expect-version <M.m.p>\n"
        "                 [--no-relaunch] [--log <file>]\n"
        "                                auto-update handoff (implies silent)\n"
        "  --version | --help            print and exit\n"
        "Exit codes: 0 ok, 1 internal, 2 usage, 3 unsupported OS, 4 elevation,\n"
        "5 app running, 6 disk full, 7 payload corrupt, 8 rolled back,\n"
        "9 rollback incomplete, 10 cancelled, 11 nothing installed, 12 downgrade\n"
        "refused, 13 busy, 14 version mismatch.\n");
}

} // namespace dish::installer
