// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight (GameStream) subsystem's command surface, the sibling of
// WifiConnectionManager on the Satellite path. It owns the persistent identity,
// the remembered + discovered host list, and the live MoonlightSession per host,
// and it exposes the user commands (discover, add manual host, pair, connect,
// disconnect, forget). It also owns the per-slot input routing into a live
// session, which is a PARALLEL seam to the Satellite path's routing tables in
// AppModel rather than a change to them, so that path is never perturbed.

#pragma once

#include "Network/MoonlightHost.h"
#include "core/moonlight/MoonlightIdentity.h"
#include "core/moonlight/MoonlightPadSlots.h"
#include "core/moonlight/MoonlightSessionMachine.h"
#include "core/moonlight/MoonlightSessionUi.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class QSettings;

namespace dish::repository {
class MoonlightHostRepository;
}

namespace dish::net {

class MoonlightSession;

// The flat row the host-list UI renders: identity crossed with live phase, as
// tokens the QML localizes (never sentences), matching the Satellite path's
// render-key discipline.
struct MoonlightHostRow {
    QString id;
    QString name;
    QString ip;
    bool paired = false;
    bool discovered = false;
    // "idle" | "pairing" | "paired" | "launching" | "connecting" | "streaming" |
    // "faltering" | "closed" | "failed".
    QString phaseToken;
    // The remembered app pick and emulated-device pick, so the row can render
    // both without a second lookup.
    QString appName;
    int deviceType = models::kMoonlightDeviceAuto;

    bool operator==(const MoonlightHostRow& o) const {
        return id == o.id && name == o.name && ip == o.ip && paired == o.paired &&
               discovered == o.discovered && phaseToken == o.phaseToken && appName == o.appName &&
               deviceType == o.deviceType;
    }
};

// The lowercase phase token for a session state, for the row above.
QString moonlightPhaseToken(moonlight::SessionPhase phase);

// A fresh four-digit pairing code. Here rather than in QML because a PIN is
// security-relevant and JavaScript's Math.random() is not a suitable source.
QString generateMoonlightPin();

// Pure merge of the remembered list with a discovery sweep and the live session
// phases into the flat row list. Remembered rows come first; a discovered host
// already remembered is folded (not duplicated). Exposed for unit testing.
QList<MoonlightHostRow> mergeMoonlightRows(const QList<models::MoonlightHost>& remembered,
                                           const QList<models::MoonlightHost>& discovered,
                                           const QHash<QString, QString>& phaseTokensById);

class MoonlightManager : public QObject {
    Q_OBJECT
  public:
    explicit MoonlightManager(std::shared_ptr<QSettings> settings = nullptr,
                              QObject* parent = nullptr);
    ~MoonlightManager() override;

    // The merged host rows for the UI.
    QList<MoonlightHostRow> hostRows() const;

    bool isScanning() const { return scanning_; }

    // Kick a background _nvstream._tcp sweep; results merge into the host list.
    void startDiscovery();

    // Add a host the user typed by hand (discovery fallback).
    void addManualHost(const QString& ip, const QString& name);

    // Run PIN pairing against the host; pairingFinished(id, ok) reports the end.
    void pairHost(const QString& id, const QString& pin);

    // Launch (or resume) an app and bring the control stream up. Empty appId
    // launches the host's remembered pick, then the host's default.
    void connectHost(const QString& id, const QString& appId);

    // GET /applist on a paired host; the reply arrives as appListReady.
    void refreshApps(const QString& id);

    // Persist the user's app pick for a host. The app belongs to the SESSION, so
    // it is set by whoever creates one and inherited by everything that joins.
    void setHostApp(const QString& id, const QString& appId, const QString& appName);
    void setHostDeviceType(const QString& id, int deviceType);

    // The app the host is running for us, named for the bindings that join it.
    QString runningAppName(const QString& id) const;

    // The host's own words for its last refusal, so a message we cannot phrase
    // better is quoted rather than paraphrased. Empty when the failure did not
    // come from the host's body.
    QString refusalMessage(const QString& id) const;

    // Re-verify remembered trust: a plaintext /serverinfo for PairStatus and the
    // host's uniqueid. Never polled; run on entering a screen and before a
    // session starts. The answer lands on hostsChanged().
    void probeHost(const QString& id);

    void disconnectHost(const QString& id);

    // Stop whatever app the host is running, without a session of ours to tear
    // down. The answer to a host that refused /launch because an app is already
    // running and offered no resumable session.
    void cancelHostApp(const QString& id);

    void forgetHost(const QString& id);

    std::optional<moonlight::SessionPhase> sessionPhase(const QString& id) const;

    // ── Per-slot input routing ───────────────────────────────────────────────
    // A PARALLEL seam to the Satellite path's routing tables in AppModel, which
    // this never touches: a slot bound to a Moonlight host streams here, and the
    // Satellite tables keep answering for slots bound to a satellite.
    //
    // Main thread. Allocates a controller number, sends CONTROLLER_ARRIVAL with
    // THIS BINDING's emulated-device pick and the pad's real capabilities, and
    // adds the pad to the active mask. `hasRumble` and friends are the pad's
    // detected hardware.
    //
    // THE SESSION IS REFERENCE COUNTED PER HOST, never one per binding: a host
    // carries one session for up to four controllers, so the first pad on a host
    // starts (or rejoins) it and every later pad only announces itself on the
    // stream that is already up.
    void bindSlot(const QString& slotId, const QString& hostId, int controllerType, bool hasRumble,
                  bool hasMotion, bool hasTouchpad, bool hasBattery, bool hasLightbar);

