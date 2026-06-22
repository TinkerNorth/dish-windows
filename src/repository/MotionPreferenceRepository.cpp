// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "repository/MotionPreferenceRepository.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace dish::repository {

namespace {

// One QSettings key holds the whole list (android keeps it under "preferences"
// inside a dedicated "motion_preferences" prefs file). Each element is
// {k:<storageKey>, slot:<value.slotId>, on:<bool>}. The storage key is kept
// independent of the value's own slotId so put(key, value)/get(key) round-trip
// the value verbatim even when they differ — the Map<K,V> faithfulness the
// RepositoryContract pins. In normal use key == value.slotId, so the persisted
// `k` and `slot` coincide.
constexpr const char* kListKey = "motion_preferences";
constexpr const char* kFieldKey = "k";
constexpr const char* kFieldSlot = "slot";
constexpr const char* kFieldOn = "on";

// One persisted (storageKey -> value) row.
struct Row {
    QString key;
    MotionPreference value;
};

} // namespace

MotionPreferenceRepository::MotionPreferenceRepository(std::shared_ptr<QSettings> settings)
    : settings_(settings
                    ? std::move(settings)
                    : std::make_shared<QSettings>(QStringLiteral("Dish"), QStringLiteral("Dish"))) {
}

namespace {

std::vector<Row> readRows(const QSettings& settings) {
    const auto raw = settings.value(QLatin1String(kListKey)).toByteArray();
    if (raw.isEmpty()) { return {}; }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(raw, &err);
    // Corrupt blob -> empty (don't crash on garbled prefs); a non-array shape is
    // treated the same way.
    if (err.error != QJsonParseError::NoError || !doc.isArray()) { return {}; }

    std::vector<Row> out;
    for (const auto& v : doc.array()) {
        if (!v.isObject()) { continue; }
        const auto obj = v.toObject();
        const QString slot = obj.value(QLatin1String(kFieldSlot)).toString();
        // Legacy rows (pre-storage-key) only carried `slot`; key falls back to it.
        const QString key = obj.contains(QLatin1String(kFieldKey))
                                ? obj.value(QLatin1String(kFieldKey)).toString()
                                : slot;
        if (key.isEmpty()) { continue; }
        out.push_back(
            Row{key, MotionPreference{slot, obj.value(QLatin1String(kFieldOn)).toBool()}});
    }
    return out;
}

void writeRows(QSettings& settings, const std::vector<Row>& rows) {
    QJsonArray arr;
    for (const auto& r : rows) {
        arr.append(QJsonObject{
            {kFieldKey, r.key}, {kFieldSlot, r.value.slotId}, {kFieldOn, r.value.enabled}});
    }
    settings.setValue(QLatin1String(kListKey), QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace

std::vector<MotionPreference> MotionPreferenceRepository::readList() const {
    std::vector<MotionPreference> out;
    for (const auto& r : readRows(*settings_)) { out.push_back(r.value); }
    return out;
}

void MotionPreferenceRepository::writeList(const std::vector<MotionPreference>& list) {
    std::vector<Row> rows;
    rows.reserve(list.size());
    for (const auto& v : list) { rows.push_back(Row{v.slotId, v}); }
    writeRows(*settings_, rows);
}

std::optional<MotionPreference> MotionPreferenceRepository::get(const QString& slotId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& r : readRows(*settings_)) {
        if (r.key == slotId) { return r.value; }
    }
    return std::nullopt; // honest "never written"
}

std::vector<MotionPreference> MotionPreferenceRepository::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return readList();
}

void MotionPreferenceRepository::put(const QString& slotId, const MotionPreference& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto rows = readRows(*settings_);
    // Storage key authoritative: replace the row under this key in place (the
    // list never grows on a repeat put for the same key); store the value
    // verbatim under it.
    rows.erase(
        std::remove_if(rows.begin(), rows.end(), [&](const Row& r) { return r.key == slotId; }),
        rows.end());
    rows.push_back(Row{slotId, value});
    writeRows(*settings_, rows);
}

void MotionPreferenceRepository::remove(const QString& slotId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto rows = readRows(*settings_);
    rows.erase(
        std::remove_if(rows.begin(), rows.end(), [&](const Row& r) { return r.key == slotId; }),
        rows.end());
    writeRows(*settings_, rows);
}

void MotionPreferenceRepository::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_->remove(QLatin1String(kListKey));
}

} // namespace dish::repository
