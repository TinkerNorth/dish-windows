// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// main() of dish-setup-ui.exe and of its byte-copy uninstall.exe. The stub
// extracts the install image into %TEMP% and spawns this binary with
// `--staging <dir> --source-exe <original dish-setup.exe> -- <verbatim tail>`
// (spec 1.3); running it directly (dev builds, the installed uninstall.exe) is
// equally legal. The wiring order follows spec 6.4 and mirrors src/main.cpp
// verbatim where the two share steps.
//
// Dispatch: CliOptions::parse runs BEFORE any Q*Application so the silent
// modes never construct (or flash) a GUI stack. Version/Help print through the
// console seam and exit 0. SilentInstall/SilentUninstall/ExtractOnly drive
// SilentRunner on a QCoreApplication; UpdateApply runs the section 16.6
// handoff; the two UI modes build the QGuiApplication + QML stack and return
// SetupController's exit code (closing the wizard IS a cancel, exit 10).

#include "installer/app/SetupController.h"

#include "installer/CliOptions.h"
#include "installer/Errors.h"
#include "installer/Logger.h"
#include "installer/SilentRunner.h"
#include "installer/UpdateApply.h"
#include "installer/ops/Win32FileOps.h"
#include "installer/ops/Win32ProcessOps.h"
#include "qml/chrome/ChromeBridge.h"
#include "qml/chrome/ChromeSingletons.h"
#include "qml/chrome/FramelessWindowChrome.h"
#include "qml/chrome/ThemeBridge.h"
#include "UI/CrashHandler.h"
#include "UI/Theme.h"
#include "Util/Localization.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStringList>
#include <QTranslator>
#include <qqml.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>

#include <variant>

#ifndef DISH_VERSION
#define DISH_VERSION "0.0.0"
#endif

namespace {

using dish::installer::CliOptions;
using dish::installer::ExitCode;
using dish::installer::Logger;

// GetCommandLineW-faithful argv, element 0 included. QCoreApplication does not
// exist yet (deliberately, spec 6.4) and the narrow argc/argv would mangle
// non-ASCII install paths.
QStringList wideArgv() {
    QStringList args;
    int count = 0;
    LPWSTR* raw = CommandLineToArgvW(GetCommandLineW(), &count);
    if (raw == nullptr) { return args; }
    args.reserve(count);
    for (int i = 0; i < count; ++i) { args.append(QString::fromWCharArray(raw[i])); }
    LocalFree(raw);
    return args;
}

// Lowercased basename without extension; "uninstall" flips CliOptions' default
// mode (spec 9).
QString baseNameOf(const QString& argv0) {
    QString name = argv0;
    const int slash = qMax(name.lastIndexOf(QLatin1Char('/')), name.lastIndexOf(QLatin1Char('\\')));
    if (slash >= 0) { name = name.mid(slash + 1); }
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot > 0) { name = name.left(dot); }
    return name.toLower();
}

