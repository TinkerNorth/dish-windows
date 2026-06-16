// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// AppViewModel — the single QML-facing exposure object over dish::AppModel. It
// is a THIN adapter: it owns no UI state of its own beyond a cache of the values
// it re-publishes as NOTIFY properties, and every command forwards verbatim to
// the existing AppModel / WifiConnectionManager / ConnectionCoordinator. All
// derivation (status text, connection rows, slot live-stats) already happens in
// the C++ stores/composers; this class only maps them to Q_PROPERTY + role
// models and re-emits change signals so QML bindings stream.
//
// Registered as a context property by QmlEntryPoint (the model outlives the
// engine). Lives in dish_core so its mapping helpers are unit-testable without
// the Quick/Qml stack; the QML registration is done in the Quick target.

#pragma once

#include "Models/Models.h"
#include "architecture/Observable.h"
#include "qml/ConnectionListModel.h"
#include "qml/SlotListModel.h"
#include "source/store/CrashReportingStore.h"
#include "source/store/OnboardingPreferenceStore.h"
#include "source/store/ThemePreferenceStore.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

class QTimer;

namespace dish {
class AppModel;
}

namespace dish::qml {

class AppViewModel : public QObject {
    Q_OBJECT

    // ── Dashboard header (mirrors MainWindow::rebuildHeader) ─────────────────
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY stateChanged)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY stateChanged)
    Q_PROPERTY(int connectionCount READ connectionCount NOTIFY stateChanged)
    // True while any connection is registering a controller (the dashboard's
    // indeterminate spinner). Mirrors MainUiState::busy.
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)

    // ── Live telemetry footer (mirrors MainWindow::onTelemetryTick) ──────────
    // Sampled ~1 Hz off the processor; the bindings stream as the numbers move.
    Q_PROPERTY(int eventsPerSec READ eventsPerSec NOTIFY telemetryChanged)
    Q_PROPERTY(int sendsPerSec READ sendsPerSec NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong totalSent READ totalSent NOTIFY telemetryChanged)

    // ── Pairing one-shot (mirrors MainWindow::showPairingPrompt) ─────────────
    // pairingActive flips true when the AppModel parks a pairingTarget; the QML
    // pairing sheet opens on it and calls clearPairingTarget() before showing.
    Q_PROPERTY(bool pairingActive READ pairingActive NOTIFY stateChanged)
    Q_PROPERTY(QString pairingServerName READ pairingServerName NOTIFY stateChanged)

    // ── Collections the page agents iterate ──────────────────────────────────
    // The slot/controller model (a SlotCard per row) and the connection-row
    // model (a ConnectionsDialog row per row). Both are owned children; the
    // pointers are stable for the app lifetime so QML can bind once. NOT named
    // `slots` because moc strips that token (the Q_SLOTS keyword), the same trap
    // MainUiState::slotList avoids.
    Q_PROPERTY(dish::qml::SlotListModel* slotModel READ slotModel CONSTANT)
    Q_PROPERTY(dish::qml::ConnectionListModel* connectionModel READ connectionModel CONSTANT)

    // ── Settings: appearance + diagnostics (re-projections of the stores) ─────
    // themeMode 0=Light 1=Dark 2=System (the SettingsPage chip order). setThemeMode
    // forwards to ThemePreferenceStore; the ThemeController re-themes the live app
    // off its Observable, and we re-emit so QML re-reads + the chrome dark-mode
    // attribute follows. crashReportingEnabled forwards to CrashReportingStore.
    Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
    Q_PROPERTY(bool crashReportingEnabled READ crashReportingEnabled WRITE
                   setCrashReportingEnabled NOTIFY crashReportingChanged)

    // ── About ─────────────────────────────────────────────────────────────────
    // The build version string (CMake project VERSION, threaded in as DISH_VERSION).
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

    // ── Connections discovery (reactive) ─────────────────────────────────────
    // The FOUND list + scan flag, exposed REACTIVELY so QML bindings stream as a
    // scan lands (P2's plain invokables had no NOTIFY, so the FOUND list only
    // refreshed on page recreation). The properties read THROUGH the kept
    // invokables; discoveredChanged folds the manager's discoveredChanged, and
    // scanning folds its scanningChanged.
    Q_PROPERTY(QVariantList discoveredServers READ discoveredServers NOTIFY discoveredChanged)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)

    // ── Reverse (host-initiated) pairing (reactive) ──────────────────────────
    // The host shows a 4-digit PIN; the operator types it on the satellite. The
    // sheet binds the phase ("idle"/"awaiting"/"approved"/"declined"/"timedout"),
    // the PIN to display, and the server name. All fold the manager's
    // reversePairingChanged into one NOTIFY.
    Q_PROPERTY(QString reversePairingPhase READ reversePairingPhase NOTIFY reversePairingChanged)
    Q_PROPERTY(QString reversePairingPin READ reversePairingPin NOTIFY reversePairingChanged)
    Q_PROPERTY(
        QString reversePairingServerName READ reversePairingServerName NOTIFY reversePairingChanged)

    // ── First-run onboarding (mirrors maybeShowOnboarding's gate) ─────────────
    // onboardingNeeded == !OnboardingPreferenceStore::welcomeCompleted(). Main.qml
    // pushes the onboarding flow on it; markOnboardingComplete() persists the flag.
    Q_PROPERTY(bool onboardingNeeded READ onboardingNeeded NOTIFY onboardingNeededChanged)

    // ── Donate (brand defaults, mirroring DonateView's localizable URLs) ──────
    Q_PROPERTY(QString donateSponsorsUrl READ donateSponsorsUrl CONSTANT)
    Q_PROPERTY(QString donateKofiUrl READ donateKofiUrl CONSTANT)
    Q_PROPERTY(QString donateBmacUrl READ donateBmacUrl CONSTANT)

  public:
    explicit AppViewModel(dish::AppModel* model, QObject* parent = nullptr);

    QString statusText() const { return statusText_; }
    QString summaryText() const { return summaryText_; }
    int onlineCount() const { return onlineCount_; }
    int connectionCount() const { return connectionCount_; }
    bool busy() const { return busy_; }

    int eventsPerSec() const { return eventsPerSec_; }
    int sendsPerSec() const { return sendsPerSec_; }
    qulonglong totalSent() const { return totalSent_; }

    bool pairingActive() const { return pairingActive_; }
    QString pairingServerName() const { return pairingServerName_; }

    SlotListModel* slotModel() { return &slotModel_; }
    ConnectionListModel* connectionModel() { return &connectionModel_; }

    int themeMode() const;
    void setThemeMode(int mode);
    bool crashReportingEnabled() const;
    void setCrashReportingEnabled(bool enabled);
    QString appVersion() const;
    bool onboardingNeeded() const;
    QString reversePairingPhase() const;
    QString reversePairingPin() const;
    QString reversePairingServerName() const;
    QString donateSponsorsUrl() const;
    QString donateKofiUrl() const;
    QString donateBmacUrl() const;

    // The external-open sink: the QmlEntryPoint injects the real ExternalLink
    // path (which routes a failure through the NotificationQueue toast, matching
    // Widgets). Returns true iff the open was handed off. When unset (tests / no
    // entry point) the default below opens via QDesktopServices and reports a
    // failure through errorMessage(). Kept as a seam so dish_core need not link
    // the exe-only ExternalLink/NotificationQueue.
    using ExternalOpenSink = std::function<bool(const QString& url)>;
    void setExternalOpenSink(ExternalOpenSink sink) { externalOpenSink_ = std::move(sink); }

    // The theme-applied sink: the QmlEntryPoint injects a callback that refreshes
    // the QML Theme singleton + flips the chrome immersive-dark attribute to the
    // resolved appearance (true == dark). Called after a setThemeMode so the live
    // QML palette + the native title bar follow the new mode. Unset in tests.
    using ThemeAppliedSink = std::function<void(bool dark)>;
    void setThemeAppliedSink(ThemeAppliedSink sink) { themeAppliedSink_ = std::move(sink); }

    // ── Commands (forward verbatim to the existing AppModel surface) ─────────

    // Bind / unbind a slot to a connection (the SlotCard bind menu / Unbind).
    Q_INVOKABLE void bindSlot(const QString& slotId, const QString& connectionId);
    Q_INVOKABLE void unbindSlot(const QString& slotId);

    // Force a slot's USB input path: "standard" (let SDL/XInput own the pad),
    // "direct" (raw-HID claim), or "auto" (clear the override; the resolution
    // policy decides). Resolves slotId -> (vid, pid) — a synthetic slot's id IS
    // the packed vpKey string; an SDL slot's identity comes from the bridge
    // device list — then forwards to the existing UsbGamepadManager::setPathChoice
    // / clearChoice. AppModel already rebuilds + emits stateChanged on a USB path
    // change, so the slot roles refresh with no new NOTIFY. A no-op when the slot
    // has no resolvable (vid, pid) or no USB manager is wired.
    Q_INVOKABLE void setSlotPath(const QString& slotId, const QString& choice);

    // The connections a slot may actually bind to, for the bind chooser. Returns
    // JS objects {connectionId,label,dotColor,glyph} via the SAME pure
    // reducer::connectionsVisibleInPicker the Widgets SlotCard uses: connections
    // bound to ANOTHER slot are excluded, live-available unbound ones are offered,
    // and the slot's OWN current binding is held over even when offline. One-shot
    // (read when the chooser opens, like emulateTypes), so no NOTIFY.
    Q_INVOKABLE QVariantList availableConnectionsForSlot(const QString& slotId) const;

    // Emulate picker: kick a catalog refresh, then read the offerable types +
    // the slot's current type. emulateTypes returns a list of JS objects
    // {type,slug,name,shortName,description,known}; emulateCurrentType the
    // pre-selected wire id. setControllerType applies the choice.
    Q_INVOKABLE void refreshEmulate(const QString& slotId);
    Q_INVOKABLE QVariantList emulateTypes(const QString& slotId) const;
    Q_INVOKABLE int emulateCurrentType(const QString& slotId) const;
    Q_INVOKABLE void setControllerType(const QString& slotId, int type);

    // Connections page: discovery + connect + forget. discoveredServers returns
    // {name,ip,udpPort,pairPort,httpPort,machineId,source,id} objects for the
    // FOUND list (the rows that aren't yet remembered surface only here).
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE bool isScanning() const;
    Q_INVOKABLE QVariantList discoveredServers() const;
    Q_INVOKABLE void forgetConnection(const QString& connectionId);

    // Connect a discovered server by its STABLE id (resolves the server out of
    // the live discovered list). De-raced replacement for connectByIndex — an
    // index goes stale if the list reorders between read and call. Matches the
    // Widgets onConnectClicked, which matches on s.id().
    Q_INVOKABLE void connectByServerId(const QString& serverId);

    // DEPRECATED: index-based connect; racy if the discovered list reorders
    // between read and call. Kept until the QML page migrates to
    // connectByServerId. Prefer connectByServerId.
    Q_INVOKABLE void connectByIndex(int discoveredIndex);

    // Reconnect a REMEMBERED (possibly currently-undiscovered) satellite by id
    // WITHOUT a rescan requirement and WITHOUT re-pairing. Forwards to the
    // coordinator's reconnectConnection (Widgets ConnectionsDialog Reconnect).
    Q_INVOKABLE void reconnectConnection(const QString& connectionId);

    // Graceful disconnect of a LIVE session WITHOUT forgetting (row + key stay).
    // Forwards to the coordinator's disconnectConnection. Gate on the row's
    // liveLink role: enable only when the link is live.
    Q_INVOKABLE void disconnectConnection(const QString& connectionId);

    // Pairing sheet: submit a PIN for a discovered server (by id or by index),
    // query the in-flight state, and clear the one-shot pairing trigger before
    // showing the sheet.

    // De-raced PIN submit, resolving the server by its stable id (matches
    // connectByServerId). Prefer this over pairWithPin.
    Q_INVOKABLE void pairByServerId(const QString& serverId, const QString& pin);

    // DEPRECATED: index-based pair; racy if the discovered list reorders. Kept
    // until the QML page migrates to pairByServerId.
    Q_INVOKABLE void pairWithPin(int discoveredIndex, const QString& pin);
    Q_INVOKABLE bool isPairingInFlight(const QString& serverId) const;
    Q_INVOKABLE void clearPairingTarget();

    // Reverse (host-initiated) pairing: kick a host-initiated pair for the
    // discovered server with that stable id (de-raced, like connectByServerId);
    // cancel an in-flight one. The displayed PIN + phase stream through the
    // reversePairing* properties.
    Q_INVOKABLE void requestReversePairing(const QString& serverId);
    Q_INVOKABLE void cancelReversePairing();

    // ── Deadzone settings page ───────────────────────────────────────────────
    // The per-device rows {id,name,hasGyro,stickFlat,triggerFlat,forwardMotion}
    // (re-pull on deadzonesChanged). setDeadzones persists the override AND pushes
    // it into the live processor (same pair the Widgets view + MainWindow do);
    // setMotionEnabled forwards to MotionEnabledStore keyed by the device id.
    Q_INVOKABLE QVariantList deadzoneDevices() const;
    Q_INVOKABLE void setDeadzones(const QString& deviceId, int stickFlat, int triggerFlat);
    Q_INVOKABLE void setMotionEnabled(const QString& deviceId, bool enabled);

    // ── Licenses page ────────────────────────────────────────────────────────
    // The bundled third-party manifest as {name,version,license,url} rows.
    Q_INVOKABLE QVariantList licenses() const;

    // ── Onboarding + external links ──────────────────────────────────────────
    // Persist the welcome-completed flag (Main.qml calls this when the flow's
    // completed() fires). openExternalUrl routes through the injected sink so a
    // failure raises the same toast the Widgets screens do.
    Q_INVOKABLE void markOnboardingComplete();
    Q_INVOKABLE void openExternalUrl(const QString& url);

  signals:
    // Any header/slot/connection/pairing state changed (folds AppModel's
    // stateChanged + the coordinator's connectionsChanged).
    void stateChanged();
    // Telemetry footer numbers moved (the ~1 Hz sample).
    void telemetryChanged();
    // Transient one-shot error, forwarded from AppModel::errorMessage so the QML
    // toast host can surface it.
    void errorMessage(const QString& message);

    // Settings property NOTIFYs. themeMode/crashReporting re-emit when the store
    // republishes; onboardingNeeded flips false once markOnboardingComplete lands.
    void themeModeChanged();
    void crashReportingChanged();
    void onboardingNeededChanged();

    // Discovery results moved (P2 had to re-pull discoveredServers() on the broad
    // stateChanged; this is the precise edge to re-pull on). Folds the
    // WifiConnectionManager's discoveredChanged. NOTIFY for the discoveredServers
    // property so QML bindings stream as a scan lands.
    void discoveredChanged();

    // The scan flag flipped (a scan started or finished). Folds the
    // WifiConnectionManager's scanningChanged; NOTIFY for the scanning property.
    void scanningChanged();

    // A reverse-pairing transition (phase / pin / server name moved). Folds the
    // WifiConnectionManager's reversePairingChanged; NOTIFY for the three
    // reversePairing* properties.
    void reversePairingChanged();

    // The deadzone device rows / their seeded values moved (a device attached or
    // detached, or a setDeadzones/setMotionEnabled landed). Re-pull deadzoneDevices().
    void deadzonesChanged();

    // A one-shot pairing-success edge (a session reached Connected after a pair).
    // Mirrors the rising edge of a connection going live; the QML pairing sheet
    // closes on it. Best-effort, fired at most once per live transition.
    void pairingSucceeded();

  private:
    // Recompute the cached header/pairing fields + repush the slot model from
    // the AppModel's current state slice, then emit stateChanged().
    void onStateChanged();
    // Repush the connection model from the coordinator's derived rows.
    void onConnectionsChanged();
    // Sample the processor telemetry (the same drain MainWindow does).
    void onTelemetryTick();

    dish::AppModel* model_;
    SlotListModel slotModel_;
    ConnectionListModel connectionModel_;
    QTimer* telemetryTimer_;

    QString statusText_;
    QString summaryText_;
    int onlineCount_ = 0;
    int connectionCount_ = 0;
    bool busy_ = false;

    int eventsPerSec_ = 0;
    int sendsPerSec_ = 0;
    qulonglong totalSent_ = 0;

    bool pairingActive_ = false;
    QString pairingServerName_;

    // Cached so onStateChanged can fire pairingSucceeded() on the rising edge of
    // the online count (a fresh connection reached Connected).
    int lastOnlineCount_ = 0;

    // Cached onboarding gate so onStateChanged can fire onboardingNeededChanged
    // only on a real transition (markOnboardingComplete is the only mover today,
    // but the store could change out from under us).
    bool onboardingNeeded_ = false;

    ExternalOpenSink externalOpenSink_;
    ThemeAppliedSink themeAppliedSink_;

    // Store-observable subscriptions held for the model's lifetime so the
    // store-side republish (theme/crash/onboarding) re-emits our Qt NOTIFYs.
    arch::Observable<source::ThemeMode>::Subscription themeSub_;
    arch::Observable<bool>::Subscription crashSub_;
    arch::Observable<source::OnboardingState>::Subscription onboardingSub_;
};

} // namespace dish::qml
