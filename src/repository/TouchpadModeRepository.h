// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// TouchpadModeRepository — the durable per-satellite touchpad-mode pick store.
//
// Persists, per satellite id, which touchpad routing the user picked for that
// satellite ("off" | "ds4" | "mouse" — the wire strings, kept as strings so
// they round-trip without a mapping shim). Stored as ONE JSON array under the
// "touchpad_mode_preferences" key of a dedicated QSettings store (mirrors
// dish-android TouchpadModeRepository.kt, which keeps a kotlinx-serialization
// list under a "preferences" key in its own "touchpad_mode_preferences"
// SharedPreferences). Dumb synchronous storage: one std::mutex guards each
// read-modify-write; no Observables inside (wrap in TouchpadModeStore for
// reactive reads). Header-only.
//
// Invariants the android tests pin and this preserves:
//   * get() on a satellite that was NEVER written returns std::nullopt — NOT a
//     default mode. The layers above turn absence into the pair-time default,
//     so the repo must stay honest about "never picked" vs "explicitly off".
//   * put() of an UNKNOWN mode is rejected at the door (a no-op that never
//     disturbs an existing valid pick), so a typo never persists a value the
//     satellite cannot route.
//   * Corrupt persisted JSON falls back to an EMPTY list rather than crashing —
//     losing picks beats bricking app startup on a garbled blob.
//
// A KeyedRepository<QString, TouchpadModePreference> whose keyOf() is the
// entry's satelliteId; it instantiates Wave 0's RepositoryContract.

#pragma once

#include "architecture/Repository.h"
#include "core/reducer/TouchpadModeResolve.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace dish::repository {

// One persisted per-satellite touchpad-mode pick.
struct TouchpadModePreference {
    QString satelliteId;
    QString mode;

    bool operator==(const TouchpadModePreference& o) const {
        return satelliteId == o.satelliteId && mode == o.mode;
    }
    bool operator!=(const TouchpadModePreference& o) const { return !(*this == o); }
};

// Storage key, declared locally for now — consolidation into SettingsKeys.h
// happens in a later pass so this port does not touch the shared namespace
// registry. One QSettings key holds the whole list; each element is
// {k:<storageKey>, sat:<value.satelliteId>, mode:<value.mode>}. The storage
// key is kept independent of the value's own satelliteId so put(key, value)/
// get(key) round-trip the value verbatim even when they differ — the Map<K,V>
// faithfulness the RepositoryContract pins. In normal use key ==
// value.satelliteId, so the persisted `k` and `sat` coincide.
inline constexpr const char* kTouchpadModeListKey = "touchpad_mode_preferences";

class TouchpadModeRepository : public arch::KeyedRepository<QString, TouchpadModePreference> {
  public:
    // `settings` lets tests inject an in-memory-style store; production passes a
    // QSettings under the app org. nullptr -> the default HKCU store.
    explicit TouchpadModeRepository(std::shared_ptr<QSettings> settings = nullptr)
        : settings_(settings ? std::move(settings)
                             : std::make_shared<QSettings>(QStringLiteral("Dish"),
                                                           QStringLiteral("Dish"))) {}

    QString keyOf(const TouchpadModePreference& value) const override { return value.satelliteId; }

    std::optional<TouchpadModePreference> get(const QString& satelliteId) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& r : readRows()) {
            if (r.key == satelliteId) { return r.value; }
        }
        return std::nullopt; // honest "never picked"
    }

    std::vector<TouchpadModePreference> all() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<TouchpadModePreference> out;
        for (const auto& r : readRows()) { out.push_back(r.value); }
        return out;
    }

    void put(const QString& satelliteId, const TouchpadModePreference& value) override {
        // Reject unknown modes so a typo never persists a value the satellite
        // cannot route (mirrors android's put-door validation).
        if (!reducer::isValidTouchpadModeName(value.mode.toStdString())) { return; }
        std::lock_guard<std::mutex> lock(mutex_);
        auto rows = readRows();
        // Storage key authoritative: replace the row under this key in place
        // (the list never grows on a repeat put for the same key); store the
        // value verbatim under it.
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&](const Row& r) { return r.key == satelliteId; }),
                   rows.end());
        rows.push_back(Row{satelliteId, value});
        writeRows(rows);
    }

    void remove(const QString& satelliteId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto rows = readRows();
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&](const Row& r) { return r.key == satelliteId; }),
                   rows.end());
        writeRows(rows);
    }

    void clear() override {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_->remove(QLatin1String(kTouchpadModeListKey));
    }

    // Pull up the KeyedRepository value-overloads hidden by the get/put/remove
    // declarations above.
    using arch::KeyedRepository<QString, TouchpadModePreference>::put;
    using arch::KeyedRepository<QString, TouchpadModePreference>::removeValue;

  private:
    // One persisted (storageKey -> value) row.
    struct Row {
        QString key;
        TouchpadModePreference value;
    };

    // Read/write the whole row list. Both assume mutex_ is held.
    std::vector<Row> readRows() const {
        const auto raw = settings_->value(QLatin1String(kTouchpadModeListKey)).toByteArray();
        if (raw.isEmpty()) { return {}; }
        QJsonParseError err{};
        const auto doc = QJsonDocument::fromJson(raw, &err);
        // Corrupt blob -> empty (don't crash on garbled prefs); a non-array
        // shape is treated the same way.
        if (err.error != QJsonParseError::NoError || !doc.isArray()) { return {}; }

        std::vector<Row> out;
        for (const auto& v : doc.array()) {
            if (!v.isObject()) { continue; }
            const auto obj = v.toObject();
            const QString sat = obj.value(QLatin1String("sat")).toString();
            const QString key = obj.contains(QLatin1String("k"))
                                    ? obj.value(QLatin1String("k")).toString()
                                    : sat;
            if (key.isEmpty()) { continue; }
            out.push_back(
                Row{key, TouchpadModePreference{sat, obj.value(QLatin1String("mode")).toString()}});
        }
        return out;
    }

    void writeRows(const std::vector<Row>& rows) {
        QJsonArray arr;
        for (const auto& r : rows) {
            arr.append(QJsonObject{
                {"k", r.key}, {"sat", r.value.satelliteId}, {"mode", r.value.mode}});
        }
        settings_->setValue(QLatin1String(kTouchpadModeListKey),
                            QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }

    std::shared_ptr<QSettings> settings_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
