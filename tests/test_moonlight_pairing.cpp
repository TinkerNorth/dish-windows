// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pairing crypto round-trip. The test plays the SERVER side (mirroring the Wolf
// reference algorithm) against our PairingClient, so both directions of the
// five-phase exchange are exercised with deterministic inputs.

#include "core/moonlight/MoonlightCrypto.h"
#include "core/moonlight/MoonlightIdentity.h"
#include "core/moonlight/MoonlightPairing.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

namespace c = dish::moonlight::crypto;
using dish::moonlight::Identity;
using dish::moonlight::PairingClient;

namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> v) { return {v}; }

std::vector<std::uint8_t> fill(std::uint8_t b) { return std::vector<std::uint8_t>(16, b); }

std::array<std::uint8_t, 16> asKey(const std::vector<std::uint8_t>& salt, const std::string& pin) {
    return c::genAesKey(salt.data(), salt.size(), pin);
}

// A minimal server side, following the Wolf endpoints/moonlight.cpp logic, so we
// can drive the client through every phase deterministically.
struct FakeServer {
    Identity id;
    std::array<std::uint8_t, 16> aesKey{};
    std::vector<std::uint8_t> serverSecret;
    std::vector<std::uint8_t> serverChallenge;
    std::vector<std::uint8_t> clientHash;    // recovered in phase 3
    std::vector<std::uint8_t> clientCertSig; // from the received client cert
    bool paired = false;

    FakeServer(const std::vector<std::uint8_t>& salt, const std::string& pin,
               std::vector<std::uint8_t> secret, std::vector<std::uint8_t> challenge)
        : serverSecret(std::move(secret)), serverChallenge(std::move(challenge)) {
        id = *dish::moonlight::generateIdentity();
        aesKey = asKey(salt, pin);
    }

    std::string plaincertHex() const {
        return c::hexEncode(c::Bytes(id.certPem.begin(), id.certPem.end()));
    }

    // Phase 2: decrypt the client challenge, build challengeresponse.
    std::string challengeResponse(const std::string& clientChallengeHex) {
        const auto blob = *c::hexDecode(clientChallengeHex);
        const auto clientChallenge = *c::aesEcbDecrypt(aesKey, blob);
        const auto serverSig = *dish::moonlight::certSignature(id.certPem);
        c::Bytes hashInput = clientChallenge;
        hashInput.insert(hashInput.end(), serverSig.begin(), serverSig.end());
        hashInput.insert(hashInput.end(), serverSecret.begin(), serverSecret.end());
        const auto hash = c::sha256(hashInput);
        c::Bytes pt(hash.begin(), hash.end());
        pt.insert(pt.end(), serverChallenge.begin(), serverChallenge.end());
        return c::hexEncode(*c::aesEcbEncrypt(aesKey, pt));
    }

    // Phase 3: recover the client hash, return pairingsecret.
    std::string pairingSecret(const std::string& serverChallengeRespHex) {
        const auto blob = *c::hexDecode(serverChallengeRespHex);
        clientHash = *c::aesEcbDecrypt(aesKey, blob);
        const auto sig = *c::rsaSign(id.privateKeyPem, serverSecret.data(), serverSecret.size());
        c::Bytes out = serverSecret;
        out.insert(out.end(), sig.begin(), sig.end());
        return c::hexEncode(out);
    }

    // Phase 4: verify the client's secret + signature.
    void verifyClient(const std::string& clientPairingSecretHex, const std::string& clientCertPem) {
        const auto blob = *c::hexDecode(clientPairingSecretHex);
        const c::Bytes clientSecret(blob.begin(), blob.begin() + 16);
        const c::Bytes clientSig(blob.begin() + 16, blob.end());
        clientCertSig = *dish::moonlight::certSignature(clientCertPem);

        c::Bytes hashInput = serverChallenge;
        hashInput.insert(hashInput.end(), clientCertSig.begin(), clientCertSig.end());
        hashInput.insert(hashInput.end(), clientSecret.begin(), clientSecret.end());
        const auto hash = c::sha256(hashInput);
        const c::Bytes hashV(hash.begin(), hash.end());
        if (hashV != clientHash) {
            paired = false;
            return;
        }
        const auto pub = *dish::moonlight::certPublicKeyPem(clientCertPem);
        paired = c::rsaVerify(pub, clientSecret.data(), clientSecret.size(), clientSig.data(),
                              clientSig.size());
    }
};

} // namespace

