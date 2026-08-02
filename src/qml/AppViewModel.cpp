// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/AppViewModel.h"

#include "AppModel.h"
#include "FeatureSettings.h"
#include "Input/GamepadInputProcessor.h"
#include "Input/JoystickMapping.h"
#include "Input/SDLGamepadBridge.h"
#include "Network/WifiConnectionManager.h"
#include "composer/CatalogComposer.h"
#include "composer/ConnectionCoordinator.h"
#include "composer/StreamingSlotCount.h"
#include "core/catalog/BundledCatalog.h"
#include "core/input/Deadzones.h"
#include "core/reducer/CapabilitySolver.h"
#include "core/reducer/CatalogFeatureGate.h"
#include "core/reducer/ConnectionRows.h"
#include "core/reducer/FoundVisibility.h"
#include "core/reducer/PathChoice.h"
#include "core/reducer/PickerVisibility.h"
#include "core/reducer/SlotPathFields.h"
#include "source/usb/UsbGamepadManager.h"
#include "qml/AppSettingsMaps.h"
#include "repository/DeadzoneRepository.h"
#include "source/store/CrashReportingStore.h"
#include "source/store/MotionEnabledStore.h"
#include "source/store/OnboardingPreferenceStore.h"
#include "source/store/ControllerTypeStore.h"
#include "source/store/ThemePreferenceStore.h"
#include "source/store/TouchpadModeStore.h"
#include "UI/Theme.h"
#include "UI/licenses/LicenseManifest.h"

#include <QDesktopServices>
#include <QGuiApplication>
#include <QSet>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <map>
#include <optional>

// Pulls windows.h (WIN32_LEAN_AND_MEAN + NOMINMAX) — keep it last so no Win32
// macro reaches the Qt headers above.
#include "source/system/BluetoothRadioProbe.h"

#ifndef DISH_VERSION
#define DISH_VERSION "0.0.0"
#endif

namespace dish::qml {

namespace {

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

// ── Capability vocabulary → QML tokens ───────────────────────────────────────
// Lowercase tokens only; every user-facing word is composed in QML so the
// strings stay in the qsTr catalogs. The order matches CapFeature's.

QString capFeatureToken(reducer::CapFeature f) {
    switch (f) {
    case reducer::CapFeature::Gamepad:
        return QStringLiteral("gamepad");
    case reducer::CapFeature::Triggers:
        return QStringLiteral("triggers");
    case reducer::CapFeature::Motion:
        return QStringLiteral("motion");
    case reducer::CapFeature::Touchpad:
        return QStringLiteral("touchpad");
    case reducer::CapFeature::Mouse:
        return QStringLiteral("mouse");
    case reducer::CapFeature::Rumble:
        return QStringLiteral("rumble");
    case reducer::CapFeature::Lightbar:
        return QStringLiteral("lightbar");
    }
    return {};
}

QString capVerdictToken(reducer::CapVerdict v) {
    switch (v) {
    case reducer::CapVerdict::Available:
        return QStringLiteral("available");
    case reducer::CapVerdict::Unavailable:
        return QStringLiteral("unavailable");
    case reducer::CapVerdict::Pending:
        return QStringLiteral("pending");
    case reducer::CapVerdict::Off:
        return QStringLiteral("off");
    }
    return {};
}

QString capLayerToken(reducer::CapLayer l) {
    switch (l) {
    case reducer::CapLayer::Input:
        return QStringLiteral("input");
    case reducer::CapLayer::Link:
        return QStringLiteral("link");
    case reducer::CapLayer::Type:
        return QStringLiteral("type");
    case reducer::CapLayer::Host:
        return QStringLiteral("host");
    }
    return {};
}

// The step-state tokens the apply overlay switches on.
QString applyStepToken(reducer::ApplyStepState s) {
    switch (s) {
    case reducer::ApplyStepState::Pending:
        return QStringLiteral("pending");
    case reducer::ApplyStepState::Active:
        return QStringLiteral("active");
    case reducer::ApplyStepState::Done:
        return QStringLiteral("done");
    case reducer::ApplyStepState::Failed:
        return QStringLiteral("failed");
    case reducer::ApplyStepState::Skipped:
        return QStringLiteral("skipped");
    }
    return {};
}

// applyFinished's reason vocabulary. PathClaimTimeout never reaches here: a
// claim that times out is a FALLBACK (directFellBack), not a failure.
QString applyFailureToken(reducer::ApplyFailure f) {
    switch (f) {
    case reducer::ApplyFailure::SlotGone:
        return QStringLiteral("slotGone");
    case reducer::ApplyFailure::HostUnreachable:
    case reducer::ApplyFailure::PathClaimTimeout:
        return QStringLiteral("hostUnreachable");
    case reducer::ApplyFailure::BindRejected:
        return QStringLiteral("bindRejected");
    case reducer::ApplyFailure::Cancelled:
        return QStringLiteral("cancelled");
    }
    return {};
}

// The satellite's controller board is four pads wide, so the pre-bind copy can
// say "<n> slots free" without asserting WHICH slot the bind will land in.
constexpr int kHostSlotCapacity = 4;
// Budgets, android parity: a raw-HID claim can hold the device for 20 s while
// Windows releases it; a REST round-trip that has not answered in 8 s is an
// unreachable host.
constexpr int kPathBudgetMs = 20'000;
constexpr int kBindBudgetMs = 8'000;
constexpr int kApplyTickMs = 250;

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

