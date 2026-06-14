// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure, Qt-free classifiers that turn an HTTP status + the protocol-relevant
// body fields into a decision the session FSM acts on. Free functions only so
// the rules unit-test without sockets or Qt (the android pattern). These encode
// the contract's error model (§Error model, §hmacProof) once, in one place.

#pragma once

#include <cstdint>
#include <string>

namespace dish::reducer {

// What a REST exchange means to the caller, independent of which route it was.
enum class RestVerdict {
    Ok,              // 2xx with the fields we need
    Unauthorized,    // 401 NOT_PAIRED|BAD_PROOF — TERMINAL: drop key, re-pair
    VersionMismatch, // 409 — TERMINAL: client/server protocol skew
    ShuttingDown,    // 503 — retryable later
    Unreachable,     // transport failure / empty body (status 0) — retryable
    ServerError,     // any other non-2xx with a body — usually retryable
};

inline bool restVerdictTerminal(RestVerdict v) {
    return v == RestVerdict::Unauthorized || v == RestVerdict::VersionMismatch;
}

inline bool restVerdictRetryable(RestVerdict v) {
    return v == RestVerdict::Unreachable || v == RestVerdict::ShuttingDown ||
           v == RestVerdict::ServerError;
}

// The decoded shape every REST reply carries through this layer: the HTTP
// status, whether the body parsed at all, and the optional `code` (401 cause).
// A status of 0 means the transport never produced a response (our gateway's
// synthesised-failure sentinel, matching android's HttpReply(status=0)).
struct RestReply {
    int status = 0;
    bool bodyParsed = false;
    std::string code; // NOT_PAIRED | BAD_PROOF on a 401, else empty
};

// Classify a generic authenticated REST reply (PUT/GET/DELETE session/
// controller). `code` is consulted on 401 so a NOT_PAIRED and a BAD_PROOF both
// surface as Unauthorized (both terminal — contract §hmacProof).
inline RestVerdict classifyRest(const RestReply& r) {
    if (r.status == 0 || !r.bodyParsed) { return RestVerdict::Unreachable; }
    if (r.status >= 200 && r.status <= 299) { return RestVerdict::Ok; }
    if (r.status == 401) { return RestVerdict::Unauthorized; }
    if (r.status == 409) { return RestVerdict::VersionMismatch; }
    if (r.status == 503) { return RestVerdict::ShuttingDown; }
    return RestVerdict::ServerError;
}

// ── Pairing classification ──────────────────────────────────────────────────
// POST /api/pair always answers 200 on the PIN paths; the dish classifies on
// the body's ok/pending fields, not the HTTP status. A 409 is a protocol skew.

enum class PairVerdict {
    Success,         // ok && sharedKey present — adopt the key, open the session
    Pending,         // Path B: {ok:false, pending:true} — poll /api/pair/status
    AuthRequired,    // reachable but no key (first-time pair / forgot us) — PIN
    VersionMismatch, // 409 — client/server protocol skew
    Unreachable,     // transport failure / empty body
};

// The protocol-relevant fields of a parsed POST /api/pair reply.
struct PairReply {
    int status = 0;
    bool bodyParsed = false;
    bool ok = false;
    bool pending = false;
    bool hasSharedKey = false; // sharedKey present AND non-empty
};

inline PairVerdict classifyPair(const PairReply& r) {
    if (r.status == 409) { return PairVerdict::VersionMismatch; }
    if (r.status == 0 || !r.bodyParsed) { return PairVerdict::Unreachable; }
    if (r.ok && r.hasSharedKey) { return PairVerdict::Success; }
    if (r.pending) { return PairVerdict::Pending; }
    // Reachable, parsed, but no usable key: a server that says ok=true with no
    // key falls here too (caching an empty key would break every reconnect).
    return PairVerdict::AuthRequired;
}

// ── Path-B approval-status classification (GET /api/pair/status) ─────────────
// {"ok":true,"status":"approved","sharedKey":...} exactly once, else
// {"ok":false,"status":"pending"|"denied"|"none"}.
enum class ApprovalVerdict { Approved, Pending, Declined, Unreachable };

struct ApprovalReply {
    int status = 0;
    bool bodyParsed = false;
    std::string statusStr; // "approved"|"pending"|"denied"|"none"
    bool hasSharedKey = false;
};

inline ApprovalVerdict classifyApproval(const ApprovalReply& r) {
    if (r.status == 0 || !r.bodyParsed) { return ApprovalVerdict::Unreachable; }
    if (r.statusStr == "approved" && r.hasSharedKey) { return ApprovalVerdict::Approved; }
    if (r.statusStr == "denied") { return ApprovalVerdict::Declined; }
    // "pending" / "none" / anything transient → keep waiting.
    return ApprovalVerdict::Pending;
}

} // namespace dish::reducer
