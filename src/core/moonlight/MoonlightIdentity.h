// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The client's Moonlight identity: a self-signed RSA-2048 X.509 certificate and
// its private key, generated once and persisted alongside the existing Satellite
// identity storage. The certificate is presented on every HTTPS call to a paired
// host, and its DER signature feeds the pairing challenge hashes.
//
// OpenSSL X.509 handling adapted from the MIT-licensed Wolf reference
// (src/moonlight-protocol/crypto/src/x509.cpp); see THIRD_PARTY.md.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dish::moonlight {

// An identity as PEM text plus the cert's DER bytes. The PEM strings are what we
// persist and what we hand to the TLS stack; the DER is only needed transiently.
struct Identity {
    std::string certPem;       // the self-signed X.509 certificate, PEM
    std::string privateKeyPem; // its RSA-2048 private key, PEM
};

// Generate a fresh self-signed RSA-2048 identity (CN "dish", 20-year validity,
// SHA-256 self-signature). nullopt only on an OpenSSL failure.
std::optional<Identity> generateIdentity();

// The certificate's ASN.1 signature bytes (X509_get0_signature). Both pairing
// ends hash the SAME cert's signature, so this must be extracted identically on
// each; it is the raw signature BIT STRING, not the whole cert. nullopt if the
// PEM does not parse.
std::optional<std::vector<std::uint8_t>> certSignature(const std::string& certPem);

// The certificate's public key as a PEM SubjectPublicKeyInfo string, for RSA
// verification of the peer's pairing signature. nullopt on a parse failure.
std::optional<std::string> certPublicKeyPem(const std::string& certPem);

} // namespace dish::moonlight
