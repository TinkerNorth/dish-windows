// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/AppViewModel.h"

#include "AppModel.h"
#include "Input/GamepadInputProcessor.h"
#include "Input/JoystickMapping.h"
#include "Input/SDLGamepadBridge.h"
#include "Network/WifiConnectionManager.h"
#include "composer/CatalogComposer.h"
#include "composer/ConnectionCoordinator.h"
#include "core/input/Deadzones.h"
#include "core/reducer/ConnectionRows.h"
#include "core/reducer/PathChoice.h"
#include "core/reducer/PickerVisibility.h"
#include "core/reducer/SlotPathFields.h"
#include "source/usb/UsbGamepadManager.h"
#include "qml/AppSettingsMaps.h"
#include "repository/DeadzoneRepository.h"
#include "source/store/CrashReportingStore.h"
#include "source/store/MotionEnabledStore.h"
#include "source/store/OnboardingPreferenceStore.h"
#include "source/store/ThemePreferenceStore.h"
#include "UI/Theme.h"
#include "UI/licenses/LicenseManifest.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <map>
#include <optional>

#ifndef DISH_VERSION
#define DISH_VERSION "0.0.0"
#endif

namespace dish::qml {

namespace {

// "Bound to <ip> • UDP <port>" detail not needed here — the header strings
// mirror MainWindow::rebuildHeader, which keys off ConnectionSummary only.
QString tr(const char* s) { return QCoreApplication::translate("AppViewModel", s); }

// models::LinkState and reducer::UiLinkState are the SAME 7-value enum in the
// same order (the latter is the Qt-free mirror); the picker rows carry a
// models::LinkState, so cross over to feed the reducer render-key mappers.
reducer::UiLinkState toUiLinkState(models::LinkState s) {
    return static_cast<reducer::UiLinkState>(s);
}

// Same token strings ConnectionListModel exposes (dotToken/glyphToken there), so
// a bind-chooser row renders identically to a Connections-page row by
// construction — both go through the same reducer mapper + token switch.
QString dotColorToken(models::LinkState s) {
    switch (reducer::dotColorForState(toUiLinkState(s))) {
    case reducer::DotColor::Success:
        return QStringLiteral("success");
    case reducer::DotColor::Primary:
        return QStringLiteral("primary");
    case reducer::DotColor::Warning:
        return QStringLiteral("warning");
    case reducer::DotColor::Muted:
        return QStringLiteral("muted");
    }
    return {};
}

QString glyphToken(models::LinkState s) {
    switch (reducer::glyphForConnection(reducer::ConnectionKind::Satellite, toUiLinkState(s))) {
    case reducer::ConnectionGlyph::SatelliteBase:
        return QStringLiteral("satelliteBase");
    case reducer::ConnectionGlyph::SatelliteConnected:
        return QStringLiteral("satelliteConnected");
    case reducer::ConnectionGlyph::SatelliteOff:
        return QStringLiteral("satelliteOff");
    }
    return {};
}

// ── Configure-controls token maps ────────────────────────────────────────────
// The page passes a stable string for the logical OUTPUT it is assigning; map it
// to the pure RemapTarget enum. A string the C++ doesn't know returns nullopt so
// the caller no-ops (forward-compat: a newer page can introduce a target without
// crashing an older C++). The strings match the JoystickRemap field names.
std::optional<input::RemapTarget> remapTargetFromString(const QString& t) {
    using T = input::RemapTarget;
    static const std::map<QString, T> kMap{
        {QStringLiteral("a"), T::A},
        {QStringLiteral("b"), T::B},
        {QStringLiteral("x"), T::X},
        {QStringLiteral("y"), T::Y},
        {QStringLiteral("dpadUp"), T::DpadUp},
        {QStringLiteral("dpadDown"), T::DpadDown},
        {QStringLiteral("dpadLeft"), T::DpadLeft},
        {QStringLiteral("dpadRight"), T::DpadRight},
        {QStringLiteral("leftShoulder"), T::LeftShoulder},
        {QStringLiteral("rightShoulder"), T::RightShoulder},
        {QStringLiteral("back"), T::Back},
        {QStringLiteral("start"), T::Start},
        {QStringLiteral("leftThumb"), T::LeftThumb},
        {QStringLiteral("rightThumb"), T::RightThumb},
        {QStringLiteral("leftStickX"), T::LeftStickX},
        {QStringLiteral("leftStickY"), T::LeftStickY},
        {QStringLiteral("rightStickX"), T::RightStickX},
        {QStringLiteral("rightStickY"), T::RightStickY},
        {QStringLiteral("leftTrigger"), T::LeftTrigger},
        {QStringLiteral("rightTrigger"), T::RightTrigger},
    };
    const auto it = kMap.find(t);
    if (it == kMap.end()) { return std::nullopt; }
    return it->second;
}

// A trigger source rendered as a JS object {kind:"axis"|"button", index:int}.
QVariantMap triggerSourceToMap(const input::TriggerSource& src) {
    QVariantMap m;
    m[QStringLiteral("kind")] = src.kind == input::TriggerSourceKind::Button
                                    ? QStringLiteral("button")
                                    : QStringLiteral("axis");
    m[QStringLiteral("index")] = src.index;
    return m;
}

} // namespace

AppViewModel::AppViewModel(dish::AppModel* model, QObject* parent)
    : QObject(parent), model_(model), slotModel_(this), connectionModel_(this) {
    QObject::connect(model_, &dish::AppModel::stateChanged, this, &AppViewModel::onStateChanged);
    QObject::connect(model_, &dish::AppModel::errorMessage, this, &AppViewModel::errorMessage);
    QObject::connect(model_->connections(), &composer::ConnectionCoordinator::connectionsChanged,
                     this, &AppViewModel::onConnectionsChanged);
    // Relay raw-input captures (configure-controls page). Filtered to the
    // capturing slot in the handler so only that page's assignment fires.
    QObject::connect(model_, &dish::AppModel::rawJoystickInput, this,
                     &AppViewModel::onRawJoystickInput);

    // Re-pull the discovered list + scan flag on their precise edges (P2 had to
    // key off the broad stateChanged, and had no scan-flag NOTIFY at all). The
    // WifiConnectionManager owns the scan results. Routed through explicit
    // lambdas (not bare signal→signal) so the emission is unambiguous, mirroring
    // the working connectionModel path below.
    QObject::connect(model_->wifi(), &net::WifiConnectionManager::discoveredChanged, this,
                     [this] { emit discoveredChanged(); });
    QObject::connect(model_->wifi(), &net::WifiConnectionManager::scanningChanged, this,
                     [this] { emit scanningChanged(); });
    QObject::connect(model_->wifi(), &net::WifiConnectionManager::reversePairingChanged, this,
                     [this] { emit reversePairingChanged(); });
    // The catalog fetch lifecycle (Idle/Loading/Success/Error) moved — fold it
    // into one emulate-state NOTIFY so the Emulate picker re-reads emulateLoading
    // / emulateError / emulateStale and shows a spinner, a typed error + retry, or
    // an empty-vs-content distinction (was: the fetch silently returned nothing).
    QObject::connect(model_, &dish::AppModel::catalogStateChanged, this,
                     [this] { emit emulateStateChanged(); });

    // The settings stores republish through their StateSource Observables (not Qt
    // signals); subscribe so a republish (incl. the ThemeController's own re-theme
    // path and any external setEnabled) re-emits our Qt NOTIFY. emitCurrent=false
    // — the initial values are read lazily by the getters; we only want deltas.
    themeSub_ = model_->themeStore()->state().subscribe(
        [this](const source::ThemeMode&) { emit themeModeChanged(); }, false);
    crashSub_ = model_->crashStore()->state().subscribe(
        [this](bool) { emit crashReportingChanged(); }, false);
    onboardingSub_ = model_->onboardingStore()->state().subscribe(
        [this](const source::OnboardingState&) {
            const bool needed = !model_->onboardingStore()->welcomeCompleted();
            if (needed != onboardingNeeded_) {
                onboardingNeeded_ = needed;
                emit onboardingNeededChanged();
            }
        },
        false);
    onboardingNeeded_ = !model_->onboardingStore()->welcomeCompleted();

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

    // Pairing-success edge: a fresh connection reached Connected (online count
    // rose). Mirrors the rising edge the Widgets pairing sheet closes on. Cheap
    // and best-effort — the QML sheet treats it as "a pair likely just landed".
    if (live > lastOnlineCount_) { emit pairingSucceeded(); }
    lastOnlineCount_ = live;

    // The slot list moving also covers a device attach/detach (the bridge feeds
    // rebuild()), so the deadzone device rows may have changed — nudge the page.
    emit deadzonesChanged();

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

namespace {
// Resolve a slot id to its model (vid, pid). A synthetic slot's id IS the packed
// vpKey string (parse it); an SDL slot's id ("sdl:<iid>") doesn't parse, so fall
// back to the bridge device list, matching the id. Returns nullopt when neither
// path yields an identity (an unknown slot, or an SDL pad SDL couldn't identify).
std::optional<std::pair<int, int>> resolveSlotVidPid(dish::AppModel* model, const QString& slotId) {
    if (const auto parsed = reducer::parseSyntheticSlotId(slotId.toStdString())) { return parsed; }
    for (const auto& d : model->bridge()->devices()) {
        if (d.id == slotId) {
            if (d.vendorId == 0 || d.productId == 0) { return std::nullopt; }
            return std::make_pair(d.vendorId, d.productId);
        }
    }
    return std::nullopt;
}
} // namespace

void AppViewModel::setSlotPath(const QString& slotId, const QString& choice) {
    auto* usb = model_->usbManager();
    if (usb == nullptr) { return; }
    const auto vidPid = resolveSlotVidPid(model_, slotId);
    if (!vidPid.has_value()) { return; }
    const auto [vendorId, productId] = *vidPid;
    // "auto" clears the override (the resolution policy decides); the two explicit
    // picks persist + drive a user-initiated Choose through the existing manager.
    if (choice == QLatin1String("auto")) {
        usb->clearChoice(vendorId, productId);
    } else if (choice == QLatin1String("direct")) {
        usb->setPathChoice(vendorId, productId, reducer::PathChoice::Direct);
    } else if (choice == QLatin1String("standard")) {
        usb->setPathChoice(vendorId, productId, reducer::PathChoice::Standard);
    }
    // An unrecognised string is ignored (forward-compat); a real pick triggers the
    // manager's effects + AppModel's rebuild, which re-emits stateChanged.
}

QVariantList AppViewModel::availableConnectionsForSlot(const QString& slotId) const {
    const auto& st = model_->state();

    // The slot's own current binding (the holdover the picker keeps even offline).
    std::optional<QString> boundConnectionId;
    for (const auto& s : st.slotList) {
        if (s.id == slotId) {
            boundConnectionId = s.boundConnectionId;
            break;
        }
    }

    // Candidate set = connections NOT bound to ANOTHER slot (mirrors
    // MainWindow.cpp's `available`), but we KEEP this slot's own binding in so the
    // reducer's holdover can surface it. The reducer then drops the non-available
    // unbound rows and re-adds the slot's own offline binding.
    QList<models::ConnectionSummary> candidates;
    for (const auto& c : st.connections) {
        const bool boundElsewhere = c.boundSlotId.has_value() && *c.boundSlotId != slotId;
        if (!boundElsewhere) { candidates.append(c); }
    }

    QVariantList out;
    for (const auto& c : reducer::connectionsVisibleInPicker(candidates, boundConnectionId)) {
        QVariantMap m;
        m[QStringLiteral("connectionId")] = c.id;
        m[QStringLiteral("label")] = c.label;
        m[QStringLiteral("dotColor")] = dotColorToken(c.live);
        m[QStringLiteral("glyph")] = glyphToken(c.live);
        out.append(m);
    }
    return out;
}

void AppViewModel::refreshEmulate(const QString& slotId) { model_->refreshCatalogForSlot(slotId); }

bool AppViewModel::emulateLoading() const {
    // Loading is only meaningful before the first content arrives; once we have
    // cached types a background revalidate shows them (stale), not a blank
    // spinner — so the picker treats "loading" as "loading AND nothing to show".
    const auto& s = model_->catalogState();
    return s.isLoading() && !s.hasData();
}

QString AppViewModel::emulateError() const {
    // Localize the typed CatalogError at the UI edge (the core carries a reason
    // code, not a string). Empty string = no error to show.
    const auto& s = model_->catalogState();
    if (!s.isError() || !s.error.has_value()) { return {}; }
    switch (*s.error) {
    case source::CatalogError::Unreachable:
        return tr("Couldn't reach the satellite to load controller types.");
    case source::CatalogError::ServerError:
        return tr("The satellite couldn't provide controller types right now.");
    case source::CatalogError::Malformed:
        return tr("The satellite sent an unreadable controller-type list.");
    }
    return {};
}

bool AppViewModel::emulateStale() const { return model_->catalogState().stale; }

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
        // The discovery-source label (Widgets ConnectionsDialog showed it on the
        // FOUND row, e.g. "mDNS + broadcast").
        m[QStringLiteral("source")] = models::discoverySourceLabel(s.source);
        m[QStringLiteral("id")] = s.id();
        out.append(m);
    }
    return out;
}

