// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight behaviour contract, claim by claim, driven through the REAL
// MoonlightManager and MoonlightSession.
//
// What separates this file from test_moonlight_manager_flow.cpp is what it
// watches. That one asserts state: what the store holds, which pad has which
// number, what a forget left behind. This one asserts ACTS: whether the client
// spoke to the host at all, and what it said. Half the contract is about a
// single HTTPS call - "the client quits only what it started" - and a session
// that skips it, or makes it when it should not, is indistinguishable from one
// that gets it right by every state assertion there is. So every request is
// observed as it goes out (MoonlightRequestLog), the world signals a host would
// raise are fed in (MoonlightSessionTestAccess), and the pairing exchange is
// answered by a real socket (MoonlightFakeHost).
//
// NO LIVE HOST IS CONTACTED. Addresses are RFC 5737 TEST-NET-1 (192.0.2.0/24)
// or loopback on an ephemeral port; the HTTPS port handed to a fake host is
// deliberately NOT 47984, because this machine may be running Sunshine and a
// unit test that reaches it is one that can close somebody's session.

#include "Network/MoonlightManager.h"
#include "Network/MoonlightSession.h"
#include "Network/WinsockInit.h"
#include "core/moonlight/MoonlightSessionUi.h"
#include "repository/MoonlightHostRepository.h"
#include "source/connection/NvstreamDiscovery.h"

#include "MoonlightFakeHost.h"
#include "MoonlightRequestLog.h"
#include "MoonlightSessionTestAccess.h"
#include "MoonlightWolfControlHost.h"
#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

#include <memory>
#include <optional>
#include <utility>

using dish::models::MoonlightBinding;
using dish::models::MoonlightHost;
using dish::moonlight::BindOutcome;
using dish::moonlight::SessionEvent;
using dish::moonlight::SessionFailure;
using dish::moonlight::SessionPhase;
using dish::moonlight::SessionUiState;
using dish::moonlight::TrustState;
using dish::net::MoonlightManager;
using dish::net::MoonlightSession;
using dish::net::MoonlightSessionTestAccess;
using dish::repository::MoonlightHostRepository;
using dish::test::makeSharedSettings;
using dish::test::MoonlightFakeHost;
using dish::test::MoonlightRequestLog;
using dish::test::MoonlightWolfControlHost;

namespace {

// MoonlightSession's calls open QSslSockets, whose app-static TLS backend loader
// asserts unless a QCoreApplication exists, and Catch2WithMain creates none.
void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

// Turn the event loop until `done` or the deadline. Every wait in this file is
// bounded: a test that hangs waiting for a host is worse than one that fails.
template <class Fn> bool pumpUntil(Fn done, int budgetMs = 5000) {
    QDeadlineTimer deadline(budgetMs);
    while (!deadline.hasExpired()) {
        if (done()) { return true; }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

void pumpFor(int ms) {
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); }
}

MoonlightHost host(const QString& name, const QString& ip) {
    MoonlightHost h;
    h.name = name;
    h.ip = ip;
    return h;
}

// The manager, a second view of the same store, and the request log. The log is
// attached after a first probe, because a session (and therefore its access
// manager) is not built until something asks the host a question.
struct Fixture {
    std::shared_ptr<QSettings> settings;
    std::unique_ptr<MoonlightManager> manager;
    std::unique_ptr<MoonlightHostRepository> store;

    Fixture() : settings(makeSharedSettings()) {
        ensureApp();
        manager = std::make_unique<MoonlightManager>(settings);
        store = std::make_unique<MoonlightHostRepository>(settings);
    }

    // The session object the manager built for a host, so a test can feed it the
    // world signals a live host would raise.
    MoonlightSession* sessionFor(const QString& ip) const {
        for (auto* session : manager->findChildren<MoonlightSession*>()) {
            if (session->host().ip == ip) { return session; }
        }
        return nullptr;
    }

    BindOutcome bind(const QString& slotId, const QString& hostId) const {
        return manager->bindSlot(slotId, hostId, dish::models::kMoonlightDeviceAuto, false, false,
                                 false, false, false);
    }

    void remember(const QString& slotId, const QString& hostId) const {
        MoonlightBinding b;
        b.slotId = slotId;
        b.hostId = hostId;
        b.controllerType = dish::models::kMoonlightDeviceAuto;
        manager->rememberBinding(b);
    }

    bool hasBinding(const QString& slotId) const {
        for (const auto& b : store->bindings()) {
            if (b.slotId == slotId) { return true; }
        }
        return false;
    }

    // A host the client genuinely holds a pairing with: the record says paired
    // and the certificate that half pins against is on file.
    void establishPairing(const QString& ip) const {
        manager->addManualHost(ip, QStringLiteral("PC"));
        store->setServerCert(QStringLiteral("ml:ip:%1").arg(ip), QStringLiteral("deadbeef"));
        manager->rememberProvenTrust(QStringLiteral("ml:ip:%1").arg(ip));
    }

    SessionUiState uiState(const QString& hostId, const QString& slotId = QString()) const {
        return dish::moonlight::sessionUiState(manager->sessionUiInputs(hostId, slotId));
    }

    // What the section renders once the host has answered. A TEST-NET address
    // answers nothing and the resolver draws a spinner over everything until a
    // probe comes back, so the two facts only a live host can supply are set on
    // the copy: it is there, and its mutual-TLS call went through. Every input
    // the claim actually turns on - the outcome, the pairing, the pad count -
    // is still the manager's own. The existing suite uses the same device.
    SessionUiState uiStateAnswered(const QString& hostId, const QString& slotId = QString()) const {
        auto in = manager->sessionUiInputs(hostId, slotId);
        in.probeInFlight = false;
        in.probeAnswered = true;
        in.hostPairStatus = true;
        return dish::moonlight::sessionUiState(in);
    }
};

const QString kIpA = QStringLiteral("192.0.2.11");
const QString kIpB = QStringLiteral("192.0.2.12");
const QString kIdA = QStringLiteral("ml:ip:192.0.2.11");
const QString kIdB = QStringLiteral("ml:ip:192.0.2.12");
const QString kCancel = QStringLiteral("/cancel");
const QString kLaunch = QStringLiteral("/launch");
const QString kResume = QStringLiteral("/resume");
const QString kPair = QStringLiteral("/pair");
const QString kServerInfo = QStringLiteral("/serverinfo");

// A refusal shaped exactly as a live Sunshine host sends one: HTTP 200 with the
// no in the body. `resume` 1 means the running session is ours to take back.
dish::net::MoonlightXmlResponse busyReply(bool resumable) {
    return dish::net::parseMoonlightXml(
        QStringLiteral("<root status_code=\"400\" status_message=\"An app is already running on "
                       "this host\"><resume>%1</resume></root>")
            .arg(resumable ? 1 : 0)
            .toUtf8());
}

dish::net::MoonlightXmlResponse acceptedReply() {
    return dish::net::parseMoonlightXml(
        QByteArrayLiteral("<root status_code=\"200\"><gamesession>1</gamesession>"
                          "<sessionUrl0>rtsp://192.0.2.11:48010</sessionUrl0></root>"));
}

// The rikey a /launch would have carried: the control stream is sealed with it.
const MoonlightWolfControlHost::Key kRikey = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                                              0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F};