    // The keep-awake pill follows the wake composer's DERIVED intent, not
    // keepAwakeCount(): that observable is the keep-screen-on override input,
    // which nothing sets, so binding to it left the pill dark while a pad was
    // streaming. Folded into stateChanged (a wake flip always rides a
    // binding/liveness change, but the subscription is what actually catches it).
    keepAwakeSub_ = model_->wakeState().subscribe(
        [this](const composer::WakeState& wake) {
            if (wake.shouldInhibit != keepAwakeActive_) {
                keepAwakeActive_ = wake.shouldInhibit;
                emit stateChanged();
            }
        },
        true);

    // An external light-bar mutation (nothing mutates it but us today, yet the
    // store could grow callers) re-reads through the one NOTIFY.
    QObject::connect(model_->featureSettings(), &FeatureSettings::changed, this,
                     [this] { emit lightbarChanged(); });

    // A rejected forward PIN, re-keyed onto the STABLE server id the sheet knows
    // (WifiConnection::idFor(server) IS that id, so the forward is verbatim).
    QObject::connect(model_, &dish::AppModel::pairingFailed, this,
                     [this](const QString& connectionId, const QString& reason) {
                         emit pairingFailed(connectionId, reason);
                     });

    // The satellite refusing a controller descriptor is the authoritative
    // negative edge for the apply sequencer's Destination step.
    QObject::connect(model_->wifi(), &net::WifiConnectionManager::slotRegistrationFailed, this,
                     [this](const QString& slotId) {
                         if (apply_.phase != reducer::ApplyPhase::Binding ||
                             slotId != applySlotId_) {
                             return;
                         }
                         dispatchApply(reducer::apply_event::BindRejected{/*unreachable=*/false});
                     });

    // Live System-theme follow: the startup resolve was already correct, but a
    // mode change while the app runs never re-resolved. Only System listens —
    // an explicit Light/Dark pick must ignore the OS.
    if (auto* hints = QGuiApplication::styleHints()) {
        QObject::connect(hints, &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (model_->themeStore()->mode() != source::ThemeMode::System) { return; }
            ui::setActiveAppearance(ui::detectSystemAppearance());
            if (themeAppliedSink_) {
                themeAppliedSink_(ui::activeAppearance() == ui::Appearance::Dark);
            }
            emit themeModeChanged();
        });
    }

    // ── Apply budgets ────────────────────────────────────────────────────────
    applyPathTimer_ = new QTimer(this);
    applyPathTimer_->setSingleShot(true);
    applyPathTimer_->setInterval(kPathBudgetMs);
    QObject::connect(applyPathTimer_, &QTimer::timeout, this,
                     [this] { dispatchApply(reducer::apply_event::PathTimedOut{}); });
    applyBindTimer_ = new QTimer(this);
    applyBindTimer_->setSingleShot(true);
    applyBindTimer_->setInterval(kBindBudgetMs);
    QObject::connect(applyBindTimer_, &QTimer::timeout, this,
                     [this] { dispatchApply(reducer::apply_event::BindTimedOut{}); });
    applyTickTimer_ = new QTimer(this);
    applyTickTimer_->setInterval(kApplyTickMs);
    QObject::connect(applyTickTimer_, &QTimer::timeout, this, &AppViewModel::onApplyTick);