void AppViewModel::connectByServerId(const QString& serverId) {
    // Resolve the server out of the live list by its stable id — de-raced vs. an
    // index that goes stale if the list reorders. Matches Widgets onConnectClicked.
    for (const auto& s : model_->wifi()->discoveredServers()) {
        if (s.id() == serverId) {
            model_->wifi()->connectTo(s);
            return;
        }
    }
}

void AppViewModel::reconnectConnection(const QString& connectionId) {
    model_->connections()->reconnectConnection(connectionId);
}

void AppViewModel::disconnectConnection(const QString& connectionId) {
    model_->connections()->disconnectConnection(connectionId);
}

void AppViewModel::forgetConnection(const QString& connectionId) {
    model_->connections()->forgetConnection(connectionId);
}

void AppViewModel::pairByServerId(const QString& serverId, const QString& pin) {
    for (const auto& s : model_->wifi()->discoveredServers()) {
        if (s.id() == serverId) {
            model_->wifi()->pairWithPin(s, pin);
            return;
        }
    }
}

bool AppViewModel::isPairingInFlight(const QString& serverId) const {
    return model_->wifi()->isPairingInFlight(serverId);
}

void AppViewModel::clearPairingTarget() { model_->clearPairingTarget(); }

// ── Reverse (host-initiated) pairing ─────────────────────────────────────────