// A remembered host the client holds a pairing with, answering the plaintext probe from `plain`
// and taking its mutual-TLS calls on `tlsPort`.
MoonlightHost pairedHostAt(const MoonlightFakeHost& plain, int tlsPort) {
    MoonlightHost h = host(QStringLiteral("Fake"), QStringLiteral("127.0.0.1"));
    h.httpPort = plain.port();
    h.httpsPort = tlsPort;
    h.paired = true;
    return h;
}

// A port that takes a connection and hangs up at once, so a mutual-TLS call made to it fails in
// its handshake, with no status line at all.
class HangUpPort : public QObject {
  public:
    HangUpPort() {
        server_.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server_, &QTcpServer::newConnection, this, &HangUpPort::hangUp);
    }
    bool listening() const { return server_.isListening(); }
    int port() const { return static_cast<int>(server_.serverPort()); }

  private:
    void hangUp() {
        while (auto* socket = server_.nextPendingConnection()) {
            socket->close();
            socket->deleteLater();
        }
    }

    QTcpServer server_;
};

// Collects what the app actually logged, which is where "name the phase that
// failed" is answered: the copy the user sees is fixed by the UX spec, so the
// diagnostic is the log line or it is nowhere.
class LogCapture {
  public:
    LogCapture() {
        instance() = this;
        previous_ = qInstallMessageHandler(&LogCapture::handle);
    }
    ~LogCapture() {
        qInstallMessageHandler(previous_);
        instance() = nullptr;
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    bool sawContaining(const QString& needle) const {
        for (const auto& line : lines_) {
            if (line.contains(needle)) { return true; }
        }
        return false;
    }

  private:
    static LogCapture*& instance() {
        static LogCapture* current = nullptr;
        return current;
    }
    static void handle(QtMsgType type, const QMessageLogContext& context, const QString& message) {
        if (auto* self = instance()) { self->lines_.append(message); }
        Q_UNUSED(type);
        Q_UNUSED(context);
    }

    QStringList lines_;
    QtMessageHandler previous_ = nullptr;
};

} // namespace

// ── B1 · discovery is bounded ───────────────────────────────────────────────

TEST_CASE("A scan runs for a bounded period and then stops", "[moonlight][behaviour][b1]") {
    // The list is opened by a person waiting in front of it, so the sweep has to
    // end whether or not anything answers. Real socket, real multicast query,
    // nothing on the other end: what is asserted is that it comes back.
    const dish::net::WinsockInit winsock;
    REQUIRE(winsock.ok());

    QElapsedTimer clock;
    clock.start();
    const auto found = dish::net::NvstreamDiscovery::discover(250);
    const qint64 elapsed = clock.elapsed();

    // The budget plus one receive timeout, with room for a loaded runner.
    REQUIRE(elapsed < 3000);
    // Whatever it found or did not find, it answered.
    REQUIRE(found.size() >= 0);
    // And the default budget is a human wait rather than an open-ended one.
    REQUIRE(dish::net::NvstreamDiscovery::kDefaultTimeoutMs > 0);
    REQUIRE(dish::net::NvstreamDiscovery::kDefaultTimeoutMs <= 10000);
}

// ── B2 · add by address ─────────────────────────────────────────────────────

TEST_CASE("Add by address writes the host AND asks it", "[moonlight][behaviour][b2]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));

    // Durable, so the row outlives the process that typed it.
    REQUIRE(fx.store->hosts().size() == 1);
    REQUIRE(fx.store->hosts().first().id() == kIdA);

    // AND ASKED. Typing an address is a question about a machine; a row that
    // appears having never been spoken to can only report the memory of the
    // typing. The probe is what turns it into an answer.
    REQUIRE(fx.manager->sessionUiInputs(kIdA, QString()).probeInFlight);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);

    // It is in the list, and a sweep that answered without it does not take it.
    fx.manager->applyDiscoverySweep({host(QStringLiteral("Other"), kIpB)});
    bool listed = false;
    for (const auto& row : fx.manager->hostRows()) {
        if (row.id == kIdA) { listed = true; }
    }
    REQUIRE(listed);
}

// ── B3 · pairing ────────────────────────────────────────────────────────────

TEST_CASE("A pairing runs the five phases in order against a host that answers",
          "[moonlight][behaviour][b3]") {
    // The whole exchange, over a real socket, with the server half computed from
    // what this client actually sent. Nothing here can pass on a client that
    // gets the AES key or a challenge hash wrong: phase 3 is only reachable by a
    // client whose phase 2 the server could decrypt.
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightHostRepository repo(settings);
    const auto identity = repo.getOrCreateIdentity();
    REQUIRE(identity.has_value());

    MoonlightFakeHost fake(QStringLiteral("4271"));
    REQUIRE(fake.listening());

    MoonlightHost h = host(QStringLiteral("Fake"), QStringLiteral("127.0.0.1"));
    h.httpPort = fake.port();
    // NEVER the real HTTPS port: phase 5 must not be able to reach a live host.
    h.httpsPort = fake.port();

    MoonlightSession session(h, *identity, &repo);
    MoonlightRequestLog log(session);

    session.pair(QStringLiteral("4271"));
    // Phase 5 is the mutual-TLS one, which the plaintext fake cannot complete;
    // reaching it at all is the proof that phases 1 to 4 all verified.
    REQUIRE(pumpUntil([&] { return fake.phasesServed().size() == 4; }));
    // Every request carried this install's own uniqueid, not the constant the
    // Moonlight clients share: the host keys its pending pairing on it.
    REQUIRE(log.query(0).contains(QStringLiteral("uniqueid=") + repo.getOrCreateUniqueId()));
    REQUIRE_FALSE(log.query(0).contains(QStringLiteral("uniqueid=0123456789ABCDEF")));
    REQUIRE(fake.phasesServed() == QList<int>({1, 2, 3, 4}));
    REQUIRE(fake.pairedSomebody());
    REQUIRE(pumpUntil([&] { return log.paths().count(kPair) == 5; }));
    // The last call is the TLS one, and it is the one that names itself.
    REQUIRE(log.query(4).contains(QStringLiteral("phrase=pairchallenge")));
}

TEST_CASE("A pairing holds its request open rather than giving up on the human",
          "[moonlight][behaviour][b3]") {
    // Phase 1 PARKS ON THE HOST until somebody types the code, so the client's
    // budget is the person's patience and not the network's. A client-side
    // deadline shorter than that would report a refusal the host never made,
    // about a code the user is still reading off the screen.
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightHostRepository repo(settings);
    const auto identity = repo.getOrCreateIdentity();
    REQUIRE(identity.has_value());

    MoonlightFakeHost fake(QStringLiteral("4271"), MoonlightFakeHost::kAnswerNothing);
    REQUIRE(fake.listening());

    MoonlightHost h = host(QStringLiteral("Fake"), QStringLiteral("127.0.0.1"));
    h.httpPort = fake.port();
    h.httpsPort = fake.port();

    MoonlightSession session(h, *identity, &repo);
    int finished = 0;
    QObject::connect(&session, &MoonlightSession::pairingFinished, [&](bool) { ++finished; });

    session.pair(QStringLiteral("4271"));
    pumpFor(2500);

    // Still waiting, and still saying so. The full window is the host's: a live
    // Sunshine holds the request for about five minutes.
    REQUIRE(finished == 0);
    REQUIRE(session.phase() == SessionPhase::Pairing);

    // And backing out reaches the exchange rather than only the screen.
    session.cancelPairing();
    REQUIRE(session.phase() != SessionPhase::Pairing);
}

