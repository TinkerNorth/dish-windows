// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// MotionPreferenceRepository — the durable per-slot motion-enable toggle store.
//
// Persists, per slot id, whether the user has switched motion forwarding on or
// off for that slot. Stored as ONE JSON array under the "motion_preferences"
// key of a dedicated QSettings store (mirrors dish-android
// MotionPreferenceRepository.kt, which keeps a kotlinx-serialization list under
// a "preferences" key in its own "motion_preferences" SharedPreferences). Dumb
// synchronous storage: one std::mutex guards each read-modify-write; no
// Observables inside (wrap in MotionEnabledStore for reactive reads).
//
// Two invariants the android tests pin and this preserves:
//   * get() on a slot that was NEVER written returns std::nullopt — NOT a
//     default boolean. The store layer above turns absence into the default,
//     so the repo must stay honest about "undecided" vs "explicitly enabled".
//   * Corrupt persisted JSON falls back to an EMPTY list rather than crashing —
//     losing toggles beats bricking app startup on a garbled blob.
//
// A KeyedRepository<QString, MotionPreference> whose keyOf() is the entry's
// slotId; it instantiates Wave 0's RepositoryContract.

#pragma once

#include "architecture/Repository.h"

#include <QSettings>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>

namespace dish::repository {

// One persisted per-slot motion toggle.
struct MotionPreference {
    QString slotId;
    bool enabled = false;

    bool operator==(const MotionPreference& o) const {
        return slotId == o.slotId && enabled == o.enabled;
    }
    bool operator!=(const MotionPreference& o) const { return !(*this == o); }
};

class MotionPreferenceRepository : public arch::KeyedRepository<QString, MotionPreference> {
  public:
    // `settings` lets tests inject an in-memory-style store; production passes a
    // QSettings under the app org. nullptr -> the default HKCU store.
    explicit MotionPreferenceRepository(std::shared_ptr<QSettings> settings = nullptr);

    QString keyOf(const MotionPreference& value) const override { return value.slotId; }

    std::optional<MotionPreference> get(const QString& slotId) const override;
    std::vector<MotionPreference> all() const override;
    void put(const QString& slotId, const MotionPreference& value) override;
    void remove(const QString& slotId) override;
    void clear() override;

    // Pull up the KeyedRepository value-overloads hidden by the get/put/remove
    // declarations above.
    using arch::KeyedRepository<QString, MotionPreference>::put;
    using arch::KeyedRepository<QString, MotionPreference>::removeValue;

  private:
    // Read/write the slotId -> entry list. Both assume mutex_ is held.
    std::vector<MotionPreference> readList() const;
    void writeList(const std::vector<MotionPreference>& list);

    std::shared_ptr<QSettings> settings_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