    // Main thread. Drops the pad's bit from the active mask and sends one final
    // CONTROLLER_MULTI naming it with the bit already cleared, which is the
    // protocol's unplug signal. The LAST pad off a host tears its session down,
    // so no app is left stranded on it.
    void unbindSlot(const QString& slotId);

    QString boundHostFor(const QString& slotId) const;

    // How many controllers this host currently carries, for the four-pad ceiling
    // and for the host row's "in use by" chip.
    int boundSlotCount(const QString& hostId) const;

    // ── Standing bindings ────────────────────────────────────────────────────
    // A binding outlives the session: it is an intent, and pairing is remembered
    // trust verified lazily, so nothing here is gated on the host answering.
    QList<models::MoonlightBinding> bindings() const;
    std::optional<models::MoonlightBinding> binding(const QString& slotId) const;
    void rememberBinding(const models::MoonlightBinding& binding);
    void forgetBinding(const QString& slotId);

    // Everything the session section renders from, for one host. Pure inputs;
    // MoonlightSessionUi turns them into the one state that renders.
    moonlight::SessionUiInputs sessionUiInputs(const QString& hostId) const;

    // The reverse of the routing table: which local slot a host's inbound
    // rumble / LED event (addressed by controller number) belongs to. Empty when
    // nothing matches, so a stale event is dropped rather than misrouted.
    QString slotForController(const QString& hostId, int controllerNumber) const;

    // Hot path, called from the SDL input thread. Returns immediately when no
    // slot is bound to a Moonlight host: the atomic guard means an install with
    // no Moonlight host pays one relaxed load per report and never takes a lock.
    void forwardReport(const std::string& slotId, std::uint16_t buttons, std::uint8_t lt,
                       std::uint8_t rt, std::int16_t lx, std::int16_t ly, std::int16_t rx,
                       std::int16_t ry);
    void forwardMotion(const std::string& slotId, std::int16_t gyroX, std::int16_t gyroY,
                       std::int16_t gyroZ, std::int16_t accelX, std::int16_t accelY,
                       std::int16_t accelZ);
    void forwardBattery(const std::string& slotId, std::uint8_t level,
                        std::uint8_t satelliteStatus);

    // True while at least one slot is bound to a Moonlight host.
    bool hasBoundSlots() const { return anyBound_.load(std::memory_order_relaxed); }

  signals:
    void hostsChanged();
    void scanningChanged();
    void pairingFinished(const QString& id, bool ok);
    void sessionPhaseChanged(const QString& id);
    // Parallel id/title lists from /applist for the host's app picker.
    void appListReady(const QString& id, const QStringList& appIds, const QStringList& appTitles);
    // Host -> client, forwarded for a future binding to route to a local pad.
    void rumbleReceived(const QString& id, int controllerNumber, int lowFreq, int highFreq);
    void rgbLedReceived(const QString& id, int controllerNumber, int r, int g, int b);

  private:
    // Ensures a session exists for `host`, wiring its signals through. Lazily
    // loads the client identity on first use (RSA keygen is not paid at startup).
    MoonlightSession* ensureSession(const models::MoonlightHost& host);
    std::optional<models::MoonlightHost> hostById(const QString& id) const;

    // Resolves a slot to its live session + controller number under routeMtx_.
    struct Route {
        MoonlightSession* session = nullptr;
        std::uint8_t controllerNumber = 0;
        QString hostId;
    };

    // Per host: what the last probe and the last /applist answered, so the
    // session section renders from what we asked rather than from a guess.
    struct HostProbe {
        bool inFlight = false;
        bool answered = false;
        bool timedOut = false;
        bool pairStatus = false;
        bool uniqueIdChanged = false;
        bool appsInFlight = false;
        bool appsFetched = false;
        bool appsFailed = false;
        int appCount = 0;
        bool pairingActive = false;
        bool pairingRefused = false;
    };

    std::unique_ptr<repository::MoonlightHostRepository> repo_;
    std::optional<moonlight::Identity> identity_;
    QList<models::MoonlightHost> discovered_;
    // Held in memory because the slot list asks for them on every move, and the
    // store is the same shared QSettings the Satellite path writes through.
    QList<models::MoonlightBinding> bindings_;
    QHash<QString, MoonlightSession*> sessions_;
    QHash<QString, HostProbe> probes_;
    bool scanning_ = false;
    std::thread discoveryThread_;

    // The routing table is written on the main thread and read on the SDL input
    // thread, so it has its own mutex, held only for the lookup. anyBound_ keeps
    // the uncontended no-Moonlight case off the lock entirely.
    mutable std::mutex routeMtx_;
    std::map<std::string, Route> routes_;
    // Per host: which controller numbers are in use and the active mask.
    QHash<QString, moonlight::PadSlots> padSlots_;
    std::atomic<bool> anyBound_{false};
};

} // namespace dish::net
