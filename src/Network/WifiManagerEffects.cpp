// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WifiManagerEffects.h"

#include "SatelliteClient.h"
#include "source/connection/DiscoveryGateway.h"
#include "source/connection/LANDiscovery.h"
#include "source/connection/MdnsDiscovery.h"

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <utility>

namespace dish::net {

namespace {

using ServerList = QList<models::DiscoveredServer>;

void afterOnQtTimer(int delayMs, QObject* context, WifiManagerEffects::Callback fn) {
    QTimer::singleShot(delayMs, context, std::move(fn));
}

// Both scans at once, merged; blocks for the scan window, so it runs on the thread pool.
ServerList scanBothTransports() {
    auto mdnsFuture = QtConcurrent::run([] { return MdnsDiscovery::discover(); });
    const ServerList beacon = LANDiscovery::discover();
    const ServerList mdns = mdnsFuture.result();
    const ServerList merged = DiscoveryGateway::mergeDiscovered(beacon, mdns);
    qInfo("discovery scan: broadcast=%lld mdns=%lld merged=%lld",
          static_cast<long long>(beacon.size()), static_cast<long long>(mdns.size()),
          static_cast<long long>(merged.size()));
    return merged;
}

// The watcher is a child of `context`, so a context destroyed mid-scan takes the delivery with it.
void scanOnThreadPool(QObject* context, WifiManagerEffects::ScanDone done) {
    auto* watcher = new QFutureWatcher<ServerList>(context);
    QObject::connect(watcher, &QFutureWatcherBase::finished, context,
                     [watcher, done = std::move(done)] {
                         watcher->deleteLater();
                         done(watcher->result());
                     });
    watcher->setFuture(QtConcurrent::run(&scanBothTransports));
}

std::shared_ptr<SatelliteClient> openUdpLink(const std::string& ip, int port) {
    auto client = std::make_shared<SatelliteClient>();
    if (!client->openSocket(ip, port)) { return nullptr; }
    return client;
}

} // namespace

WifiManagerEffects realWifiManagerEffects() {
    WifiManagerEffects effects;
    effects.after = &afterOnQtTimer;
    effects.scan = &scanOnThreadPool;
    effects.openLink = &openUdpLink;
    return effects;
}

} // namespace dish::net