    const auto radio = source::probeBluetoothRadio();
    bluetoothPresent_ = radio.present;
    bluetoothEnabled_ = radio.enabled;

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
    // the two UIs read identically. No new behavior, only a re-projection. The
    // "Bound to <ip> • UDP <port>" detail is not needed: rebuildHeader keys off
    // ConnectionSummary only.
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
        statusText_ = tr("%n paired", "", total);
    } else if (live == 1) {
        statusText_ = firstLabel;
    } else {
        statusText_ = tr("%n online", "", live);
    }

    if (live == 0 && total == 0) {
        summaryText_ = tr("Open Connections to add one");
    } else if (live == 0) {
        summaryText_ = tr("%n paired", "", total);
    } else {
        // Quantity rides the TOTAL, matching Android's status_connected_of:
        // "1 of 1 online" reads singular, "2 of 5 online" plural.
        summaryText_ = tr("%1 of %n online", "", total).arg(live);
    }

    busy_ = st.busy;

    // Shell-header primitives (contract A2): the pages assemble the design's
    // sub-lines ("2 of 3 online · nothing bound") from these in QML.
    slotCount_ = static_cast<int>(st.slotList.size());
    int bound = 0;
    for (const auto& s : st.slotList) {
        if (s.boundConnectionId.has_value()) { ++bound; }
    }
    boundSlotCount_ = bound;
    firstOnlineName_ = firstLabel;

    // The Home header's "N controllers streaming" — the SAME pure
    // composer::streamingSlotCount the wake controller keys the display on
    // (bound AND Connected), fed from this state slice's bindings x links.
    {
        QHash<QString, QString> bindings;
        for (const auto& s : st.slotList) {
            if (s.boundConnectionId.has_value()) { bindings.insert(s.id, *s.boundConnectionId); }
        }
        QHash<QString, models::LinkState> links;
        for (const auto& c : conns) { links.insert(c.id, c.live); }
        streamingSlotCount_ = composer::streamingSlotCount(bindings, links);
    }

    pairingActive_ = st.pairingTarget.has_value();
    pairingServerName_ = pairingActive_ ? st.pairingTarget->name : QString();
    pairingServerId_ = pairingActive_ ? st.pairingTarget->id() : QString();

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
    const auto rows = model_->connections()->connections().value();
    connectionModel_.setRows(rows);
    // The slot model's bound-satellite join (the Home signal-path right cell +
    // wire latency) reads through the same derived rows.
    slotModel_.setConnectionRows(rows);

    // The FOUND list is the discovered scan MINUS these row ids (the one-spot
    // rule, reducer::serversVisibleInFound), so a membership move — a pair
    // landing a new remembered row, a forget dropping one — must re-read the
    // discoveredServers/foundCount bindings too. Keyed on the id SET, not the
    // rows, so the per-second latency ticks (which also arrive here) never
    // churn the FOUND Repeater.
    QSet<QString> ids;
    ids.reserve(static_cast<qsizetype>(rows.size()));
    for (const auto& row : rows) { ids.insert(QString::fromStdString(row.id)); }
    if (ids != connectionRowIds_) {
        connectionRowIds_ = std::move(ids);
        emit discoveredChanged();
    }
}

void AppViewModel::onTelemetryTick() {
    const auto snap = model_->processor()->drainTelemetry();
    eventsPerSec_ = snap.events;
    sendsPerSec_ = snap.sends;
    totalSent_ = snap.totalSent;
    emit telemetryChanged();
}

// Derived from the (one-spot filtered) FOUND list on read (UI cadence, list is
// tiny) so no second cache can drift from discoveredServers().
int AppViewModel::foundCount() const { return static_cast<int>(discoveredServers().size()); }

bool AppViewModel::railCollapsed() const { return uiPrefs_.railCollapsed(); }

