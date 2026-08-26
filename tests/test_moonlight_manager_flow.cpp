// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight path end to end, driven through the REAL MoonlightManager over a
// temp QSettings. The two failures this file exists to prevent were both about
// state crossing the boundary between the manager and the store, and neither is
// visible from a pure function: one was trust that was proved and never written
// down, the other a host that lived only in a discovery result until a sweep
// found nothing and took the binding with it.
//
// Covered here: a sweep that finds nothing, add by address, trust confirmed
// without a PIN, forgetting a host and every piece of state that has to go with
// it, binding to a host nobody has paired, a binding surviving a restart,
// controller-number allocation and the four-pad ceiling, session refcounting
// across several bindings, and every way a bind or a pairing can refuse.
//
// NO REAL HOST IS CONTACTED. Every address is in RFC 5737 TEST-NET-1
// (192.0.2.0/24) or TEST-NET-2 (198.51.100.0/24), which are guaranteed
// non-routable, so the HTTP a launch starts goes nowhere and is aborted with the
// session that owns it. Nothing here waits on a reply.

#include "Network/MoonlightManager.h"
#include "Network/MoonlightSession.h"
#include "repository/MoonlightHostRepository.h"

#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QString>
#include <QStringList>

#include <memory>

using dish::models::MoonlightBinding;
using dish::models::MoonlightHost;
using dish::moonlight::BindOutcome;
using dish::net::mergeDiscoverySweep;
using dish::net::MoonlightManager;
using dish::repository::MoonlightHostRepository;
using dish::test::makeSharedSettings;

namespace {

// MoonlightSession builds a QNetworkAccessManager, whose app-static factory
// asserts unless a QCoreApplication exists, and Catch2WithMain creates none. Same
// function-local static with a leaked argv that test_connection_coordinator uses.
void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

MoonlightHost host(const QString& name, const QString& ip) {
    MoonlightHost h;
    h.name = name;
    h.ip = ip;
    return h;
}

// The manager plus a SECOND view of the same store, so an assertion can ask what
// was actually written rather than what the manager happens to be holding.
struct Fixture {
    std::shared_ptr<QSettings> settings;
    std::unique_ptr<MoonlightManager> manager;
    std::unique_ptr<MoonlightHostRepository> store;

    Fixture() : settings(makeSharedSettings()) {
        ensureApp();
        manager = std::make_unique<MoonlightManager>(settings);
        store = std::make_unique<MoonlightHostRepository>(settings);
    }

    // What survives a process restart: a fresh manager over the same store.
    std::unique_ptr<MoonlightManager> reopen() const {
        return std::make_unique<MoonlightManager>(settings);
    }

    bool storedHost(const QString& id) const {
        for (const auto& h : store->hosts()) {
            if (h.id() == id) { return true; }
        }
        return false;
    }

    bool storedAsPaired(const QString& id) const {
        for (const auto& h : store->hosts()) {
            if (h.id() == id) { return h.paired; }
        }
        return false;
    }

    bool listedRow(const QString& id) const {
        for (const auto& row : manager->hostRows()) {
            if (row.id == id) { return true; }
        }
        return false;
    }

    BindOutcome bind(const QString& slotId, const QString& hostId) const {
        return manager->bindSlot(slotId, hostId, dish::models::kMoonlightDeviceAuto, false, false,
                                 false, false, false);
    }

    void remember(const QString& slotId, const QString& hostId,
                  int type = dish::models::kMoonlightDeviceAuto) const {
        MoonlightBinding b;
        b.slotId = slotId;
        b.hostId = hostId;
        b.controllerType = type;
        manager->rememberBinding(b);
    }
};

const QString kIpA = QStringLiteral("192.0.2.11");
const QString kIpB = QStringLiteral("192.0.2.12");
const QString kIdA = QStringLiteral("ml:ip:192.0.2.11");
const QString kIdB = QStringLiteral("ml:ip:192.0.2.12");
// A host no route and no list ever names.
const QString kIdNowhere = QStringLiteral("ml:ip:198.51.100.7");

} // namespace

