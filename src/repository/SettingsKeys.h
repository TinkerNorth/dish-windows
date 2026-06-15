// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SettingsKeys — the QSettings key namespaces the trust/identity repositories
// co-tenant in one connection-store file (HKCU\Software\Dish\Dish), mirroring
// dish-android's single "connection_store" SharedPreferences shared by the
// satellite list, shared-key, and cert-pin repos.
//
// Namespace isolation is a hard invariant (a cert pin must never read back as a
// shared key, and clear() on one repo must not touch another's keys). These
// prefixes are the seam that keeps the three disjoint, so they live in one place
// the repos and the ConnectionStore facade all reference.

#pragma once

namespace dish::repository::keys {

// Per-id cert-pin entries: "satellite_cert_pin:<id>" -> fingerprint hex.
inline constexpr const char* kCertPinPrefix = "satellite_cert_pin:";

// Per-id pairing-key entries: "satellite_shared_key:<id>" -> key hex.
// NB: distinct from the legacy "wifi_shared_key/<id>" the pre-2a ConnectionStore
// used; the facade migrates legacy entries on first read (see ConnectionStore).
inline constexpr const char* kSharedKeyPrefix = "satellite_shared_key:";

// The remembered-satellite list, stored as one JSON array under a single key.
inline constexpr const char* kSatelliteListKey = "satellite_list";

// Per-device deadzone profiles: "deadzone:<deviceId>" -> {s,t} JSON
// (Workstream 2d). Disjoint from the pin/shared-key namespaces; clear() on the
// deadzone repo wipes only this prefix.
inline constexpr const char* kDeadzonePrefix = "deadzone:";

// Per-slot motion-enable toggles: "motion_enabled:<slotId>" -> "1"/"0"
// (Workstream 2d). Absence means "never written" -> the store defaults it on.
inline constexpr const char* kMotionEnabledPrefix = "motion_enabled:";

// Stable per-install device id (machineId source for X-Device-Id). One owner.
inline constexpr const char* kDeviceIdKey = "deviceId";

// Pre-2a key names kept only so the facade can upgrade old installs in place.
inline constexpr const char* kLegacyWifiListKey = "wifi_list";
inline constexpr const char* kLegacySharedKeyPrefix = "wifi_shared_key/";

} // namespace dish::repository::keys
