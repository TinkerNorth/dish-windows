// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UTF-8 file logger, opened immediately in every mode (spec 6.4): the log file
// plus the exit code are the authoritative silent-mode interfaces. Lines are
// timestamped, flushed per write (a crash must not eat the tail), and echoed
// best-effort to an attached parent console when attachConsole() found one.
// Diagnostic English only — never user-facing, never translated.

#pragma once

#include <QFile>
#include <QMutex>
#include <QString>

namespace dish::installer {

class Logger {
  public:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    // "%TEMP%\<baseName>-<yyyyMMdd-HHmmss>.log"; baseName is "dish-setup" or
    // "dish-uninstall" per mode.
    static QString defaultLogPath(const QString& baseName);

    // Creates parent dirs as needed. On failure the logger stays silent (a
    // missing log never blocks an install); path() reports what was opened.
    bool open(const QString& absPath);
    QString path() const;
    bool isOpen() const;

    // Best-effort AttachConsole(ATTACH_PARENT_PROCESS) echo for scripted
    // callers; no console is not an error.
    void attachConsole();

    // "[hh:mm:ss.zzz] text\n". Thread-safe: the worker and the GUI thread both
    // log.
    void line(const QString& text);

    void close();

  private:
    mutable QMutex mutex_;
    QFile file_;
    QString path_;
    bool echo_ = false;
};

} // namespace dish::installer
