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
    return h;
}

} // namespace dish::models
