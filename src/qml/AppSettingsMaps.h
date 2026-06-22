// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure mapping helpers for the AppViewModel's settings/about/deadzone/license
// surfaces. Split out of AppViewModel so the mapping (the only logic the QML
// exposure layer adds — it re-projects already-tested stores) is unit-testable
// without constructing the heavyweight AppModel (which owns SDL/USB/timers).
//
// Every function here is a PURE re-projection over the existing, already-tested
// stores (DeadzoneRepository, MotionEnabledStore, LicenseManifest, the theme
// enum). No store mutation, no Qt Quick — they operate on plain values and
// borrowed store pointers, returning the QVariant shapes the QML pages bind. The
// AppViewModel invokables are thin wrappers over these. Lives in dish_core so
// the unit tests link it directly (same rule as AppViewModel itself).

#pragma once

#include "source/store/ThemePreferenceStore.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>

namespace dish::ui {
struct LicenseManifest;
}

namespace dish::input {
class SDLGamepadBridge;
}

namespace dish::repository {
class DeadzoneRepository;
}

namespace dish::source {
class MotionEnabledStore;
}

namespace dish::qml {

// ── Theme-mode <-> int (the QML segmented control speaks 0/1/2) ──────────────
// The contract order is 0=Light, 1=Dark, 2=System (the SettingsPage chip order),
// which deliberately differs from the source enum's declaration order — so this
// is a real, tested mapping, not a static_cast. Out-of-range ints fall back to
// System (matches the store's lenient fromStorage default).
int themeModeToInt(source::ThemeMode mode);
source::ThemeMode themeModeFromInt(int value);

// ── Deadzone device rows ─────────────────────────────────────────────────────
// The default deadzone profile the SDL bridge installs at attach (the seed the
// row uses when a device has no stored override). Exposed for tests.
constexpr int kDefaultDeadzoneStickFlat = 3277;
constexpr int kDefaultDeadzoneTriggerFlat = 13;

// One row for a single device, folding the durable deadzone override
// (DeadzoneRepository, defaulting to the bridge's hard-coded profile when unset)
// and the per-device motion-forward toggle (MotionEnabledStore, default-on,
// keyed by the device id). Shape:
// { id, name, hasGyro, stickFlat, triggerFlat, forwardMotion }. `deadzoneRepo`
// and `motionStore` are borrowed (may be null -> defaults). This is the pure,
// bridge-free unit the device-list helper iterates — tested directly.
QVariantMap deadzoneRowFor(const QString& deviceId, const QString& name, bool hasGyro,
                           const dish::repository::DeadzoneRepository* deadzoneRepo,
                           const dish::source::MotionEnabledStore* motionStore);

// One row per currently-attached SDL device (iterates the bridge, calling
// deadzoneRowFor per device). `bridge` may be null -> empty list.
QVariantList deadzoneDeviceRows(const dish::input::SDLGamepadBridge* bridge,
                                const dish::repository::DeadzoneRepository* deadzoneRepo,
                                const dish::source::MotionEnabledStore* motionStore);

// ── License rows ─────────────────────────────────────────────────────────────
// Project the parsed manifest into the page's row shape using the SAME pure
// display rules the Widgets LicensesView uses (licenseDisplayName / -VersionLabel
// / -Label / -ClickUrl). Shape: { name, version, license, url }. A row with no
// display name is dropped (mirrors the adapter hiding an unnamed entry).
QVariantList licenseRows(const dish::ui::LicenseManifest& manifest);

} // namespace dish::qml
