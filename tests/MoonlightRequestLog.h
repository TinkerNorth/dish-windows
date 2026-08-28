// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Every HTTP(S) request a Moonlight session actually issued, in order.
//
// "Did the client tell the host to quit?" is the question half the behaviour
// contract turns on, and it cannot be answered by reading a switch statement:
// the effect list, the coordinator that executes it and the HTTP client that
// carries it are three separate pieces, and a claim is only pinned if all three
// are exercised. Nothing here is a stub. The session makes its real request
// through its real QNetworkAccessManager; the address is simply one that goes
// nowhere (RFC 5737 TEST-NET) or a loopback socket under the test's control, so
// what is observed is that the call was MADE.
//
// The seam is Qt's own: a QNetworkReply is parented to the access manager as it
// is constructed, and ChildAdded is delivered SYNCHRONOUSLY. That matters twice
// over. It means a test can read the log the instant the call under test
// returns, without turning an event loop and without waiting out a connect
// timeout. And it means `witness` runs at the exact moment the request goes out,
// which is the only way to assert that something else had, or had not, already
// happened by then.
//
// Nothing is DECIDED at ChildAdded time, because two things are not true yet.
// The reply's URL is not set, and neither is its dynamic type: the event is sent
// from inside QObject's own constructor, where a qobject_cast to QNetworkReply
// still fails because the vtable has not reached the derived class. So each
// child is recorded and read twice over - by a queued call that lands on the
// next turn of the event loop, and by a sweep on every read, for a test that
// never turns one at all. Between them no request goes unrecorded, and once
// recorded it outlives the reply that a finished request deleteLater's away.

#pragma once

#include <QChildEvent>
#include <QEvent>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <utility>

namespace dish::test {

class MoonlightRequestLog : public QObject {
  public:
    // Called synchronously as each request goes out, so a test can record what
    // was true at that instant rather than what is true afterwards.
    using Witness = std::function<QString()>;

    // `owner` is anything the session hangs under: the session itself, or the
    // manager that owns it. Every access manager already beneath it is watched.
    // Sessions are built lazily, so a manager must have made its session before
    // this is constructed; adding or probing the host first is the usual way.
    explicit MoonlightRequestLog(QObject& owner, Witness witness = {})
        : witness_(std::move(witness)) {
        for (auto* nam : owner.findChildren<QNetworkAccessManager*>()) {
            nam->installEventFilter(this);
        }
    }

    // The path of every request, in the order they were issued: "/launch",
    // "/cancel", "/pair", "/serverinfo", "/applist".
    QStringList paths() {
        harvest();
        QStringList out;
        for (const auto& entry : entries_) {
            if (entry.isRequest) { out.append(entry.url.path()); }
        }
        return out;
    }

    // The full query of the nth request, for the phase a /pair call names.
    QString query(int index) {
        harvest();
        int seen = 0;
        for (const auto& entry : entries_) {
            if (!entry.isRequest) { continue; }
            if (seen == index) { return entry.url.query(); }
            ++seen;
        }
        return {};
    }

    // What the witness saw at the moment each request went out.
    QStringList witnessed() {
        harvest();
        QStringList out;
        for (const auto& entry : entries_) {
            if (entry.isRequest) { out.append(entry.witness); }
        }
        return out;
    }

    int count(const QString& path) {
        int n = 0;
        for (const auto& p : paths()) {
            if (p == path) { ++n; }
        }
        return n;
    }

    bool sawAny(const QString& path) { return count(path) > 0; }

    void clear() { entries_.clear(); }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::ChildAdded) {
            auto* child = static_cast<QChildEvent*>(event)->child();
            entries_.append(Entry{child, witness_ ? witness_() : QString()});
            // The next turn of the loop is the earliest moment the child is
            // fully itself, and it comes before the deleteLater a finished reply
            // posts, so a request is always read before it is thrown away.
            QMetaObject::invokeMethod(this, [this] { harvest(); }, Qt::QueuedConnection);
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    struct Entry {
        QPointer<QObject> child;
        QString witness;
        QUrl url;
        bool isRequest = false;
    };

    void harvest() {
        for (auto& entry : entries_) {
            if (entry.isRequest || entry.child.isNull()) { continue; }
            if (auto* reply = qobject_cast<QNetworkReply*>(entry.child.data())) {
                entry.url = reply->url();
                entry.isRequest = true;
            }
        }
    }

    QList<Entry> entries_;
    Witness witness_;
};

} // namespace dish::test