// ── Discovery ───────────────────────────────────────────────────────────────

TEST_CASE("A sweep that finds nothing keeps what the last one found", "[moonlight][flow]") {
    const QList<MoonlightHost> found = {host(QStringLiteral("PC"), kIpA)};

    // An empty answer is what a blocked multicast, a Wi-Fi roam and a timeout all
    // look like, and a host that drops out of this list is one no binding can name.
    REQUIRE(mergeDiscoverySweep(found, {}).size() == 1);
    REQUIRE(mergeDiscoverySweep(found, {}).first().ip == kIpA);

    // A sweep that DID answer is the truth about the network right now, so a host
    // that really went away still leaves.
    const QList<MoonlightHost> other = {host(QStringLiteral("PC2"), kIpB)};
    REQUIRE(mergeDiscoverySweep(found, other).size() == 1);
    REQUIRE(mergeDiscoverySweep(found, other).first().ip == kIpB);

    // Nothing found over nothing held is still nothing.
    REQUIRE(mergeDiscoverySweep({}, {}).isEmpty());
}

TEST_CASE("An empty sweep through the manager does not empty the host list", "[moonlight][flow]") {
    Fixture fx;
    fx.manager->applyDiscoverySweep({host(QStringLiteral("PC"), kIpA)});
    REQUIRE(fx.listedRow(kIdA));

    fx.manager->applyDiscoverySweep({});
    REQUIRE(fx.listedRow(kIdA));
    REQUIRE_FALSE(fx.manager->isScanning());
}

TEST_CASE("Add by address remembers the host; an empty address does not", "[moonlight][flow]") {
    Fixture fx;

    fx.manager->addManualHost(QString(), QStringLiteral("nameless"));
    REQUIRE(fx.store->hosts().isEmpty());

    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    REQUIRE(fx.storedHost(kIdA));
    REQUIRE(fx.store->hosts().first().name == QStringLiteral("Study PC"));
    // Added is not paired, and the record says so rather than implying trust.
    REQUIRE_FALSE(fx.storedAsPaired(kIdA));

    // The name falls back to the address rather than rendering blank.
    fx.manager->addManualHost(kIpB, QString());
    for (const auto& h : fx.store->hosts()) {
        if (h.id() == kIdB) { REQUIRE(h.name == kIpB); }
    }
}

TEST_CASE("A host id is the address and does not move when the identity is learned",
          "[moonlight][flow]") {
    // The id has to come from ONE source. An id that upgraded when a probe
    // answered would orphan the pinned certificate, the standing bindings and the
    // routing table under the key they were written with, which is exactly the
    // residue this suite exists to close.
    MoonlightHost h = host(QStringLiteral("PC"), kIpA);
    const QString before = h.id();
    h.uuid = QStringLiteral("0123456789abcdef");
    REQUIRE(h.id() == before);
    REQUIRE(h.id() == kIdA);
}

// ── Trust ───────────────────────────────────────────────────────────────────

TEST_CASE("Confirming trust writes the record a pairing would have written",
          "[moonlight][flow][trust]") {
    Fixture fx;
    fx.manager->applyDiscoverySweep({host(QStringLiteral("PC"), kIpA)});
    // Discovered only: visible, and not durable.
    REQUIRE(fx.listedRow(kIdA));
    REQUIRE_FALSE(fx.storedHost(kIdA));

    // A mutual-TLS call the host answered is proof it still holds our
    // certificate, which is the same fact a five-phase pairing establishes. A
    // client that only REPORTS the trust it just proved can never write a
    // forgotten host down again, and the user presses Pair forever.
    fx.manager->rememberProvenTrust(kIdA);
    REQUIRE(fx.storedHost(kIdA));
    REQUIRE(fx.storedAsPaired(kIdA));

    // And it survives the process, which is the whole point of writing it.
    const auto reopened = fx.reopen();
    REQUIRE(reopened->hostRows().size() == 1);
    REQUIRE(reopened->hostRows().first().paired);
}