namespace {
// Map the manager's reverse-pairing phase enum to the stable string the QML
// sheet switches on. Kept here (not in the manager) so the wire string is a
// view-model concern; a unit test pins this mapping.
QString reversePhaseString(net::ReversePairingPhase phase) {
    switch (phase) {
    case net::ReversePairingPhase::Idle:
        return QStringLiteral("idle");
    case net::ReversePairingPhase::AwaitingApproval:
        return QStringLiteral("awaiting");
    case net::ReversePairingPhase::Approved:
        return QStringLiteral("approved");
    case net::ReversePairingPhase::Declined:
        return QStringLiteral("declined");
    case net::ReversePairingPhase::TimedOut:
        return QStringLiteral("timedout");
    }
    return QStringLiteral("idle");
}
} // namespace

QString AppViewModel::reversePairingPhase() const {
    return reversePhaseString(model_->wifi()->reversePairingPhase());
}

QString AppViewModel::reversePairingPin() const { return model_->wifi()->reversePairingPin(); }

QString AppViewModel::reversePairingServerName() const {
    return model_->wifi()->reversePairingServerName();
}

void AppViewModel::requestReversePairing(const QString& serverId) {
    // De-raced resolve out of the live discovered list, mirroring connectByServerId.
    for (const auto& s : model_->wifi()->discoveredServers()) {
        if (s.id() == serverId) {
            model_->wifi()->requestReversePairing(s);
            return;
        }
    }
}

