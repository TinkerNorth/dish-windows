// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The stored shape of a protected setting: "dpapi:" plus the cipher's blob in
// base64, so a value can be told from the plaintext an older build wrote
// without guessing at its bytes. The prefix names the mechanism rather than
// saying "protected" so a future cipher can carry its own.

#pragma once

#include "source/system/SecretCipher.h"

#include <QByteArray>
#include <QLatin1String>
#include <QString>

#include <optional>

namespace dish::repository::secret {

inline constexpr const char* kWrappedPrefix = "dpapi:";

inline bool isWrapped(const QString& stored) {
    return stored.startsWith(QLatin1String(kWrappedPrefix));
}

// The value to store for a plaintext secret, or nullopt when the platform
// refused to protect it.
inline std::optional<QString> wrap(const source::SecretCipher& cipher, const QString& plain) {
    const auto blob = cipher.protect(plain.toUtf8());
    if (!blob.has_value()) { return std::nullopt; }
    return QLatin1String(kWrappedPrefix) + QString::fromLatin1(blob->toBase64());
}

// The plaintext behind a stored value: a wrapped value through the cipher, a
// legacy plaintext value as it is. Nullopt only for a wrapped value the
// platform will not open, which the caller treats as absent.
inline std::optional<QString> unwrap(const source::SecretCipher& cipher, const QString& stored) {
    if (!isWrapped(stored)) { return stored; }
    const QByteArray blob = QByteArray::fromBase64(
        stored.mid(static_cast<qsizetype>(QLatin1String(kWrappedPrefix).size())).toLatin1());
    const auto plain = cipher.unprotect(blob);
    if (!plain.has_value()) { return std::nullopt; }
    return QString::fromUtf8(*plain);
}

} // namespace dish::repository::secret
