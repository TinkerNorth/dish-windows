// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "composer/MotionCapabilityComposer.h"

namespace dish::composer {

namespace {

const MotionConnection* findConnection(const MotionConnectionList& connections,
                                       const std::string& id) {
    for (const auto& c : connections) {
        if (c.id == id) { return &c; }
    }
    return nullptr;
}

// True iff the slot is bound to a live satellite connection (motion only flows
// on a Connected satellite link).
bool carriesMotion(const std::string& slotId, const MotionBindings& bindings,
                   const MotionConnectionList& connections) {
    const auto it = bindings.find(slotId);
    if (it == bindings.end()) { return false; }
    const MotionConnection* conn = findConnection(connections, it->second);
    if (conn == nullptr) { return false; }
    return conn->isSatellite && conn->connected;
}

// True iff the host has a motion sink for the slot's controller type. The
// satellite sinks motion only for PlayStation- and DualSense-typed slots (both
// ride the DS4 report set); an unknown binding, a non-satellite connection, or an
// unknown type all default to true (no false warning) — the limiting case is an
// explicitly Xbox- (or Switch Pro-) typed satellite slot.
bool hostSinkForType(const std::string& slotId, const MotionBindings& bindings,
                     const MotionConnectionList& connections) {
    const auto it = bindings.find(slotId);
    if (it == bindings.end()) { return true; }
    const MotionConnection* conn = findConnection(connections, it->second);
    if (conn == nullptr) { return true; }
    if (!conn->isSatellite) { return true; }
    const auto typeIt = conn->slotControllerTypes.find(slotId);
    if (typeIt == conn->slotControllerTypes.end()) { return true; }
    const int type = typeIt->second;
    return type == static_cast<int>(proto::kControllerTypePlayStation) ||
           type == static_cast<int>(proto::kControllerTypeDualSense);
}

// The satellite backend status observed for the slot's bound connection, keyed
// on (connectionId, slotId). Null when unbound or no observation has landed.
std::optional<source::SatelliteMotionBackendStatus>
satelliteStatus(const std::string& slotId, const MotionBindings& bindings,
                const source::SatelliteMotionBackendStatusMap& backend) {
    const auto it = bindings.find(slotId);
    if (it == bindings.end()) { return std::nullopt; }
    const auto statusIt = backend.find({it->second, slotId});
    if (statusIt == backend.end()) { return std::nullopt; }
    return statusIt->second;
}

bool userEnabledFor(const std::string& slotId, const MotionEnabledMap& enabled) {
    const auto it = enabled.find(slotId);
    if (it == enabled.end()) { return kDefaultMotionEnabled; }
    return it->second;
}

} // namespace

MotionCapabilityMap
deriveMotionCapabilities(const MotionDeviceList& devices, const MotionBindings& bindings,
                         const MotionConnectionList& connections, const MotionEnabledMap& enabled,
                         const source::SatelliteMotionBackendStatusMap& backend) {
    MotionCapabilityMap out;
    for (const auto& device : devices) {
        const std::string& slotId = device.slotId;
        MotionCapability cap;
        cap.hasGyro = device.hasGyro;
        cap.carriesOnConnection = carriesMotion(slotId, bindings, connections);
        cap.userEnabled = userEnabledFor(slotId, enabled);
        cap.hostHasSinkForType = hostSinkForType(slotId, bindings, connections);
        cap.satelliteBackendStatus = satelliteStatus(slotId, bindings, backend);
        out[slotId] = cap;
    }
    return out;
}

} // namespace dish::composer
