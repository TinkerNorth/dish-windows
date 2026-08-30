// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightHost.h"

namespace dish::models {

QJsonObject MoonlightHost::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("name")] = name;
    obj[QStringLiteral("ip")] = ip;
    obj[QStringLiteral("httpPort")] = httpPort;
    obj[QStringLiteral("httpsPort")] = httpsPort;
    obj[QStringLiteral("uuid")] = uuid;
    obj[QStringLiteral("paired")] = paired;
    obj[QStringLiteral("lastAppId")] = lastAppId;
    obj[QStringLiteral("lastAppName")] = lastAppName;
    obj[QStringLiteral("deviceType")] = deviceType;
    return obj;
}

MoonlightHost MoonlightHost::fromJson(const QJsonObject& obj) {
    MoonlightHost h;
    h.name = obj.value(QStringLiteral("name")).toString();
    h.ip = obj.value(QStringLiteral("ip")).toString();
    h.httpPort = obj.value(QStringLiteral("httpPort")).toInt(kMoonlightHttpPort);
    h.httpsPort = obj.value(QStringLiteral("httpsPort")).toInt(kMoonlightHttpsPort);
    h.uuid = obj.value(QStringLiteral("uuid")).toString();
    h.paired = obj.value(QStringLiteral("paired")).toBool();
    h.lastAppId = obj.value(QStringLiteral("lastAppId")).toString();
    h.lastAppName = obj.value(QStringLiteral("lastAppName")).toString();
    h.deviceType =
        migrateDevicePick(obj.value(QStringLiteral("deviceType")).toInt(kMoonlightDeviceAuto));
    return h;
}

QJsonObject MoonlightBinding::toJson() const {
    QJsonObject obj;
    obj[QStringLiteral("slotId")] = slotId;
    obj[QStringLiteral("hostId")] = hostId;
    obj[QStringLiteral("controllerType")] = controllerType;
    return obj;
}

MoonlightBinding MoonlightBinding::fromJson(const QJsonObject& obj) {
    MoonlightBinding b;
    b.slotId = obj.value(QStringLiteral("slotId")).toString();
    b.hostId = obj.value(QStringLiteral("hostId")).toString();
    b.controllerType =
        migrateDevicePick(obj.value(QStringLiteral("controllerType")).toInt(kMoonlightDeviceAuto));
    return b;
}

} // namespace dish::models
