// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CrashReportingController — the EFFECT half of the crash-reporting subsystem
// (Workstream 3e): a kernel Controller that subscribes the CrashReportingStore's
// Observable<bool> and routes each opt-in/opt-out flip to a
// CrashReportingBackend gateway. Mirrors dish-android composer/
// CrashReportingController.kt (an AbstractController<Boolean>).
//
// The LOAD-BEARING detail (the one behaviour that distinguishes this Controller
// from every other): stop() is a DELIBERATE NO-OP. android's onStop is `= Unit`
// with the comment "Process-scoped: do not cancel. Must propagate opt-in flips
// across activity restarts." PROMPT_01 calls this out as the canonical reason the
// kernel Controller's stop() is overridable. So here stop() does NOT cancel the
// subscription — a store flip after stop() STILL reaches the backend.
//
// The backend itself is a no-op this wave (D4 — no Crashlytics on Windows; see
// CrashReportingBackend.h). apply(bool) routes to the seam; the seam decides.

#pragma once

#include "architecture/Controller.h"
#include "composer/CrashReportingBackend.h"
#include "source/store/CrashReportingStore.h"

namespace dish::composer {

class CrashReportingController : public arch::Controller<bool> {
  public:
    // `backend` is borrowed (owned by the composition root). The production
    // backend is a NoopCrashReportingBackend; tests inject a fake to assert the
    // forwarded flip sequence.
    CrashReportingController(const arch::Observable<bool>& enabled, CrashReportingBackend* backend);

  protected:
    void apply(const bool& enabled) override;

  public:
    // DELIBERATE no-op: do NOT cancel the subscription. Process-scoped — the
    // opt-in must keep propagating across teardowns (android's onStop = Unit).
    // This is the single behaviour that distinguishes this Controller from the
    // kernel default (which cancels).
    void stop() override;

  private:
    CrashReportingBackend* backend_;
};

} // namespace dish::composer
