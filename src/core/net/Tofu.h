// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Tofu — Trust-On-First-Use cert-pinning primitives, pure and Qt-free.
//
// The satellite presents a self-signed TLS cert, so there is no CA chain to
// validate. Instead the dish pins the SHA-256 fingerprint of the cert it first
// saw for a given satellite id and, on every later connection, refuses any cert
// whose fingerprint differs (anti-MITM). The verdict ladder + the fingerprint
// hash are the only logic here; the storage (which id maps to which pin) lives
// in repository/SatellitePinRepository, and the TLS-callback that calls both is
// in source/http. Mirrors dish-android repository/SatellitePinRepository.kt's
// tofuVerdict + sha256FingerprintHex top-level functions.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace dish::net {

enum class TofuVerdict {
    // No pin stored for this id yet — accept and pin (first contact).
    TrustFirstUse,
    // The presented fingerprint equals the stored one (case-insensitive hex).
    Match,
    // A pin exists and the presented fingerprint differs — reject, keep the pin.
    Mismatch,
};

// Decide the verdict for a presented fingerprint against the stored pin.
//   stored == nullopt  -> TrustFirstUse  (only "never pinned" trusts blindly)
//   equalsIgnoreCase    -> Match
//   otherwise           -> Mismatch
// An *empty-string* stored pin is a present, non-matching pin: it can Mismatch.
// Only std::nullopt means never-pinned. Matches dish-android's `stored == null`
// vs `stored.equals(presented, ignoreCase = true)`.
TofuVerdict tofuVerdict(const std::optional<std::string>& stored, const std::string& presented);

// Lowercase 64-char hex SHA-256 of `bytes` (the cert's DER encoding). Known
// vectors: ""  -> e3b0c442…b855, "abc" -> ba7816bf…015ad.
std::string sha256FingerprintHex(const std::uint8_t* bytes, std::size_t len);

} // namespace dish::net
