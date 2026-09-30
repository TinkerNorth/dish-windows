// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The audio a launch and a resume ask the host for, read off the requests the
// session actually sends (MoonlightRequestLog). The host record is TEST-NET-1
// (RFC 5737), so each request is made and goes nowhere.

#include "Network/MoonlightSession.h"
#include "core/moonlight/MoonlightIdentity.h"
#include "core/moonlight/MoonlightSessionMachine.h"

#include "MoonlightRequestLog.h"
#include "MoonlightSessionTestAccess.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QUrlQuery>

using dish::models::MoonlightHost;
using dish::moonlight::SessionPhase;
using dish::net::MoonlightSession;
using dish::net::MoonlightSessionTestAccess;
using dish::test::MoonlightRequestLog;

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

// Stereo, in the form both hosts parse: the channel mask (front left and front
// right, 0x3) in the high 16 bits over the channel count (2) in the low 16.
// Wolf reads the count as `value & 0xffff`.
const QString kStereo = QStringLiteral("196610");

MoonlightHost unroutableHost() {
    MoonlightHost h;
    h.name = QStringLiteral("Study PC");
    h.ip = QStringLiteral("192.0.2.41");
    return h;
}

dish::moonlight::Identity clientIdentity() {
    const auto identity = dish::moonlight::generateIdentity();
    REQUIRE(identity.has_value());
    return *identity;
}

QString surroundAudioInfoOf(const QString& query) {
    return QUrlQuery(query).queryItemValue(QStringLiteral("surroundAudioInfo"));
}

// How a host holding this client's own session answers a launch: HTTP 200 with
// the refusal in the body, and a <resume> that hands the session back.
dish::net::MoonlightXmlResponse ourSessionIsRunning() {
    return dish::net::parseMoonlightXml(QByteArrayLiteral(
        "<root status_code=\"400\" status_message=\"An app is already running on this host\">"
        "<resume>1</resume></root>"));
}

} // namespace

TEST_CASE("A launch asks the host for stereo", "[moonlight][launch]") {
    ensureApp();
    MoonlightSession session(unroutableHost(), clientIdentity(), nullptr);
    MoonlightRequestLog log(session);

    session.launch(QStringLiteral("1"));

    REQUIRE(log.paths() == QStringList{QStringLiteral("/launch")});
    REQUIRE(surroundAudioInfoOf(log.query(0)) == kStereo);
}

TEST_CASE("A resume asks the host for stereo", "[moonlight][launch]") {
    ensureApp();
    MoonlightSession session(unroutableHost(), clientIdentity(), nullptr);
    MoonlightSessionTestAccess::settle(session, SessionPhase::Launching);
    MoonlightRequestLog log(session);

    MoonlightSessionTestAccess::feedLaunchReply(session, ourSessionIsRunning(),
                                                /*resuming=*/false);

    REQUIRE(log.paths() == QStringList{QStringLiteral("/resume")});
    REQUIRE(surroundAudioInfoOf(log.query(0)) == kStereo);
}
