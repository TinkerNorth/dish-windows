// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// In-memory FileOps / RegistryOps / ShortcutOps / ProcessOps for the installer
// suites, so a reducer, a journal replay or a shortcut lifecycle is asserted
// without touching a real disk, a real HKCU or a real .lnk.
//
// Two deliberate choices about failure shapes:
//   - remove() reports ERROR_FILE_NOT_FOUND for a file that is already gone
//     (Win32FileOps swallows that into success), which is what exercises
//     applyJournalUndo's "already done" handling.
//   - removeDirIfEmpty() reports ERROR_DIR_NOT_EMPTY (145) for a directory that
//     still has children — the code applyJournalUndo treats as a soft success —
//     and mirrors Win32FileOps for a directory that is already gone (success),
//     because the CreatedDir undo has no not-found branch and a fake that
//     invented one would report a rollback failure the product cannot have.
// Every mutating call is appended to calls() in order, because most of what
// these suites pin is ORDER.

#pragma once

#include "installer/ops/FileOps.h"
#include "installer/ops/ProcessOps.h"
#include "installer/ops/RegistryOps.h"
#include "installer/ops/ShortcutOps.h"

#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <optional>

namespace dish::test {

// Win32 codes the fakes hand back, spelled as values so no test TU needs
// windows.h.
inline constexpr quint32 kFakeErrorFileNotFound = 2;
inline constexpr quint32 kFakeErrorDirNotEmpty = 145;
inline constexpr quint32 kFakeErrorAccessDenied = 5;

class FakeFileOps final : public dish::installer::FileOps {
  public:
    using OpResult = dish::installer::OpResult;
    using SetupError = dish::installer::SetupError;

    // ── world seeding ────────────────────────────────────────────────────────
    void addFile(const QString& path, qint64 size = 0) { files_.insert(path, size); }
    void addDir(const QString& path) { dirs_.insert(path); }
    void setSha(const QString& path, const QByteArray& hex) { hashes_.insert(path, hex); }
    void setFreeBytes(qint64 bytes) { freeBytes_ = bytes; }
    // Any operation naming `path` fails with this typed error.
    void failOn(const QString& path, SetupError error = SetupError::FileOpFailed,
                quint32 win32 = kFakeErrorAccessDenied) {
        failures_.insert(path, OpResult::failure(error, path, win32));
    }

    bool hasFile(const QString& path) const { return files_.contains(path); }
    bool hasDir(const QString& path) const { return dirs_.contains(path); }
    qint64 sizeOf(const QString& path) const { return files_.value(path, -1); }
    const QStringList& calls() const { return calls_; }
    void clearCalls() { calls_.clear(); }

    // ── FileOps ──────────────────────────────────────────────────────────────
    OpResult copyWithProgress(const QString& from, const QString& to,
                              const std::function<bool(qint64)>& onBytes) override {
        calls_.append(QStringLiteral("copy ") + from + QStringLiteral(" -> ") + to);
        if (const auto forced = forcedFailure(to)) { return *forced; }
        if (const auto forced = forcedFailure(from)) { return *forced; }
        if (!files_.contains(from)) {
            return OpResult::failure(SetupError::FileOpFailed, from, kFakeErrorFileNotFound);
        }
        const qint64 size = files_.value(from);
        if (onBytes && size > 0 && !onBytes(size)) {
            return OpResult::failure(SetupError::Cancelled, to);
        }
        files_.insert(to, size);
        if (hashes_.contains(from)) { hashes_.insert(to, hashes_.value(from)); }
        return OpResult::success(to);
    }

    OpResult verifySha256(const QString& path, const QByteArray& expectedHex) override {
        calls_.append(QStringLiteral("verify ") + path);
        if (const auto forced = forcedFailure(path)) { return *forced; }
        if (!files_.contains(path)) {
            return OpResult::failure(SetupError::FileOpFailed, path, kFakeErrorFileNotFound);
        }
        // No recorded hash means "whatever the caller expects": only the tests
        // that care about corruption seed one.
        if (hashes_.contains(path) && hashes_.value(path).toLower() != expectedHex.toLower()) {
            return OpResult::failure(SetupError::PayloadCorrupt, path);
        }
        return OpResult::success(path);
    }

    OpResult rename(const QString& from, const QString& to) override {
        calls_.append(QStringLiteral("rename ") + from + QStringLiteral(" -> ") + to);
        if (const auto forced = forcedFailure(from)) { return *forced; }
        if (const auto forced = forcedFailure(to)) { return *forced; }
        if (!files_.contains(from)) {
            return OpResult::failure(SetupError::FileOpFailed, from, kFakeErrorFileNotFound);
        }
        files_.insert(to, files_.value(from));
        files_.remove(from);
        if (hashes_.contains(from)) {
            hashes_.insert(to, hashes_.value(from));
            hashes_.remove(from);
        }
        return OpResult::success(to);
    }

