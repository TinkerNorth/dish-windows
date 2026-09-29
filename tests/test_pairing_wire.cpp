// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The pairing exchange as HTTPClient sends it, answered by the satellite's REST
// API in-process (FakeSatelliteRest.h). test_pairing_outcome.cpp pins what a
// reply MEANS; this pins what the client SENDS on each of the two pairing routes,
// and how each route's reply is read.

#include "Models/Models.h"
#include "Network/HTTPClient.h"
#include "Network/PairingOutcome.h"
#include "core/model/Protocol.h"

#include "FakeSatelliteRest.h"
#include "InstalledCatalog.h"

#include <catch2/catch_test_macros.hpp>

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

// An HTTPClient whose every request the in-process satellite answers.
struct PairingWire {
    FakeSatelliteRest* rest = nullptr; // owned by the client
    std::unique_ptr<HTTPClient> client;

    PairingWire() {
        dish::test::ensureApp();
        rest = new FakeSatelliteRest;
        client = std::make_unique<HTTPClient>(rest, nullptr);
    }

    // The one pair most cases send: device dev-1, named Den PC, with the operator's PIN.
    std::optional<PairResponse> pairDen() {
        return await([this](HTTPClient::PairCb cb) {
            client->pair(kSatelliteIp, kPairPort, QStringLiteral("dev-1"), QStringLiteral("Den PC"),
                         QStringLiteral("1234"), QString(), std::move(cb));
        });
    }

    std::optional<PairResponse> pollStatus(const QString& deviceId) {
        return await([this, &deviceId](HTTPClient::PairCb cb) {
            client->pairStatus(kSatelliteIp, kPairPort, deviceId, std::move(cb));
        });
    }

    QJsonObject sentBody() const {
        return QJsonDocument::fromJson(rest->requests().front().body).object();
    }

    // Sends one call and turns the event loop until its callback lands.
    static std::optional<PairResponse> await(const std::function<void(HTTPClient::PairCb)>& send) {
        std::optional<PairResponse> got;
        send([&got](const PairResponse& response) { got = response; });
        const QDeadlineTimer deadline(kReplyTimeoutMs);
        while (!got.has_value() && !deadline.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return got;
    }
};

template <typename Arm> bool classifiesAs(const PairResponse& response) {
    return std::holds_alternative<Arm>(PairingOutcome::classify(response));
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
    CHECK(reply->httpStatus == 409);
    CHECK(classifiesAs<PairingOutcome::VersionMismatch>(*reply));
}

TEST_CASE("pairing wire: a pair nobody answers reads as unreachable", "[pairing][wire]") {
    PairingWire wire;

    const auto reply = wire.pairDen();

    REQUIRE(reply.has_value());
    CHECK_FALSE(reply->reachable);
    CHECK(reply->httpStatus == 0);
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
    CHECK(reply->reachable);
    CHECK(reply->httpStatus == 200);
    CHECK(reply->status == QStringLiteral("approved"));
    CHECK(reply->sharedKey == QStringLiteral("0a0b"));
}
