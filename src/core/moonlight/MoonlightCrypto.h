// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Moonlight cryptographic primitives, built on OpenSSL's EVP layer.
//
// The Satellite protocol-1 path uses libsodium (ChaCha20-Poly1305, HKDF,
// HMAC-SHA256) in core/wire/SessionCrypto and is untouched. The Moonlight
// (GameStream) path needs primitives libsodium does not offer — AES-128-ECB,
// AES-128-GCM with a 16-byte IV, RSA-2048 sign/verify, and X.509 generation — so
// this module links OpenSSL, exactly as the MIT-licensed Wolf reference does.
// The algorithm choices and the control-stream IV construction are adapted from
// Wolf (src/moonlight-protocol/crypto); see THIRD_PARTY.md.
//
// Everything here is deterministic given its inputs and unit-tested against the
// byte-exact vectors published in Wolf's docs and testControl.cpp.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dish::moonlight::crypto {

inline constexpr std::size_t kAesBlock = 16;
inline constexpr std::size_t kGcmTagSize = 16;
inline constexpr std::size_t kAesKey128 = 16;
inline constexpr std::size_t kSha256Len = 32;

using Bytes = std::vector<std::uint8_t>;

// SHA-256 of `data`, 32 raw bytes.
std::array<std::uint8_t, kSha256Len> sha256(const std::uint8_t* data, std::size_t len);
std::array<std::uint8_t, kSha256Len> sha256(const Bytes& data);

// The pairing AES key: first 16 bytes of SHA-256(salt || pin). `salt` is the raw
// salt bytes the client generated; `pin` is the ASCII PIN string.
std::array<std::uint8_t, kAesKey128> genAesKey(const std::uint8_t* salt, std::size_t saltLen,
                                               const std::string& pin);

// AES-128-ECB, no padding. `data` length must be a multiple of 16. Returns
// nullopt on a bad length or an OpenSSL failure.
std::optional<Bytes> aesEcbEncrypt(const std::array<std::uint8_t, kAesKey128>& key,
                                   const Bytes& data);
std::optional<Bytes> aesEcbDecrypt(const std::array<std::uint8_t, kAesKey128>& key,
                                   const Bytes& data);

// ── Control-stream AEAD (AES-128-GCM) ────────────────────────────────────────
// Wolf's control packets use a 16-byte IV whose only non-zero byte is the low
// byte of the sequence number. This is faithfully reproduced so the wire format
// interoperates and the published vectors pass.

// Seal a plaintext control payload into a full on-the-wire ENCRYPTED packet:
//   [0x0001 LE][len LE][seq LE u32][GCM tag 16][ciphertext]
// `key` is the 16 raw bytes decoded from the launch rikey hex. Returns nullopt
// on OpenSSL failure.
std::optional<Bytes> sealControl(const std::array<std::uint8_t, kAesKey128>& key, std::uint32_t seq,
                                 const std::uint8_t* plaintext, std::size_t len);

// Open a full ENCRYPTED packet back to its plaintext, verifying the GCM tag.
// Returns nullopt on a malformed packet, a wrong type, or a tag mismatch
// (tamper). The sequence number is read from the packet, not supplied.
std::optional<Bytes> openControl(const std::array<std::uint8_t, kAesKey128>& key,
                                 const std::uint8_t* packet, std::size_t len);

// The hot-path sealer: one OpenSSL cipher context and one output buffer, reused
// across every packet, so a per-report send does zero heap allocation and never
// rebuilds the AES key schedule. Not thread-safe: one sealer per sending thread.
// It produces the SAME bytes as sealControl (verified in the test suite).
class ControlSealer {
  public:
    explicit ControlSealer(const std::array<std::uint8_t, kAesKey128>& key);
    ~ControlSealer();

    ControlSealer(const ControlSealer&) = delete;
    ControlSealer& operator=(const ControlSealer&) = delete;
    ControlSealer(ControlSealer&&) = delete;
    ControlSealer& operator=(ControlSealer&&) = delete;

    // Seal `plaintext` under `seq` into the internal buffer. Returns a pointer to
    // the full on-the-wire ENCRYPTED packet and writes its length to `outLen`, or
    // nullptr on failure. The pointer is valid until the next seal() call.
    const std::uint8_t* seal(std::uint32_t seq, const std::uint8_t* plaintext, std::size_t len,
                             std::size_t* outLen) noexcept;

    bool ok() const noexcept { return ctx_ != nullptr; }

  private:
    void* ctx_ = nullptr; // EVP_CIPHER_CTX*, opaque to keep OpenSSL out of the header
    std::vector<std::uint8_t> buffer_;
};

// ── RSA-2048 sign / verify (PKCS#1 v1.5, SHA-256) ────────────────────────────
// `privateKeyPem` / `publicKeyPem` are PEM strings. Signatures are 256 bytes.
std::optional<Bytes> rsaSign(const std::string& privateKeyPem, const std::uint8_t* msg,
                             std::size_t len);
bool rsaVerify(const std::string& publicKeyPem, const std::uint8_t* msg, std::size_t msgLen,
               const std::uint8_t* sig, std::size_t sigLen);

// Cryptographically-secure random bytes (OpenSSL RAND_bytes).
Bytes randomBytes(std::size_t n);

// ── Hex helpers matching Moonlight's on-the-wire hex encoding ────────────────
// Uppercase, no separators. hexDecode is lenient about case and returns nullopt
// on an odd length or a non-hex character.
std::string hexEncode(const std::uint8_t* data, std::size_t len);
std::string hexEncode(const Bytes& data);
std::optional<Bytes> hexDecode(const std::string& hex);

} // namespace dish::moonlight::crypto