void AppViewModel::cancelReversePairing() { model_->wifi()->cancelReversePairing(); }

// ── Settings: appearance + diagnostics ──────────────────────────────────────

int AppViewModel::themeMode() const { return themeModeToInt(model_->themeStore()->mode()); }

void AppViewModel::setThemeMode(int mode) {
    const source::ThemeMode next = themeModeFromInt(mode);
    // Forward to the store. The ThemeController (subscribed to the same store)
    // resolves SYSTEM + swaps the active palette + re-applies the global QSS, so
    // the palette is already current when we push the resolved appearance to the
    // QML Theme singleton + the chrome dark-mode attribute below. setMode is
    // distinct-until-changed; our themeSub_ re-emits themeModeChanged on a real
    // transition, so we don't double-emit here.
    model_->themeStore()->setMode(next);
    // Push the now-resolved appearance to the QML side + the native chrome so the
    // live palette and the title-bar immersive-dark attribute follow the mode.
    if (themeAppliedSink_) { themeAppliedSink_(ui::activeAppearance() == ui::Appearance::Dark); }
}

bool AppViewModel::crashReportingEnabled() const { return model_->crashStore()->enabled(); }

void AppViewModel::setCrashReportingEnabled(bool enabled) {
    model_->crashStore()->setEnabled(enabled);
}

