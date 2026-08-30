// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The CLIENT side of Moonlight's five-phase PIN pairing. It is the mirror of the
// server logic documented in http-pairing.adoc and implemented in the MIT Wolf
// reference (moonlight.cpp / endpoints.hpp); see THIRD_PARTY.md.
//
// The class is a pure state machine: given the client identity, the PIN, and the
// three random inputs (salt, client challenge, client secret), each phaseN...()
// produces the query parameters for that HTTP call and consumeN...() folds in the
// response. Injecting the randomness makes the whole exchange deterministic and
// unit-testable with a fixed vector; the network layer supplies real random
// bytes in production.
//
// AES-128-ECB blobs, SHA-256 challenge hashes and RSA-2048 signatures are all
// per the protocol. No GPL Moonlight source was consulted.

#pragma once

#include "core/moonlight/MoonlightIdentity.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dish::moonlight {

// Why a phase failed, so the UI can localize a cause rather than a raw string.
enum class PairError {
    None,
    BadServerCert,   // plaincert did not decode / parse
    BadResponse,     // a phase response was malformed or the wrong length
    ServerSignature, // the server's pairing signature did not verify
    CryptoFailure,   // an OpenSSL operation failed
};

class PairingClient {
  public:
    // `salt`, `clientChallenge` and `clientSecret` must each be 16 bytes. In
    // production they are fresh random bytes; tests pass fixed vectors.
    PairingClient(Identity identity, std::string pin, std::vector<std::uint8_t> salt,
                  std::vector<std::uint8_t> clientChallenge,
                  std::vector<std::uint8_t> clientSecret);

    bool valid() const { return valid_; }
    PairError error() const { return error_; }

    // Phase 1 query params. The salt and the hex-encoded client certificate.
    std::string saltHex() const;
    std::string clientCertHex() const;

    // Consume phase 1's `plaincert` (hex of the server cert PEM). Extracts the
    // server cert's signature and public key for the later phases. Returns false
    // and sets error() on a decode/parse failure.
    bool consumeServerCert(const std::string& plaincertHex);

    // Phase 2: the AES-ECB-encrypted client challenge, hex. Empty on failure.
    std::string clientChallengeHex();

    // Consume phase 2's `challengeresponse`. Recovers the server challenge.
    bool consumeChallengeResponse(const std::string& challengeResponseHex);

    // Phase 3: the AES-ECB-encrypted client hash, hex. Empty on failure.
    std::string serverChallengeRespHex();

    // Consume phase 3's `pairingsecret` and verify the server's signature over
    // its secret. Returns false (ServerSignature / BadResponse) on any mismatch.
    bool consumePairingSecret(const std::string& pairingSecretHex);

    // Phase 4: the client's own secret + RSA signature over it, hex. Empty on
    // failure.
    std::string clientPairingSecretHex();

    // Phase 5 (HTTPS) query value. Constant, present for symmetry.
    static std::string pairChallengePhrase() { return "pairchallenge"; }

    // The server certificate PEM, valid after consumeServerCert. The caller pins
    // it and presents nothing back, but persists it for later TLS verification.
    const std::string& serverCertPem() const { return serverCertPem_; }

  private:
    void fail(PairError e);

    Identity identity_;
    std::string pin_;
    std::vector<std::uint8_t> salt_;
    std::vector<std::uint8_t> clientChallenge_;
    std::vector<std::uint8_t> clientSecret_;

    std::array<std::uint8_t, 16> aesKey_{};
    std::string serverCertPem_;
    std::vector<std::uint8_t> serverCertSignature_;
    std::string serverPublicKeyPem_;
    std::vector<std::uint8_t> clientCertSignature_;

    std::vector<std::uint8_t> serverChallenge_; // recovered in phase 2
    std::vector<std::uint8_t> clientHash_;      // computed in phase 3

    bool valid_ = false;
    PairError error_ = PairError::None;
};

} // namespace dish::moonlight
