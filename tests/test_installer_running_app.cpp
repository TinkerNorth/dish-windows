// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The running-app gate against the REAL Win32 implementation, because the whole
// point of processesUnder() is a Toolhelp snapshot filtered by canonical image
// path — a fake cannot pin that.
//
// The holder is a copy of the system PING.EXE named dish.exe inside a temp
// directory: one process, no children, a bounded lifetime, and it terminates
// cleanly. Everything the test creates lives under QTemporaryDir and the holder
// is killed on every exit path, including a failing assertion (the QProcess
// destructor kills it).

#include "installer/ops/Win32ProcessOps.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

using dish::installer::buildCommandLine;
using dish::installer::ProcInfo;
using dish::installer::Win32ProcessOps;

namespace {

// Catch2WithMain creates no QCoreApplication; QProcess and applicationDirPath()
// both want one. A function-local static with a leaked argv keeps one alive for
// the process.
void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

QString systemPingExe() {
    const QString root = QDir::fromNativeSeparators(qEnvironmentVariable("SystemRoot"));
    if (root.isEmpty()) { return {}; }
    return root + QStringLiteral("/System32/PING.EXE");
}

// The canonical (long-form, real-case) directory a copied exe ended up in. The
// %TEMP% value can be an 8.3 short path while QueryFullProcessImageNameW always
// reports the long one, and the prefix compare has to see the same shape.
QString canonicalDirOf(const QString& filePath) {
    const QString canonical = QFileInfo(filePath).canonicalFilePath();
    return QFileInfo(canonical.isEmpty() ? filePath : canonical).absolutePath();
}

} // namespace

TEST_CASE("installer running app: a process under the install dir is found, closed and gone",
          "[installer][running-app]") {
    ensureApp();
    const QString ping = systemPingExe();
    REQUIRE_FALSE(ping.isEmpty());
    REQUIRE(QFileInfo::exists(ping));

    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString installDir = QDir::fromNativeSeparators(temp.path());
    const QString holderExe = installDir + QStringLiteral("/dish.exe");
    REQUIRE(QFile::copy(ping, holderExe));

    const QString scanDir = canonicalDirOf(holderExe);

    QProcess holder;
    holder.setProgram(holderExe);
    // ~30 s of doing nothing, which is far longer than this test needs.
    holder.setArguments(
        QStringList{QStringLiteral("-n"), QStringLiteral("30"), QStringLiteral("127.0.0.1")});
    holder.start();
    REQUIRE(holder.waitForStarted(10000));
    const qint64 holderPid = holder.processId();
    REQUIRE(holderPid > 0);

    Win32ProcessOps ops;
    const QVector<ProcInfo> found = ops.processesUnder(scanDir);
    REQUIRE_FALSE(found.isEmpty());

    bool sawHolder = false;
    for (const ProcInfo& proc : found) {
        if (proc.pid == static_cast<quint32>(holderPid)) {
            sawHolder = true;
            // Renamed exes are caught by PATH, not by name: the gate is
            // "anything running out of the install dir".
            CHECK(proc.name.compare(QStringLiteral("dish.exe"), Qt::CaseInsensitive) == 0);
            CHECK(proc.imagePath.startsWith(scanDir, Qt::CaseInsensitive));
            CHECK(proc.imagePath.endsWith(QStringLiteral("/dish.exe"), Qt::CaseInsensitive));
        }
    }
    CHECK(sawHolder);

    // A copy running from ANOTHER directory never blocks this install.
    QTemporaryDir elsewhere;
    REQUIRE(elsewhere.isValid());
    CHECK(ops.processesUnder(canonicalDirOf(elsewhere.path() + QStringLiteral("/x"))).isEmpty());

    // No top-level window: a graceful close cannot clear it, which is exactly
    // the case the UI escalates to Force close.
    CHECK_FALSE(ops.requestClose(found, 400));

    CHECK(ops.terminate(found));
    CHECK(ops.processesUnder(scanDir).isEmpty());
    // A pid that has exited (or never existed) counts as exited.
    CHECK(ops.waitForPid(static_cast<quint32>(holderPid), 2000));

    holder.waitForFinished(5000);
    if (holder.state() != QProcess::NotRunning) { holder.kill(); }
}

