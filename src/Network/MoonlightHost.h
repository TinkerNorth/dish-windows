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

// The "device to emulate" picker values, persisted per binding. The three named
// ones map straight onto the CONTROLLER_ARRIVAL type byte. AUTO IS 0xFF, NOT 0:
// 0 is the wire's CONTROLLER_TYPE_UNKNOWN, and a picker value that collides with
// a wire value is a bug waiting for someone to pass one where the other belongs.
// Auto resolves client-side before the packet is built, so Unknown never leaves
// this machine. A record written before this carried 0 for Auto and is migrated
// on read.
inline constexpr int kMoonlightDeviceAuto = 0xFF;
inline constexpr int kMoonlightDeviceXbox = 1;
inline constexpr int kMoonlightDevicePlayStation = 2;
inline constexpr int kMoonlightDeviceNintendo = 3;

// The stored 0 that used to mean Auto.
inline constexpr int kMoonlightDeviceLegacyAuto = 0;

inline int migrateDevicePick(int stored) {
    return stored == kMoonlightDeviceLegacyAuto ? kMoonlightDeviceAuto : stored;
}

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
    // kMoonlightDevice*. The pick used to live here, one per host; it is now per
    // binding, and this is what a fresh binding on this host starts from, so a
    // record written before the move keeps the answer its user gave.
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

// One controller's standing intent to drive a Moonlight host. The type is PER
// BINDING because two pads on one host can be two different devices; the app and
// the pairing are per host, and live on MoonlightHost above.
struct MoonlightBinding {
    QString slotId;
    QString hostId;
    int controllerType = kMoonlightDeviceAuto;

    bool isValid() const { return !slotId.isEmpty() && !hostId.isEmpty(); }

    bool operator==(const MoonlightBinding& o) const {
        return slotId == o.slotId && hostId == o.hostId && controllerType == o.controllerType;
    }

    QJsonObject toJson() const;
    static MoonlightBinding fromJson(const QJsonObject& obj);
};

} // namespace dish::models