TEST_CASE("Proving trust twice writes once and proving it for nothing writes nothing",
          "[moonlight][flow][trust]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.manager->rememberProvenTrust(kIdA);
    fx.manager->rememberProvenTrust(kIdA);
    REQUIRE(fx.store->hosts().size() == 1);

    fx.manager->rememberProvenTrust(kIdNowhere);
    REQUIRE(fx.store->hosts().size() == 1);
}

TEST_CASE("Confirming trust keeps everything else the record carries", "[moonlight][flow][trust]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.manager->setHostApp(kIdA, QStringLiteral("7"), QStringLiteral("Steam"));
    fx.manager->setHostDeviceType(kIdA, dish::models::kMoonlightDeviceNintendo);

    fx.manager->rememberProvenTrust(kIdA);

    REQUIRE(fx.storedAsPaired(kIdA));
    REQUIRE(fx.manager->runningAppName(kIdA) == QStringLiteral("Steam"));
    REQUIRE(fx.manager->hostRows().first().deviceType == dish::models::kMoonlightDeviceNintendo);
    REQUIRE(fx.manager->hostRows().first().name == QStringLiteral("Study PC"));
}

// ── Bindings ────────────────────────────────────────────────────────────────

TEST_CASE("A binding to a host nobody has paired is remembered, host and all",
          "[moonlight][flow][binding]") {
    Fixture fx;
    // Discovered and never paired. This is the shape that went dormant: the
    // binding was written, the host was not, and one missed sweep later there was
    // no host left for the binding to name.
    fx.manager->applyDiscoverySweep({host(QStringLiteral("PC"), kIpA)});
    fx.remember(QStringLiteral("sdl:1"), kIdA, dish::models::kMoonlightDeviceXbox);

    REQUIRE(fx.store->bindings().size() == 1);
    REQUIRE(fx.storedHost(kIdA));
    // Pairing is NOT the condition. A binding is a durable intent, and a host
    // nobody has paired yet is a perfectly good thing to intend to drive.
    REQUIRE_FALSE(fx.storedAsPaired(kIdA));

    // Which is what makes it survive a sweep that answered without it.
    fx.manager->applyDiscoverySweep({host(QStringLiteral("Other"), kIpB)});
    REQUIRE(fx.listedRow(kIdA));
}

TEST_CASE("Bindings and their hosts survive a process restart", "[moonlight][flow][binding]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.remember(QStringLiteral("sdl:1"), kIdA, dish::models::kMoonlightDevicePlayStation);

    const auto reopened = fx.reopen();
    REQUIRE(reopened->bindings().size() == 1);
    REQUIRE(reopened->binding(QStringLiteral("sdl:1"))->hostId == kIdA);
    REQUIRE(reopened->binding(QStringLiteral("sdl:1"))->controllerType ==
            dish::models::kMoonlightDevicePlayStation);
    // And the host is there for it to name, before any scan has run.
    REQUIRE(reopened->hostRows().size() == 1);
    REQUIRE(reopened->hostRows().first().id == kIdA);
}

TEST_CASE("A binding naming a host nothing resolves is still kept", "[moonlight][flow][binding]") {
    Fixture fx;
    // The host cannot be remembered with it, which is logged, but the INTENT is
    // the user's and outlives a host that is merely absent right now.
    fx.remember(QStringLiteral("sdl:1"), kIdNowhere);
    REQUIRE(fx.manager->bindings().size() == 1);
    REQUIRE(fx.reopen()->binding(QStringLiteral("sdl:1"))->hostId == kIdNowhere);
}

TEST_CASE("A record naming no slot or no host is refused", "[moonlight][flow][binding]") {
    Fixture fx;
    MoonlightBinding noHost;
    noHost.slotId = QStringLiteral("sdl:1");
    fx.manager->rememberBinding(noHost);

    MoonlightBinding noSlot;
    noSlot.hostId = kIdA;
    fx.manager->rememberBinding(noSlot);

    REQUIRE(fx.manager->bindings().isEmpty());
    REQUIRE(fx.store->bindings().isEmpty());
}

