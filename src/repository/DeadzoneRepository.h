// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// DeadzoneRepository — the durable per-device deadzone profile store.
//
// Persists, per controller device id, the user's `(stickFlat, triggerFlat)`
// override under the "deadzone:<deviceId>" key namespace of the shared
// connection-store QSettings (HKCU\Software\Dish\Dish). Dumb synchronous
// storage: one std::mutex guards each read-modify-write; no Observables inside
// (the SDL bridge reads this once at device-add and pushes the value into
// GamepadInputProcessor::setDeadzones — never per event, per the hot-path rule).
//
// A KeyedRepository<QString, DeadzoneEntry> whose keyOf() is the entry's device
// id. Each entry is stored as a tiny JSON object {s,t}; a corrupt/garbled value
// for a device falls back to nothing for that device (get -> nullopt) rather
// than crashing — a bad blob must not break controller setup. Namespace
// isolation is a hard invariant: deadzone keys never collide with the pin /
// shared-key / list namespaces, and clear() touches only this repo's prefix.
//
// Windows has no per-device deadzone persistence today (the SDL bridge installs
// a single hard-coded default at attach); this is the new store the deadzone
// Settings UI writes and the bridge consults.

#pragma once

#include "core/input/Deadzones.h"

#include "architecture/Repository.h"

#include <QSettings>
#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace dish::repository {

// One persisted per-device deadzone profile. `deviceId` is the SDL device id
// the SDLGamepadBridge keys on; `stickFlat` / `triggerFlat` are the same scale
// as core/input Deadzones (int16 stick, 0..255 trigger).
struct DeadzoneEntry {
    QString deviceId;
    std::int16_t stickFlat = 0;
    std::uint8_t triggerFlat = 0;

    // Convenience: the pure core/input value this entry carries.
    input::deadzone::Deadzones deadzones() const { return {stickFlat, triggerFlat}; }

    bool operator==(const DeadzoneEntry& o) const {
        return deviceId == o.deviceId && stickFlat == o.stickFlat && triggerFlat == o.triggerFlat;
    }
    bool operator!=(const DeadzoneEntry& o) const { return !(*this == o); }
};

class DeadzoneRepository : public arch::KeyedRepository<QString, DeadzoneEntry> {
  public:
    // `settings` lets tests inject an in-memory-style store; production passes
    // the shared connection-store QSettings so the deadzone/pin/key/list repos
    // co-tenant one backing file. nullptr -> the default HKCU store.
    explicit DeadzoneRepository(std::shared_ptr<QSettings> settings = nullptr);

    QString keyOf(const DeadzoneEntry& value) const override { return value.deviceId; }

    // Domain-named convenience over the generic surface.
    std::optional<input::deadzone::Deadzones> deadzonesFor(const QString& deviceId) const;
    void setDeadzones(const QString& deviceId, const input::deadzone::Deadzones& dz);

    std::optional<DeadzoneEntry> get(const QString& deviceId) const override;
    std::vector<DeadzoneEntry> all() const override;
    void put(const QString& deviceId, const DeadzoneEntry& value) override;
    void remove(const QString& deviceId) override;
    void clear() override;

    // Pull up the KeyedRepository value-overloads hidden by the get/put/remove
    // declarations above.
    using arch::KeyedRepository<QString, DeadzoneEntry>::put;
    using arch::KeyedRepository<QString, DeadzoneEntry>::removeValue;

  private:
    std::shared_ptr<QSettings> settings_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
