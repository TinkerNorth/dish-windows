// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight (GameStream) subsystem's command surface, the sibling of
// WifiConnectionManager on the Satellite path. It owns the persistent identity,
// the remembered + discovered host list, and the live MoonlightSession per host,
// and it exposes the user commands (discover, add manual host, pair, connect,
// disconnect, forget). The UI binds host rows and drives these commands; the
// per-slot input routing into a live session is the remaining wiring step (see
// the PR notes), kept out of AppModel's hot path so the Satellite path is never
// perturbed.

#pragma once

#include "Network/MoonlightHost.h"
#include "core/moonlight/MoonlightIdentity.h"
#include "core/moonlight/MoonlightSessionMachine.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <thread>

class QSettings;

namespace dish::repository {
class MoonlightHostRepository;
}

namespace dish::net {

class MoonlightSession;

// The flat row the host-list UI renders: identity crossed with live phase, as
// tokens the QML localizes (never sentences), matching the Satellite path's
// render-key discipline.
struct MoonlightHostRow {
    QString id;
    QString name;
    QString ip;
    bool paired = false;
    bool discovered = false;
    // "idle" | "pairing" | "paired" | "launching" | "connecting" | "streaming" |
    // "faltering" | "closed" | "failed".
    QString phaseToken;

    bool operator==(const MoonlightHostRow& o) const {
        return id == o.id && name == o.name && ip == o.ip && paired == o.paired &&
               discovered == o.discovered && phaseToken == o.phaseToken;
    }
};

// The lowercase phase token for a session state, for the row above.
QString moonlightPhaseToken(moonlight::SessionPhase phase);

// Pure merge of the remembered list with a discovery sweep and the live session
// phases into the flat row list. Remembered rows come first; a discovered host
// already remembered is folded (not duplicated). Exposed for unit testing.
QList<MoonlightHostRow> mergeMoonlightRows(const QList<models::MoonlightHost>& remembered,
                                           const QList<models::MoonlightHost>& discovered,
                                           const QHash<QString, QString>& phaseTokensById);

class MoonlightManager : public QObject {
    Q_OBJECT
  public:
    explicit MoonlightManager(std::shared_ptr<QSettings> settings = nullptr,
                              QObject* parent = nullptr);
    ~MoonlightManager() override;

    // The merged host rows for the UI.
    QList<MoonlightHostRow> hostRows() const;

    bool isScanning() const { return scanning_; }

    // Kick a background _nvstream._tcp sweep; results merge into the host list.
    void startDiscovery();

    // Add a host the user typed by hand (discovery fallback).
    void addManualHost(const QString& ip, const QString& name);

    // Run PIN pairing against the host; pairingFinished(id, ok) reports the end.
    void pairHost(const QString& id, const QString& pin);

    // Launch (or resume) an app and bring the control stream up. Empty appId
    // launches the host's default (Sunshine's "Desktop").
    void connectHost(const QString& id, const QString& appId);

    void disconnectHost(const QString& id);
    void forgetHost(const QString& id);

    std::optional<moonlight::SessionPhase> sessionPhase(const QString& id) const;

  signals:
    void hostsChanged();
    void scanningChanged();
    void pairingFinished(const QString& id, bool ok);
    void sessionPhaseChanged(const QString& id);
    // Host -> client, forwarded for a future binding to route to a local pad.
    void rumbleReceived(const QString& id, int controllerNumber, int lowFreq, int highFreq);
    void rgbLedReceived(const QString& id, int controllerNumber, int r, int g, int b);

  private:
    // Ensures a session exists for `host`, wiring its signals through. Lazily
    // loads the client identity on first use (RSA keygen is not paid at startup).
    MoonlightSession* ensureSession(const models::MoonlightHost& host);
    std::optional<models::MoonlightHost> hostById(const QString& id) const;

    std::unique_ptr<repository::MoonlightHostRepository> repo_;
    std::optional<moonlight::Identity> identity_;
    QList<models::MoonlightHost> discovered_;
    QHash<QString, MoonlightSession*> sessions_;
    bool scanning_ = false;
    std::thread discoveryThread_;
};

} // namespace dish::net