TEST_CASE("Forgetting a binding drops it, and forgetting none is not an error",
          "[moonlight][flow][binding]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.remember(QStringLiteral("sdl:1"), kIdA);

    fx.manager->forgetBinding(QStringLiteral("sdl:9"));
    REQUIRE(fx.manager->bindings().size() == 1);

    fx.manager->forgetBinding(QStringLiteral("sdl:1"));
    REQUIRE(fx.manager->bindings().isEmpty());
    REQUIRE(fx.store->bindings().isEmpty());
    // The host stays: forgetting one pad's intent is not forgetting the PC.
    REQUIRE(fx.storedHost(kIdA));
}

// ── Bind and unbind ─────────────────────────────────────────────────────────

TEST_CASE("Binding allocates controller numbers and refuses a fifth pad",
          "[moonlight][flow][bind]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));

    const QStringList pads = {QStringLiteral("sdl:1"), QStringLiteral("sdl:2"),
                              QStringLiteral("sdl:3"), QStringLiteral("sdl:4")};
    for (int i = 0; i < pads.size(); ++i) {
        REQUIRE(fx.bind(pads.at(i), kIdA) == BindOutcome::Bound);
        REQUIRE(fx.manager->boundSlotCount(kIdA) == i + 1);
        // The lowest free index, so the numbers the host sees are 0..3 in order.
        REQUIRE(fx.manager->slotForController(kIdA, i) == pads.at(i));
    }

    // Four is a protocol ceiling rather than a state that resolves itself, and
    // the refusal has to be REPORTED: a bind that returns silently is what "I
    // pressed bind and nothing happened" is made of.
    REQUIRE(fx.bind(QStringLiteral("sdl:5"), kIdA) == BindOutcome::HostFull);
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 4);
    REQUIRE(fx.manager->boundHostFor(QStringLiteral("sdl:5")).isEmpty());
}

TEST_CASE("Binding a host that resolves to nothing says so", "[moonlight][flow][bind]") {
    Fixture fx;
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), kIdNowhere) == BindOutcome::UnknownHost);
    REQUIRE(fx.manager->boundHostFor(QStringLiteral("sdl:1")).isEmpty());
}

TEST_CASE("Unbinding releases the controller number for the next pad", "[moonlight][flow][bind]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    fx.bind(QStringLiteral("sdl:2"), kIdA);
    REQUIRE(fx.manager->slotForController(kIdA, 0) == QStringLiteral("sdl:1"));
    REQUIRE(fx.manager->slotForController(kIdA, 1) == QStringLiteral("sdl:2"));

    fx.manager->unbindSlot(QStringLiteral("sdl:1"));
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 1);
    REQUIRE(fx.manager->boundHostFor(QStringLiteral("sdl:1")).isEmpty());
    REQUIRE(fx.manager->slotForController(kIdA, 0).isEmpty());

    // Number 0 is free again and the next pad takes it. A reference host skips an
    // arrival for a number already present, so a released one has to be reusable.
    fx.bind(QStringLiteral("sdl:3"), kIdA);
    REQUIRE(fx.manager->slotForController(kIdA, 0) == QStringLiteral("sdl:3"));

    // Unbinding a slot that was never routed is not an error.
    fx.manager->unbindSlot(QStringLiteral("sdl:9"));
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 2);
}

TEST_CASE("Re-binding a slot moves it and leaves nothing behind on the old host",
          "[moonlight][flow][bind]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("A"));
    fx.manager->addManualHost(kIpB, QStringLiteral("B"));

    fx.bind(QStringLiteral("sdl:1"), kIdA);
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 1);

    fx.bind(QStringLiteral("sdl:1"), kIdB);
    REQUIRE(fx.manager->boundHostFor(QStringLiteral("sdl:1")) == kIdB);
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 0);
    REQUIRE(fx.manager->boundSlotCount(kIdB) == 1);
}

