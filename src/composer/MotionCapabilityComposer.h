// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// MotionCapabilityComposer — a kernel Composer deriving, per slot, the motion
// capability the dish should advertise + display: does the pad have a gyro, does
// the slot currently carry motion on its connection, has the user enabled it,
// does the host have a motion sink for the slot's controller type, and the
// satellite's reported backend status. Re-derivation of dish-android
// composer/MotionCapabilityComposer.kt against the Windows connection shape.
//
// SoC: the `derive` transform is a PURE free function over upstream snapshots
// (no Qt widgets, no tr(), no IO) so it unit-tests in isolation; the Composer
// just wraps it over the four upstream Observables. There is no phone/virtual
// slot on Windows (physical controllers only), so — unlike android — the map is
// keyed purely on physical device slot ids.
//
// `toCapBits` is the wire contract the descriptor PUT reads: CAP_MOTION iff the
// pad has a gyro AND the user enabled motion — deliberately NOT gated on
// carriesOnConnection (a satellite reconnect must recover motion without a
// re-handshake) and NOT on the satellite backend status (the dish advertises
// what it can emit; receiver health is the receiver's concern).

#pragma once

#include "architecture/Composer.h"
#include "architecture/Observable.h"
#include "core/model/Protocol.h"
#include "source/store/SatelliteMotionBackendStatusStore.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace dish::composer {

// ── Upstream snapshot shapes (what the transform reads) ──────────────────────

// One physical controller device, as the registry/SDL bridge sees it: the slot
// id it maps to and whether SDL reported a gyro for it.
struct MotionDevice {
    std::string slotId;
    bool hasGyro = false;

    bool operator==(const MotionDevice& o) const {
        return slotId == o.slotId && hasGyro == o.hasGyro;
    }
};

// One connection, reduced to what the motion derivation needs: its id, whether
// it is a satellite link that is currently live (carries motion only then), and
// — keyed per slot — the controller type the satellite applied for that slot
// (PlayStation-typed slots are the only ones the host sinks motion for).
struct MotionConnection {
    std::string id;
    bool isSatellite = true;
    bool connected = false; // live satellite session
    // slotId -> applied controller type (proto::kControllerType*). Absent slot
    // means "type unknown" (treated as sink-supported, no false warning).
    std::map<std::string, int> slotControllerTypes;

    bool operator==(const MotionConnection& o) const {
        return id == o.id && isSatellite == o.isSatellite && connected == o.connected &&
               slotControllerTypes == o.slotControllerTypes;
    }
};

using MotionDeviceList = std::vector<MotionDevice>;
using MotionConnectionList = std::vector<MotionConnection>;
// slotId -> bound connection id.
using MotionBindings = std::map<std::string, std::string>;
// slotId -> user motion-enable toggle (absent = default-on).
using MotionEnabledMap = std::map<std::string, bool>;

// ── Derived per-slot capability ──────────────────────────────────────────────

struct MotionCapability {
    bool hasGyro = false;
    bool carriesOnConnection = false;
    bool userEnabled = true;
    bool hostHasSinkForType = true;
    std::optional<source::SatelliteMotionBackendStatus> satelliteBackendStatus;

    // Local-listener gate: emit motion only when all three local conditions
    // hold. NOT gated on the satellite backend status (that governs the
    // user-facing indicator, not whether the dish bothers reading the gyro).
    bool effective() const { return hasGyro && carriesOnConnection && userEnabled; }

    // Wire caps word contribution: CAP_MOTION iff the dish CAN emit motion for
    // this slot — gyro present and user-enabled. Independent of link state and
    // of receiver health (see class comment).
    int toCapBits() const {
        return (hasGyro && userEnabled) ? static_cast<int>(proto::kCapMotion) : 0;
    }

    // The "no capability" sentinel returned for an unknown slot.
    static MotionCapability off() {
        return MotionCapability{false, false, true, true, std::nullopt};
    }

    bool operator==(const MotionCapability& o) const {
        return hasGyro == o.hasGyro && carriesOnConnection == o.carriesOnConnection &&
               userEnabled == o.userEnabled && hostHasSinkForType == o.hostHasSinkForType &&
               satelliteBackendStatus == o.satelliteBackendStatus;
    }
    bool operator!=(const MotionCapability& o) const { return !(*this == o); }
};

// Default user toggle for a slot the user has never touched: motion ON. (Mirrors
// MotionEnabledStore::kDefaultEnabled; duplicated here so the pure transform has
// no dependency on the store class.)
inline constexpr bool kDefaultMotionEnabled = true;

using MotionCapabilityMap = std::map<std::string, MotionCapability>;

// The pure derivation: per physical device, build its MotionCapability from the
// device's gyro fact, the slot's binding, the bound connection's live/kind/type,
// the user toggle, and the satellite backend status. A device that is in the
// registry but unbound still gets an entry (carriesOnConnection=false). Pure.
MotionCapabilityMap
deriveMotionCapabilities(const MotionDeviceList& devices, const MotionBindings& bindings,
                         const MotionConnectionList& connections, const MotionEnabledMap& enabled,
                         const source::SatelliteMotionBackendStatusMap& backend);

// The Composer: combines the four upstream Observables through deriveMotionCapabilities.
class MotionCapabilityComposer
    : public arch::Composer<MotionCapabilityMap, MotionDeviceList, MotionBindings,
                            MotionConnectionList, MotionEnabledMap,
                            source::SatelliteMotionBackendStatusMap> {
  public:
    MotionCapabilityComposer(
        const arch::Observable<MotionDeviceList>& devices,
        const arch::Observable<MotionBindings>& bindings,
        const arch::Observable<MotionConnectionList>& connections,
        const arch::Observable<MotionEnabledMap>& enabled,
        const arch::Observable<source::SatelliteMotionBackendStatusMap>& backend)
        : arch::Composer<MotionCapabilityMap, MotionDeviceList, MotionBindings,
                         MotionConnectionList, MotionEnabledMap,
                         source::SatelliteMotionBackendStatusMap>(
              devices, bindings, connections, enabled, backend, deriveMotionCapabilities) {}

    // The latest derived capability for a slot, or Off() for an unknown slot.
    MotionCapability capabilityFor(const std::string& slotId) const {
        const auto& snapshot = state().value();
        const auto it = snapshot.find(slotId);
        if (it == snapshot.end()) { return MotionCapability::off(); }
        return it->second;
    }
};

} // namespace dish::composer
