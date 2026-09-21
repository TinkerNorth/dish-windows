// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "repository/SatelliteSharedKeyRepository.h"

#include "repository/SecretValue.h"
#include "repository/SettingsKeys.h"
#include "source/system/DpapiSecretCipher.h"

#include <QLoggingCategory>

namespace dish::repository {

namespace {
Q_LOGGING_CATEGORY(lcDishKeys, "dish.keys")
} // namespace

SatelliteSharedKeyRepository::SatelliteSharedKeyRepository(
    std::shared_ptr<QSettings> settings, std::shared_ptr<source::SecretCipher> cipher)
    : settings_(settings
                    ? std::move(settings)
                    : std::make_shared<QSettings>(QStringLiteral("Dish"), QStringLiteral("Dish"))),
      cipher_(cipher ? std::move(cipher) : std::make_shared<source::DpapiSecretCipher>()) {
    std::lock_guard<std::mutex> lock(mutex_);
    wrapLegacyValues();
}

// One-time in-place upgrade of the plaintext an older build stored. Idempotent:
// a wrapped value is left alone, and a value the cipher refuses to wrap stays
// readable as it is rather than being lost.
void SatelliteSharedKeyRepository::wrapLegacyValues() {
    const QString prefix = QLatin1String(keys::kSharedKeyPrefix);
    for (const auto& key : settings_->allKeys()) {
        if (!key.startsWith(prefix)) { continue; }
        const QString stored = settings_->value(key).toString();
        if (stored.isEmpty() || secret::isWrapped(stored)) { continue; }
        if (const auto wrapped = secret::wrap(*cipher_, stored)) {
            settings_->setValue(key, *wrapped);
        } else {
            qCWarning(lcDishKeys) << "pairing key left unwrapped: the platform refused";
        }
    }
}

std::optional<QString> SatelliteSharedKeyRepository::readKey(const QString& settingsKey) const {
    const QString stored = settings_->value(settingsKey).toString();
    if (stored.isEmpty()) { return std::nullopt; }
    auto plain = secret::unwrap(*cipher_, stored);
    if (!plain.has_value()) {
        qCWarning(lcDishKeys) << "pairing key unreadable on this account; re-pair to replace it";
        return std::nullopt;
    }
    if (plain->isEmpty()) { return std::nullopt; }
    return plain;
}

std::optional<QString> SatelliteSharedKeyRepository::get(const QString& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return readKey(QLatin1String(keys::kSharedKeyPrefix) + id);
}

std::vector<QString> SatelliteSharedKeyRepository::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<QString> out;
    const QString prefix = QLatin1String(keys::kSharedKeyPrefix);
    for (const auto& key : settings_->allKeys()) {
        if (!key.startsWith(prefix)) { continue; }
        if (auto v = readKey(key)) { out.push_back(std::move(*v)); }
    }
    return out;
}

void SatelliteSharedKeyRepository::put(const QString& id, const QString& keyHex) {
    std::lock_guard<std::mutex> lock(mutex_);
    const QString settingsKey = QLatin1String(keys::kSharedKeyPrefix) + id;
    if (const auto wrapped = secret::wrap(*cipher_, keyHex)) {
        settings_->setValue(settingsKey, *wrapped);
        return;
    }
    // Losing the key would mean re-pairing on every launch; the plaintext the
    // older builds always stored is the lesser harm, and it is logged.
    qCWarning(lcDishKeys) << "storing a pairing key unwrapped: the platform refused to protect it";
    settings_->setValue(settingsKey, keyHex);
}

void SatelliteSharedKeyRepository::remove(const QString& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_->remove(QLatin1String(keys::kSharedKeyPrefix) + id);
}

void SatelliteSharedKeyRepository::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    const QString prefix = QLatin1String(keys::kSharedKeyPrefix);
    QStringList toRemove;
    for (const auto& key : settings_->allKeys()) {
        if (key.startsWith(prefix)) { toRemove.append(key); }
    }
    for (const auto& key : toRemove) { settings_->remove(key); }
}

} // namespace dish::repository
