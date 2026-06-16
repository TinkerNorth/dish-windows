// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/AppViewModel.h"

#include "AppModel.h"
#include "Input/GamepadInputProcessor.h"
#include "Network/WifiConnectionManager.h"
#include "composer/CatalogComposer.h"
#include "composer/ConnectionCoordinator.h"

#include <QCoreApplication>
#include <QTimer>
#include <QVariantMap>

namespace dish::qml {

namespace {

// "Bound to <ip> • UDP <port>" detail not needed here — the header strings
// mirror MainWindow::rebuildHeader, which keys off ConnectionSummary only.
QString tr(const char* s) { return QCoreApplication::translate("AppViewModel", s); }

} // namespace

AppViewModel::AppViewModel(dish::AppModel* model, QObject* parent)
    : QObject(parent), model_(model), slotModel_(this), connectionModel_(this) {
    QObject::connect(model_, &dish::AppModel::stateChanged, this, &AppViewModel::onStateChanged);
    QObject::connect(model_, &dish::AppModel::errorMessage, this, &AppViewModel::errorMessage);
    QObject::connect(model_->connections(), &composer::ConnectionCoordinator::connectionsChanged,
                     this, &AppViewModel::onConnectionsChanged);

    telemetryTimer_ = new QTimer(this);
    telemetryTimer_->setInterval(1'000);
    QObject::connect(telemetryTimer_, &QTimer::timeout, this, &AppViewModel::onTelemetryTick);
    telemetryTimer_->start();

    onStateChanged();
    onConnectionsChanged();
    onTelemetryTick();
}

void AppViewModel::onStateChanged() {
    const auto& st = model_->state();

    // Header derivation — byte-for-byte mirror of MainWindow::rebuildHeader so
    // the two UIs read identically. No new behavior, only a re-projection.
    const auto& conns = st.connections;
    int live = 0;
    QString firstLabel;
    for (const auto& c : conns) {
        if (c.live == models::LinkState::Connected) {
            ++live;
            if (firstLabel.isEmpty()) { firstLabel = c.label; }
        }
    }
    const int total = static_cast<int>(conns.size());
    onlineCount_ = live;
    connectionCount_ = total;

    if (live == 0 && total == 0) {
        statusText_ = tr("No connections yet");
    } else if (live == 0) {
        statusText_ = tr("%1 remembered").arg(total);
    } else if (live == 1) {
        statusText_ = firstLabel;
    } else {
        statusText_ = tr("%1 online").arg(live);
    }

    if (live == 0 && total == 0) {
        summaryText_ = tr("Tap Manage to add one");
    } else if (live == 0) {
        summaryText_ = tr("%1 remembered").arg(total);
    } else {
        summaryText_ = tr("%1 of %2 online").arg(live).arg(total);
    }

    busy_ = st.busy;

    pairingActive_ = st.pairingTarget.has_value();
    pairingServerName_ = pairingActive_ ? st.pairingTarget->name : QString();

    slotModel_.setState(st.slotList);

    emit stateChanged();
}

void AppViewModel::onConnectionsChanged() {
    connectionModel_.setRows(model_->connections()->connections().value());
}

void AppViewModel::onTelemetryTick() {
    const auto snap = model_->processor()->drainTelemetry();
    eventsPerSec_ = snap.events;
    sendsPerSec_ = snap.sends;
    totalSent_ = snap.totalSent;
    emit telemetryChanged();
}

void AppViewModel::bindSlot(const QString& slotId, const QString& connectionId) {
    model_->hub()->bind(slotId, connectionId);
}

void AppViewModel::unbindSlot(const QString& slotId) { model_->hub()->unbind(slotId); }

void AppViewModel::refreshEmulate(const QString& slotId) {
    model_->refreshCatalogForSlot(slotId);
}

QVariantList AppViewModel::emulateTypes(const QString& slotId) const {
    QVariantList out;
    for (const auto& t : model_->pickableTypesFor(slotId)) {
        QVariantMap m;
        m[QStringLiteral("type")] = t.type;
        m[QStringLiteral("slug")] = t.slug;
        m[QStringLiteral("name")] = t.name;
        m[QStringLiteral("shortName")] = t.shortName;
        m[QStringLiteral("description")] = t.description;
        m[QStringLiteral("known")] = t.known;
        out.append(m);
    }
    return out;
}

int AppViewModel::emulateCurrentType(const QString& slotId) const {
    return model_->currentTypeFor(slotId);
}

void AppViewModel::setControllerType(const QString& slotId, int type) {
    model_->setSlotControllerType(slotId, type);
}

void AppViewModel::startDiscovery() { model_->wifi()->startDiscovery(); }

bool AppViewModel::isScanning() const { return model_->wifi()->isScanning(); }

QVariantList AppViewModel::discoveredServers() const {
    QVariantList out;
    for (const auto& s : model_->wifi()->discoveredServers()) {
        QVariantMap m;
        m[QStringLiteral("name")] = s.name;
        m[QStringLiteral("ip")] = s.ip;
        m[QStringLiteral("udpPort")] = s.udpPort;
        m[QStringLiteral("pairPort")] = s.pairPort;
        m[QStringLiteral("httpPort")] = s.httpPort;
        m[QStringLiteral("machineId")] = s.machineId;
        m[QStringLiteral("id")] = s.id();
        out.append(m);
    }
    return out;
}

void AppViewModel::connectByIndex(int discoveredIndex) {
    const auto servers = model_->wifi()->discoveredServers();
    if (discoveredIndex < 0 || discoveredIndex >= servers.size()) { return; }
    model_->wifi()->connectTo(servers.at(discoveredIndex));
}

void AppViewModel::forgetConnection(const QString& connectionId) {
    model_->connections()->forgetConnection(connectionId);
}

void AppViewModel::pairWithPin(int discoveredIndex, const QString& pin) {
    const auto servers = model_->wifi()->discoveredServers();
    if (discoveredIndex < 0 || discoveredIndex >= servers.size()) { return; }
    model_->wifi()->pairWithPin(servers.at(discoveredIndex), pin);
}

bool AppViewModel::isPairingInFlight(const QString& serverId) const {
    return model_->wifi()->isPairingInFlight(serverId);
}

void AppViewModel::clearPairingTarget() { model_->clearPairingTarget(); }

} // namespace dish::qml
