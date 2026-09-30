// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"

#include <QList>

#include <functional>
#include <memory>
#include <string>

class QObject;

namespace dish::net {

class SatelliteClient;

// What WifiConnectionManager reaches besides the REST API: time, discovery and the UDP link.
// realWifiManagerEffects() is the running app's; a test hands in its own, so the manager runs on
// virtual time with no scan and no socket.
struct WifiManagerEffects {
    using Callback = std::function<void()>;
    using ScanDone = std::function<void(const QList<models::DiscoveredServer>&)>;

    // Calls `fn` once after `delayMs`, unless `context` is destroyed first.
    std::function<void(int delayMs, QObject* context, Callback fn)> after;
    // Starts one discovery scan. `done` runs on `context`'s thread with the merged result, and
    // never once `context` is gone.
    std::function<void(QObject* context, ScanDone done)> scan;
    // A UDP link to the satellite, or null when the socket will not open.
    std::function<std::shared_ptr<SatelliteClient>(const std::string& ip, int port)> openLink;
};

WifiManagerEffects realWifiManagerEffects();

} // namespace dish::net
