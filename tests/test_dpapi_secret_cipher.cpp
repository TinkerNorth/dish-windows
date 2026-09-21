// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The real Data Protection API, on the account the tests run as: the round
// trip is byte-exact, the blob is not the plaintext, and a tampered or
// foreign blob reads as absent rather than as garbage.

#include "source/system/DpapiSecretCipher.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>

using dish::source::DpapiSecretCipher;

TEST_CASE("DPAPI: protect and unprotect round-trip byte for byte", "[secrets][dpapi]") {
    const DpapiSecretCipher cipher;
    const QByteArray plain = QByteArrayLiteral("0123456789abcdef0123456789abcdef");
    const auto blob = cipher.protect(plain);
    REQUIRE(blob.has_value());
    REQUIRE_FALSE(blob->isEmpty());
    REQUIRE_FALSE(blob->contains(plain));
    const auto back = cipher.unprotect(*blob);
    REQUIRE(back.has_value());
    REQUIRE(*back == plain);
}

TEST_CASE("DPAPI: an empty secret still round-trips", "[secrets][dpapi]") {
    const DpapiSecretCipher cipher;
    const auto blob = cipher.protect(QByteArray());
    REQUIRE(blob.has_value());
    const auto back = cipher.unprotect(*blob);
    REQUIRE(back.has_value());
    REQUIRE(back->isEmpty());
}

TEST_CASE("DPAPI: two protects of one secret differ, both open", "[secrets][dpapi]") {
    // A fresh salt per call: the store never reveals that two satellites share
    // a key by storing the same bytes twice.
    const DpapiSecretCipher cipher;
    const QByteArray plain = QByteArrayLiteral("same-secret");
    const auto a = cipher.protect(plain);
    const auto b = cipher.protect(plain);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(*a != *b);
    REQUIRE(cipher.unprotect(*a) == plain);
    REQUIRE(cipher.unprotect(*b) == plain);
}

TEST_CASE("DPAPI: a tampered or foreign blob is absent, never garbage", "[secrets][dpapi]") {
    const DpapiSecretCipher cipher;
    auto blob = cipher.protect(QByteArrayLiteral("secret"));
    REQUIRE(blob.has_value());
    QByteArray tampered = *blob;
    tampered[tampered.size() / 2] = static_cast<char>(tampered[tampered.size() / 2] ^ 0x5A);
    REQUIRE_FALSE(cipher.unprotect(tampered).has_value());
    REQUIRE_FALSE(cipher.unprotect(QByteArrayLiteral("not a blob")).has_value());
    REQUIRE_FALSE(cipher.unprotect(QByteArray()).has_value());
}
