// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Wire-protocol & UI-aggregation DTOs for protocol-1 (satellite/docs/contract.md).
// The REST request/response shapes mirror dish-android's core/model/Models.kt +
// SatelliteHttpClient.kt verbatim so the JSON on the wire (and any persisted
// blobs) stay byte-for-byte compatible. The pure protocol CONSTANTS (opcodes,
// caps, apply results, close reasons) live Qt-free in core/model/Protocol.h;
// this file is the Qt/QJson DTO surface that parses/serialises them.

#pragma once

#include "core/model/Protocol.h"
#include "core/reducer/DirectClaimFailure.h"
#include "core/reducer/PathChoice.h"
#include "core/reducer/UsbPathMachine.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <optional>

namespace dish::models {

inline constexpr int kDefaultUdpPort = 9876;
// The satellite's client-facing API is HTTPS (TLS) on a single port 9443. Both
// the connection API and pairing share it; discovery advertises 9443 under the
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
    // Stable per-install satellite identity from the beacon ("machineId") /
    // mDNS TXT ("mid"). Empty for satellites that predate it. Protocol-1 keys
    // remembered satellites on this (never ip/port) — see `id()`.
    QString machineId;
    // Discovery path this server was heard on. Not serialised — `toJson` /
    // `fromJson` omit it, so a decoded beacon keeps the Broadcast default.
    DiscoverySource source = DiscoverySource::Broadcast;

    // The stable identity a dish keys a satellite on. Prefers `machineId`
    // (survives DHCP address changes), falls back to ip:udpPort for older
    // satellites that don't advertise one. Both discovery paths and the
    // remembered store key on this, so one physical receiver collapses to a
    // single entry instead of one row per IP. Mirrors dish-android
    // DiscoveredServer.stableKey + SatelliteConnection.idFor.
    QString id() const {
        if (!machineId.isEmpty()) { return QStringLiteral("mid:%1").arg(machineId); }
        return QStringLiteral("wifi:%1:%2").arg(ip).arg(udpPort);
    }
    bool isValid() const { return !ip.isEmpty(); }

    QJsonObject toJson() const;

    // Lenient parse: any missing field falls back to its default — the discovery
    // beacon from the satellite server omits `ip` (the recipient observes it
    // from the packet source). See `satellite/src/net/discovery.cpp`.
    static DiscoveredServer fromJson(const QJsonObject& obj);
};

// POST /api/pair response (and /api/pair/status reuse via fromStatusJson).
// Protocol-1: the PIN paths always answer HTTP 200; the dish classifies on
// ok/pending. Path B replies {ok:false, pending:true}; rotation/Path A reply
// {ok:true, sharedKey}. `reachable` is NOT on the wire — it is set client-side
// (true once a JSON body parses, false on synthesised network-error responses).
struct PairResponse {
    bool ok = false;
    bool pending = false;          // Path B: awaiting operator approval
    std::optional<QString> status; // /api/pair/status: approved|pending|denied|none
    std::optional<QString> error;
    std::optional<QString> sharedKey;
    int protocolVersion = proto::kProtocolVersion;
    // HTTP status of the exchange (0 = transport never produced a response).
    // Lets the manager spot a 409 version mismatch without re-reading the body.
    int httpStatus = 0;
    bool reachable = false;

    static PairResponse fromJson(const QJsonObject& obj);
    // GET /api/pair/status body → PairResponse (sets status + sharedKey + ok).
    static PairResponse fromStatusJson(const QJsonObject& obj);
};

// One controller's apply outcome inside a session/controller PUT response.
// `result` is the protocol string (never localized); `resultCode` is its
// proto::kApply* mapping. `motion*` mirror the response's motion sub-object.
struct ControllerApplyDto {
    int ctrlIdx = 0;
    QString result;
    std::uint8_t resultCode = proto::kApplyUnknown;
    int appliedType = proto::kControllerTypeXbox;
    bool motionSinkSupportedForType = false;
    bool motionBackendOk = false;

    bool ok() const { return resultCode == proto::kApplyOk; }
    // replugFailed leaves the PREVIOUS pad live (appliedType reports it):
    // streams keep flowing rather than killing a working pad.
    bool slotIsLive() const { return proto::applyResultSlotIsLive(resultCode); }

    static ControllerApplyDto fromJson(const QJsonObject& obj);
};