TEST_CASE("A session is one per host and counted by the pads riding it",
          "[moonlight][flow][session]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));

    // The first pad on a host brings a session up.
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    REQUIRE(fx.manager->sessionPhase(kIdA).has_value());
    REQUIRE(*fx.manager->sessionPhase(kIdA) == dish::moonlight::SessionPhase::Launching);

    // A second joins the one already there rather than starting a second: the
    // phase does not restart and no new session appears.
    fx.bind(QStringLiteral("sdl:2"), kIdA);
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 2);
    REQUIRE(*fx.manager->sessionPhase(kIdA) == dish::moonlight::SessionPhase::Launching);

    // The LAST pad off owns the teardown: a session nobody is bound to is an app
    // stranded on somebody's desktop, and is also what refuses the next launch.
    fx.manager->unbindSlot(QStringLiteral("sdl:1"));
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 1);
    REQUIRE(*fx.manager->sessionPhase(kIdA) == dish::moonlight::SessionPhase::Launching);

    fx.manager->unbindSlot(QStringLiteral("sdl:2"));
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 0);
    REQUIRE(*fx.manager->sessionPhase(kIdA) == dish::moonlight::SessionPhase::Closed);
}

TEST_CASE("Two hosts carry two sessions and neither counts the other's pads",
          "[moonlight][flow][session]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("A"));
    fx.manager->addManualHost(kIpB, QStringLiteral("B"));

    fx.bind(QStringLiteral("sdl:1"), kIdA);
    fx.bind(QStringLiteral("sdl:2"), kIdB);
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 1);
    REQUIRE(fx.manager->boundSlotCount(kIdB) == 1);
    // Controller numbers are per host, so both pads are controller 0 on theirs.
    REQUIRE(fx.manager->slotForController(kIdA, 0) == QStringLiteral("sdl:1"));
    REQUIRE(fx.manager->slotForController(kIdB, 0) == QStringLiteral("sdl:2"));

    fx.manager->unbindSlot(QStringLiteral("sdl:1"));
    REQUIRE(*fx.manager->sessionPhase(kIdA) == dish::moonlight::SessionPhase::Closed);
    REQUIRE(*fx.manager->sessionPhase(kIdB) == dish::moonlight::SessionPhase::Launching);
}

// ── Forget ──────────────────────────────────────────────────────────────────

TEST_CASE("Forget leaves no residue of any kind", "[moonlight][flow][forget]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.manager->addManualHost(kIpB, QStringLiteral("Other PC"));

    // Every piece of state a host can own, established first.
    fx.manager->rememberProvenTrust(kIdA);                                       // entry, paired
    fx.store->setServerCert(kIdA, QStringLiteral("deadbeef"));                   // pinned cert
    fx.manager->setHostApp(kIdA, QStringLiteral("7"), QStringLiteral("Steam"));  // remembered app
    fx.manager->setHostDeviceType(kIdA, dish::models::kMoonlightDeviceNintendo); // remembered type
    fx.remember(QStringLiteral("sdl:1"), kIdA);                                  // binding
    fx.bind(QStringLiteral("sdl:1"), kIdA); // route, pad number, session

    // And the same on the other host, to prove the forget is surgical.
    fx.store->setServerCert(kIdB, QStringLiteral("cafebabe"));
    fx.remember(QStringLiteral("sdl:2"), kIdB);
    fx.bind(QStringLiteral("sdl:2"), kIdB);

    REQUIRE(fx.storedHost(kIdA));
    REQUIRE(fx.store->serverCert(kIdA).has_value());
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 1);
    REQUIRE(fx.manager->runningAppName(kIdA) == QStringLiteral("Steam"));

    fx.manager->forgetHost(kIdA);

    // 1. The list entry, and the remembered app and type that lived on it.
    REQUIRE_FALSE(fx.storedHost(kIdA));
    REQUIRE(fx.manager->runningAppName(kIdA).isEmpty());
    // 2. The pinned certificate. A host forgotten and added again is a stranger,
    //    and a pin left behind is the one piece of state that decides whether the
    //    NEXT connection is allowed to complete.
    REQUIRE_FALSE(fx.store->serverCert(kIdA).has_value());
    // 3. Every binding that named it.
    REQUIRE_FALSE(fx.manager->binding(QStringLiteral("sdl:1")).has_value());
    REQUIRE(fx.store->bindings().size() == 1);
    // 4. The routing table and the controller-number allocations.
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 0);
    REQUIRE(fx.manager->boundHostFor(QStringLiteral("sdl:1")).isEmpty());
    REQUIRE(fx.manager->slotForController(kIdA, 0).isEmpty());
    // 5. The session.
    REQUIRE_FALSE(fx.manager->sessionPhase(kIdA).has_value());
    // 6. The row.
    REQUIRE_FALSE(fx.listedRow(kIdA));
    // 7. And it stays gone across a restart, which is what durable has to mean in
    //    both directions.
    const auto reopened = fx.reopen();
    REQUIRE_FALSE(reopened->binding(QStringLiteral("sdl:1")).has_value());
    REQUIRE(reopened->hostRows().size() == 1);
    REQUIRE(reopened->hostRows().first().id == kIdB);

    // The other host keeps its pin, its binding and its route.
    REQUIRE(fx.store->serverCert(kIdB).value() == QStringLiteral("cafebabe"));
    REQUIRE(fx.manager->binding(QStringLiteral("sdl:2")).has_value());
    REQUIRE(fx.manager->boundSlotCount(kIdB) == 1);
}

