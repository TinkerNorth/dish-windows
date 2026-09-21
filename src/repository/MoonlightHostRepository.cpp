// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "repository/MoonlightHostRepository.h"

#include "repository/SecretValue.h"
#include "repository/SettingsKeys.h"
#include "source/system/DpapiSecretCipher.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

namespace dish::repository {

MoonlightHostRepository::MoonlightHostRepository(std::shared_ptr<QSettings> settings,
                                                 std::shared_ptr<source::SecretCipher> cipher)
    : settings_(settings
                    ? std::move(settings)
                    : std::make_shared<QSettings>(QStringLiteral("Dish"), QStringLiteral("Dish"))),
      cipher_(cipher ? std::move(cipher) : std::make_shared<source::DpapiSecretCipher>()) {
    std::lock_guard<std::mutex> lock(mutex_);
    wrapLegacyKey();
}

// One-time in-place upgrade of the plaintext PEM an older build stored.
void MoonlightHostRepository::wrapLegacyKey() {
    const QString stored = settings_->value(QLatin1String(keys::kMoonlightKeyKey)).toString();
    if (stored.isEmpty() || secret::isWrapped(stored)) { return; }
    if (const auto wrapped = secret::wrap(*cipher_, stored)) {
        settings_->setValue(QLatin1String(keys::kMoonlightKeyKey), *wrapped);
    }
}

std::optional<moonlight::Identity> MoonlightHostRepository::getOrCreateIdentity() {
    std::lock_guard<std::mutex> lock(mutex_);
    const QString cert = settings_->value(QLatin1String(keys::kMoonlightCertKey)).toString();
    const QString storedKey = settings_->value(QLatin1String(keys::kMoonlightKeyKey)).toString();
    if (!cert.isEmpty() && !storedKey.isEmpty()) {
        // A key this account cannot open is no identity at all: mint a fresh
        // one below, which every remembered host will refuse until it is
        // paired again. Better than presenting a certificate we cannot sign
        // for and failing every request with no explanation.
        if (const auto key = secret::unwrap(*cipher_, storedKey)) {
            moonlight::Identity id;
            id.certPem = cert.toStdString();
            id.privateKeyPem = key->toStdString();
            return id;
        }
    }
    auto fresh = moonlight::generateIdentity();
    if (!fresh) { return std::nullopt; }
    settings_->setValue(QLatin1String(keys::kMoonlightCertKey),
                        QString::fromStdString(fresh->certPem));
    const QString keyPem = QString::fromStdString(fresh->privateKeyPem);
    const auto wrapped = secret::wrap(*cipher_, keyPem);
    settings_->setValue(QLatin1String(keys::kMoonlightKeyKey), wrapped ? *wrapped : keyPem);
    return fresh;
}

QString MoonlightHostRepository::getOrCreateUniqueId() {
    std::lock_guard<std::mutex> lock(mutex_);
    QString stored = settings_->value(QLatin1String(keys::kMoonlightUniqueIdKey)).toString();
    if (!stored.isEmpty()) { return stored; }
    // Sixteen uppercase hex digits, the shape every GameStream host expects.
    // system() rather than global(): the id is long-lived identity, so it is
    // seeded from the OS entropy pool the way the key material above is.
    QString fresh;
    fresh.reserve(16);
    auto* rng = QRandomGenerator::system();
    static const char digits[] = "0123456789ABCDEF";
    for (int i = 0; i < 16; ++i) { fresh.append(QLatin1Char(digits[rng->bounded(16)])); }
    settings_->setValue(QLatin1String(keys::kMoonlightUniqueIdKey), fresh);
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

void MoonlightHostRepository::clearServerCert(const QString& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_->remove(QLatin1String(keys::kMoonlightServerCertPrefix) + id);
}

QList<models::MoonlightBinding> MoonlightHostRepository::readBindings() const {
    const QByteArray raw =
        settings_->value(QLatin1String(keys::kMoonlightBindingListKey)).toString().toUtf8();
    QList<models::MoonlightBinding> out;
    if (raw.isEmpty()) { return out; }
    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isArray()) { return out; } // corrupt blob must not brick the list
    for (const auto v : doc.array()) {
        if (v.isObject()) {
            const auto binding = models::MoonlightBinding::fromJson(v.toObject());
            if (binding.isValid()) { out.append(binding); }
        }
    }
    return out;
}

void MoonlightHostRepository::writeBindings(const QList<models::MoonlightBinding>& bindings) {
    QJsonArray arr;
    for (const auto& b : bindings) { arr.append(b.toJson()); }
    settings_->setValue(QLatin1String(keys::kMoonlightBindingListKey),
                        QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QList<models::MoonlightBinding> MoonlightHostRepository::bindings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return readBindings();
}

std::optional<models::MoonlightBinding>
MoonlightHostRepository::binding(const QString& slotId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& b : readBindings()) {
        if (b.slotId == slotId) { return b; }
    }
    return std::nullopt;
}

void MoonlightHostRepository::rememberBinding(const models::MoonlightBinding& binding) {
    if (!binding.isValid()) { return; }
    std::lock_guard<std::mutex> lock(mutex_);
    auto list = readBindings();
    for (auto& b : list) {
        if (b.slotId == binding.slotId) {
            b = binding; // upsert in place
            writeBindings(list);
            return;
        }
    }
    list.append(binding);
    writeBindings(list);
}

void MoonlightHostRepository::forgetBinding(const QString& slotId) {
    std::lock_guard<std::mutex> lock(mutex_);
    QList<models::MoonlightBinding> kept;
    for (const auto& b : readBindings()) {
        if (b.slotId != slotId) { kept.append(b); }
    }
    writeBindings(kept);
}

void MoonlightHostRepository::forgetBindingsForHost(const QString& hostId) {
    std::lock_guard<std::mutex> lock(mutex_);
    QList<models::MoonlightBinding> kept;
    for (const auto& b : readBindings()) {
        if (b.hostId != hostId) { kept.append(b); }
    }
    writeBindings(kept);
}

} // namespace dish::repository