// Host-feature grant (server policy, returned in the PUT/GET response).
struct HostFeatureGrant {
    bool granted = false;
    std::optional<QString> reason; // notSupported|backendUnavailable|denied, when !granted

    static HostFeatureGrant fromJson(const QJsonObject& obj);
};

// PUT /api/connections response. Also doubles as the error body (error/code).
// `sessionSalt` (16-hex → 8 bytes) + `token` feed deriveSessionKey; missing
// `sessionSalt` means the key can't be derived (a pre-protocol-1 server).
struct SessionResponse {
    std::optional<QString> connectionId;
    std::optional<QString> token;       // 8-hex (4 bytes BE)
    std::optional<QString> sessionSalt; // 16-hex (8 bytes)
    int epoch = 0;
    int maxControllers = 16;
    int protocolVersion = proto::kProtocolVersion;
    QList<ControllerApplyDto> controllers;
    HostFeatureGrant mouseControl;
    std::optional<QString> error;
    // Machine-readable 401 cause: NOT_PAIRED | BAD_PROOF. Either is terminal.
    std::optional<QString> code;
    int httpStatus = 0;
    bool reachable = false;

    bool unauthorized() const {
        return code.has_value() && (*code == QLatin1String(proto::kAuthCodeNotPaired.data()) ||
                                    *code == QLatin1String(proto::kAuthCodeBadProof.data()));
    }

    static SessionResponse fromJson(const QJsonObject& obj);
};

// PUT /api/connections/{id}/controllers/{idx} response: one controller's apply
// result + the session epoch (no token rotation on the per-controller route).
struct ControllerPutResponse {
    int epoch = 0;
    std::optional<ControllerApplyDto> controller;
    std::optional<QString> error;
    std::optional<QString> code;
    int httpStatus = 0;
    bool reachable = false;

    bool unauthorized() const {
        return code.has_value() && (*code == QLatin1String(proto::kAuthCodeNotPaired.data()) ||
                                    *code == QLatin1String(proto::kAuthCodeBadProof.data()));
    }

    static ControllerPutResponse fromJson(const QJsonObject& obj);
};

// One applied controller from GET /api/connections/{id} (the reconcile view).
struct SessionViewControllerDto {
    int ctrlIdx = 0;
    bool active = false;
    int appliedType = proto::kControllerTypeXbox;
    QString touchpadMode;
    // Applied caps word folded from the view's `caps` object. `capsPresent`
    // distinguishes "server omitted the block" (older satellite) from an
    // all-false fold, so reconcile only compares caps it was actually told.
    std::uint16_t caps = 0;
    bool capsPresent = false;
    // Per-controller motion delivery truth (absent on older satellites): does
    // the applied type have a motion sink, and is the backend healthy for it.
    std::optional<bool> motionSinkSupportedForType;
    std::optional<bool> motionBackendOk;

    static SessionViewControllerDto fromJson(const QJsonObject& obj);
};

// GET /api/connections/{id}: the reconcile endpoint's applied state + epoch.
struct SessionViewDto {
    std::optional<QString> connectionId;
    int epoch = 0;
    int protocolVersion = proto::kProtocolVersion;
    int maxControllers = 16;
    QList<SessionViewControllerDto> controllers;
    HostFeatureGrant mouseControl;
    std::optional<QString> error;
    std::optional<QString> code;
    int httpStatus = 0;
    bool reachable = false;

    bool unauthorized() const {
        return code.has_value() && (*code == QLatin1String(proto::kAuthCodeNotPaired.data()) ||
                                    *code == QLatin1String(proto::kAuthCodeBadProof.data()));
    }

    static SessionViewDto fromJson(const QJsonObject& obj);
};

// One entry of the capabilities `host` block: the receiver's own inventory for
// a feature slug. `supported` is the static fact; `available` is a coarse
// runtime read (bus open), absent when the server doesn't report it.
struct HostCapabilityDto {
    bool supported = false;
    std::optional<bool> available;
};

// GET /api/server/capabilities: current DYNAMIC backend health. Gates the
// motion/DS4 UI on live backend availability. Unauthenticated, so it is the
// only pre-pairing signal that the receiver's driver stack is broken.
struct CapabilitiesDto {
    int protocolVersion = proto::kProtocolVersion;
    QString serverVersion;
    int maxControllers = 16;
    QString backendId;
    bool backendSupported = false;
    bool backendAvailable = false;
    std::optional<QString> backendErrorCode;
    bool motionAvailable = false;
    // The `host` capability inventory (contract §capabilities). `hasHostBlock`
    // is the presence signal — an older satellite omits the block entirely and
    // the client falls back to catalog-era defaults rather than reading four
    // false entries as "receiver can do nothing".
    bool hasHostBlock = false;
    HostCapabilityDto hostCatalog;
    HostCapabilityDto hostMouseControl;
    HostCapabilityDto hostKeyboardControl;
    HostCapabilityDto hostRumble;
    int httpStatus = 0;
    bool reachable = false;

