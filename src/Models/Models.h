// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Wire-protocol & UI-aggregation DTOs. Field names mirror dish-mac/Models.swift
// and dish-android/Models.kt verbatim so the JSON shape on the wire (and any
// persisted blobs) stay byte-for-byte compatible.

#pragma once

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>

namespace dish::models {

inline constexpr int kDefaultUdpPort = 9876;
// The satellite's client-facing API is HTTPS (TLS) on a single port. Both the
// connection API and pairing now share it; discovery advertises 9443 under the
// `http` and `pair` TXT keys (and the legacy beacon's httpPort/pairPort JSON
// fields), so both constants resolve to the same value.
inline constexpr int kDefaultHttpPort = 9443;
inline constexpr int kDefaultPairPort = 9443;

// Which discovery path surfaced a satellite. mDNS / Bonjour is the modern
// path; Broadcast is the legacy UDP beacon; Both means it answered on each.
// Not a wire field — assigned client-side by the discovery merge.
enum class DiscoverySource { Broadcast, Mdns, Both };

// Short human label for the connections list. Wrapped in
// QCoreApplication::translate so the labels participate in the .ts catalog;
// the strings are protocol acronyms (UDP / mDNS) and will typically read the
// same in every locale, but routing them through translate keeps the i18n
// pipeline complete and lets a future translator override if needed.
inline QString discoverySourceLabel(DiscoverySource source) {
    constexpr const char* ctx = "dish::models::DiscoverySource";
    switch (source) {
    case DiscoverySource::Broadcast:
        return QCoreApplication::translate(ctx, "UDP broadcast");
    case DiscoverySource::Mdns:
        return QCoreApplication::translate(ctx, "mDNS");
    case DiscoverySource::Both:
        return QCoreApplication::translate(ctx, "mDNS + broadcast");
    }
    return {};
}

struct DiscoveredServer {
    QString name;
    QString ip;
    int udpPort = kDefaultUdpPort;
    int pairPort = kDefaultPairPort;
    int httpPort = kDefaultHttpPort;
    // Discovery path this server was heard on. Not serialised — `toJson` /
    // `fromJson` omit it, so a decoded beacon keeps the Broadcast default.
    DiscoverySource source = DiscoverySource::Broadcast;

    QString id() const { return QStringLiteral("wifi:%1:%2").arg(ip).arg(udpPort); }
    bool isValid() const { return !ip.isEmpty(); }

    QJsonObject toJson() const;

    // Lenient parse: any missing field falls back to its default — the discovery
    // beacon from the satellite server omits `ip` (the recipient observes it
    // from the packet source). See `satellite/src/net/discovery.cpp`.
    static DiscoveredServer fromJson(const QJsonObject& obj);
};

struct PairResponse {
    bool ok = false;
    std::optional<QString> error;
    std::optional<QString> sharedKey;
    // True iff we received any JSON body from the server. False for synthesized
    // failure responses (socket / connect / send errors). Not on the wire —
    // the server never sends this field; it's set client-side by
    // `PairingClient::pair` so the manager can distinguish "moved networks"
    // from "needs PIN". Mirrors dish-mac PairResponse.reachable.
    bool reachable = false;

    static PairResponse fromJson(const QJsonObject& obj);
};

struct ConnectResponse {
    std::optional<QString> connectionId;
    std::optional<QString> token;
    std::optional<QString> error;

    static ConnectResponse fromJson(const QJsonObject& obj);
};

// UI-facing link state for one connection. This is the chip a row renders;
// combines the persistent "Pairing" axis (have we paired?) and the live
// "Presence" axis (do we see it / is the session up?).
//
// Internally a Satellite session also has [net::SessionState] (the wire-level
// presence axis only); [LinkState] is derived from that plus discovery /
// remembered presence in [ConnectionHub::rebuild].
//
// | LinkState  | Pairing axis    | Presence axis    | User-facing chip |
// |------------|-----------------|------------------|------------------|
// | Found      | unpaired        | seen             | "Found"          |
// | Stale      | broken (lost)   | any              | "Needs pairing"  |
// | Saved      | paired          | absent           | "Offline"        |
// | Ready      | paired          | seen, no session | "Ready"          |
// | Connecting | paired          | linking          | "Connecting…"    |
// | Connected  | paired          | live             | "Online"         |
// | Unstable   | paired          | faltering        | "Unsteady"       |
//
// **Stale** is NOT YET ENTERED: it requires the satellite to return a
// `PAIRING_UNKNOWN` error so the client can distinguish "peer forgot us"
// from a generic connect failure. Until that protocol change lands, a
// server-side forget surfaces as a generic disconnect.
//
// **Unstable** is NOT YET ENTERED: it requires the native layer to expose
// the consecutive-missed-heartbeat count separately from the binary alive
// poll. Today the connection flips Connected → (Saved | Ready) directly
// when misses hit the death threshold.
enum class LinkState { Found, Stale, Saved, Ready, Connecting, Connected, Unstable };

// What a physical controller's *hardware* exposes, detected once at attach by
// SDLGamepadBridge. Distinct from any user "forward this feature?" preference —
// this is purely a hardware-capability statement. The slot card surfaces it as
// a chip so the player can tell apart "my pad has no gyro" (an Xbox pad) from
// "gyro is switched off". Mirrors dish-mac's `ControllerCapabilities`.
struct ControllerCapabilities {
    // True iff SDL reported an IMU (gyro and/or accelerometer) for the device
    // — DualSense / DualShock 4 / Switch Pro / Joy-Con. False for Xbox 360 /
    // Xbox One pads, which have no motion hardware.
    bool hasMotion = false;