// ── B4 · trust that is proved is trust that is written down ─────────────────

TEST_CASE("A certificate pinned by a handshake is not on its own a pairing",
          "[moonlight][behaviour][b4]") {
    // TOFU pins on the FIRST TLS handshake with a host, and the binding screen
    // asks an unpaired host for its app list, so a host nobody has ever paired
    // with ends up with a certificate on file and a record saying paired:false.
    // Reading the pin as trust made that host claim to be Remembered, which
    // promises a session it cannot start, and made it claim TrustLost once it
    // answered, which tells a first-time user a pairing they never made was
    // removed.
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.store->setServerCert(kIdA, QStringLiteral("deadbeef"));

    auto in = fx.manager->sessionUiInputs(kIdA, QString());
    REQUIRE_FALSE(in.pairingHeld);
    REQUIRE(dish::moonlight::trustFor(in) == dish::moonlight::TrustState::NotPaired);

    // And once the host answers, it is NOT paired rather than trust lost.
    in.probeAnswered = true;
    REQUIRE(dish::moonlight::sessionUiState(in) == SessionUiState::NotPaired);

    // Confirming trust is what turns the pin into a pairing, and it PERSISTS.
    fx.manager->rememberProvenTrust(kIdA);
    REQUIRE(fx.store->hosts().first().paired);
    const auto after = fx.manager->sessionUiInputs(kIdA, QString());
    REQUIRE(after.pairingHeld);

    // Both halves, or it is not a pairing: dropping the certificate for a host
    // that announced a new identity leaves the record alone and the trust gone.
    fx.store->clearServerCert(kIdA);
    REQUIRE_FALSE(fx.manager->sessionUiInputs(kIdA, QString()).pairingHeld);
}

TEST_CASE("The pairing question is asked on the one route that can answer it",
          "[moonlight][behaviour][b4][h1]") {
    // A live Sunshine host answers PairStatus 0 over plaintext to every caller,
    // measured against the host at 192.168.68.98 for the very uniqueid this
    // client sends. So a plaintext probe alone can never make a paired host look
    // paired, and the hosts screen, which is the only surface that probes, would
    // show Not paired over a host the user had just paired with. A host we hold
    // a pairing with is therefore asked again over mutual TLS.
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightManager manager(settings);
    MoonlightHostRepository store(settings);

    MoonlightFakeHost fake(QStringLiteral("0000"));
    REQUIRE(fake.listening());

    // A remembered, paired host that answers plaintext with PairStatus 0.
    MoonlightHost h = host(QStringLiteral("Fake"), QStringLiteral("127.0.0.1"));
    h.httpPort = fake.port();
    h.httpsPort = fake.port();
    h.paired = true;
    store.rememberHost(h);
    store.setServerCert(h.id(), QStringLiteral("deadbeef"));

    manager.probeHost(h.id());
    MoonlightRequestLog log(manager);

    // The plaintext answer is followed by the mutual-TLS question, which only an
    // answered plaintext probe asks.
    REQUIRE(pumpUntil([&] { return log.sawAny(QStringLiteral("/applist")); }));
}

// ── H1 · trust is the mutual-TLS answer's to give ───────────────────────────

TEST_CASE("A paired host is not called unpaired while its mutual-TLS answer is still out",
          "[moonlight][behaviour][h1]") {
    // The plaintext half of a probe says the host is there. Its PairStatus is 0
    // for every caller on Sunshine and on Wolf alike, so that half cannot say
    // whether the pairing stands, and the row must not offer Pair over a host
    // it has not heard from yet.
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightManager manager(settings);
    MoonlightHostRepository store(settings);
    MoonlightFakeHost plain(QStringLiteral("0000"));
    REQUIRE(plain.listening());
    // Takes the mutual-TLS call and never answers it.
    QTcpServer silentTls;
    REQUIRE(silentTls.listen(QHostAddress::LocalHost, 0));
    const MoonlightHost h = pairedHostAt(plain, static_cast<int>(silentTls.serverPort()));
    store.rememberHost(h);
    store.setServerCert(h.id(), QStringLiteral("deadbeef"));

    manager.probeHost(h.id());
    MoonlightRequestLog log(manager);
    REQUIRE(pumpUntil([&] { return log.sawAny(QStringLiteral("/applist")); }));

    const auto in = manager.sessionUiInputs(h.id(), QString());
    CHECK(dish::moonlight::trustFor(in) == TrustState::Remembered);
    CHECK(dish::moonlight::sessionUiState(in) == SessionUiState::Checking);
}

TEST_CASE("A probe asked again while its mutual-TLS answer is out asks nothing twice",
          "[moonlight][behaviour][h1]") {
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightManager manager(settings);
    MoonlightHostRepository store(settings);
    MoonlightFakeHost plain(QStringLiteral("0000"));
    REQUIRE(plain.listening());
    QTcpServer silentTls;
    REQUIRE(silentTls.listen(QHostAddress::LocalHost, 0));
    const MoonlightHost h = pairedHostAt(plain, static_cast<int>(silentTls.serverPort()));
    store.rememberHost(h);
    store.setServerCert(h.id(), QStringLiteral("deadbeef"));
    manager.probeHost(h.id());
    MoonlightRequestLog log(manager);
    REQUIRE(pumpUntil([&] { return log.sawAny(QStringLiteral("/applist")); }));

    manager.probeHost(h.id());

    CHECK(log.count(kServerInfo) == 0);
    CHECK(log.count(QStringLiteral("/applist")) == 1);
}

TEST_CASE("A paired host whose mutual-TLS call fails has lost the trust",
          "[moonlight][behaviour][h1]") {
    // The same host once the mutual-TLS half is over: a call the host would
    // not complete is its verdict, and the pairing we hold is not confirmed.
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightManager manager(settings);
    MoonlightHostRepository store(settings);
    MoonlightFakeHost plain(QStringLiteral("0000"));
    REQUIRE(plain.listening());
    HangUpPort refusingTls;
    REQUIRE(refusingTls.listening());
    const MoonlightHost h = pairedHostAt(plain, refusingTls.port());
    store.rememberHost(h);
    store.setServerCert(h.id(), QStringLiteral("deadbeef"));

    int apps = 0;
    QObject::connect(&manager, &MoonlightManager::appListReady,
                     [&apps](const QString&, const QStringList&, const QStringList&) { ++apps; });
    manager.probeHost(h.id());
    REQUIRE(pumpUntil([&apps] { return apps > 0; }));

    const auto in = manager.sessionUiInputs(h.id(), QString());
    CHECK(dish::moonlight::trustFor(in) == TrustState::NotPaired);
    CHECK(dish::moonlight::sessionUiState(in) == SessionUiState::TrustLost);
}

