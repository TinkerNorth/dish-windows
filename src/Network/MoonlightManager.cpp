// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightManager.h"

#include "Network/MoonlightSession.h"
#include "repository/MoonlightHostRepository.h"
#include "source/connection/NvstreamDiscovery.h"

#include <QSet>
#include <QSettings>

namespace dish::net {

QString moonlightPhaseToken(moonlight::SessionPhase phase) {
    switch (phase) {
    case moonlight::SessionPhase::Idle:
        return QStringLiteral("idle");
    case moonlight::SessionPhase::Pairing:
        return QStringLiteral("pairing");
    case moonlight::SessionPhase::Paired:
        return QStringLiteral("paired");
    case moonlight::SessionPhase::Launching:
        return QStringLiteral("launching");
    case moonlight::SessionPhase::RtspHandshake:
    case moonlight::SessionPhase::ControlConnecting:
        return QStringLiteral("connecting");
    case moonlight::SessionPhase::Streaming:
        return QStringLiteral("streaming");
    case moonlight::SessionPhase::Faltering:
        return QStringLiteral("faltering");
    case moonlight::SessionPhase::Closed:
        return QStringLiteral("closed");
    case moonlight::SessionPhase::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}

QList<MoonlightHostRow> mergeMoonlightRows(const QList<models::MoonlightHost>& remembered,
                                           const QList<models::MoonlightHost>& discovered,
                                           const QHash<QString, QString>& phaseTokensById) {
    QList<MoonlightHostRow> rows;
    QSet<QString> seen;

    auto rowFor = [&](const models::MoonlightHost& h, bool isDiscovered) {
        MoonlightHostRow r;
        r.id = h.id();
        r.name = h.name.isEmpty() ? h.ip : h.name;
        r.ip = h.ip;
        r.paired = h.paired;
        r.discovered = isDiscovered;
        r.phaseToken = phaseTokensById.value(r.id, QStringLiteral("idle"));
        return r;
    };

    for (const auto& h : remembered) {
        rows.append(rowFor(h, false));
        seen.insert(h.id());
    }
    for (const auto& h : discovered) {
        // A discovered host already remembered is folded, not duplicated.
        if (seen.contains(h.id())) {
            for (auto& r : rows) {
                if (r.id == h.id()) { r.discovered = true; }
            }
            continue;
        }
        rows.append(rowFor(h, true));
        seen.insert(h.id());
    }
    return rows;
}

MoonlightManager::MoonlightManager(std::shared_ptr<QSettings> settings, QObject* parent)
    : QObject(parent),
      repo_(std::make_unique<repository::MoonlightHostRepository>(std::move(settings))) {}

MoonlightManager::~MoonlightManager() {
    if (discoveryThread_.joinable()) { discoveryThread_.join(); }
    // Sessions are QObject children of this manager; Qt deletes them.
}

QList<MoonlightHostRow> MoonlightManager::hostRows() const {
    QHash<QString, QString> phaseTokens;
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
        if (it.value() != nullptr) {
            phaseTokens.insert(it.key(), moonlightPhaseToken(it.value()->phase()));
        }
    }
    return mergeMoonlightRows(repo_->hosts(), discovered_, phaseTokens);
}

void MoonlightManager::startDiscovery() {
    if (scanning_) { return; }
    if (discoveryThread_.joinable()) { discoveryThread_.join(); }
    scanning_ = true;
    emit scanningChanged();
    discoveryThread_ = std::thread([this] {
        const auto found = NvstreamDiscovery::discover();
        QMetaObject::invokeMethod(
            this,
            [this, found] {
                discovered_ = found;
                scanning_ = false;
                emit scanningChanged();
                emit hostsChanged();
            },
            Qt::QueuedConnection);
    });
}

void MoonlightManager::addManualHost(const QString& ip, const QString& name) {
    if (ip.isEmpty()) { return; }
    models::MoonlightHost h;
    h.ip = ip;
    h.name = name.isEmpty() ? ip : name;
    repo_->rememberHost(h);
    emit hostsChanged();
}

std::optional<models::MoonlightHost> MoonlightManager::hostById(const QString& id) const {
    for (const auto& h : repo_->hosts()) {
        if (h.id() == id) { return h; }
    }
    for (const auto& h : discovered_) {
        if (h.id() == id) { return h; }
    }
    return std::nullopt;
}

MoonlightSession* MoonlightManager::ensureSession(const models::MoonlightHost& host) {
    const QString id = host.id();
    if (auto* existing = sessions_.value(id, nullptr)) { return existing; }
    if (!identity_.has_value()) {
        identity_ = repo_->getOrCreateIdentity();
        if (!identity_.has_value()) { return nullptr; } // OpenSSL failure
    }
    auto* session = new MoonlightSession(host, *identity_, repo_.get(), this);
    sessions_.insert(id, session);
    QObject::connect(session, &MoonlightSession::phaseChanged, this, [this, id] {
        emit sessionPhaseChanged(id);
        emit hostsChanged();
    });
    QObject::connect(session, &MoonlightSession::pairingFinished, this, [this, id](bool ok) {
        emit pairingFinished(id, ok);
        emit hostsChanged();
    });
    QObject::connect(session, &MoonlightSession::rumbleReceived, this,
                     [this, id](int n, int lo, int hi) { emit rumbleReceived(id, n, lo, hi); });
    QObject::connect(
        session, &MoonlightSession::rgbLedReceived, this,
        [this, id](int n, int r, int g, int b) { emit rgbLedReceived(id, n, r, g, b); });
    return session;
}

void MoonlightManager::pairHost(const QString& id, const QString& pin) {
    const auto host = hostById(id);
    if (!host.has_value()) {
        emit pairingFinished(id, false);
        return;
    }
    auto* session = ensureSession(*host);
    if (session == nullptr) {
        emit pairingFinished(id, false);
        return;
    }
    session->pair(pin);
}

void MoonlightManager::connectHost(const QString& id, const QString& appId) {
    const auto host = hostById(id);
    if (!host.has_value()) { return; }
    auto* session = ensureSession(*host);
    if (session != nullptr) { session->launch(appId); }
}

void MoonlightManager::disconnectHost(const QString& id) {
    if (auto* session = sessions_.value(id, nullptr)) { session->quit(); }
}

void MoonlightManager::forgetHost(const QString& id) {
    if (auto* session = sessions_.take(id)) {
        session->quit();
        session->deleteLater();
    }
    repo_->forgetHost(id);
    emit hostsChanged();
}

std::optional<moonlight::SessionPhase> MoonlightManager::sessionPhase(const QString& id) const {
    if (auto* session = sessions_.value(id, nullptr)) { return session->phase(); }
    return std::nullopt;
}

} // namespace dish::net
