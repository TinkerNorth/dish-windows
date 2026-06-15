// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ConnectionCoordinator — the imperative Coordinator over the connection
// subsystem. It owns the upstream Observables and the ConnectionsComposer,
// feeds those Observables from the Qt signal world (WifiConnectionManager pool +
// discovery, ConnectionHub bindings), exposes the user commands
// (bind/unbind/setControllerType/forget/autoReconnectAll), and — the canonical
// "never mirror state another class owns" rule — RE-EXPOSES composer.state() BY
// REFERENCE as connections(). It holds no summary list of its own.
//
// The binding table + hot-path sender factory + per-device capability lookups
// stay on ConnectionHub (the input thread reads them); this Coordinator wraps it
// and the manager. Mirrors dish-android composer/ConnectionCoordinator
// (val connections = composer.state; bind/unbind/forget/setType/autoReconnectAll).

#pragma once

#include "Network/ConnectionHub.h"
#include "Network/WifiConnectionManager.h"
#include "architecture/Observable.h"
#include "composer/ConnectionsComposer.h"

#include <QObject>
#include <QString>

#include <memory>
#include <string>
#include <vector>

namespace dish::composer {

class ConnectionCoordinator : public QObject {
    Q_OBJECT
  public:
    ConnectionCoordinator(net::WifiConnectionManager* wifi, net::ConnectionHub* hub,
                          QObject* parent = nullptr);
    ~ConnectionCoordinator() override;

    // RE-EXPOSED, never mirrored: the derived connections list lives on the
    // composer; this is a const reference to its Observable. Subscribers get the
    // eager initial + every distinct recompute. (android: val connections =
    // composer.state.)
    const arch::Observable<std::vector<ConnectionRow>>& connections() const {
        return composer_->state();
    }

    // The current row for an id, or nullopt — a convenience over connections().
    std::optional<ConnectionRow> summary(const std::string& id) const;

    // ── Commands (sequence the binding store + type hint + session) ──────────

    // Bind a slot to a connection with its FINAL controller type. Delegates to
    // ConnectionHub (binding table + attachSlot with the descriptor). Returns
    // the bound connection id by way of the hub's own side effects; the
    // composer re-derives the row on the resulting bindingsChanged.
    void bind(const QString& slotId, const QString& connectionId);
    void unbind(const QString& slotId);

    // Forget a remembered connection and its local state: unbind its slots,
    // then forget the host (drops the row + key + pin). Mirrors android
    // forgetConnection.
    void forgetConnection(const QString& connectionId);

    // Connect every remembered satellite that is not already live, with the
    // AutoReconnect intent (silent failures — the row chip is the cue). Moved
    // off AppModel's 15 s timer / WifiConnectionManager::autoReconnectAll into
    // the Coordinator, mirroring android ConnectionCoordinator.autoReconnectAll.
    void autoReconnectAll();

  signals:
    // Fired whenever the derived connections list may have changed (a thin Qt
    // bridge so existing QObject consumers — ConnectionsDialog, AppModel — can
    // connect without holding an Observable subscription). The authoritative
    // value is always connections().value().
    void connectionsChanged();

  private:
    // Recompute the upstream Observables from the live manager/hub state and
    // push them in (the composer reacts; connectionsChanged is emitted off the
    // composer's own change subscription).
    void refreshSessions();
    void refreshRemembered();
    void refreshDiscovered();
    void refreshBindings();

    net::WifiConnectionManager* wifi_;
    net::ConnectionHub* hub_;

    // The five upstream Observables (owned here; the composer reads them).
    arch::Observable<std::vector<SessionSnapshot>> sessions_;
    arch::Observable<std::vector<RememberedSnapshot>> remembered_;
    arch::Observable<std::vector<std::string>> discoveredIds_;
    arch::Observable<std::vector<Binding>> bindings_;
    arch::Observable<std::vector<std::string>> staleIds_;

    std::unique_ptr<ConnectionsComposer> composer_;
    arch::Observable<std::vector<ConnectionRow>>::Subscription composerSub_;
};

} // namespace dish::composer
