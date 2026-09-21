// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The seam between a repository that stores a secret and the platform that
// protects it at rest. Abstract so the repositories can be pinned with a
// reversible fake and the stored shape inspected; DpapiSecretCipher is the
// Windows implementation. The contract is byte-exact round trip
// (unprotect(protect(x)) == x) and a nullopt, never garbage, for a blob the
// platform will not or cannot open.

#pragma once

#include <QByteArray>

#include <optional>

namespace dish::source {

class SecretCipher {
  public:
    virtual ~SecretCipher() = default;

    virtual std::optional<QByteArray> protect(const QByteArray& plain) const = 0;
    virtual std::optional<QByteArray> unprotect(const QByteArray& blob) const = 0;
};

} // namespace dish::source