    static CapabilitiesDto fromJson(const QJsonObject& obj);
};

// GET /api/catalog sub-DTOs: the localized controller-type catalog drives the
// (later-wave) Emulate picker. Type names/descriptions render from here; feature
// slugs are capability data the client only offers when it has code for them.
struct CatalogFeatureDto {
    bool supported = false;
    std::optional<QString> requires_; // structured code e.g. "vigembus>=1.17"
    // Explicit mode slugs offered for this feature (e.g. touchpad → ["ds4"]).
    // Empty = a pre-modes catalog; the client falls back to its prior
    // assumption rather than gating the feature off.
    QStringList modes;
};

// OPTIONAL physical-pad identity hint on an offered type: which detected pad
// this virtual type is the natural default for. The mapping policy lives on
// the server so new hardware needs no client release.
struct CatalogEmulatesDto {
    QString sdlType;  // clients' SDL_GameControllerType vocabulary (ps4, ps5, …)
    QStringList usb;  // lowercase "vid:pid" identities
};

struct CatalogTypeDto {
    int id = 0;
    QString slug;
    QString name;
    QString shortName;
    QString description;
    QString imageHref;
    QString imageEtag;
    // Feature slug → support. Keys are protocol constants (rumble, motion, …).
    QHash<QString, CatalogFeatureDto> features;
    std::optional<CatalogEmulatesDto> emulates;

    static CatalogTypeDto fromJson(const QJsonObject& obj);
};

struct CatalogHostFeatureDto {
    bool supported = false;
    QStringList modes; // valid descriptor touchpadMode values for this host feature
};

struct CatalogDto {
    QString locale;
    int protocolVersion = proto::kProtocolVersion;
    // Catalog SCHEMA version, distinct from the wire protocol and the build.
    // A response omitting the field is the legacy v1 catalog (xbox360 + ds4,
    // no emulates) — absent reads as 1 so clients can branch on schema level
    // instead of sniffing for fields.
    int catalogVersion = 1;
    QString serverVersion;
    QString etag; // "<serverVersion>+<locale>" — cache key for If-None-Match
    QList<CatalogTypeDto> controllerTypes;
    QHash<QString, CatalogHostFeatureDto> hostFeatures;
    int httpStatus = 0;
    bool notModified = false; // 304 → caller serves its cache
    bool reachable = false;

    static CatalogDto fromJson(const QJsonObject& obj);
};

// Declarative per-controller desired state sent in the session/controller PUT
// body. Always sent WHOLE (a toggle = re-send with one field changed); the
// server converges. Owns its own JSON so the request shape is unit-testable
// without a socket. Mirrors dish-android core/net/ControllerDescriptor.
struct ControllerDescriptor {
    int ctrlIdx = 0;
    std::uint8_t type = proto::kControllerTypeXbox;
    std::uint16_t caps = 0; // proto::kCap* word
    std::uint8_t touchpadMode = proto::kTouchpadModeOff;

    // The single-descriptor JSON object (one element of the controllers[]
    // array, and the per-controller PUT body). `ctrlIdx` is included; on the
    // per-controller route the path's index wins server-side anyway.
    QJsonObject toJson() const;
};

// Build the controllers[] array JSON from a desired descriptor list.
QJsonArray controllersJson(const QList<ControllerDescriptor>& descriptors);