// ── H4 · the uniqueid comes from /serverinfo ────────────────────────────────

TEST_CASE("A host's uniqueid is learned from its /serverinfo and the id stays its address",
          "[moonlight][behaviour][h4]") {
    // Neither Sunshine nor Wolf publishes a uniqueid over mDNS (Wolf publishes
    // no TXT record at all), so the plaintext /serverinfo is where it comes
    // from. It is kept as the witness for a host that later answers as another
    // machine.
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightManager manager(settings);
    MoonlightHostRepository store(settings);
    MoonlightFakeHost fake(QStringLiteral("0000"));
    REQUIRE(fake.listening());
    MoonlightHost h = host(QStringLiteral("Fake"), QStringLiteral("127.0.0.1"));
    h.httpPort = fake.port();
    h.httpsPort = fake.port();
    store.rememberHost(h);

    manager.probeHost(h.id());
    REQUIRE(pumpUntil([&] { return manager.sessionUiInputs(h.id(), QString()).probeAnswered; }));

    REQUIRE(store.hosts().size() == 1);
    CHECK(store.hosts().first().uuid == QStringLiteral("FAKEHOST-0001"));
    CHECK(store.hosts().first().id() == h.id());
}

// ── B5 · a pairing that stops ───────────────────────────────────────────────

TEST_CASE("A refused pairing stops at the phase that refused it and writes nothing",
          "[moonlight][behaviour][b5]") {
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightHostRepository repo(settings);
    const auto identity = repo.getOrCreateIdentity();
    REQUIRE(identity.has_value());

    MoonlightFakeHost fake(QStringLiteral("4271"), /*refusePhase=*/2);
    REQUIRE(fake.listening());

    MoonlightHost h = host(QStringLiteral("Fake"), QStringLiteral("127.0.0.1"));
    h.httpPort = fake.port();
    h.httpsPort = fake.port();

    LogCapture logged;
    MoonlightSession session(h, *identity, &repo);
    int finished = 0;
    bool ok = true;
    QObject::connect(&session, &MoonlightSession::pairingFinished, [&](bool result) {
        ++finished;
        ok = result;
    });

    session.pair(QStringLiteral("4271"));
    REQUIRE(pumpUntil([&] { return finished > 0; }));
    REQUIRE_FALSE(ok);

    // It STOPPED: phase 3 was never sent, so the host is not left mid-exchange.
    REQUIRE(fake.phasesServed() == QList<int>({1, 2}));
    // It NAMED the phase. The copy the user sees is fixed by the render
    // contract, so the phase is in the diagnostic or it is nowhere at all.
    REQUIRE(logged.sawContaining(QStringLiteral("phase 2")));
    // And it wrote NOTHING: no record, no pin, nothing half-made.
    REQUIRE(repo.hosts().isEmpty());
    REQUIRE_FALSE(repo.serverCert(h.id()).has_value());

    // Retryable from where the user is standing: a second attempt starts over
    // rather than being refused because one is already on file.
    session.pair(QStringLiteral("4271"));
    REQUIRE(session.phase() == SessionPhase::Pairing);
}

// ── B6 · forget ─────────────────────────────────────────────────────────────

TEST_CASE("A probe answered after the forget writes nothing back", "[moonlight][behaviour][b6]") {
    // A forget with a session up sends /cancel and keeps the session alive to
    // hear its answer, so a reply to a question asked BEFORE the forget can still
    // land on it. Nothing that reply carries may reach the manager: it would put
    // a verdict back on a host that no longer exists.
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightManager manager(settings);
    MoonlightHostRepository store(settings);
    MoonlightFakeHost plain(QStringLiteral("0000"));
    REQUIRE(plain.listening());
    // Takes the /cancel and never answers it, so the session waits out its grace.
    QTcpServer silentTls;
    REQUIRE(silentTls.listen(QHostAddress::LocalHost, 0));
    const MoonlightHost h = pairedHostAt(plain, static_cast<int>(silentTls.serverPort()));
    store.rememberHost(h);
    store.setServerCert(h.id(), QStringLiteral("deadbeef"));
    REQUIRE(manager.bindSlot(QStringLiteral("sdl:1"), h.id(), dish::models::kMoonlightDeviceAuto,
                             false, false, false, false, false) == BindOutcome::Bound);
    auto* session = manager.findChild<MoonlightSession*>();
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Streaming);
    bool landed = false;
    QObject::connect(session, &MoonlightSession::probeFinished,
                     [&landed](bool, const QString&) { landed = true; });

    manager.probeHost(h.id());
    manager.forgetHost(h.id());
    REQUIRE(session->cancelInFlight());
    REQUIRE(pumpUntil([&landed] { return landed; }));

    const auto in = manager.sessionUiInputs(h.id(), QString());
    CHECK_FALSE(in.probeAnswered);
    CHECK_FALSE(in.probeInFlight);
    CHECK(store.hosts().isEmpty());
    CHECK_FALSE(store.serverCert(h.id()).has_value());
}

// ── B7, B8, B22, B23 · one session per host ─────────────────────────────────

TEST_CASE("The first pad on a host launches and the second makes no call at all",
          "[moonlight][behaviour][b7][b8]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    REQUIRE(fx.sessionFor(kIpA) != nullptr);
    MoonlightRequestLog log(*fx.manager);
    log.clear();

    // The app belongs to the SESSION, and the first pad on a host is what
    // settles it: the pick the binding flow wrote is the one that is launched.
    fx.manager->setHostApp(kIdA, QStringLiteral("881448767"), QStringLiteral("Desktop"));

    // First pad: the binding is saved and the session is asked for.
    fx.remember(QStringLiteral("sdl:1"), kIdA);
    REQUIRE(fx.hasBinding(QStringLiteral("sdl:1")));
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), kIdA) == BindOutcome::Bound);
    REQUIRE(log.count(kLaunch) == 1);
    REQUIRE(log.query(0).contains(QStringLiteral("appid=881448767")));
    REQUIRE(*fx.manager->sessionPhase(kIdA) == SessionPhase::Launching);

    // Second pad on the same host: NO HTTP AT ALL. Not a second launch, not a
    // resume, not a probe. It rides the session that is already being brought up.
    const QStringList before = log.paths();
    fx.remember(QStringLiteral("sdl:2"), kIdA);
    REQUIRE(fx.bind(QStringLiteral("sdl:2"), kIdA) == BindOutcome::Bound);
    REQUIRE(log.paths() == before);
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 2);
    REQUIRE(*fx.manager->sessionPhase(kIdA) == SessionPhase::Launching);
}