void AppViewModel::setRailCollapsed(bool collapsed) {
    if (uiPrefs_.railCollapsed() == collapsed) { return; }
    uiPrefs_.setRailCollapsed(collapsed);
    emit railCollapsedChanged();
}

bool AppViewModel::lightbarFollowGame() const {
    return model_->featureSettings()->lightbarMode() == LightbarMode::FollowGame;
}

void AppViewModel::setLightbarFollowGame(bool followGame) {
    model_->featureSettings()->setLightbarMode(followGame ? LightbarMode::FollowGame
                                                          : LightbarMode::Off);
    // FeatureSettings::changed also fires; the extra emit keeps the property
    // NOTIFY correct even if that connect is ever removed.
    emit lightbarChanged();
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
        return tr("Couldn’t reach the satellite to load controller types.");
    case source::CatalogError::ServerError:
        return tr("The satellite couldn’t provide controller types right now.");
    case source::CatalogError::Malformed:
        return tr("The satellite sent an unreadable controller-type list.");
    }
    return {};
}

bool AppViewModel::emulateStale() const { return model_->catalogState().stale; }

namespace {
QVariantList typeRows(const QList<composer::PickableType>& types) {
    QVariantList out;
    for (const auto& t : types) {
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
} // namespace

QVariantList AppViewModel::emulateTypes(const QString& slotId) const {
    return typeRows(model_->pickableTypesFor(slotId));
}

int AppViewModel::emulateCurrentType(const QString& slotId) const {
    return model_->currentTypeFor(slotId);
}

void AppViewModel::refreshEmulateForHost(const QString& connectionId) {
    model_->refreshCatalogForConnection(connectionId);
}

QVariantList AppViewModel::emulateTypesForHost(const QString& connectionId) const {
    return typeRows(model_->pickableTypesForConnection(connectionId));
}

int AppViewModel::emulateCurrentTypeForHost(const QString& connectionId,
                                            const QString& slotId) const {
    return model_->currentTypeForConnection(connectionId, slotId);
}

void AppViewModel::setControllerType(const QString& slotId, int type) {
    model_->setSlotControllerType(slotId, type);
}

void AppViewModel::startDiscovery() { model_->wifi()->startDiscovery(); }

bool AppViewModel::isScanning() const { return model_->wifi()->isScanning(); }

QVariantList AppViewModel::discoveredServers() const {
    // One-spot rule (reducer::serversVisibleInFound): a satellite whose id
    // already has a row in the derived connections list (remembered ∪ live)
    // renders THERE — FOUND offers only the un-remembered rest. Both sides key
    // on the stable machineId-preferring id, so a remembered box at a fresh
    // DHCP address still folds. onConnectionsChanged re-emits discoveredChanged
    // when this id set moves, so the binding re-reads on pair/forget too.
    QSet<QString> rowIds;
    for (const auto& row : model_->connections()->connections().value()) {
        rowIds.insert(QString::fromStdString(row.id));
    }
    QVariantList out;
    for (const auto& s :
         reducer::serversVisibleInFound(model_->wifi()->discoveredServers(), rowIds)) {
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

// ── Bluetooth radio ─────────────────────────────────────────────────────────

void AppViewModel::refreshBluetoothState() {
    const auto radio = source::probeBluetoothRadio();
    if (radio.present == bluetoothPresent_ && radio.enabled == bluetoothEnabled_) { return; }
    bluetoothPresent_ = radio.present;
    bluetoothEnabled_ = radio.enabled;
    emit bluetoothChanged();
}

void AppViewModel::openBluetoothSettings() {
    // The Windows Settings deep link. Routed through the same sink as every
    // other external open so a failure raises the standard toast.
    openExternalUrl(QStringLiteral("ms-settings:bluetooth"));
}

// ── Capabilities ────────────────────────────────────────────────────────────

const models::ControllerSlot* AppViewModel::slotById(const QString& slotId) const {
    for (const auto& s : model_->state().slotList) {
        if (s.id == slotId) { return &s; }
    }
    return nullptr;
}

bool AppViewModel::catalogResolvedFor(const QString& hostId) const {
    return model_->hasCatalogFor(hostId);
}

QVariantList AppViewModel::capabilityForCandidate(const QString& slotId, int type,
                                                  const QString& hostKind, const QString& hostId,
                                                  const QString& desiredPath, bool motionOn,
                                                  bool rumbleOn, int touchpadMode) const {
    reducer::CapabilityInputs in;

    // Input layer — what the pad itself reports. An unknown slot leaves the
    // layer at its defaults rather than inventing capabilities.
    if (const auto* slot = slotById(slotId)) {
        in.padMotion = slot->capabilities.hasMotion;
        in.padTouchpad = slot->capabilities.hasTouchpad;
        in.padLightbar = slot->capabilities.hasLightbar;
        // Link layer. A Bluetooth pad has no USB path to claim, so Direct is not
        // merely unselected — it is unreachable.
        in.linkUsb = !slot->bluetooth;
        in.padClaimable = slot->pathSupported;
        in.linkDirect = in.linkUsb && slot->pathSupported && desiredPath == QLatin1String("direct");
    }

    const bool hostIsBluetooth = hostKind == QLatin1String("bluetooth");
    in.hostIsBluetooth = hostIsBluetooth;
    // A Bluetooth destination is Windows' own gamepad layer: nothing to resolve,
    // and no catalog to wait on. A satellite is resolved once its catalog lands.
    in.hostResolved = hostIsBluetooth ? !hostId.isEmpty() : model_->hasCatalogFor(hostId);

    if (!hostIsBluetooth && in.hostResolved) {
        const auto hostFeatures = model_->catalogHostFeatures(hostId);
        const auto mouse = hostFeatures.constFind(QStringLiteral("mouseControl"));
        in.hostMouseControl = mouse != hostFeatures.constEnd() && mouse->supported;
        const auto rumble = hostFeatures.constFind(catalog::kFeatureRumble);
        // An older satellite that advertises no host block at all still returns
        // rumble — the return path predates the block.
        in.hostRumble = rumble == hostFeatures.constEnd() ? true : rumble->supported;
    }

    // Type layer — the catalog's own statement about what this virtual pad
    // carries. Unresolved leaves typeResolved false, which refuses nothing.
    if (const auto typeDto = model_->catalogTypeFor(hostId, type)) {
        const auto known = catalog::knownFeatureSlugs();
        in.typeResolved = true;
        in.typeMotion = reducer::isFeatureOffered(*typeDto, catalog::kFeatureMotion, known);
        in.typeTouchpad = reducer::typeOffersTouchpadDs4(*typeDto);
        in.typeRumble = reducer::isFeatureOffered(*typeDto, catalog::kFeatureRumble, known);
        in.typeLightbar = reducer::isFeatureOffered(*typeDto, catalog::kFeatureLightbar, known);
    }

    in.userMotionOn = motionOn;
    in.userRumbleOn = rumbleOn;
    in.userTouchpadMode = touchpadMode;

    QVariantList out;
    for (const auto& row : reducer::solveCapabilities(in)) {
        QVariantMap m;
        m[QStringLiteral("feature")] = capFeatureToken(row.feature);
        m[QStringLiteral("inOk")] = row.inOk;
        m[QStringLiteral("linkOk")] = row.linkOk;
        m[QStringLiteral("typeOk")] = row.typeOk;
        m[QStringLiteral("hostOk")] = row.hostOk;
        m[QStringLiteral("verdict")] = capVerdictToken(row.verdict);
        m[QStringLiteral("failingLayer")] = capLayerToken(row.failingLayer);
        m[QStringLiteral("hasFailingLayer")] = row.hasFailingLayer;
        out.append(m);
    }
    return out;
}

QVariantList AppViewModel::typeFeatureSummary(const QString& hostId, int type) const {
    const auto typeDto = model_->catalogTypeFor(hostId, type);
    // No catalog, no pills: a guessed "supported" is worse than saying nothing.
    if (!typeDto.has_value()) { return {}; }

    const auto known = catalog::knownFeatureSlugs();
    // The TYPE layer's own answer, in the solver's fixed render order so the
    // preview pills and the full table cannot use two vocabularies.
    const std::pair<const char*, bool> rows[] = {
        {"gamepad", true},
        {"triggers", true},
        {"motion", reducer::isFeatureOffered(*typeDto, catalog::kFeatureMotion, known)},
        {"touchpad", reducer::typeOffersTouchpadDs4(*typeDto)},
        {"mouse", true},
        {"rumble", reducer::isFeatureOffered(*typeDto, catalog::kFeatureRumble, known)},
        {"lightbar", reducer::isFeatureOffered(*typeDto, catalog::kFeatureLightbar, known)},
    };
    QVariantList out;
    for (const auto& [feature, supported] : rows) {
        QVariantMap m;
        m[QStringLiteral("feature")] = QString::fromLatin1(feature);
        m[QStringLiteral("supported")] = supported;
        out.append(m);
    }
    return out;
}

// ── Host slot accounting ────────────────────────────────────────────────────

int AppViewModel::hostBoundSlotCount(const QString& connectionId) const {
    if (connectionId.isEmpty()) { return 0; }
    int count = 0;
    for (const auto& s : model_->state().slotList) {
        if (s.boundConnectionId.has_value() && *s.boundConnectionId == connectionId) { ++count; }
    }
    return count;
}

int AppViewModel::hostSlotCapacity() const { return kHostSlotCapacity; }

QString AppViewModel::displacedSlotName(const QString& connectionId) const {
    if (hostBoundSlotCount(connectionId) < kHostSlotCapacity) { return {}; }
    // The board fills in slot-list order, so the first pad bound to this host is
    // the one a fifth bind would push off it.
    for (const auto& s : model_->state().slotList) {
        if (s.boundConnectionId.has_value() && *s.boundConnectionId == connectionId) {
            return s.name;
        }
    }
    return {};
}

// ── Binding-draft helpers ───────────────────────────────────────────────────

QString AppViewModel::resolveSlotIdForBind(const QString& slotId) const {
    const auto& st = model_->state();
    const auto vidPid = resolveSlotVidPid(model_, slotId);
    if (!vidPid.has_value()) {
        // No (vid, pid) identity to re-resolve through (an SDL pad SDL could not
        // identify): the id is all we have, and it is valid exactly as long as
        // the slot is still listed.
        return slotById(slotId) != nullptr ? slotId : QString();
    }
    // A Direct claim retires the framework slot and publishes a synthetic twin
    // under a different id, so prefer the synthetic when both are present.
    QString framework;
    for (const auto& s : st.slotList) {
        const auto identity = resolveSlotVidPid(model_, s.id);
        if (!identity.has_value() || *identity != *vidPid) { continue; }
        if (s.usbDirect) { return s.id; }
        if (framework.isEmpty()) { framework = s.id; }
    }
    return framework;
}

bool AppViewModel::isVerifiedModel(const QString& slotId) const {
    const auto* slot = slotById(slotId);
    return slot != nullptr && slot->verifiedModel;
}

QString AppViewModel::touchpadModeFor(const QString& connectionId) const {
    const auto pick = model_->touchpadModeStore()->modeFor(connectionId.toStdString());
    // No invented default here — an unpicked host reads "off" and the resolve
    // ladder owns any richer behaviour on the wire.
    return pick.has_value() ? QString::fromStdString(*pick) : QStringLiteral("off");
}

void AppViewModel::setTouchpadMode(const QString& connectionId, const QString& mode) {
    if (connectionId.isEmpty()) { return; }
    if (mode != QLatin1String("off") && mode != QLatin1String("pad") &&
        mode != QLatin1String("mouse")) {
        return; // unrecognised mode — forward-compat no-op
    }
    model_->touchpadModeStore()->setMode(connectionId.toStdString(), mode.toStdString());
}

bool AppViewModel::motionEnabledFor(const QString& slotId) const {
    if (slotId.isEmpty()) { return source::MotionEnabledStore::kDefaultEnabled; }
    return model_->motionEnabledStore()->isEnabled(slotId.toStdString());
}

bool AppViewModel::rumbleEnabledFor(const QString& /*slotId*/) const {
    // TODO(v3): no per-binding rumble store exists; rumble rides the descriptor
    // caps. Add a RumbleEnabledStore mirroring MotionEnabledStore.
    return true;
}

void AppViewModel::setRumbleEnabled(const QString& /*slotId*/, bool /*on*/) {
    // TODO(v3): no per-binding rumble store exists; rumble rides the descriptor
    // caps. Add a RumbleEnabledStore mirroring MotionEnabledStore.
}

QString AppViewModel::discoverySourceFor(const QString& serverId) const {
    for (const auto& s : model_->wifi()->discoveredServers()) {
        if (s.id() == serverId) { return models::discoverySourceLabel(s.source); }
    }
    return {};
}

// ── Apply ───────────────────────────────────────────────────────────────────

bool AppViewModel::applyInFlight() const { return reducer::applyInFlight(apply_); }
bool AppViewModel::applyCancellable() const { return reducer::applyCancellable(apply_); }
QString AppViewModel::applyConnectionState() const { return applyStepToken(apply_.connection); }
QString AppViewModel::applyDestinationState() const { return applyStepToken(apply_.destination); }

void AppViewModel::applyBinding(const QString& slotId, const QString& connectionId, int type,
                                const QString& desiredPath, bool motionOn, bool rumbleOn,
                                int touchpadMode) {
    if (applyInFlight()) { return; } // one run at a time; the overlay is modal

    // The slot-id trap: re-resolve by (vid, pid) BEFORE anything is written. A
    // path switch earlier in the flow can have retired the id the page opened
    // with, and binding a dead id silently does nothing.
    const QString resolved = resolveSlotIdForBind(slotId);
    if (resolved.isEmpty()) {
        apply_ = reducer::ApplyState{};
        emit applyChanged();
        emit applyFinished(false, QStringLiteral("slotGone"), false);
        return;
    }

    applySlotId_ = resolved;
    applyConnectionId_ = connectionId;
    applyType_ = type;
    applyMotionOn_ = motionOn;
    applyRumbleOn_ = rumbleOn;
    applyTouchpadMode_ = touchpadMode;

    // Does the path actually need switching? Only a claimable wired pad has a
    // path at all, and only a genuine change is worth a 20 s budget.
    const auto* slot = slotById(resolved);
    const bool wantsDirect = desiredPath == QLatin1String("direct");
    bool needsPathSwitch = false;
    if (slot != nullptr && slot->pathSupported && !slot->bluetooth &&
        (desiredPath == QLatin1String("direct") || desiredPath == QLatin1String("standard"))) {
        const bool currentlyDirect = slot->desiredPath == reducer::PathChoice::Direct;
        needsPathSwitch = currentlyDirect != wantsDirect;
    }

    dispatchApply(reducer::apply_event::Start{needsPathSwitch, wantsDirect});
    applyTickTimer_->start();
    if (needsPathSwitch) {
        applyPathTimer_->start();
        setSlotPath(resolved, desiredPath);
        return; // the tick reads the FSM settling and hands off to the bind
    }
    beginApplyBind();
}

void AppViewModel::beginApplyBind() {
    // Every setting is written BEFORE the binding, because ConnectionHub::bind
    // resolves the type, the motion grant and the touchpad mode while it builds
    // the descriptor. Writing them first means one PUT carries the whole draft;
    // writing them after would re-attach the slot once per setting.
    //
    // The type goes straight into the store rather than through
    // AppModel::setSlotControllerType, which requires an EXISTING binding (it is
    // the Emulate-picker path) and would re-bind a second time.
    if (applyType_ > 0) {
        model_->typeStore()->setType(applyConnectionId_.toStdString(), applySlotId_.toStdString(),
                                     applyType_);
    }
    setMotionEnabled(applySlotId_, applyMotionOn_);
    setTouchpadMode(applyConnectionId_, applyTouchpadMode_ == 2   ? QStringLiteral("mouse")
                                        : applyTouchpadMode_ == 1 ? QStringLiteral("pad")
                                                                  : QStringLiteral("off"));
    setRumbleEnabled(applySlotId_, applyRumbleOn_);
    bindSlot(applySlotId_, applyConnectionId_);
    applyBindTimer_->start();
}

void AppViewModel::cancelApply() { dispatchApply(reducer::apply_event::Cancel{}); }

void AppViewModel::dispatchApply(const reducer::ApplyEvent& event) {
    const reducer::ApplyState before = apply_;
    apply_ = reducer::reduceApply(before, event);
    if (apply_.phase == before.phase && apply_.connection == before.connection &&
        apply_.destination == before.destination &&
        apply_.elapsedMsOnStep == before.elapsedMsOnStep &&
        apply_.directFellBack == before.directFellBack) {
        return; // the machine refused the event (wrong phase) — nothing moved
    }

    // The Connection step just handed off: stop its budget and start the bind.
    const bool enteredBinding = before.phase == reducer::ApplyPhase::SwitchingPath &&
                                apply_.phase == reducer::ApplyPhase::Binding;
    if (before.phase == reducer::ApplyPhase::SwitchingPath &&
        apply_.phase != reducer::ApplyPhase::SwitchingPath) {
        applyPathTimer_->stop();
    }
    if (!reducer::applyInFlight(apply_)) {
        applyPathTimer_->stop();
        applyBindTimer_->stop();
        applyTickTimer_->stop();
    }

    emit applyChanged();
    if (enteredBinding) { beginApplyBind(); }

    switch (apply_.phase) {
    case reducer::ApplyPhase::Succeeded:
        emit applyFinished(true, QString(), apply_.directFellBack);
        break;
    case reducer::ApplyPhase::Failed:
    case reducer::ApplyPhase::Cancelled:
        emit applyFinished(false,
                           apply_.failure.has_value() ? applyFailureToken(*apply_.failure)
                                                      : QStringLiteral("bindRejected"),
                           apply_.directFellBack);
        break;
    default:
        break;
    }
}

void AppViewModel::onApplyTick() {
    dispatchApply(reducer::apply_event::Tick{kApplyTickMs});
    if (!applyInFlight()) { return; }

    // The pad going away mid-apply is terminal from either step (a claim can
    // retire the id we resolved, and a physical unplug certainly does).
    const auto* slot = slotById(applySlotId_);
    if (slot == nullptr && resolveSlotIdForBind(applySlotId_).isEmpty()) {
        dispatchApply(reducer::apply_event::SlotVanished{});
        return;
    }

    if (apply_.phase == reducer::ApplyPhase::SwitchingPath) {
        // The SAME derived "switching" predicate the slot card's spinner reads,
        // so the overlay and the card can never disagree about when a claim has
        // settled. A slot that vanished into its synthetic twin re-resolves.
        const QString liveId = resolveSlotIdForBind(applySlotId_);
        if (liveId != applySlotId_ && !liveId.isEmpty()) { applySlotId_ = liveId; }
        const auto* live = slotById(applySlotId_);
        if (live == nullptr) { return; }
        const bool switching = reducer::slotPathSwitching(
            live->pathPhase, live->desiredPath, live->usbDirect, live->liveRates.directPollHz,
            live->directFailure.has_value());
        if (!switching) {
            dispatchApply(
                reducer::apply_event::PathSettled{live->pathPhase == reducer::UsbPhase::Direct});
        }
        return;
    }

    // Binding: the hub applies the binding locally and the satellite answers
    // asynchronously. A rollback (the row losing the binding) and the typed
    // slotRegistrationFailed are the negative edges; a live session still
    // carrying the binding is the positive one.
    //
    // Never read the outcome on the tick that ENTERED this step: the local bind
    // is synchronous, so a same-tick read would report success before the
    // satellite had a chance to refuse — and would flash the step past the user.
    if (slot == nullptr || apply_.elapsedMsOnStep <= 0) { return; }
    const bool stillBound =
        slot->boundConnectionId.has_value() && *slot->boundConnectionId == applyConnectionId_;
    if (!stillBound) {
        dispatchApply(reducer::apply_event::BindRejected{/*unreachable=*/false});
        return;
    }
    if (slot->boundStatus.has_value() && slot->boundStatus->live == models::LinkState::Connected) {
        dispatchApply(reducer::apply_event::BindAccepted{});
    }
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
    if (!ok) { emit errorMessage(tr("Couldn’t open browser")); }
}

} // namespace dish::qml
