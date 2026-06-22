// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// IdentityKey — the stable per-satellite identity key, pure and Qt-free.
//
// Protocol-1 keys a satellite on its machineId (a per-install id the beacon /
// mDNS TXT advertises), NOT on its network address: a box that moves to a new
// DHCP lease keeps one remembered row + one pairing key + one cert pin instead
// of fragmenting into one row per IP. Older satellites that predate machineId
// fall back to "ip:udpPort". Mirrors dish-android's DiscoveredServer.stableKey
// (core/model/Models.kt).
//
// A *blank* machineId (empty or whitespace-only) is treated as absent — the
// same rule Kotlin's String.isNotBlank() applies — so a beacon that carries an
// empty "mid" field still keys on its address rather than collapsing every
// machineId-less box under one "mid:" key.

#pragma once

#include <string>

namespace dish::model {

// True when `s` is empty or contains only ASCII whitespace. The std::string
// analogue of Kotlin's String.isBlank() for the machineId presence test.
inline bool isBlank(const std::string& s) {
    for (const unsigned char c : s) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v') {
            return false;
        }
    }
    return true;
}

// The stable identity key: "mid:<machineId>" when a machineId is present, else
// "<ip>:<udpPort>". Both discovery paths and the remembered store key on this
// so one physical receiver collapses to a single entry.
inline std::string stableKey(const std::string& machineId, const std::string& ip, int udpPort) {
    if (!isBlank(machineId)) { return "mid:" + machineId; }
    return ip + ":" + std::to_string(udpPort);
}

} // namespace dish::model