TEST_CASE("A bound pad on a stream that came up is Live under its controller number",
          "[moonlight][behaviour][b7]") {
    Fixture fx;
    fx.establishPairing(kIpA);
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), kIdA) == BindOutcome::Bound);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);

    MoonlightSessionTestAccess::settle(*session, SessionPhase::Streaming);

    CHECK(fx.uiStateAnswered(kIdA, QStringLiteral("sdl:1")) == SessionUiState::Live);
    CHECK(fx.manager->slotForController(kIdA, 0) == QStringLiteral("sdl:1"));
    // A pad that is not on the stream is offered the session instead.
    CHECK(fx.uiStateAnswered(kIdA, QStringLiteral("sdl:2")) == SessionUiState::Joining);
}

TEST_CASE("A pad joining a live session is plugged in on the stream and asks the host nothing",
          "[moonlight][behaviour][b8]") {
    // The host that holds the stream, declared first so it outlives the session.
    MoonlightWolfControlHost wolf(kRikey);
    REQUIRE(wolf.listening());
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), kIdA) == BindOutcome::Bound);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    REQUIRE(MoonlightSessionTestAccess::control(*session).connect("127.0.0.1", wolf.port(), kRikey,
                                                                  0, 2000));
    MoonlightSessionTestAccess::settle(*session, SessionPhase::ControlConnecting);
    MoonlightSessionTestAccess::feed(*session, SessionEvent::ControlConnected);
    REQUIRE(pumpUntil([&wolf] { return wolf.padType(0).has_value(); }));
    MoonlightRequestLog log(*fx.manager);

    REQUIRE(fx.bind(QStringLiteral("sdl:2"), kIdA) == BindOutcome::Bound);

    CHECK(pumpUntil([&wolf] { return wolf.padType(1).has_value(); }));
    CHECK(wolf.padType(0).has_value());
    CHECK(log.paths().isEmpty());
    CHECK(session->phase() == SessionPhase::Streaming);
}

TEST_CASE("Four pads ride one session and only the last one off cancels it",
          "[moonlight][behaviour][b13][b22]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);

    const QStringList pads = {QStringLiteral("sdl:1"), QStringLiteral("sdl:2"),
                              QStringLiteral("sdl:3"), QStringLiteral("sdl:4")};
    for (const auto& pad : pads) { REQUIRE(fx.bind(pad, kIdA) == BindOutcome::Bound); }
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 4);

    // The session came up, which is what makes the last unbind a quit.
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Streaming);
    MoonlightRequestLog log(*fx.manager);

    // Three off, one still riding: the session stays up and the host is told
    // nothing. Cancelling here would close the app under the pads that remain.
    for (int i = 0; i < 3; ++i) { fx.manager->unbindSlot(pads.at(i)); }
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 1);
    REQUIRE(session->phase() == SessionPhase::Streaming);
    REQUIRE(log.count(kCancel) == 0);

    // The last one off owns the teardown, exactly once.
    fx.manager->unbindSlot(pads.at(3));
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 0);
    REQUIRE(session->phase() == SessionPhase::Closed);
    REQUIRE(log.count(kCancel) == 1);
}

TEST_CASE("Two bindings arriving at once never start two sessions", "[moonlight][behaviour][b23]") {
    // The real convergence hazard on this client is not two threads: bindSlot is
    // main-thread by contract. It is REENTRANCY. Binding emits phaseChanged, the
    // binding flow renders off that signal, and a second pad arriving from inside
    // that handler is a second bind landing in the middle of the first one, before
    // its launch effect has run. This drives exactly that: the second bind is
    // issued from the phase signal the first one raises.
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    REQUIRE(fx.sessionFor(kIpA) != nullptr);
    MoonlightRequestLog log(*fx.manager);
    log.clear();

    int reentries = 0;
    QObject::connect(fx.manager.get(), &MoonlightManager::sessionPhaseChanged, fx.manager.get(),
                     [&](const QString& id) {
                         if (id != kIdA || reentries > 0) { return; }
                         ++reentries;
                         fx.bind(QStringLiteral("sdl:2"), kIdA);
                     });

    REQUIRE(fx.bind(QStringLiteral("sdl:1"), kIdA) == BindOutcome::Bound);
    REQUIRE(reentries == 1);

    // One session, one launch, and two pads that did not collide over a number.
    REQUIRE(fx.manager->findChildren<MoonlightSession*>().size() == 1);
    REQUIRE(log.count(kLaunch) == 1);
    REQUIRE(log.count(kResume) == 0);
    REQUIRE(fx.manager->boundSlotCount(kIdA) == 2);
    REQUIRE(fx.manager->slotForController(kIdA, 0) == QStringLiteral("sdl:1"));
    REQUIRE(fx.manager->slotForController(kIdA, 1) == QStringLiteral("sdl:2"));
}

// ── B9, B10 · what the host says about a session already running ────────────

TEST_CASE("A session the host says is ours is rejoined silently", "[moonlight][behaviour][b9]") {
    // Sunshine sets <resume>1</resume> when the running session belongs to the
    // client asking, so there is no decision to put to the user: no PIN, no
    // prompt, no state of its own. It is never shown to them at all.
    Fixture fx;
    fx.establishPairing(kIpA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Launching);
    MoonlightRequestLog log(*fx.manager);

    MoonlightSessionTestAccess::feedLaunchReply(*session, busyReply(/*resumable=*/true),
                                                /*resuming=*/false);

    // It resumed, by itself, and said nothing.
    REQUIRE(log.count(kResume) == 1);
    REQUIRE(session->phase() == SessionPhase::Launching);
    REQUIRE(session->failure() == SessionFailure::None);
    // Nothing the user has to answer is on screen.
    REQUIRE(fx.uiStateAnswered(kIdA) != SessionUiState::BusyOther);
    REQUIRE(fx.uiStateAnswered(kIdA) != SessionUiState::ResumeFailed);
}

TEST_CASE("A host busy with somebody else keeps the binding and offers to close the app",
          "[moonlight][behaviour][b10]") {
    Fixture fx;
    fx.establishPairing(kIpA);
    fx.remember(QStringLiteral("sdl:1"), kIdA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Launching);
    MoonlightRequestLog log(*fx.manager);

    MoonlightSessionTestAccess::feedLaunchReply(*session, busyReply(/*resumable=*/false),
                                                /*resuming=*/false);

    // Not a resume, and not a generic refusal: the one state whose way forward
    // is closing somebody else's app, which is the user's call and not ours.
    REQUIRE(log.count(kResume) == 0);
    REQUIRE(session->failure() == SessionFailure::AppAlreadyRunning);
    REQUIRE(fx.uiStateAnswered(kIdA) == SessionUiState::BusyOther);
    REQUIRE(dish::moonlight::sessionUiOffersQuit(fx.uiStateAnswered(kIdA)));
    // Nothing was closed on our own initiative.
    REQUIRE(log.count(kCancel) == 0);
    // The binding survives all of it, which is the whole point of a binding.
    REQUIRE(fx.hasBinding(QStringLiteral("sdl:1")));
    REQUIRE_FALSE(dish::moonlight::sessionUiBlocksApply(fx.uiStateAnswered(kIdA)));
}