TEST_CASE("A session detached from the store can no longer write to it",
          "[moonlight][flow][forget]") {
    // The half of the forget that no state assertion can reach: the pin verifier
    // runs on a TLS handshake that completes on a LATER turn of the event loop,
    // and the teardown a forget triggers opens exactly one such handshake to send
    // /cancel. Detaching the session is what stops that late callback writing the
    // pairing back into a store the forget has already cleared.
    auto settings = makeSharedSettings();
    ensureApp();
    MoonlightHostRepository repo(settings);
    const auto identity = repo.getOrCreateIdentity();
    REQUIRE(identity.has_value());

    dish::net::MoonlightSession session(host(QStringLiteral("PC"), kIpA), *identity, &repo);
    session.detachFromStore();

    int finished = 0;
    bool ok = true;
    QObject::connect(&session, &dish::net::MoonlightSession::pairingFinished, [&](bool result) {
        ++finished;
        ok = result;
    });

    // Nothing a detached session is asked to do reaches the store any more, and
    // it says so rather than appearing to succeed.
    session.pair(QStringLiteral("1234"));
    REQUIRE(finished == 1);
    REQUIRE_FALSE(ok);
    REQUIRE(repo.hosts().isEmpty());
    REQUIRE_FALSE(repo.serverCert(QStringLiteral("ml:ip:192.0.2.11")).has_value());
}

TEST_CASE("The identity is the client's and never goes with a host", "[moonlight][flow][forget]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    const auto identity = fx.store->getOrCreateIdentity();
    REQUIRE(identity.has_value());

    fx.manager->forgetHost(kIdA);

    // Forgetting one host must not cost the user the certificate every OTHER host
    // knows them by: pairing is per host, identity is per install.
    const auto after = fx.store->getOrCreateIdentity();
    REQUIRE(after.has_value());
    REQUIRE(after->certPem == identity->certPem);
}

TEST_CASE("Forgetting a host nothing knows about is not an error", "[moonlight][flow][forget]") {
    Fixture fx;
    fx.manager->forgetHost(kIdNowhere);
    REQUIRE(fx.manager->hostRows().isEmpty());
}

