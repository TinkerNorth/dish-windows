// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/Logger.h"

#include "installer/ops/KnownFolders.h"

#include <QDateTime>
#include <QDir>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace dish::installer {

Logger::~Logger() { close(); }

QString Logger::defaultLogPath(const QString& baseName) {
    const QString temp = tempDir();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    if (temp.isEmpty()) { return baseName + QLatin1Char('-') + stamp + QLatin1String(".log"); }
    return temp + QLatin1Char('/') + baseName + QLatin1Char('-') + stamp + QLatin1String(".log");
}

bool Logger::open(const QString& absPath) {
    QMutexLocker lock(&mutex_);
    if (file_.isOpen()) { file_.close(); }
    path_.clear();
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(absPath));
    const int slash = clean.lastIndexOf(QLatin1Char('/'));
    if (slash > 0) { QDir().mkpath(clean.left(slash)); }
    file_.setFileName(clean);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    path_ = clean;
    return true;
}

QString Logger::path() const {
    QMutexLocker lock(&mutex_);
    return path_;
}

bool Logger::isOpen() const {
    QMutexLocker lock(&mutex_);
    return file_.isOpen();
}

void Logger::attachConsole() {
    QMutexLocker lock(&mutex_);
    // Attaching to the parent's console lets `dish-setup.exe --silent` echo
    // into the invoking terminal even though both binaries are GUI-subsystem.
    // Failure means "double-clicked, no console": silently keep file-only.
    echo_ = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
}

void Logger::line(const QString& text) {
    QMutexLocker lock(&mutex_);
    const QString stamped = QLatin1Char('[') +
                            QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")) +
                            QLatin1String("] ") + text + QLatin1Char('\n');
    const QByteArray utf8 = stamped.toUtf8();
    if (file_.isOpen()) {
        file_.write(utf8);
        file_.flush();
    }
    if (echo_) {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out != nullptr && out != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(out, utf8.constData(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }
}

void Logger::close() {
    QMutexLocker lock(&mutex_);
    if (file_.isOpen()) { file_.close(); }
}

} // namespace dish::installer