QString AppViewModel::appVersion() const { return QStringLiteral(DISH_VERSION); }

bool AppViewModel::onboardingNeeded() const {
    return !model_->onboardingStore()->welcomeCompleted();
}

QString AppViewModel::donateSponsorsUrl() const {
    return tr("https://github.com/sponsors/TinkerNorth");
}
QString AppViewModel::donateKofiUrl() const { return tr("https://ko-fi.com/tinkernorth"); }
QString AppViewModel::donateBmacUrl() const { return tr("https://buymeacoffee.com/tinkernorth"); }

// ── Deadzone settings ───────────────────────────────────────────────────────

QVariantList AppViewModel::deadzoneDevices() const {
    return deadzoneDeviceRows(model_->bridge(), model_->deadzoneRepository(),
                              model_->motionEnabledStore());
}

void AppViewModel::setDeadzones(const QString& deviceId, int stickFlat, int triggerFlat) {
    const input::deadzone::Deadzones dz{static_cast<std::int16_t>(stickFlat),
                                        static_cast<std::uint8_t>(triggerFlat)};
    // Persist the override AND push it into the live processor — the exact pair
    // the Widgets view (repo->setDeadzones) + MainWindow (AppModel::applyDeadzones)
    // do, so a slider move re-tunes the hot path without a re-attach.
    model_->deadzoneRepository()->setDeadzones(deviceId, dz);
    model_->applyDeadzones(deviceId, dz);
    emit deadzonesChanged();
}

void AppViewModel::setMotionEnabled(const QString& deviceId, bool enabled) {
    // Keyed by the device id (the Widgets view's slotKey == deviceId.toStdString()).
    model_->motionEnabledStore()->setEnabled(deviceId.toStdString(), enabled);
    emit deadzonesChanged();
}

// ── Configure-controls (raw-joystick remap) ──────────────────────────────────

QVariantMap AppViewModel::slotRemap(const QString& slotId) const {
    QVariantMap out;
    const auto vidPid = resolveSlotVidPid(model_, slotId);
    if (!vidPid.has_value()) { return out; }
    const auto [vendorId, productId] = *vidPid;
    const input::JoystickRemap r = model_->remapFor(vendorId, productId);

    // A flat JS object the page renders: which raw source each logical output
    // reads + the invert/hat states. Button indices are the raw source-button
    // indices (-1 = unassigned); the two trigger sources are {kind,index} objects.
    using Btn = input::RemapButton;
    const auto btn = [&](Btn b) { return r.buttons[static_cast<int>(b)]; };
    out[QStringLiteral("a")] = btn(Btn::A);
    out[QStringLiteral("b")] = btn(Btn::B);
    out[QStringLiteral("x")] = btn(Btn::X);
    out[QStringLiteral("y")] = btn(Btn::Y);
    out[QStringLiteral("dpadUp")] = btn(Btn::DpadUp);
    out[QStringLiteral("dpadDown")] = btn(Btn::DpadDown);
    out[QStringLiteral("dpadLeft")] = btn(Btn::DpadLeft);
    out[QStringLiteral("dpadRight")] = btn(Btn::DpadRight);
    out[QStringLiteral("leftShoulder")] = btn(Btn::LeftShoulder);
    out[QStringLiteral("rightShoulder")] = btn(Btn::RightShoulder);
    out[QStringLiteral("back")] = btn(Btn::Back);
    out[QStringLiteral("start")] = btn(Btn::Start);
    out[QStringLiteral("leftThumb")] = btn(Btn::LeftThumb);
    out[QStringLiteral("rightThumb")] = btn(Btn::RightThumb);
    out[QStringLiteral("leftStickX")] = r.leftStickX;
    out[QStringLiteral("leftStickY")] = r.leftStickY;
    out[QStringLiteral("rightStickX")] = r.rightStickX;
    out[QStringLiteral("rightStickY")] = r.rightStickY;
    out[QStringLiteral("leftTrigger")] = triggerSourceToMap(r.leftTrigger);
    out[QStringLiteral("rightTrigger")] = triggerSourceToMap(r.rightTrigger);
    out[QStringLiteral("hatIndex")] = r.hatIndex;
    out[QStringLiteral("invertLeftY")] = r.invertLeftY;
    out[QStringLiteral("invertRightY")] = r.invertRightY;
    return out;
}

