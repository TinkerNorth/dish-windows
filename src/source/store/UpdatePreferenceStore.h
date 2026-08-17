// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UpdatePreferenceStore — the three reactive update preferences, republished so
// the coordinator and the Settings page re-render on a flip. Follows the exact
// UiPreferenceStore shape (scalar prefs directly in the Source over QSettings,
// no keyed Repository, hence no RepositoryContract).
//
// The DEFAULT QSettings(), not QSettings("Dish","Dish"): updates are a
// Windows-desktop-only concept, so they live in the Windows-shell hive
// HKCU\Software\TinkerNorth\Dish beside ui_rail_collapsed, never in the
// cross-client schema the android app also defines. The boot gate reaches the
// same path before QGuiApplication sets the org names by naming them
// explicitly.

#pragma once

#include "architecture/StateSource.h"

#include <QSettings>
#include <QString>

#include <memory>
#include <utility>

namespace dish::source {

// ==-comparable so distinct-until-changed suppresses no-op re-emits.
struct UpdatePreferences {
    bool checksEnabled = true;
    bool autoDownload = true;
    // The exact version the user muted; "" when none. Cleared by a newer
    // release, ignored while the running build is below the supported minimum.
    QString skippedVersion;

    bool operator==(const UpdatePreferences& o) const {
        return checksEnabled == o.checksEnabled && autoDownload == o.autoDownload &&
               skippedVersion == o.skippedVersion;
    }
    bool operator!=(const UpdatePreferences& o) const { return !(*this == o); }
};

// The imperative bookkeeping keys share the hive but NOT the reactive slice:
// nothing re-renders on them, and the boot gate writes two of them before any
// store exists. Declared here so every literal has exactly one home.
inline constexpr const char* kKeyUpdatesLastCheckUtcMs = "updates_last_check_utc_ms";
inline constexpr const char* kKeyUpdatesHandoffVersion = "updates_handoff_version";
inline constexpr const char* kKeyUpdatesHandoffAttempts = "updates_handoff_attempts";
inline constexpr const char* kKeyUpdatesLastRunVersion = "updates_last_run_version";

class UpdatePreferenceStore : public arch::StateSource<UpdatePreferences> {
  public:
    static constexpr const char* kKeyChecksEnabled = "updates_check_enabled";
    static constexpr const char* kKeyAutoDownload = "updates_auto_download";
    static constexpr const char* kKeySkippedVersion = "updates_skipped_version";

    UpdatePreferenceStore() : UpdatePreferenceStore(std::make_unique<QSettings>()) {}

    explicit UpdatePreferenceStore(std::unique_ptr<QSettings> settings)
        : arch::StateSource<UpdatePreferences>(readInitial(*settings)),
          settings_(std::move(settings)) {}

    bool checksEnabled() const { return state().value().checksEnabled; }
    bool autoDownload() const { return state().value().autoDownload; }
    QString skippedVersion() const { return state().value().skippedVersion; }

    // All three persist + republish and are idempotent (a repeat set does not
    // re-emit, so the coordinator cannot loop through its own subscription).
    void setChecksEnabled(bool enabled) {
        UpdatePreferences next = state().value();
        if (next.checksEnabled == enabled) { return; }
        next.checksEnabled = enabled;
        settings_->setValue(QLatin1String(kKeyChecksEnabled), enabled);
        settings_->sync();
        setState(next);
    }

    void setAutoDownload(bool enabled) {
        UpdatePreferences next = state().value();
        if (next.autoDownload == enabled) { return; }
        next.autoDownload = enabled;
        settings_->setValue(QLatin1String(kKeyAutoDownload), enabled);
        settings_->sync();
        setState(next);
    }

    void setSkippedVersion(const QString& version) {
        UpdatePreferences next = state().value();
        if (next.skippedVersion == version) { return; }
        next.skippedVersion = version;
        settings_->setValue(QLatin1String(kKeySkippedVersion), version);
        settings_->sync();
        setState(next);
    }

  private:
    static UpdatePreferences readInitial(QSettings& settings) {
        UpdatePreferences initial;
        initial.checksEnabled = settings.value(QLatin1String(kKeyChecksEnabled), true).toBool();
        initial.autoDownload = settings.value(QLatin1String(kKeyAutoDownload), true).toBool();
        initial.skippedVersion =
            settings.value(QLatin1String(kKeySkippedVersion), QString()).toString();
        return initial;
    }

    std::unique_ptr<QSettings> settings_;
};

} // namespace dish::source