// Best-effort console write for --version/--help/usage errors (spec 9: both
// binaries are GUI-subsystem; the exit code and the log stay authoritative). A
// redirected stdout handle (Start-Process -RedirectStandardOutput) wins;
// otherwise attach to the parent's console.
void printToConsole(const QString& text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        out = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    if (out == nullptr || out == INVALID_HANDLE_VALUE) { return; }
    const QByteArray utf8 = text.toUtf8();
    DWORD written = 0;
    WriteFile(out, utf8.constData(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

// The single-instance gate (spec 11.3, exit 13). The handle is deliberately
// held until process exit. An elevated relaunch (--elevated) skips the gate:
// its unelevated parent still holds the mutex during the handoff, and that
// parent is either exiting (UI relaunch) or waiting on this very child
// (--update-apply H5), so acquiring here would deadlock the one legal pair.
bool acquireSetupMutex() {
    SetLastError(0);
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\TinkerNorth.DishSetup");
    if (mutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS) { return true; }
    if (mutex != nullptr) { CloseHandle(mutex); }
    return false;
}

int runUi(const CliOptions& options, Logger& logger, int argc, char** argv) {
    QGuiApplication app(argc, argv);
    // Same literals as src/main.cpp:44-46: the wizard shares the app's
    // QSettings identity (it only ever probes; the install record itself lives
    // in .dish-manifest.json, spec D5).
    QCoreApplication::setOrganizationName(QStringLiteral("TinkerNorth"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tinkernorth.com"));
    QCoreApplication::setApplicationName(QStringLiteral("Dish"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    dish::crash::install();

    // dish.rc's PE icon is invisible to Qt (main.cpp:59-62); the same .ico
    // rides in packaging/dish.qrc for Alt-Tab and the taskbar.
    app.setWindowIcon(QIcon(QStringLiteral(":/dish.ico")));

    // main.cpp:64-73 verbatim: the bundled Inter weight ladder the tokens use.
    for (const char* face : {":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Medium.ttf",
                             ":/fonts/Inter-SemiBold.ttf", ":/fonts/Inter-Bold.ttf"}) {
        QFontDatabase::addApplicationFont(QLatin1String(face));
    }
    QFont uiFont(QStringLiteral("Inter"));
    uiFont.setPixelSize(13); // the token base; pages override per role
    app.setFont(uiFont);

    // main.cpp:54-57 with --lang folded in. `static` keeps the translator
    // alive for the whole run; SetupController::setUiLanguage swaps the
    // catalogue inside this same object later (Welcome language selector).
    static QTranslator translator;
    const bool forcedLang =
        !options.langOverride.isEmpty() && options.langOverride != QLatin1String("system");
    const QLocale startupLocale = forcedLang ? QLocale(options.langOverride) : QLocale::system();
    if (forcedLang) { QLocale::setDefault(startupLocale); }
    if (dish::i18n::loadCatalog(translator, startupLocale)) {
        QCoreApplication::installTranslator(&translator);
    }

    // Instance-registers ChromeBridge/Theme/Tokens into Dish.Chrome (LTCG-safe,
    // spec D4) and resolves the startup appearance once.
    const dish::chrome::ChromeSingletons chrome = dish::chrome::registerChromeSingletons();

    dish::installer::SetupController controller(options, logger);
    QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
    qmlRegisterSingletonInstance("Dish.Setup", 1, 0, "Setup", &controller);

    QQmlApplicationEngine engine;
    // applicationDirPath is Qt's highest-priority import path, and in the
    // build tree it holds the APP's Dish/Chrome qmldir, whose `prefer` points
    // at resources only dish.exe embeds — this binary's kit copy then loses
    // the race and every new kit type fails to load. Moving the resource root
    // to the front gives the linked dish_setup_kit the same win it gets in
    // the installed image, where no filesystem qmldir ships at all. The list
    // is reordered by hand because addImportPath() does not re-rank a path
    // the engine already knows.
    QStringList importPaths = engine.importPathList();
    importPaths.removeAll(QStringLiteral("qrc:/qt/qml"));
    importPaths.prepend(QStringLiteral("qrc:/qt/qml"));
    engine.setImportPathList(importPaths);
    controller.attachRuntime(&engine, &translator);

    // QmlEntryPoint.cpp:80-105 pattern, minus Mica: SetupRoot paints a solid
    // Theme.background by design (visual spec), so no backdrop is applied and
    // micaActive is pinned false; the OS-drawn frame edges still follow the
    // resolved appearance.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, qApp,
        [chromeBridge = chrome.chromeBridge, themeBridge = chrome.themeBridge](QObject* obj,
                                                                               const QUrl&) {
            auto* window = qobject_cast<QQuickWindow*>(obj);
            if (window == nullptr) { return; }
            // Parented to the app, not the window: it must survive the
            // messages that still pump while the engine tears the window down.
            auto* filter = new dish::chrome::FramelessWindowChrome(window, qApp);
            qApp->installNativeEventFilter(filter);
            if (chromeBridge != nullptr) { chromeBridge->setChrome(filter); }
            const bool dark = dish::ui::activeAppearance() == dish::ui::Appearance::Dark;
            filter->setImmersiveDarkMode(dark);
            if (chromeBridge != nullptr) {
                chromeBridge->setMicaActive(false);
                chromeBridge->setDark(dark);
            }
            // Any binding that evaluated before the palette settled re-reads
            // now that the window exists.
            if (themeBridge != nullptr) { themeBridge->refresh(); }
        },
        Qt::DirectConnection);

    engine.loadFromModule(QStringLiteral("Dish.Setup"), QStringLiteral("SetupRoot"));
    if (engine.rootObjects().isEmpty()) {
        logger.line(QStringLiteral("main: SetupRoot failed to load"));
        return static_cast<int>(ExitCode::Internal);
    }

    (void)QGuiApplication::exec();
    return controller.exitCode();
}

} // namespace

int main(int argc, char* argv[]) {
    const QStringList rawArgs = wideArgv();
    const QString exeBaseName =
        rawArgs.isEmpty() ? QStringLiteral("dish-setup-ui") : baseNameOf(rawArgs.first());
    const auto parsed = CliOptions::parse(rawArgs.mid(1), exeBaseName);

    if (const auto* usageError = std::get_if<QString>(&parsed)) {
        Logger logger;
        logger.open(Logger::defaultLogPath(QStringLiteral("dish-setup")));
        logger.attachConsole();
        logger.line(QStringLiteral("usage error: ") + *usageError);
        printToConsole(*usageError + QLatin1Char('\n') + dish::installer::cliUsageText());
        return static_cast<int>(ExitCode::Usage);
    }
    const CliOptions options = std::get<CliOptions>(parsed);

    // Version/Help exit before any logging or gating: a probe must not litter
    // %TEMP% with log files or trip the single-instance mutex.
    if (options.mode == CliOptions::Mode::Version) {
        printToConsole(QStringLiteral(DISH_VERSION "\n"));
        return 0;
    }
    if (options.mode == CliOptions::Mode::Help) {
        printToConsole(dish::installer::cliUsageText());
        return 0;
    }

    // The log opens immediately in every run mode (spec 6.4): the log file and
    // the exit code are the authoritative silent interfaces.
    Logger logger;
    const QString logBase =
        options.isUninstall() ? QStringLiteral("dish-uninstall") : QStringLiteral("dish-setup");
    logger.open(options.logPath.isEmpty() ? Logger::defaultLogPath(logBase) : options.logPath);
    logger.attachConsole();
    logger.line(QStringLiteral("dish-setup-ui %1: \"%2\"")
                    .arg(QStringLiteral(DISH_VERSION), rawArgs.join(QStringLiteral("\" \""))));

    if (!options.elevated && !acquireSetupMutex()) {
        logger.line(
            QStringLiteral("main: another setup instance holds Local\\TinkerNorth.DishSetup"));
        // The one exit that can strand the user with no app at all: the boot
        // gate already let dish.exe return 0 in favour of this apply. Put the
        // old build back before reporting Busy.
        if (options.mode == CliOptions::Mode::UpdateApply) {
            dish::installer::Win32FileOps fileOps;
            dish::installer::Win32ProcessOps processOps;
            (void)dish::installer::relaunchTargetAfterBusy(options, fileOps, processOps, logger);
        }
        return static_cast<int>(ExitCode::Busy);
    }

    switch (options.mode) {
    case CliOptions::Mode::SilentInstall:
    case CliOptions::Mode::SilentUninstall:
    case CliOptions::Mode::ExtractOnly: {
        QCoreApplication app(argc, argv);
        dish::installer::SilentRunner runner(logger);
        return runner.run(options, app);
    }
    case CliOptions::Mode::UpdateApply: {
        QCoreApplication app(argc, argv);
        dish::installer::UpdateApply apply(logger);
        return apply.run(options, app);
    }
    case CliOptions::Mode::UiInstall:
    case CliOptions::Mode::UiUninstall:
        return runUi(options, logger, argc, argv);
    case CliOptions::Mode::Version:
    case CliOptions::Mode::Help:
        break; // handled above
    }
    return static_cast<int>(ExitCode::Internal);
}
