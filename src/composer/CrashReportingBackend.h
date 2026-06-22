// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CrashReportingBackend — the IO seam the CrashReportingController routes opt-in
// flips to (Workstream 3e). A Gateway behind a narrow virtual interface, the way
// android's CrashReportingController.apply() calls
// FirebaseCrashlytics.isCrashlyticsCollectionEnabled.
//
// D4 (MIGRATION_PLAN §7): the toggle + survive-restart controller + the
// default-ON (opt-out) posture are the parity deliverable for this wave; a REAL
// crash backend is explicitly DEFERRED. Crashlytics is Android-only and does not
// port. The shipped impl is NoopCrashReportingBackend (logs the flip, does
// nothing) — see the prominent TODO(D4) below for where a Windows backend would
// wire in.

#pragma once

namespace dish::composer {

// The narrow gateway interface. setEnabled(bool) is the only call — it routes a
// collection on/off flip to whatever backend exists. No domain state.
class CrashReportingBackend {
  public:
    virtual ~CrashReportingBackend() = default;
    virtual void setEnabled(bool enabled) = 0;
};

// The shipped no-op backend. Records the requested flip to a debug log and does
// nothing else — there is NO crash backend wired this wave (D4).
//
// TODO(D4): wire a Windows backend here (local minidump via MiniDumpWriteDump /
// Sentry-native / Crashpad). Deferred per MIGRATION_PLAN §7 D4. The store +
// controller + toggle already carry the user's opt-in/opt-out; only this
// gateway body is a stub.
class NoopCrashReportingBackend : public CrashReportingBackend {
  public:
    void setEnabled(bool enabled) override;
};

} // namespace dish::composer