TEST_CASE("Full pairing round-trip succeeds with the right PIN", "[moonlight][pairing]") {
    const auto salt = bytes({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16});
    const std::string pin = "1234";
    const auto clientChallenge = fill(0xAA);
    const auto clientSecret = fill(0xBB);

    const auto clientId = *dish::moonlight::generateIdentity();
    PairingClient client(clientId, pin, salt, clientChallenge, clientSecret);
    REQUIRE(client.valid());

    FakeServer server(salt, pin, fill(0xCC), fill(0xDD));

    // Phase 1
    REQUIRE(client.saltHex() == c::hexEncode(salt));
    REQUIRE(client.consumeServerCert(server.plaincertHex()));

    // Phase 2
    const std::string cc = client.clientChallengeHex();
    REQUIRE_FALSE(cc.empty());
    const std::string chalResp = server.challengeResponse(cc);
    REQUIRE(client.consumeChallengeResponse(chalResp));

    // Phase 3
    const std::string scr = client.serverChallengeRespHex();
    REQUIRE_FALSE(scr.empty());
    const std::string pairingSecret = server.pairingSecret(scr);
    REQUIRE(client.consumePairingSecret(pairingSecret));

    // Phase 4
    const std::string cps = client.clientPairingSecretHex();
    REQUIRE_FALSE(cps.empty());
    server.verifyClient(cps, clientId.certPem);
    REQUIRE(server.paired);
    REQUIRE(client.valid());
    REQUIRE(client.error() == dish::moonlight::PairError::None);
}

TEST_CASE("A wrong PIN fails at the server hash check", "[moonlight][pairing]") {
    const auto salt = fill(0x11);
    const auto clientChallenge = fill(0xAA);
    const auto clientSecret = fill(0xBB);
    const auto clientId = *dish::moonlight::generateIdentity();

    // Client uses "1234", server uses "9999": the AES keys diverge.
    PairingClient client(clientId, "1234", salt, clientChallenge, clientSecret);
    FakeServer server(salt, "9999", fill(0xCC), fill(0xDD));

    REQUIRE(client.consumeServerCert(server.plaincertHex()));
    const std::string chalResp = server.challengeResponse(client.clientChallengeHex());
    // The client still decodes SOMETHING (garbage), so it does not hard-fail here.
    client.consumeChallengeResponse(chalResp);
    const std::string pairingSecret = server.pairingSecret(client.serverChallengeRespHex());
    // The server's own signature still verifies (its real key), so phase 3 passes.
    client.consumePairingSecret(pairingSecret);
    server.verifyClient(client.clientPairingSecretHex(), clientId.certPem);

    // The server catches the mismatch: the derived keys never agreed.
    REQUIRE_FALSE(server.paired);
}

TEST_CASE("consumePairingSecret rejects a tampered server signature", "[moonlight][pairing]") {
    const auto salt = fill(0x22);
    const auto clientId = *dish::moonlight::generateIdentity();
    PairingClient client(clientId, "4321", salt, fill(0xAA), fill(0xBB));
    FakeServer server(salt, "4321", fill(0xCC), fill(0xDD));

    REQUIRE(client.consumeServerCert(server.plaincertHex()));
    REQUIRE(client.consumeChallengeResponse(server.challengeResponse(client.clientChallengeHex())));

    std::string pairingSecret = server.pairingSecret(client.serverChallengeRespHex());
    // Corrupt the last hex nibble of the signature.
    pairingSecret.back() = (pairingSecret.back() == 'A') ? 'B' : 'A';
    REQUIRE_FALSE(client.consumePairingSecret(pairingSecret));
    REQUIRE(client.error() == dish::moonlight::PairError::ServerSignature);
}

TEST_CASE("PairingClient rejects malformed inputs", "[moonlight][pairing]") {
    const auto clientId = *dish::moonlight::generateIdentity();

    // Wrong-length randomness invalidates the client up front.
    PairingClient bad(clientId, "1234", fill(0xAA), std::vector<std::uint8_t>(4, 0), fill(0xBB));
    REQUIRE_FALSE(bad.valid());

    PairingClient client(clientId, "1234", fill(0x01), fill(0xAA), fill(0xBB));
    REQUIRE_FALSE(client.consumeServerCert("nothex!!"));
    REQUIRE(client.error() == dish::moonlight::PairError::BadServerCert);
}
