// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Every line the app logs while one of these is alive, for a test whose answer is in the log.

#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <algorithm>

namespace dish::test {

class LogCapture {
  public:
    LogCapture() {
        instance() = this;
        previous_ = qInstallMessageHandler(&LogCapture::handle);
    }
    ~LogCapture() {
        qInstallMessageHandler(previous_);
        instance() = nullptr;
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    bool sawContaining(const QString& needle) const { return countContaining(needle) > 0; }

    long long countContaining(const QString& needle) const {
        return std::count_if(lines_.cbegin(), lines_.cend(),
                             [&needle](const QString& line) { return line.contains(needle); });
    }

  private:
    // qInstallMessageHandler takes a plain function, which reaches the live capture through this.
    static LogCapture*& instance() {
        static LogCapture* current = nullptr;
        return current;
    }
    static void handle(QtMsgType type, const QMessageLogContext& context, const QString& message) {
        if (auto* self = instance()) { self->lines_.append(message); }
        Q_UNUSED(type);
        Q_UNUSED(context);
    }

    QStringList lines_;
    QtMessageHandler previous_ = nullptr;
};

} // namespace dish::test