TEST_CASE("Re-adding a forgotten host starts from nothing", "[moonlight][flow][forget]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.manager->rememberProvenTrust(kIdA);
    fx.store->setServerCert(kIdA, QStringLiteral("deadbeef"));
    fx.manager->forgetHost(kIdA);

    // Same address, same id, and NOT paired: a forget that left the trust behind
    // would show a host as paired that has to be pinned again before anything can
    // talk to it, which is the disagreement the user cannot see or fix.
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    REQUIRE(fx.storedHost(kIdA));
    REQUIRE_FALSE(fx.storedAsPaired(kIdA));
    REQUIRE_FALSE(fx.store->serverCert(kIdA).has_value());

    // And the trust is re-establishable from exactly where the user is.
    fx.manager->rememberProvenTrust(kIdA);
    REQUIRE(fx.storedAsPaired(kIdA));
}

TEST_CASE("A pin can be dropped without losing the host", "[moonlight][flow][forget]") {
    // The recovery a host that announced a new identity needs. The pin we hold is
    // the OLD host's, so keeping it refuses the pairing phase that runs over TLS
    // before the host ever answers, and the user has no way past it from inside
    // the app. Everything else about the host is still true and stays.
    auto settings = makeSharedSettings();
    MoonlightHostRepository repo(settings);
    MoonlightHost h = host(QStringLiteral("PC"), kIpA);
    h.paired = true;
    repo.rememberHost(h);
    repo.setServerCert(h.id(), QStringLiteral("the-old-host"));

    repo.clearServerCert(h.id());
    REQUIRE_FALSE(repo.serverCert(h.id()).has_value());
    REQUIRE(repo.hosts().size() == 1);
    REQUIRE(repo.hosts().first().id() == h.id());
}

// ── Pairing ─────────────────────────────────────────────────────────────────

TEST_CASE("Pairing a host that resolves to nothing reports a failure", "[moonlight][flow][pair]") {
    Fixture fx;
    int finished = 0;
    bool ok = true;
    QObject::connect(fx.manager.get(), &MoonlightManager::pairingFinished,
                     [&](const QString&, bool result) {
                         ++finished;
                         ok = result;
                     });

    fx.manager->pairHost(kIdNowhere, QStringLiteral("1234"));
    REQUIRE(finished == 1);
    REQUIRE_FALSE(ok);
}

TEST_CASE("Cancelling a pairing clears it rather than reporting a refusal",
          "[moonlight][flow][pair]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));

    fx.manager->pairHost(kIdA, QStringLiteral("1234"));
    REQUIRE(fx.manager->sessionUiInputs(kIdA, QString()).pairingActive);
    REQUIRE(*fx.manager->sessionPhase(kIdA) == dish::moonlight::SessionPhase::Pairing);

    fx.manager->cancelPairing(kIdA);
    const auto after = fx.manager->sessionUiInputs(kIdA, QString());
    // Neither active nor refused: the host never said no, the user backed out,
    // and the section drops back to whatever the last probe knew.
    REQUIRE_FALSE(after.pairingActive);
    REQUIRE_FALSE(after.pairingRefused);
    REQUIRE(*fx.manager->sessionPhase(kIdA) != dish::moonlight::SessionPhase::Pairing);
}

TEST_CASE("A second pairing on the same host replaces the first", "[moonlight][flow][pair]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));

    fx.manager->pairHost(kIdA, QStringLiteral("1234"));
    // "New code" while the first exchange is still parked on the host waiting for
    // a PIN nobody will type. Racing a second against it would leave two chains
    // reporting into one row.
    fx.manager->pairHost(kIdA, QStringLiteral("5678"));
    REQUIRE(fx.manager->sessionUiInputs(kIdA, QString()).pairingActive);
    REQUIRE_FALSE(fx.manager->sessionUiInputs(kIdA, QString()).pairingRefused);
    REQUIRE(*fx.manager->sessionPhase(kIdA) == dish::moonlight::SessionPhase::Pairing);
}

TEST_CASE("Cancelling a pairing on a host with no session is not an error",
          "[moonlight][flow][pair]") {
    Fixture fx;
    fx.manager->cancelPairing(kIdNowhere);
    REQUIRE(fx.manager->hostRows().isEmpty());
}