TEST_CASE("A resume the host offered and then refused is its own answer",
          "[moonlight][behaviour][b10]") {
    Fixture fx;
    fx.establishPairing(kIpA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Launching);

    MoonlightSessionTestAccess::feedLaunchReply(*session, busyReply(/*resumable=*/true),
                                                /*resuming=*/true);
    REQUIRE(session->failure() == SessionFailure::ResumeRejected);
    REQUIRE(fx.uiStateAnswered(kIdA) == SessionUiState::ResumeFailed);
    REQUIRE(dish::moonlight::sessionUiOffersQuit(fx.uiStateAnswered(kIdA)));
}

// ── H3 · a refusal in the status line is still an answer ────────────────────

TEST_CASE("A launch refused in the status line is a refusal, not a host that never answered",
          "[moonlight][behaviour][h3]") {
    // Wolf answers a /launch for an app it does not know with HTTP 400 and
    // <root status_code="400"/>, where Sunshine refuses inside a 200 (the busy
    // cases above). Read as a transport failure, the host that said no rendered
    // as a host nobody could reach.
    ensureApp();
    MoonlightFakeHost wolf(QStringLiteral("0000"));
    REQUIRE(wolf.listening());
    wolf.answerWith(kLaunch, 400, QByteArrayLiteral("<root status_code=\"400\"/>"));

    // The reply as the client's own transport reads it. /launch rides TLS and
    // this host is plaintext, but both routes hand their reply to one reader.
    dish::net::MoonlightHttpClient client;
    std::optional<dish::net::MoonlightXmlResponse> reply;
    client.getHttp(QStringLiteral("127.0.0.1"), wolf.port(), kLaunch, {},
                   [&reply](const dish::net::MoonlightXmlResponse& r) { reply = r; });
    REQUIRE(pumpUntil([&reply] { return reply.has_value(); }));
    CHECK(reply->reachable);
    CHECK(reply->statusCode == 400);

    Fixture fx;
    fx.establishPairing(kIpA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Launching);
    MoonlightRequestLog log(*fx.manager);

    MoonlightSessionTestAccess::feedLaunchReply(*session, *reply, /*resuming=*/false);

    CHECK(session->failure() == SessionFailure::LaunchRejected);
    CHECK(fx.uiStateAnswered(kIdA) == SessionUiState::Refused);
    // A refused launch started nothing of ours, so there is nothing to close.
    CHECK(log.count(kCancel) == 0);
}

TEST_CASE("A refusal whose body names no status of its own takes the status line's",
          "[moonlight][behaviour][h3]") {
    ensureApp();
    MoonlightFakeHost host(QStringLiteral("0000"));
    REQUIRE(host.listening());
    host.answerWith(kLaunch, 404, QByteArrayLiteral("<html>Not Found</html>"));

    dish::net::MoonlightHttpClient client;
    std::optional<dish::net::MoonlightXmlResponse> reply;
    client.getHttp(QStringLiteral("127.0.0.1"), host.port(), kLaunch, {},
                   [&reply](const dish::net::MoonlightXmlResponse& r) { reply = r; });
    REQUIRE(pumpUntil([&reply] { return reply.has_value(); }));

    CHECK(reply->reachable);
    CHECK_FALSE(reply->ok());
    CHECK(reply->statusCode == 404);
}

TEST_CASE("A launch the host turns into a resume brings the session up",
          "[moonlight][behaviour][h3][b9]") {
    // A /launch from a client that already holds a session on Wolf is answered
    // as a resume: HTTP 200, a sessionUrl0 and <resume>1</resume>, and no
    // <gamesession>. It is the session this client already holds, coming back.
    ensureApp();
    auto settings = makeSharedSettings();
    MoonlightHostRepository repo(settings);
    const auto identity = repo.getOrCreateIdentity();
    REQUIRE(identity.has_value());
    // The RTSP handshake the accepted launch starts dials this host, which
    // answers it at once rather than leaving a worker waiting on a timeout.
    MoonlightFakeHost rtsp(QStringLiteral("0000"));
    REQUIRE(rtsp.listening());
    MoonlightHost h = host(QStringLiteral("Fake"), QStringLiteral("127.0.0.1"));
    h.httpPort = rtsp.port();
    h.httpsPort = rtsp.port();
    MoonlightSession session(h, *identity, &repo);
    MoonlightSessionTestAccess::settle(session, SessionPhase::Launching);

    const auto resumed = dish::net::parseMoonlightXml(
        QStringLiteral("<root status_code=\"200\"><sessionUrl0>rtsp://127.0.0.1:%1</sessionUrl0>"
                       "<resume>1</resume></root>")
            .arg(rtsp.port())
            .toUtf8());
    MoonlightSessionTestAccess::feedLaunchReply(session, resumed, /*resuming=*/false);

    CHECK(session.phase() == SessionPhase::RtspHandshake);
    CHECK(session.failure() == SessionFailure::None);
}

// ── B11, B12, B21 · nothing about a host may block a binding ────────────────

TEST_CASE("A binding is saved in every host state except a host already full",
          "[moonlight][behaviour][b11][b12][b21]") {
    Fixture fx;
    // Unpaired, never answered, never even discovered: the binding is an intent
    // and the session is attempted when the controller is used, not when it is
    // saved. Nothing about the host was asked before the record went down.
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.remember(QStringLiteral("sdl:1"), kIdA);
    REQUIRE(fx.hasBinding(QStringLiteral("sdl:1")));
    REQUIRE_FALSE(fx.store->hosts().first().paired);
    REQUIRE_FALSE(dish::moonlight::sessionUiBlocksApply(fx.uiState(kIdA)));

    // The failure states need a host that HAS answered, because every one of
    // them is reached by a mutual-TLS call the host let through.
    fx.establishPairing(kIpB);
    auto* session = fx.sessionFor(kIpB);
    REQUIRE(session != nullptr);

    // Every way an attempt can end, the state each one renders as, and the fact
    // that not one of them stands between the user and a saved binding.
    const std::pair<SessionFailure, SessionUiState> outcomes[] = {
        {SessionFailure::LaunchRejected, SessionUiState::Refused},
        {SessionFailure::AppAlreadyRunning, SessionUiState::BusyOther},
        {SessionFailure::ResumeRejected, SessionUiState::ResumeFailed},
        {SessionFailure::RtspFailed, SessionUiState::SetupFailed},
        {SessionFailure::ControlFailed, SessionUiState::SetupFailed},
        {SessionFailure::LinkDropped, SessionUiState::Dropped},
        {SessionFailure::ServerTerminated, SessionUiState::EndedByHost},
    };
    for (const auto& [failure, rendered] : outcomes) {
        MoonlightSessionTestAccess::settle(*session, SessionPhase::Failed, failure);
        // A real state each, not one spinner standing in for all of them.
        CHECK(fx.uiStateAnswered(kIdB) == rendered);
        CHECK_FALSE(dish::moonlight::sessionUiBlocksApply(fx.uiStateAnswered(kIdB)));
        // And the binding goes down while the host is in it.
        fx.remember(QStringLiteral("sdl:9"), kIdB);
        CHECK(fx.hasBinding(QStringLiteral("sdl:9")));
        fx.manager->forgetBinding(QStringLiteral("sdl:9"));
    }

    // The two the section answers from the trust inputs rather than from the
    // wire: a PIN the host refused and a host that could not be reached are not
    // session outcomes, and neither of them blocks a binding either.
    for (const auto failure : {SessionFailure::PairRejected, SessionFailure::Unreachable}) {
        MoonlightSessionTestAccess::settle(*session, SessionPhase::Failed, failure);
        CHECK_FALSE(dish::moonlight::sessionUiBlocksApply(fx.uiStateAnswered(kIdB)));
    }

    // The ONE refusal, and it is a protocol ceiling rather than a state that
    // resolves itself: there is no fifth controller number to hand out.
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Idle);
    for (int i = 1; i <= 4; ++i) {
        REQUIRE(fx.bind(QStringLiteral("sdl:%1").arg(i), kIdB) == BindOutcome::Bound);
    }
    REQUIRE(fx.bind(QStringLiteral("sdl:5"), kIdB) == BindOutcome::HostFull);
    REQUIRE(fx.uiStateAnswered(kIdB) == SessionUiState::HostFull);
    REQUIRE(dish::moonlight::sessionUiBlocksApply(fx.uiStateAnswered(kIdB)));
}