void AppViewModel::assignSlotInput(const QString& slotId, const QString& target, int kind,
                                   int index) {
    const auto vidPid = resolveSlotVidPid(model_, slotId);
    if (!vidPid.has_value()) { return; }
    const auto tgt = remapTargetFromString(target);
    if (!tgt.has_value()) { return; } // unknown target — forward-compat no-op
    const auto [vendorId, productId] = *vidPid;
    // Fold the capture into the effective remap via the pure helper, then persist
    // (the store pushes into the bridge → takes effect on the next report).
    const input::JoystickRemap next =
        input::withAssignment(model_->remapFor(vendorId, productId), *tgt, kind, index);
    model_->setJoystickRemap(vendorId, productId, next);
}

void AppViewModel::setSlotInvert(const QString& slotId, const QString& which, bool on) {
    const auto vidPid = resolveSlotVidPid(model_, slotId);
    if (!vidPid.has_value()) { return; }
    std::optional<input::InvertTarget> inv;
    if (which == QLatin1String("leftY")) {
        inv = input::InvertTarget::LeftY;
    } else if (which == QLatin1String("rightY")) {
        inv = input::InvertTarget::RightY;
    }
    if (!inv.has_value()) { return; } // unknown flag — no-op
    const auto [vendorId, productId] = *vidPid;
    const input::JoystickRemap next =
        input::withInvert(model_->remapFor(vendorId, productId), *inv, on);
    model_->setJoystickRemap(vendorId, productId, next);
}

void AppViewModel::resetSlotRemap(const QString& slotId) {
    const auto vidPid = resolveSlotVidPid(model_, slotId);
    if (!vidPid.has_value()) { return; }
    const auto [vendorId, productId] = *vidPid;
    model_->clearJoystickRemap(vendorId, productId);
}

void AppViewModel::startInputCapture(const QString& slotId) {
    // Remember which slot is capturing so the rawJoystickInput relay filters to
    // it, then arm the bridge. A second start for another slot simply re-points
    // the filter (capture is global in the bridge; the slot filter is here).
    capturingSlotId_ = slotId;
    model_->setInputCaptureEnabled(true);
}

void AppViewModel::stopInputCapture() {
    capturingSlotId_.clear();
    model_->setInputCaptureEnabled(false);
}

void AppViewModel::onRawJoystickInput(const QString& deviceId, int kind, int index, int value) {
    // Map the source deviceId back to a slot id and re-emit ONLY for the slot the
    // page is currently capturing. For an SDL slot the slot id IS the device id; a
    // synthetic (USB-direct) slot is never a raw joystick, so a deviceId match
    // against the capturing slot is sufficient.
    if (capturingSlotId_.isEmpty() || deviceId != capturingSlotId_) { return; }
    emit rawInputCaptured(capturingSlotId_, kind, index, value);
}

// ── Licenses ────────────────────────────────────────────────────────────────

QVariantList AppViewModel::licenses() const {
    return licenseRows(ui::loadBundledLicenseManifest());
}

// ── Onboarding + external links ─────────────────────────────────────────────

void AppViewModel::markOnboardingComplete() {
    model_->onboardingStore()->markWelcomeCompleted();
    // onboardingSub_ flips onboardingNeeded_ + emits onboardingNeededChanged on
    // the store republish; no direct emit here.
}

void AppViewModel::openExternalUrl(const QString& url) {
    if (url.isEmpty()) { return; }
    // The injected sink routes through ExternalLink (the Widgets path); when no
    // NotificationQueue is wired (the Quick path has none yet) it returns false on
    // failure, so we surface the failure on errorMessage — the QML toast channel —
    // matching the Widgets "Couldn't open browser" warning. Without a sink (tests)
    // open directly and report the same way.
    const bool ok =
        externalOpenSink_ ? externalOpenSink_(url) : QDesktopServices::openUrl(QUrl(url));
    if (!ok) { emit errorMessage(tr("Couldn't open browser")); }
}

} // namespace dish::qml
