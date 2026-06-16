// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/AppViewModel.h"

#include "AppModel.h"
#include "Input/GamepadInputProcessor.h"
#include "Input/SDLGamepadBridge.h"
#include "Network/WifiConnectionManager.h"
#include "composer/CatalogComposer.h"
#include "composer/ConnectionCoordinator.h"
#include "core/input/Deadzones.h"
#include "qml/AppSettingsMaps.h"
#include "repository/DeadzoneRepository.h"
#include "source/store/CrashReportingStore.h"
#include "source/store/MotionEnabledStore.h"
#include "source/store/OnboardingPreferenceStore.h"
#include "source/store/ThemePreferenceStore.h"
#include "UI/Theme.h"
#include "ui/licenses/LicenseManifest.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#ifndef DISH_VERSION
#define DISH_VERSION "0.0.0"
#endif

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

    // Re-pull the discovered list + scan flag on their precise edges (P2 had to
    // key off the broad stateChanged, and had no scan-flag NOTIFY at all). The
    // WifiConnectionManager owns the scan results. Routed through explicit
    // lambdas (not bare signal→signal) so the emission is unambiguous, mirroring
    // the working connectionModel path below.
    QObject::connect(model_->wifi(), &net::WifiConnectionManager::discoveredChanged, this,
                     [this] { emit discoveredChanged(); });
    QObject::connect(model_->wifi(), &net::WifiConnectionManager::scanningChanged, this,
                     [this] { emit scanningChanged(); });

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

void AppViewModel::connectByIndex(int discoveredIndex) {
    const auto servers = model_->wifi()->discoveredServers();
    if (discoveredIndex < 0 || discoveredIndex >= servers.size()) { return; }
    model_->wifi()->connectTo(servers.at(discoveredIndex));
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

void AppViewModel::pairWithPin(int discoveredIndex, const QString& pin) {
    const auto servers = model_->wifi()->discoveredServers();
    if (discoveredIndex < 0 || discoveredIndex >= servers.size()) { return; }
    model_->wifi()->pairWithPin(servers.at(discoveredIndex), pin);
}

bool AppViewModel::isPairingInFlight(const QString& serverId) const {
    return model_->wifi()->isPairingInFlight(serverId);
}

void AppViewModel::clearPairingTarget() { model_->clearPairingTarget(); }

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
    if (themeAppliedSink_) {
        themeAppliedSink_(ui::activeAppearance() == ui::Appearance::Dark);
    }
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
    const bool ok = externalOpenSink_ ? externalOpenSink_(url)
                                      : QDesktopServices::openUrl(QUrl(url));
    if (!ok) { emit errorMessage(tr("Couldn't open browser")); }
}

} // namespace dish::qml
