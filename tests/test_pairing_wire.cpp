// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The pairing exchange as HTTPClient sends it, answered by the satellite's REST
// API in-process (FakeSatelliteRest.h). test_pairing_outcome.cpp pins what a
// reply MEANS; this pins what the client SENDS on each of the two pairing routes,
// how each route's reply is read, and what reaches the caller when the pin
// verifier refuses the handshake.

#include "Models/Models.h"
#include "Network/HTTPClient.h"
#include "Network/PairingOutcome.h"
#include "core/model/Protocol.h"

#include "FakeSatelliteRest.h"
#include "InstalledCatalog.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

using dish::models::PairResponse;
using dish::net::HTTPClient;
using dish::net::PairingOutcome;
using dish::test::CannedAnswer;
using dish::test::FakeSatelliteRest;

namespace {

const QString kSatelliteIp = QStringLiteral("10.0.0.5");
const QString kPairPath = QStringLiteral("/api/pair");
const QString kStatusPath = QStringLiteral("/api/pair/status");
constexpr int kPairPort = 9443;
// Bounds the event-loop turns a canned reply needs; nothing waits on a clock.
constexpr int kReplyTimeoutMs = 5000;

// One reply, as the callback delivered it.
struct Delivered {
    PairResponse response;
    bool pinMismatch = false;
};

// Accepts the certificate, as for the satellite pinned on first contact.
bool accepts(const QString&, const QByteArray&, bool&) { return true; }

// Refuses a certificate other than the pinned one, and says it changed.
bool refusesAsChanged(const QString&, const QByteArray&, bool& pinMismatch) {
    pinMismatch = true;
    return false;
}

// Refuses without calling it a change, as for a peer that presented no certificate at all.
bool refusesUnflagged(const QString&, const QByteArray&, bool&) { return false; }

// An HTTPClient whose every request the in-process satellite answers.
struct PairingWire {
    FakeSatelliteRest* rest = nullptr; // owned by the client
    std::unique_ptr<HTTPClient> client;

    PairingWire() {
        dish::test::ensureApp();
        rest = new FakeSatelliteRest;
        client = std::make_unique<HTTPClient>(rest, nullptr);
    }

    // Every reply passes the TLS edge first, where `verifier` decides.
    void verifyWith(const HTTPClient::PinVerifier& verifier) {
        rest->handshakeEveryReply();
        client->setPinVerifier(verifier);
    }

    // The one pair most cases send: device dev-1, named Den PC, with the operator's PIN.
    std::optional<Delivered> pairDen() {
        return await([this](HTTPClient::PairCb cb) {
            client->pair(kSatelliteIp, kPairPort, QStringLiteral("dev-1"), QStringLiteral("Den PC"),
                         QStringLiteral("1234"), QString(), std::move(cb));
        });
    }

    std::optional<Delivered> pollStatus(const QString& deviceId) {
        return await([this, &deviceId](HTTPClient::PairCb cb) {
            client->pairStatus(kSatelliteIp, kPairPort, deviceId, std::move(cb));
        });
    }

    QJsonObject sentBody() const {
        return QJsonDocument::fromJson(rest->requests().front().body).object();
    }

