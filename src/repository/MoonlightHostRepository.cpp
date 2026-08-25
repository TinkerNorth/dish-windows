// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "repository/MoonlightHostRepository.h"

#include "repository/SettingsKeys.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace dish::repository {

MoonlightHostRepository::MoonlightHostRepository(std::shared_ptr<QSettings> settings)
    : settings_(settings
                    ? std::move(settings)
                    : std::make_shared<QSettings>(QStringLiteral("Dish"), QStringLiteral("Dish"))) {
}

std::optional<moonlight::Identity> MoonlightHostRepository::getOrCreateIdentity() {
    std::lock_guard<std::mutex> lock(mutex_);
    const QString cert = settings_->value(QLatin1String(keys::kMoonlightCertKey)).toString();
    const QString key = settings_->value(QLatin1String(keys::kMoonlightKeyKey)).toString();
    if (!cert.isEmpty() && !key.isEmpty()) {
        moonlight::Identity id;
        id.certPem = cert.toStdString();
        id.privateKeyPem = key.toStdString();
        return id;
    }
    const auto fresh = moonlight::generateIdentity();
    if (!fresh) { return std::nullopt; }
    settings_->setValue(QLatin1String(keys::kMoonlightCertKey),
                        QString::fromStdString(fresh->certPem));
    settings_->setValue(QLatin1String(keys::kMoonlightKeyKey),
                        QString::fromStdString(fresh->privateKeyPem));
    return fresh;
}

QList<models::MoonlightHost> MoonlightHostRepository::readHosts() const {
    const QByteArray raw =
        settings_->value(QLatin1String(keys::kMoonlightHostListKey)).toString().toUtf8();
    QList<models::MoonlightHost> out;
    if (raw.isEmpty()) { return out; }
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray()) { return out; } // corrupt blob must not brick the list
    for (const auto v : doc.array()) {
        if (v.isObject()) {
            const auto host = models::MoonlightHost::fromJson(v.toObject());
            if (host.isValid()) { out.append(host); }
        }
    }
    return out;
}

void MoonlightHostRepository::writeHosts(const QList<models::MoonlightHost>& hosts) {
    QJsonArray arr;
    for (const auto& h : hosts) { arr.append(h.toJson()); }
    settings_->setValue(QLatin1String(keys::kMoonlightHostListKey),
                        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QList<models::MoonlightHost> MoonlightHostRepository::hosts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return readHosts();
}

void MoonlightHostRepository::rememberHost(const models::MoonlightHost& host) {
    if (!host.isValid()) { return; }
    std::lock_guard<std::mutex> lock(mutex_);
    auto list = readHosts();
    const QString id = host.id();
    for (auto& h : list) {
        if (h.id() == id) {
            h = host; // upsert in place
            writeHosts(list);
            return;
        }
    }
    list.append(host);
    writeHosts(list);
}

void MoonlightHostRepository::forgetHost(const QString& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto list = readHosts();
    QList<models::MoonlightHost> kept;
    for (const auto& h : list) {
        if (h.id() != id) { kept.append(h); }
    }
    writeHosts(kept);
    settings_->remove(QLatin1String(keys::kMoonlightServerCertPrefix) + id);
}

std::optional<QString> MoonlightHostRepository::serverCert(const QString& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto v = settings_->value(QLatin1String(keys::kMoonlightServerCertPrefix) + id).toString();
    if (v.isEmpty()) { return std::nullopt; }
    return v;
}

void MoonlightHostRepository::setServerCert(const QString& id, const QString& certPem) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_->setValue(QLatin1String(keys::kMoonlightServerCertPrefix) + id, certPem);
}

} // namespace dish::repository
