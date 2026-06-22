// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"
#include "core/reducer/RestOutcome.h"

#include <QString>

#include <variant>

namespace dish::net {

// Blocking protocol-1 pairing over the satellite's HTTPS client server (:9443,
// self-signed cert, verification not enforced). POST /api/pair has three modes
// (contract §Pairing): Path A (operator `pin`), Path B (client-shown
// `clientPin` → pending, then poll GET /api/pair/status), and key rotation
// (`hmacProof` of the current key → fresh key). DELETE /api/pair self-unpairs.
// All carry `protocolVersion`. Mirrors dish-android core/net/SatelliteHttpClient
// pair/pairStatus/unpair. The blocking calls drive a nested QEventLoop around
// the async QNetworkAccessManager so the public API stays synchronous (called
// from a QtConcurrent worker, never the UI thread).
class PairingClient {
  public:
    // Classification of a PairResponse — the manager fans the variant out to an
    // error toast, a PIN dialog, a poll, or the openSession path. The arms map
    // 1:1 onto reducer::PairVerdict (the pure rule); the success arm carries the
    // shared key directly. VersionMismatch (409) is terminal "client too old".
    struct Success {
        QString sharedKeyHex;
    };
    struct Pending {};         // Path B accepted — poll /api/pair/status
    struct AuthRequired {};    // reachable, no key — first-time pair / forgot us
    struct VersionMismatch {}; // 409 — protocol skew
    struct Unreachable {
        QString message;
    };
    using Outcome = std::variant<Success, Pending, AuthRequired, VersionMismatch, Unreachable>;

    // Pure classifier — driven only by fields on the response (status + ok +
    // pending + sharedKey) so it is trivially unit-testable. Delegates the rule
    // to reducer::classifyPair.
    static Outcome classify(const models::PairResponse& response);

    // Path A / Path B (clientPin non-empty) pairing POST. `protocolVersion`
    // rides in the body; both pin and clientPin are sent (empty when unused),
    // matching the android shape — the server uses a valid `pin` first.
    static models::PairResponse pair(const QString& ip, int port, const QString& deviceId,
                                     const QString& deviceName, const QString& pin,
                                     const QString& clientPin = QString());

    // Key rotation / re-pair: POST /api/pair with `hmacProof` of the CURRENT
    // key → a fresh sharedKey. A failed proof falls through to the PIN paths
    // server-side (so this behaves like a fresh attempt then).
    static models::PairResponse rotateKey(const QString& ip, int port, const QString& deviceId,
                                          const QString& deviceName, const QString& hmacProof);

    // Path-B poll: GET /api/pair/status?deviceId=... → approved/pending/denied.
    static models::PairResponse pairStatus(const QString& ip, int port, const QString& deviceId);
};

} // namespace dish::net