    OpResult remove(const QString& path) override {
        calls_.append(QStringLiteral("remove ") + path);
        if (const auto forced = forcedFailure(path)) { return *forced; }
        if (!files_.contains(path)) {
            return OpResult::failure(SetupError::FileOpFailed, path, kFakeErrorFileNotFound);
        }
        files_.remove(path);
        hashes_.remove(path);
        return OpResult::success(path);
    }

    OpResult ensureDir(const QString& path) override {
        calls_.append(QStringLiteral("ensureDir ") + path);
        if (const auto forced = forcedFailure(path)) { return *forced; }
        QString probe = path;
        while (!probe.isEmpty()) {
            dirs_.insert(probe);
            const int slash = probe.lastIndexOf(QLatin1Char('/'));
            if (slash <= 0) { break; }
            probe = probe.left(slash);
        }
        return OpResult::success(path);
    }

    OpResult removeDirIfEmpty(const QString& path) override {
        calls_.append(QStringLiteral("removeDirIfEmpty ") + path);
        if (const auto forced = forcedFailure(path)) { return *forced; }
        if (!dirs_.contains(path)) { return OpResult::success(path); } // already gone
        const QString prefix = path + QLatin1Char('/');
        for (auto it = files_.cbegin(); it != files_.cend(); ++it) {
            if (it.key().startsWith(prefix)) {
                return OpResult::failure(SetupError::FileOpFailed, path, kFakeErrorDirNotEmpty);
            }
        }
        for (const QString& dir : dirs_) {
            if (dir.startsWith(prefix)) {
                return OpResult::failure(SetupError::FileOpFailed, path, kFakeErrorDirNotEmpty);
            }
        }
        dirs_.remove(path);
        return OpResult::success(path);
    }

    qint64 freeBytesFor(const QString& path) override {
        Q_UNUSED(path);
        return freeBytes_;
    }

    bool exists(const QString& path) override {
        return files_.contains(path) || dirs_.contains(path);
    }

    QStringList listRecursive(const QString& dir) override {
        const QString prefix = dir + QLatin1Char('/');
        QStringList result;
        for (auto it = files_.cbegin(); it != files_.cend(); ++it) {
            if (it.key().startsWith(prefix)) { result.append(it.key()); }
        }
        result.sort();
        return result;
    }

  private:
    std::optional<OpResult> forcedFailure(const QString& path) const {
        const auto it = failures_.constFind(path);
        if (it == failures_.cend()) { return std::nullopt; }
        return *it;
    }

    QMap<QString, qint64> files_;
    QSet<QString> dirs_;
    QMap<QString, QByteArray> hashes_;
    QMap<QString, OpResult> failures_;
    QStringList calls_;
    qint64 freeBytes_ = 100LL * 1024 * 1024 * 1024;
};

class FakeRegistryOps final : public dish::installer::RegistryOps {
  public:
    using ArpValues = dish::installer::ArpValues;
    using InstalledInfo = dish::installer::InstalledInfo;
    using OpResult = dish::installer::OpResult;
    using Scope = dish::installer::Scope;
    using SetupError = dish::installer::SetupError;

    void failWrites(bool fail) { failWrites_ = fail; }
    void failDeletes(bool fail) { failDeletes_ = fail; }
    void seed(Scope scope, const ArpValues& values) { entry(scope) = values; }
    std::optional<ArpValues> stored(Scope scope) const {
        return scope == Scope::AllUsers ? machine_ : user_;
    }
    const QStringList& calls() const { return calls_; }

    OpResult writeArp(Scope scope, const ArpValues& values) override {
        calls_.append(QStringLiteral("writeArp ") + dish::installer::scopeToken(scope));
        if (failWrites_) {
            return OpResult::failure(SetupError::RegistryFailed,
                                     dish::installer::scopeToken(scope));
        }
        entry(scope) = values;
        return OpResult::success();
    }

    OpResult deleteArp(Scope scope) override {
        calls_.append(QStringLiteral("deleteArp ") + dish::installer::scopeToken(scope));
        if (failDeletes_) {
            return OpResult::failure(SetupError::RegistryFailed,
                                     dish::installer::scopeToken(scope));
        }
        entry(scope).reset();
        return OpResult::success();
    }

    std::optional<InstalledInfo> readInstalled(Scope scope) override {
        const std::optional<ArpValues>& values = scope == Scope::AllUsers ? machine_ : user_;
        if (!values.has_value()) { return std::nullopt; }
        InstalledInfo info;
        info.displayVersion = values->displayVersion;
        info.installLocation = values->installLocation;
        info.installScope = values->installScope;
        return info;
    }

  private:
    std::optional<ArpValues>& entry(Scope scope) {
        return scope == Scope::AllUsers ? machine_ : user_;
    }

    std::optional<ArpValues> user_;
    std::optional<ArpValues> machine_;
    QStringList calls_;
    bool failWrites_ = false;
    bool failDeletes_ = false;
};

class FakeShortcutOps final : public dish::installer::ShortcutOps {
  public:
    using OpResult = dish::installer::OpResult;
    using ShortcutSpec = dish::installer::ShortcutSpec;
    using SetupError = dish::installer::SetupError;