    // Sends one call and turns the event loop until its callback lands.
    static std::optional<Delivered> await(const std::function<void(HTTPClient::PairCb)>& send) {
        std::optional<Delivered> got;
        send([&got](const PairResponse& response, bool pinMismatch) {
            got = Delivered{response, pinMismatch};
        });
        const QDeadlineTimer deadline(kReplyTimeoutMs);
        while (!got.has_value() && !deadline.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return got;
    }
};

template <typename Arm> bool classifiesAs(const Delivered& reply) {
    return std::holds_alternative<Arm>(PairingOutcome::classify(reply.response, reply.pinMismatch));
}

} // namespace

TEST_CASE("pairing wire: a pair posts the device, both pins, and the protocol version",
          "[pairing][wire]") {
    PairingWire wire;
    wire.rest->answer("POST", kPairPath,
                      CannedAnswer{200, R"({"ok":true,"sharedKey":"00112233"})"});

    const auto reply = wire.pairDen();

    REQUIRE(reply.has_value());
    REQUIRE(wire.rest->requests().size() == 1);
    const auto& sent = wire.rest->requests().front();
    CHECK(sent.verb == "POST");
    CHECK(sent.host == kSatelliteIp);
    CHECK(sent.path == kPairPath);
    const QJsonObject body = wire.sentBody();
    CHECK(body.value(QStringLiteral("deviceId")).toString() == QStringLiteral("dev-1"));
    CHECK(body.value(QStringLiteral("deviceName")).toString() == QStringLiteral("Den PC"));
    CHECK(body.value(QStringLiteral("pin")).toString() == QStringLiteral("1234"));
    CHECK(body.value(QStringLiteral("protocolVersion")).toInt() == dish::proto::kProtocolVersion);
    // Both pins always ride in the body, empty when unused: the server tries a
    // valid operator pin first and only then the displayed one.
    CHECK(body.contains(QStringLiteral("clientPin")));
    CHECK(body.value(QStringLiteral("clientPin")).toString().isEmpty());
    CHECK_FALSE(reply->pinMismatch);
    CHECK(classifiesAs<PairingOutcome::Success>(*reply));
}

TEST_CASE("pairing wire: the transport status is stamped onto the parsed reply",
          "[pairing][wire]") {
    // The body cannot carry the HTTP status, and classify reads it: a 409 is a
    // protocol skew only because the status says so.
    PairingWire wire;
    wire.rest->answer("POST", kPairPath, CannedAnswer{409, R"({"ok":false,"error":"protocol"})"});

    const auto reply = wire.pairDen();

    REQUIRE(reply.has_value());
    CHECK(reply->response.httpStatus == 409);
    CHECK(classifiesAs<PairingOutcome::VersionMismatch>(*reply));
}

TEST_CASE("pairing wire: a pair nobody answers reads as unreachable", "[pairing][wire]") {
    PairingWire wire;

    const auto reply = wire.pairDen();

    REQUIRE(reply.has_value());
    CHECK_FALSE(reply->response.reachable);
    CHECK(reply->response.httpStatus == 0);
    CHECK_FALSE(reply->pinMismatch);
    CHECK(classifiesAs<PairingOutcome::Unreachable>(*reply));
}

TEST_CASE("pairing wire: the status poll percent-encodes the device id", "[pairing][wire]") {
    PairingWire wire;
    wire.rest->answer("GET", kStatusPath, CannedAnswer{200, R"({"status":"pending"})"});
    const QString awkwardId = QStringLiteral("a b&c=d");

    const auto reply = wire.pollStatus(awkwardId);

    REQUIRE(reply.has_value());
    REQUIRE(wire.rest->requests().size() == 1);
    const auto& sent = wire.rest->requests().front();
    CHECK(sent.verb == "GET");
    CHECK(sent.path == kStatusPath);
    // One parameter, carrying the whole id: an unencoded `&` would have split it
    // into two, and an unencoded `=` would have cut the value short.
    CHECK(sent.query.queryItems(QUrl::FullyDecoded).size() == 1);
    CHECK(sent.query.queryItemValue(QStringLiteral("deviceId"), QUrl::FullyDecoded) == awkwardId);
}

TEST_CASE("pairing wire: the status poll's reply is read as an approval, not as a pair",
          "[pairing][wire]") {
    // The two routes answer in different shapes: a pair says ok/pending, a poll
    // says a status word. Read with the pair's parser, an approval would carry
    // no status at all and the operator's answer would never be seen.
    PairingWire wire;
    wire.rest->answer("GET", kStatusPath,
                      CannedAnswer{200, R"({"status":"approved","sharedKey":"0a0b"})"});

    const auto reply = wire.pollStatus(QStringLiteral("dev-1"));

    REQUIRE(reply.has_value());
    CHECK(reply->response.reachable);
    CHECK(reply->response.httpStatus == 200);
    CHECK(reply->response.status == QStringLiteral("approved"));
    CHECK(reply->response.sharedKey == QStringLiteral("0a0b"));
}

TEST_CASE("pairing wire: an accepted certificate lets the satellite's answer through",
          "[pairing][wire][identity]") {
    PairingWire wire;
    wire.verifyWith(&accepts);
    wire.rest->answer("POST", kPairPath,
                      CannedAnswer{200, R"({"ok":true,"sharedKey":"00112233"})"});

    const auto reply = wire.pairDen();

    REQUIRE(reply.has_value());
    CHECK_FALSE(reply->pinMismatch);
    CHECK(classifiesAs<PairingOutcome::Success>(*reply));
}

TEST_CASE("pairing wire: a changed certificate reaches the pair's caller as a mismatch",
          "[pairing][wire][identity]") {
    // The satellite would have granted a key; the handshake ends first, so no answer arrives.
    PairingWire wire;
    wire.verifyWith(&refusesAsChanged);
    wire.rest->answer("POST", kPairPath,
                      CannedAnswer{200, R"({"ok":true,"sharedKey":"00112233"})"});

    const auto reply = wire.pairDen();

    REQUIRE(reply.has_value());
    CHECK_FALSE(reply->response.reachable);
    // A changed certificate, not a dead link: the flag is what tells them apart.
    CHECK(reply->pinMismatch);
    CHECK(classifiesAs<PairingOutcome::IdentityChanged>(*reply));
}

TEST_CASE("pairing wire: a refusal that flags no change reads as unreachable",
          "[pairing][wire][identity]") {
    // The verifier alone decides whether a refusal is an identity change. One
    // that refuses without saying so must not be promoted into one here, or a
    // dead link would tell the user to forget and pair again.
    PairingWire wire;
    wire.verifyWith(&refusesUnflagged);
    wire.rest->answer("POST", kPairPath,
                      CannedAnswer{200, R"({"ok":true,"sharedKey":"00112233"})"});

    const auto reply = wire.pairDen();

    REQUIRE(reply.has_value());
    CHECK_FALSE(reply->response.reachable);
    CHECK_FALSE(reply->pinMismatch);
    CHECK(classifiesAs<PairingOutcome::Unreachable>(*reply));
}

TEST_CASE("pairing wire: the status poll carries a changed certificate too",
          "[pairing][wire][identity]") {
    PairingWire wire;
    wire.verifyWith(&refusesAsChanged);
    wire.rest->answer("GET", kStatusPath,
                      CannedAnswer{200, R"({"status":"approved","sharedKey":"0a0b"})"});

    const auto reply = wire.pollStatus(QStringLiteral("dev-1"));

    REQUIRE(reply.has_value());
    CHECK_FALSE(reply->response.reachable);
    CHECK(reply->pinMismatch);
}