// UI-facing link state for one connection. This is the chip a row renders;
// combines the persistent "Pairing" axis (have we paired?) and the live
// "Presence" axis (do we see it / is the session up?).
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
// **Stale** is now reachable: a terminal 401 (NOT_PAIRED/BAD_PROOF) or a
// close-notify(unpaired) drops the key and parks the row here so the chip reads
// "Needs pairing" and auto-retry stops.
//
// **Unstable** is NOT YET ENTERED: it requires the native layer to expose the
// consecutive-missed-heartbeat count separately from the binary alive poll.
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
// Live, measured input rates for a slot — the small "live-stats" numbers the
// slot card shows, mirroring dish-android SlotInputRates rendered by
// ControllerAdapter. `gamepadHz` / `motionHz` are the current quantized Hz of
// the report and IMU streams; the `*PeakHz` are the high-water marks (shown with
// a "~" prefix when the live value is idle). `directPollHz` is the independently-
// measured USB-direct poll rate (URB completion rate) for a synthetic pad — 0
// for a non-direct pad or before the first measurement. All 0 until the
// InputRateStore / poll sampler produce a reading; `hasAny()` gates the row.
struct SlotLiveRates {
    int gamepadHz = 0;
    int gamepadPeakHz = 0;
    int motionHz = 0;
    int motionPeakHz = 0;
    int directPollHz = 0;

    bool hasAny() const {
        return gamepadHz > 0 || gamepadPeakHz > 0 || motionHz > 0 || motionPeakHz > 0 ||
               directPollHz > 0;
    }
    bool operator==(const SlotLiveRates& o) const {
        return gamepadHz == o.gamepadHz && gamepadPeakHz == o.gamepadPeakHz &&
               motionHz == o.motionHz && motionPeakHz == o.motionPeakHz &&
               directPollHz == o.directPollHz;
    }
    bool operator!=(const SlotLiveRates& o) const { return !(*this == o); }
};

struct ControllerSlot {
    QString id;
    QString name;
    std::optional<QString> boundConnectionId;
    std::optional<ConnectionSummary> boundStatus;
    // The resolved emulation type's localized short name ("Xbox 360",
    // "DualShock 4") for the bound sub-line's "· as <type>" suffix. Empty when
    // unbound or the catalog offers no name (the row then omits the suffix
    // rather than guessing).
    QString emulateName;
    // True while the pad is attaching (SDL saw the device but the slot hasn't
    // settled) — the card renders the busy "Registering controller…" state
    // instead of chips/actions.
    bool registering = false;
    // Hardware capabilities detected by SDLGamepadBridge when the device
    // attached. Drives the capability indicator in SlotCard.
    ControllerCapabilities capabilities;
    // True iff this slot is a USB-direct (raw-HID) synthetic rather than an SDL
    // pad — drives whether the slot card shows the gamepad Hz live (Direct
    // streams continuously) vs. as a "~peak". Mirrors android's
    // currentMode == Direct check on the PathCard.
    bool usbDirect = false;
    // True iff this slot is a RAW-joystick-backed SDL pad whose DirectInput
    // routing the "Configure controls" page may remap (the mapJoystick /
    // JoystickRemap path). False for synthetics (USB-direct), the virtual slot,
    // and SDL-recognised game controllers — those use SDL's own mapping and
    // ignore any remap, so the page entry must NOT show for them. Stamped from
    // the bridge Device::isRawJoystick in AppModel::rebuild for SDL slots.
    bool remappable = false;
    // Live measured rates the slot card renders as small live-stats chips.
    // Refreshed ~1 Hz off the InputRateStore / USB poll sampler, independent of
    // the slot-list shape.
    SlotLiveRates liveRates;

    // ── USB input-path state (the Standard/Direct control) ───────────────────
    // Stamped in AppModel::rebuild() by cross-referencing the matching
    // UsbController (by vid/pid) via the pure reducer::slotPathFields mapper.
    // They drive the per-slot Standard/Direct control in the Controllers page:
    // the FSM phase (toggle reflected + in-flight spinner), the resolved/stored
    // desired path (which segment reads selected), whether the device is
    // path-switchable at all (a raw-HID-claimable controller exists for it —
    // false for an Xbox/XInput pad, which hides the control), and the last
    // Direct-claim failure reason for the inline note. Defaults are the inert
    // "no controller" state for a slot with no USB path entry.
    reducer::UsbPhase pathPhase = reducer::UsbPhase::Routed;
    reducer::PathChoice desiredPath = reducer::PathChoice::Standard;
    bool pathSupported = false;
    std::optional<reducer::DirectClaimFailure> directFailure;
};

struct RememberedWifi {
    QString id;
    QString name;
    QString ip;
    int udpPort = kDefaultUdpPort;
    int pairPort = kDefaultPairPort;
    int httpPort = kDefaultHttpPort;
    // Persisted machineId so a remembered satellite that changes IP keeps its
    // identity (the `id` is already the machineId-preferring stable key).
    QString machineId;

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