// ── B14, B15, B24 · the client quits only what it started ───────────────────

TEST_CASE("The last pad off a live session tells the host to close the app",
          "[moonlight][behaviour][b14]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Streaming);

    MoonlightRequestLog log(*fx.manager);
    fx.manager->unbindSlot(QStringLiteral("sdl:1"));

    REQUIRE(log.count(kCancel) == 1);
    REQUIRE(session->phase() == SessionPhase::Closed);
}

TEST_CASE("The last pad off a session that never came up tells the host nothing",
          "[moonlight][behaviour][b15][b24]") {
    // A launch still in flight has been ASKED for and not answered, and a
    // pairing has asked for nothing at all. Cancelling either is a mutual-TLS
    // call that closes whatever the person sitting at that machine happened to
    // be running, on the strength of an app we do not know exists.
    for (const auto phase :
         {SessionPhase::Pairing, SessionPhase::Paired, SessionPhase::Launching}) {
        Fixture fx;
        fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
        fx.bind(QStringLiteral("sdl:1"), kIdA);
        auto* session = fx.sessionFor(kIpA);
        REQUIRE(session != nullptr);
        MoonlightSessionTestAccess::settle(*session, phase);

        MoonlightRequestLog log(*fx.manager);
        fx.manager->unbindSlot(QStringLiteral("sdl:1"));

        CHECK(log.count(kCancel) == 0);
        CHECK(session->phase() == SessionPhase::Closed);
    }
}

TEST_CASE("A launch abandoned mid-flight is taken back down when it comes good anyway",
          "[moonlight][behaviour][b15][b24]") {
    // The other half of not cancelling a launch in flight. The reply can still
    // land after the last pad has gone, and when it does the host HAS started an
    // app on our account, with nothing left that wants it. Left running it is
    // the very thing that refuses every later attempt.
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Launching);

    MoonlightRequestLog log(*fx.manager);
    fx.manager->unbindSlot(QStringLiteral("sdl:1"));
    REQUIRE(log.count(kCancel) == 0);
    REQUIRE(session->phase() == SessionPhase::Closed);

    MoonlightSessionTestAccess::feedLaunchReply(*session, acceptedReply(), /*resuming=*/false);
    REQUIRE(log.count(kCancel) == 1);
    // And nothing was resurrected: the session stays closed.
    REQUIRE(session->phase() == SessionPhase::Closed);
}

// ── B16 · the explicit quit ─────────────────────────────────────────────────

TEST_CASE("An explicit quit closes our own session as well as the host's app",
          "[moonlight][behaviour][b16]") {
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Streaming);

    MoonlightRequestLog log(*fx.manager);
    fx.manager->cancelHostApp(kIdA);

    // The host is told, and our end comes down with it. A quit that only sent
    // the message would leave the section drawing a stream the host has just
    // been told to close, and the pads announcing onto a wire being pulled.
    REQUIRE(log.count(kCancel) == 1);
    REQUIRE(session->phase() == SessionPhase::Closed);
}

TEST_CASE("Closing an app another device left running tears down nothing of ours",
          "[moonlight][behaviour][b16]") {
    // The state that offers this has no session of ours at all: the host refused
    // us because somebody else is on it. There is nothing local to tear down, so
    // the /cancel is all that closes it.
    Fixture fx;
    fx.manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Failed,
                                       SessionFailure::AppAlreadyRunning);

    MoonlightRequestLog log(*fx.manager);
    fx.manager->cancelHostApp(kIdA);

    REQUIRE(log.count(kCancel) == 1);
    REQUIRE(session->phase() == SessionPhase::Failed);
}

TEST_CASE("A quit asks the host again, because the answer to /cancel proves nothing",
          "[moonlight][behaviour][b16][h2]") {
    // A host answers /cancel with success whether or not anything was running
    // (Wolf: HTTP 200 and <cancel>1</cancel> either way), so the reply cannot say
    // what the quit did. Asking the host again can, for our own session and for
    // an app another device left running alike.
    const std::pair<SessionPhase, SessionFailure> sessions[] = {
        {SessionPhase::Streaming, SessionFailure::None},
        {SessionPhase::Failed, SessionFailure::AppAlreadyRunning},
    };
    for (const auto& [phase, failure] : sessions) {
        Fixture fx;
        // Found rather than typed in, so no probe is already out when the quit asks.
        fx.manager->applyDiscoverySweep({host(QStringLiteral("PC"), kIpA)});
        REQUIRE(fx.bind(QStringLiteral("sdl:1"), kIdA) == BindOutcome::Bound);
        auto* session = fx.sessionFor(kIpA);
        REQUIRE(session != nullptr);
        MoonlightSessionTestAccess::settle(*session, phase, failure);
        MoonlightRequestLog log(*fx.manager);

        fx.manager->cancelHostApp(kIdA);

        CHECK(log.paths() == QStringList({kCancel, kServerInfo}));
    }
}

// ── B17, B18 · dropped and ended are never the same thing ───────────────────

TEST_CASE("A host that ends the session keeps the binding and the next use is a new session",
          "[moonlight][behaviour][b17]") {
    Fixture fx;
    fx.establishPairing(kIpA);
    fx.remember(QStringLiteral("sdl:1"), kIdA);
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Streaming);

    MoonlightRequestLog log(*fx.manager);
    MoonlightSessionTestAccess::feed(*session, SessionEvent::ServerTerminated);

    REQUIRE(session->failure() == SessionFailure::ServerTerminated);
    REQUIRE(fx.uiStateAnswered(kIdA) == SessionUiState::EndedByHost);
    // KEPT: the binding is an intent and the host ending an app does not retire
    // it. The pad keeps its number too, so nothing has to be re-applied.
    REQUIRE(fx.hasBinding(QStringLiteral("sdl:1")));
    REQUIRE(fx.manager->boundHostFor(QStringLiteral("sdl:1")) == kIdA);

    // And the next use is a NEW session: the host ended the old one, so there is
    // nothing to resume and nothing asks it to.
    log.clear();
    fx.manager->connectHost(kIdA, QString());
    REQUIRE(session->phase() == SessionPhase::Launching);
    REQUIRE(log.count(kLaunch) == 1);
    REQUIRE(log.count(kResume) == 0);
}