    // The resolved path a real Win32ShortcutOps would get from the shell,
    // faked deterministically so tests can name it without a known folder.
    static QString linkPathFor(const ShortcutSpec& spec) {
        return QStringLiteral("C:/fake-shell/") + dish::installer::scopeToken(spec.scope) +
               QLatin1Char('/') + dish::installer::shortcutLocationToken(spec.location) +
               QStringLiteral("/Dish.lnk");
    }

    void failCreates(bool fail) { failCreates_ = fail; }
    const QStringList& calls() const { return calls_; }
    QStringList links() const { return links_.keys(); }
    std::optional<ShortcutSpec> specFor(const QString& linkAbs) const {
        const auto it = links_.constFind(linkAbs);
        if (it == links_.cend()) { return std::nullopt; }
        return *it;
    }

    OpResult create(const ShortcutSpec& spec) override {
        const QString link = linkPathFor(spec);
        calls_.append(QStringLiteral("create ") + link);
        if (failCreates_) { return OpResult::failure(SetupError::ShortcutFailed, link); }
        links_.insert(link, spec);
        return OpResult::success(link);
    }

    OpResult remove(const QString& linkAbs) override {
        calls_.append(QStringLiteral("remove ") + linkAbs);
        if (!links_.contains(linkAbs)) {
            return OpResult::failure(SetupError::ShortcutFailed, linkAbs, kFakeErrorFileNotFound);
        }
        links_.remove(linkAbs);
        return OpResult::success(linkAbs);
    }

    bool exists(const QString& linkAbs) override { return links_.contains(linkAbs); }

  private:
    QMap<QString, ShortcutSpec> links_;
    QStringList calls_;
    bool failCreates_ = false;
};

class FakeProcessOps final : public dish::installer::ProcessOps {
  public:
    using OpResult = dish::installer::OpResult;
    using ProcInfo = dish::installer::ProcInfo;
    using SetupError = dish::installer::SetupError;

    void setProcesses(const QVector<ProcInfo>& procs) { procs_ = procs; }
    void setElevated(bool elevated) { elevated_ = elevated; }
    void setCloseSucceeds(bool succeeds) { closeSucceeds_ = succeeds; }
    void setTerminateSucceeds(bool succeeds) { terminateSucceeds_ = succeeds; }
    void setWaitResult(bool result) { waitResult_ = result; }
    void setRelaunchResult(const OpResult& result) { relaunchResult_ = result; }
    void setLaunchResult(const OpResult& result) { launchResult_ = result; }

    const QStringList& calls() const { return calls_; }
    QStringList lastArgv() const { return lastArgv_; }
    QString lastExe() const { return lastExe_; }
    QString lastCwd() const { return lastCwd_; }
    bool lastDeElevate() const { return lastDeElevate_; }

    QVector<ProcInfo> processesUnder(const QString& dir) override {
        calls_.append(QStringLiteral("processesUnder ") + dir);
        QVector<ProcInfo> result;
        const QString prefix =
            (dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/')).toCaseFolded();
        for (const ProcInfo& proc : procs_) {
            if (proc.imagePath.toCaseFolded().startsWith(prefix)) { result.append(proc); }
        }
        return result;
    }

    bool requestClose(const QVector<ProcInfo>& procs, int timeoutMs) override {
        Q_UNUSED(procs);
        calls_.append(QStringLiteral("requestClose ") + QString::number(timeoutMs));
        if (closeSucceeds_) { procs_.clear(); }
        return closeSucceeds_;
    }

    bool terminate(const QVector<ProcInfo>& procs) override {
        Q_UNUSED(procs);
        calls_.append(QStringLiteral("terminate"));
        if (terminateSucceeds_) { procs_.clear(); }
        return terminateSucceeds_;
    }

    bool waitForPid(quint32 pid, int timeoutMs) override {
        calls_.append(QStringLiteral("waitForPid ") + QString::number(pid) + QLatin1Char(' ') +
                      QString::number(timeoutMs));
        return waitResult_;
    }

    bool isElevated() override { return elevated_; }

    OpResult relaunchElevated(const QString& exe, const QStringList& argv) override {
        calls_.append(QStringLiteral("relaunchElevated ") + exe);
        lastExe_ = exe;
        lastArgv_ = argv;
        return relaunchResult_;
    }

    OpResult launchDetached(const QString& exe, const QStringList& argv, const QString& cwd,
                            bool deElevate) override {
        calls_.append(QStringLiteral("launchDetached ") + exe);
        lastExe_ = exe;
        lastArgv_ = argv;
        lastCwd_ = cwd;
        lastDeElevate_ = deElevate;
        return launchResult_;
    }

  private:
    QVector<ProcInfo> procs_;
    QStringList calls_;
    QStringList lastArgv_;
    QString lastExe_;
    QString lastCwd_;
    OpResult relaunchResult_ = OpResult::success();
    OpResult launchResult_ = OpResult::success();
    bool elevated_ = false;
    bool closeSucceeds_ = true;
    bool terminateSucceeds_ = true;
    bool waitResult_ = true;
    bool lastDeElevate_ = false;
};

} // namespace dish::test
