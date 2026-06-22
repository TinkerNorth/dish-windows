// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SatelliteTlsVerifier — the TOFU cert-pinning gate the HTTPS client runs after
// the TLS handshake. The satellite presents a self-signed cert (no CA chain),
// so trust is Trust-On-First-Use: pin the SHA-256 of the cert first seen for a
// satellite id, then reject any later cert whose fingerprint differs.
//
// This is the verifier dish-android's SatelliteHttpClientVerifierTest pins. It
// composes the pure verdict ladder (core/net/Tofu) with the durable pin store
// (repository/SatellitePinRepository): on first contact it pins + accepts; the
// same cert later matches without re-pinning; a different cert is rejected and
// the trusted pin is LEFT INTACT (anti-MITM); a connection with no peer cert is
// rejected but NOT counted as a mismatch. Pins are keyed per satellite id.
//
// HTTPClient (src/Network/HTTPClient) calls verifyPeerCertificate from its
// post-handshake hook; the verifier itself takes no Qt/TLS types so it unit-
// tests against a fake cert byte buffer.

#pragma once

#include "repository/SatellitePinRepository.h"

#include <QByteArray>
#include <QString>

#include <functional>

namespace dish::http {

// Decide whether to trust the peer cert `certDer` presented for `satelliteId`,
// consulting + updating `pins`. `certDer` empty models "no peer certificate".
//   * no cert            -> false, onMismatch NOT called
//   * never pinned       -> pin(satelliteId, fp), return true (first use)
//   * fingerprint match  -> true (do not re-pin)
//   * fingerprint differ -> false, pin LEFT INTACT, onMismatch() fired once
// `onMismatch` is optional (default no-op). Mirrors android's tofuHostnameVerifier.
bool verifyPeerCertificate(const QString& satelliteId, repository::SatellitePinRepository& pins,
                           const QByteArray& certDer, const std::function<void()>& onMismatch = {});

} // namespace dish::http