    // True iff SDL reported an addressable RGB LED for the device
    // (SDL_GameControllerHasLED) — DualSense / DualShock 4. Drives the slot
    // card's lightbar chip and the CAP_LIGHTBAR advertisement.
    bool hasLightbar = false;

    // Most recent battery sample for the pad — the same (level, status) pair
    // forwarded on MSG_BATTERY. For a wireless pad this is the controller's
    // own charge; for a wired/unknown pad it is the host machine's battery
    // (the laptop's percentage, or 100 % / WIRED on a desktop). The slot card
    // renders it as a battery chip. `batteryLevel` is 0..100 percent or 0xFF
    // (unknown); `batteryStatus` is a SatelliteClient::kBatteryStatus*
    // constant. 0xFF / 0 until the first 30 s poll completes.
    std::uint8_t batteryLevel = 0xFF;
    std::uint8_t batteryStatus = 0;
};

struct ConnectionSummary {
    QString id;
    QString label;
    QString detail;
    LinkState live = LinkState::Saved;
    std::optional<QString> boundSlotId;
};

// Controller slot. Mirrors dish-mac's `ControllerSlot`. The "virtual"
// touch-controller variant the Android client exposes has no input source
// on Windows (no touch, no on-screen pad), so SlotInputType and the
// physicalDeviceId field were dropped — same removal dish-mac did in PR #7
// for the same reason on macOS.
struct ControllerSlot {
    QString id;
    QString name;
    std::optional<QString> boundConnectionId;
    std::optional<ConnectionSummary> boundStatus;
    // Hardware capabilities detected by SDLGamepadBridge when the device
    // attached. Drives the capability indicator in SlotCard.
    ControllerCapabilities capabilities;
};

struct RememberedWifi {
    QString id;
    QString name;
    QString ip;
    int udpPort = kDefaultUdpPort;
    int pairPort = kDefaultPairPort;
    int httpPort = kDefaultHttpPort;

    DiscoveredServer toDiscovered() const;
    QJsonObject toJson() const;
    static RememberedWifi fromJson(const QJsonObject& obj);
};

QJsonArray rememberedListToJson(const QList<RememberedWifi>& list);
QList<RememberedWifi> rememberedListFromJson(const QJsonArray& arr);

// Typed user-facing notification. Mirrors dish-android's `DishNotification`
// (core/model/DishNotification.kt). A pure value type — the queue surface
// (NotificationQueue) and renderer (NotificationToastHost) consume it.
//
// `kind` is a free-form short tag for callers that want to dedup or categorise
// programmatically ("server-unreachable", "session-lost", etc.); the renderer
// itself does not switch on it. `severity` picks the rail / outline tint, the
// way Android's Severity does. `dismissible` toggles a leading-edge close
// affordance — persistent banners that the user can't dismiss (e.g. a
// hardware-off warning) set this to false. `durationMs` is in ms; the
// PERSISTENT sentinel keeps the toast up until the caller dismisses it
// explicitly via NotificationQueue::dismiss.
struct DishNotification {
    enum class Severity { Info, Success, Warn, Error };

    // Sentinel values for `durationMs`. Mirrors Android's
    // DishNotification.Companion (DURATION_SHORT / _LONG / _PERSISTENT) so the
    // two clients use the same wall-clock vocabulary for transient banners.
    static constexpr int kDurationShortMs = 3'500;
    static constexpr int kDurationLongMs = 6'000;
    static constexpr int kDurationPersistent = 0;

    int id = 0;
    QString kind;
    Severity severity = Severity::Info;
    QString message;
    bool dismissible = true;
    int durationMs = kDurationShortMs;
};

} // namespace dish::models
