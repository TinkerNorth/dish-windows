// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The DTO for a Moonlight (GameStream) host: what discovery finds and what the
// remembered-host repository persists. The sibling of models::DiscoveredServer
// on the Satellite path, kept separate so the two host kinds never blur in the
// UI ("Moonlight host (Sunshine/Apollo)" vs a Satellite receiver).

#pragma once

#include <QJsonObject>
#include <QString>

namespace dish::models {

// The two fixed Moonlight ports. Everything else (RTSP, control, RTP) is dynamic
// and taken from serverinfo / RTSP SETUP, never hardcoded.
inline constexpr int kMoonlightHttpPort = 47989;
inline constexpr int kMoonlightHttpsPort = 47984;

// The "device to emulate" picker values, persisted per host. Auto lets the host
// decide (wire type Unknown); the rest map onto the CONTROLLER_ARRIVAL type.
inline constexpr int kMoonlightDeviceAuto = 0;
inline constexpr int kMoonlightDeviceXbox = 1;
inline constexpr int kMoonlightDevicePlayStation = 2;
inline constexpr int kMoonlightDeviceNintendo = 3;

struct MoonlightHost {
    QString name;
    QString ip;
    int httpPort = kMoonlightHttpPort;
    int httpsPort = kMoonlightHttpsPort;
    // The host's uniqueid from /serverinfo, once known; the stable identity.
    QString uuid;
    // True once pairing has completed and been persisted.
    bool paired = false;
    // True when this row came from an mDNS sweep this session (vs. manual entry
    // or the remembered list).
    bool discovered = false;
    // The last app the user picked from /applist (Sunshine's default exposes
    // "Desktop"); empty means launch the host default.
    QString lastAppId;
    QString lastAppName;
    // kMoonlightDevice*, the emulated-device pick for CONTROLLER_ARRIVAL.
    int deviceType = kMoonlightDeviceAuto;

    // Prefer the host UUID so a DHCP address change keeps one row; fall back to
    // the address for a host we have not queried yet.
    QString id() const {
        if (!uuid.isEmpty()) { return QStringLiteral("ml:uuid:%1").arg(uuid); }
        return QStringLiteral("ml:ip:%1").arg(ip);
    }
    bool isValid() const { return !ip.isEmpty(); }

    QJsonObject toJson() const;
    static MoonlightHost fromJson(const QJsonObject& obj);
};

} // namespace dish::models
