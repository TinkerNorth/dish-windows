// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "core/moonlight/MoonlightPairing.h"

#include "core/moonlight/MoonlightCrypto.h"

#include <array>
#include <cstring>
#include <utility>

namespace dish::moonlight {

namespace {
namespace c = crypto;

// RSA-2048 / SHA-256 signatures are always 256 bytes.
constexpr std::size_t kRsaSigLen = 256;

c::Bytes toBytes(const std::string& s) {
    return c::Bytes(reinterpret_cast<const std::uint8_t*>(s.data()),
                    reinterpret_cast<const std::uint8_t*>(s.data()) + s.size());
}

std::string toStr(const c::Bytes& b) {
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

} // namespace

PairingClient::PairingClient(Identity identity, std::string pin, std::vector<std::uint8_t> salt,
                             std::vector<std::uint8_t> clientChallenge,
                             std::vector<std::uint8_t> clientSecret)
    : identity_(std::move(identity)), pin_(std::move(pin)), salt_(std::move(salt)),
      clientChallenge_(std::move(clientChallenge)), clientSecret_(std::move(clientSecret)) {
    if (salt_.size() != 16 || clientChallenge_.size() != 16 || clientSecret_.size() != 16) {
        fail(PairError::CryptoFailure);
        return;
    }
    aesKey_ = c::genAesKey(salt_.data(), salt_.size(), pin_);
    // The client's own cert signature feeds the phase-3 hash; extract it once.
    const auto sig = certSignature(identity_.certPem);
    if (!sig) {
        fail(PairError::CryptoFailure);
        return;
    }
    clientCertSignature_ = *sig;
    valid_ = true;
}

void PairingClient::fail(PairError e) {
    valid_ = false;
    error_ = e;
}

std::string PairingClient::saltHex() const { return c::hexEncode(salt_); }

std::string PairingClient::clientCertHex() const {
    // The PEM text, hex-encoded, exactly as the reference server decodes it.
    return c::hexEncode(toBytes(identity_.certPem));
}

bool PairingClient::consumeServerCert(const std::string& plaincertHex) {
    const auto decoded = c::hexDecode(plaincertHex);
    if (!decoded) {
        fail(PairError::BadServerCert);
        return false;
    }
    serverCertPem_ = toStr(*decoded);
    const auto sig = certSignature(serverCertPem_);
    const auto pub = certPublicKeyPem(serverCertPem_);
    if (!sig || !pub) {
        fail(PairError::BadServerCert);
        return false;
    }
    serverCertSignature_ = *sig;
    serverPublicKeyPem_ = *pub;
    return true;
}

std::string PairingClient::clientChallengeHex() {
    const auto enc = c::aesEcbEncrypt(aesKey_, clientChallenge_);
    if (!enc) {
        fail(PairError::CryptoFailure);
        return {};
    }
    return c::hexEncode(*enc);
}

bool PairingClient::consumeChallengeResponse(const std::string& challengeResponseHex) {
    const auto blob = c::hexDecode(challengeResponseHex);
    if (!blob) {
        fail(PairError::BadResponse);
        return false;
    }
    const auto dec = c::aesEcbDecrypt(aesKey_, *blob);
    // hash(32) + server_challenge(16) = 48 bytes.
    if (!dec || dec->size() < 48) {
        fail(PairError::BadResponse);
        return false;
    }
    serverChallenge_.assign(dec->begin() + 32, dec->begin() + 48);
    return true;
}

std::string PairingClient::serverChallengeRespHex() {
    if (serverChallenge_.size() != 16) {
        fail(PairError::BadResponse);
        return {};
    }
    // client_hash = SHA256(server_challenge + client_cert_signature + client_secret).
    c::Bytes hashInput;
    hashInput.insert(hashInput.end(), serverChallenge_.begin(), serverChallenge_.end());
    hashInput.insert(hashInput.end(), clientCertSignature_.begin(), clientCertSignature_.end());
    hashInput.insert(hashInput.end(), clientSecret_.begin(), clientSecret_.end());
    const auto hash = c::sha256(hashInput);
    clientHash_.assign(hash.begin(), hash.end());
    const auto enc = c::aesEcbEncrypt(aesKey_, clientHash_);
    if (!enc) {
        fail(PairError::CryptoFailure);
        return {};
    }
    return c::hexEncode(*enc);
}

bool PairingClient::consumePairingSecret(const std::string& pairingSecretHex) {
    const auto blob = c::hexDecode(pairingSecretHex);
    // server_secret(16) + server_signature(256).
    if (!blob || blob->size() < 16 + kRsaSigLen) {
        fail(PairError::BadResponse);
        return false;
    }
    const std::uint8_t* serverSecret = blob->data();
    const std::uint8_t* serverSig = blob->data() + 16;
    if (!c::rsaVerify(serverPublicKeyPem_, serverSecret, 16, serverSig, kRsaSigLen)) {
        fail(PairError::ServerSignature);
        return false;
    }
    return true;
}

std::string PairingClient::clientPairingSecretHex() {
    const auto sig =
        c::rsaSign(identity_.privateKeyPem, clientSecret_.data(), clientSecret_.size());
    if (!sig || sig->size() != kRsaSigLen) {
        fail(PairError::CryptoFailure);
        return {};
    }
    c::Bytes out;
    out.insert(out.end(), clientSecret_.begin(), clientSecret_.end());
    out.insert(out.end(), sig->begin(), sig->end());
    return c::hexEncode(out);
}

} // namespace dish::moonlight