TEST_CASE("installer running app: waitForPid on an unknown pid returns immediately",
          "[installer][running-app]") {
    Win32ProcessOps ops;
    // 0xFFFFFFF0 is not a valid process id; an unwaitable pid counts as exited
    // so --update-apply never blocks for 60 s on a pid that is already gone.
    CHECK(ops.waitForPid(0xFFFFFFF0u, 100));
}

TEST_CASE("installer running app: an empty process set closes and terminates trivially",
          "[installer][running-app]") {
    Win32ProcessOps ops;
    CHECK(ops.requestClose(QVector<ProcInfo>{}, 50));
    CHECK(ops.terminate(QVector<ProcInfo>{}));
}

TEST_CASE("installer running app: the elevation probe answers without throwing",
          "[installer][running-app]") {
    Win32ProcessOps ops;
    // Both answers are legitimate (CI runners are elevated, developer shells
    // usually are not); what matters is that the probe is total.
    CHECK_NOTHROW(ops.isElevated());
}

TEST_CASE("installer running app: the process's own mapped working set is reported",
          "[installer][running-app]") {
    ensureApp();
    Win32ProcessOps ops;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList workingSet = ops.ownWorkingSetUnder(appDir);

    // The uninstaller cannot delete what it has mapped; that list is what it
    // hands to the helper instead of failing on.
    REQUIRE_FALSE(workingSet.isEmpty());
    bool sawSelf = false;
    for (const QString& path : workingSet) {
        CHECK(path.startsWith(appDir, Qt::CaseInsensitive));
        if (path.endsWith(QStringLiteral("/DishTests.exe"), Qt::CaseInsensitive)) {
            sawSelf = true;
        }
    }
    CHECK(sawSelf);

    // A directory nothing is mapped from is empty.
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    CHECK(ops.ownWorkingSetUnder(QDir::fromNativeSeparators(temp.path())).isEmpty());
}

TEST_CASE("installer running app: command lines quote the way CommandLineToArgvW parses",
          "[installer][running-app]") {
    CHECK(buildCommandLine(QStringLiteral("C:/App/dish.exe"), QStringList{}) ==
          QStringLiteral("C:\\App\\dish.exe"));
    CHECK(buildCommandLine(QStringLiteral("C:/Program Files/Dish/uninstall.exe"), QStringList{}) ==
          QStringLiteral("\"C:\\Program Files\\Dish\\uninstall.exe\""));
    CHECK(buildCommandLine(QStringLiteral("C:/App/setup.exe"),
                           QStringList{QStringLiteral("--silent")}) ==
          QStringLiteral("C:\\App\\setup.exe --silent"));
    CHECK(buildCommandLine(
              QStringLiteral("C:/App/setup.exe"),
              QStringList{QStringLiteral("--dir"), QStringLiteral("C:\\Program Files\\Dish")}) ==
          QStringLiteral("C:\\App\\setup.exe --dir \"C:\\Program Files\\Dish\""));
    // A trailing backslash inside a quoted argument must be doubled, or it
    // escapes the closing quote and swallows the next argument.
    CHECK(buildCommandLine(
              QStringLiteral("C:/App/setup.exe"),
              QStringList{QStringLiteral("C:\\Dir With Space\\"), QStringLiteral("--silent")}) ==
          QStringLiteral("C:\\App\\setup.exe \"C:\\Dir With Space\\\\\" --silent"));
    // An embedded quote is escaped, and the backslashes before it double.
    CHECK(buildCommandLine(QStringLiteral("C:/App/setup.exe"),
                           QStringList{QStringLiteral("say \"hi\"")}) ==
          QStringLiteral("C:\\App\\setup.exe \"say \\\"hi\\\"\""));
    // An empty argument still has to occupy a slot.
    CHECK(buildCommandLine(QStringLiteral("C:/App/setup.exe"), QStringList{QString()}) ==
          QStringLiteral("C:\\App\\setup.exe \"\""));
}
