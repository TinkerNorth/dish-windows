// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Coverage for Workstream 3e (D4): the CrashReportingStore (opt-out, default ON)
// and the CrashReportingController (forwards flips to a backend seam; its stop()
// is a DELIBERATE no-op so the opt-in survives teardown). The android
// CrashReportingControllerTest (2 tests) is tagged SKIP (it asserts the Firebase
// opt-in call, which doesn't port); these re-derive the portable RULES with a
// fake backend (the house pattern, cf. FakeInhibitor in
// test_screen_wake_controller). The headline assertion: the default did NOT get
// flipped to false. start()/stop() are driven via the kernel ControllerProbe.

#include "composer/CrashReportingBackend.h"
#include "composer/CrashReportingController.h"
#include "source/store/CrashReportingStore.h"

#include "ControllerProbe.h"
#include "StateSourceProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <vector>

using dish::composer::CrashReportingBackend;
using dish::composer::CrashReportingController;
using dish::source::CrashReportingStore;
using dish::test::ControllerProbe;
using dish::test::StateSourceProbe;

namespace {

std::unique_ptr<CrashReportingStore> makeStore(const QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("crash.ini"));
    auto settings = std::make_unique<QSettings>(path, QSettings::IniFormat);
    return std::make_unique<CrashReportingStore>(std::move(settings));
}

// Records every flip the controller forwards — the seam stand-in.
class FakeCrashReportingBackend : public CrashReportingBackend {
  public:
    void setEnabled(bool enabled) override { flips_.push_back(enabled); }
    const std::vector<bool>& flips() const { return flips_; }

  private:
    std::vector<bool> flips_;
};

} // namespace

// --- Store: default ON (the D4 headline) ------------------------------------

TEST_CASE("CrashReportingStore defaults to ENABLED (opt-out, matches android)", "[crash][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir);
    // The D4 decision: a fresh store reads true. Pin that the default was NOT
    // flipped to false.
    REQUIRE(store->enabled());
    REQUIRE(CrashReportingStore::kDefaultEnabled == true);
}

TEST_CASE("CrashReportingStore: opt out then back in persists across a relaunch",
          "[crash][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        auto store = makeStore(dir);
        store->setEnabled(false); // opt out
    }
    {
        auto reloaded = makeStore(dir);
        REQUIRE_FALSE(reloaded->enabled());
        reloaded->setEnabled(true); // opt back in
    }
    {
        auto reloaded = makeStore(dir);
        REQUIRE(reloaded->enabled());
    }
}

TEST_CASE("CrashReportingStore emits distinct-until-changed", "[crash][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir); // default true

    StateSourceProbe<bool> probe(store->state());
    REQUIRE(probe.count() == 1); // replayed current (true)

    store->setEnabled(true); // already true — no-op
    REQUIRE(probe.count() == 1);

    store->setEnabled(false); // real change
    REQUIRE(probe.count() == 2);

    store->setEnabled(false); // redundant — no re-emit
    REQUIRE(probe.count() == 2);
}

// --- Controller: forward flips, survive restart, idempotent start -----------

TEST_CASE("CrashReportingController forwards each flip to the backend", "[crash][controller]") {
    dish::arch::Observable<bool> enabled{true};
    FakeCrashReportingBackend backend;
    CrashReportingController controller(enabled, &backend);

    ControllerProbe<CrashReportingController> probe(controller);
    probe.start();
    // start() applies the current value immediately.
    REQUIRE(backend.flips() == std::vector<bool>{true});

    enabled.set(false);
    enabled.set(true);
    REQUIRE(backend.flips() == std::vector<bool>{true, false, true});
}

TEST_CASE("CrashReportingController SURVIVES stop() -- the opt-in keeps propagating",
          "[crash][controller]") {
    dish::arch::Observable<bool> enabled{true};
    FakeCrashReportingBackend backend;
    CrashReportingController controller(enabled, &backend);

    ControllerProbe<CrashReportingController> probe(controller);
    probe.start();
    REQUIRE(backend.flips().size() == 1); // the initial true

    // stop() is a deliberate no-op (process-scoped). A flip AFTER stop() must
    // STILL reach the backend — the one behaviour that distinguishes this
    // controller from the kernel default (which would cancel the subscription).
    probe.stop();
    enabled.set(false);
    REQUIRE(backend.flips() == std::vector<bool>{true, false});
    enabled.set(true);
    REQUIRE(backend.flips() == std::vector<bool>{true, false, true});
}

TEST_CASE("CrashReportingController: start() is idempotent (applies once)", "[crash][controller]") {
    dish::arch::Observable<bool> enabled{true};
    FakeCrashReportingBackend backend;
    CrashReportingController controller(enabled, &backend);

    controller.start();
    controller.start(); // second start is a no-op (kernel idempotency guard)
    REQUIRE(backend.flips() == std::vector<bool>{true});

    // A subsequent change still applies exactly once (one subscription, not two).
    enabled.set(false);
    REQUIRE(backend.flips() == std::vector<bool>{true, false});
}