TEST_CASE("A link that merely dropped tells the host nothing at all",
          "[moonlight][behaviour][b18]") {
    // A control stream that closed without a termination is as likely to be a
    // Wi-Fi blip as an ending, and the host will usually hand the session back.
    // Cancelling would close somebody's game to tidy up after a blip.
    Fixture fx;
    fx.establishPairing(kIpA);
    fx.remember(QStringLiteral("sdl:1"), kIdA);
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Streaming);

    MoonlightRequestLog log(*fx.manager);
    MoonlightSessionTestAccess::feed(*session, SessionEvent::ControlDropped);

    REQUIRE(log.count(kCancel) == 0);
    REQUIRE(session->failure() == SessionFailure::LinkDropped);
    REQUIRE(fx.uiStateAnswered(kIdA) == SessionUiState::Dropped);
    REQUIRE(fx.hasBinding(QStringLiteral("sdl:1")));

    // And reconnecting asks for a session rather than only asking the host for
    // its news, which is what the button says it does.
    log.clear();
    fx.manager->connectHost(kIdA, QString());
    REQUIRE(session->phase() == SessionPhase::Launching);
    REQUIRE(log.count(kLaunch) == 1);
}

TEST_CASE("Dropped and ended come out of one phase as two different answers",
          "[moonlight][behaviour][b17][b18]") {
    // Same phase, same pad, two signals: the ONLY thing that separates them is
    // whether the host said why. Merging them would either close a game after a
    // blip or promise a reconnect that the host has already refused.
    Fixture fx;
    fx.establishPairing(kIpA);
    fx.establishPairing(kIpB);
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    fx.bind(QStringLiteral("sdl:2"), kIdB);
    auto* dropped = fx.sessionFor(kIpA);
    auto* ended = fx.sessionFor(kIpB);
    REQUIRE(dropped != nullptr);
    REQUIRE(ended != nullptr);
    MoonlightSessionTestAccess::settle(*dropped, SessionPhase::Streaming);
    MoonlightSessionTestAccess::settle(*ended, SessionPhase::Streaming);

    MoonlightRequestLog log(*fx.manager);
    MoonlightSessionTestAccess::feed(*dropped, SessionEvent::ControlDropped);
    MoonlightSessionTestAccess::feed(*ended, SessionEvent::ServerTerminated);

    REQUIRE(fx.uiStateAnswered(kIdA) == SessionUiState::Dropped);
    REQUIRE(fx.uiStateAnswered(kIdB) == SessionUiState::EndedByHost);
    REQUIRE(fx.uiStateAnswered(kIdA) != fx.uiStateAnswered(kIdB));
    // NEITHER speaks to the host. A drop is left alone because the host will let
    // us resume it; an end is the host's own doing, so nothing of ours is
    // running to quit. The difference is what the next use does: Reconnect
    // resumes, Start session opens a new one.
    REQUIRE(log.count(kCancel) == 0);
}

// ── B19 · a launch that succeeded and then could not be finished ────────────

TEST_CASE("A stream that would not come up takes the app it started back down",
          "[moonlight][behaviour][b19]") {
    // The launch SUCCEEDED, so the host is running an app because we asked. The
    // stream then failing does not change that, and the copy this state renders
    // tells the user we closed it again.
    for (const auto step : {SessionPhase::RtspHandshake, SessionPhase::ControlConnecting}) {
        Fixture fx;
        fx.establishPairing(kIpA);
        fx.bind(QStringLiteral("sdl:1"), kIdA);
        auto* session = fx.sessionFor(kIpA);
        REQUIRE(session != nullptr);
        MoonlightSessionTestAccess::settle(*session, step);

        MoonlightRequestLog log(*fx.manager);
        MoonlightSessionTestAccess::feed(*session, step == SessionPhase::RtspHandshake
                                                       ? SessionEvent::RtspFailed
                                                       : SessionEvent::ControlConnectFailed);

        CHECK(log.count(kCancel) == 1);
        CHECK(fx.uiStateAnswered(kIdA) == SessionUiState::SetupFailed);
        // And it is still only an intent that failed: the binding stays and the
        // next attempt is not blocked.
        CHECK_FALSE(dish::moonlight::sessionUiBlocksApply(fx.uiStateAnswered(kIdA)));
    }
}

// ── B20 · forget with a session up ──────────────────────────────────────────

TEST_CASE("Forgetting a host with a live session closes it before the credentials go",
          "[moonlight][behaviour][b20]") {
    // Afterwards there is nothing left to authenticate a quit with: the record
    // and the pinned certificate are the whole of this client's half. So the
    // /cancel has to go out while they are still on file, and the witness below
    // records what the store held at the exact moment the request was issued
    // rather than what it holds once the dust settles.
    Fixture fx;
    fx.establishPairing(kIpA);
    fx.remember(QStringLiteral("sdl:1"), kIdA);
    fx.bind(QStringLiteral("sdl:1"), kIdA);
    auto* session = fx.sessionFor(kIpA);
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Streaming);
    REQUIRE(fx.store->serverCert(kIdA).has_value());

    // Two facts, read at the instant each request goes out. The /cancel has to
    // leave while the credentials are still on file, because they are what
    // authenticates it, AND after the store has been let go, because the
    // handshake it opens runs the pin verifier on a later turn of the loop and
    // would write the certificate back over the forget.
    MoonlightRequestLog log(*fx.manager, [&fx, session] {
        return QStringLiteral("%1 %2").arg(
            fx.store->serverCert(kIdA).has_value() ? QStringLiteral("credentials")
                                                   : QStringLiteral("none"),
            MoonlightSessionTestAccess::persists(*session) ? QStringLiteral("attached")
                                                           : QStringLiteral("detached"));
    });

    QPointer<MoonlightSession> alive(session);
    fx.manager->forgetHost(kIdA);

    // It spoke, and it spoke while it still could, with nothing left that could
    // write back what the forget is about to remove.
    REQUIRE(log.count(kCancel) == 1);
    // AND IT IS STILL THERE TO HEAR THE ANSWER. The /cancel goes out on a client
    // that is a child of the session, and a request whose client dies is
    // aborted, so a session deleted on the next turn of the loop is a quit that
    // never reached the host. A TEST-NET host never answers; the session waits.
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    REQUIRE(alive);
    REQUIRE(session->cancelInFlight());
    REQUIRE(log.witnessed() == QStringList{QStringLiteral("credentials detached")});
    // And nothing this session does afterwards can reach the store, which is what
    // stops the /cancel handshake pinning the certificate back over the forget.
    REQUIRE_FALSE(dish::net::MoonlightSessionTestAccess::persists(*session));
    REQUIRE_FALSE(fx.store->serverCert(kIdA).has_value());
    REQUIRE(fx.store->hosts().isEmpty());
    REQUIRE_FALSE(fx.hasBinding(QStringLiteral("sdl:1")));
}
