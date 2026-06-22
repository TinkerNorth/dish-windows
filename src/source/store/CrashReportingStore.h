// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CrashReportingStore — the crash-reporting opt-OUT StateSource (Workstream 3e).
// Owns the persisted "collection enabled" flag and republishes it reactively.
// Mirrors dish-android source/store/CrashReportingStore.kt (an
// AbstractStateSource<Boolean> persisting crashlytics_collection_enabled).
//
// D4 (MIGRATION_PLAN §7): the decision resolved to MATCH ANDROID — collection
// default ON with an opt-OUT toggle. So kDefaultEnabled == true: a fresh store
// reads true; the toggle turns collection OFF (opt out). Do NOT flip the default
// to false. The Windows default matches android (default-on / opt-out); the only
// thing DEFERRED is the backend (no Crashlytics — see CrashReportingBackend.h).
//
// The persisted key ("crashlytics_collection_enabled") and store name
// ("user_preferences") are kept verbatim from android for cross-client schema
// continuity.

#pragma once

#include "architecture/StateSource.h"

#include <memory>

class QSettings;

namespace dish::source {

class CrashReportingStore : public arch::StateSource<bool> {
  public:
    // Persisted key — verbatim from android (cross-client schema continuity).
    static constexpr const char* kKeyCollectionEnabled = "crashlytics_collection_enabled";

    // D4: default ON (opt-out), matching android's DEFAULT_ENABLED = true. A
    // fresh store reads true. NOT flipped to false.
    static constexpr bool kDefaultEnabled = true;

    // Production ctor: a QSettings under the app org. Test ctor: inject a store.
    CrashReportingStore();
    explicit CrashReportingStore(std::unique_ptr<QSettings> settings);
    ~CrashReportingStore() override;

    bool enabled() const { return state().value(); }

    // Persist + republish the flag. Distinct-until-changed: a redundant set does
    // not re-emit (android distinct-until-changed contract).
    void setEnabled(bool enabled);

  private:
    static bool readInitial(QSettings& settings);

    std::unique_ptr<QSettings> settings_;
};

} // namespace dish::source
