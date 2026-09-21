// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A reversible stand-in for the platform cipher, so a repository test can see
// the stored shape (the value on disk is not the secret) and drive the two
// failure paths (a blob the platform will not open, a protect that fails)
// without touching DPAPI or the developer's own profile.

#pragma once

#include "source/system/SecretCipher.h"

#include <QByteArray>

#include <optional>

namespace dish::test {

class FakeSecretCipher : public dish::source::SecretCipher {
  public:
    static constexpr const char* kMarker = "FAKE|";

    std::optional<QByteArray> protect(const QByteArray& plain) const override {
        ++protects;
        if (failProtect) { return std::nullopt; }
        QByteArray out(kMarker);
        // Reversed, so a stored value never contains the plaintext as a
        // substring and a test that greps for the secret in the store fails
        // for the right reason.
        for (qsizetype i = plain.size() - 1; i >= 0; --i) { out.append(plain.at(i)); }
        return out;
    }

    std::optional<QByteArray> unprotect(const QByteArray& blob) const override {
        ++unprotects;
        if (failUnprotect || !blob.startsWith(kMarker)) { return std::nullopt; }
        const QByteArray body = blob.mid(static_cast<qsizetype>(qstrlen(kMarker)));
        QByteArray out;
        for (qsizetype i = body.size() - 1; i >= 0; --i) { out.append(body.at(i)); }
        return out;
    }

    bool failProtect = false;
    bool failUnprotect = false;
    mutable int protects = 0;
    mutable int unprotects = 0;
};

} // namespace dish::test
