// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Secrets at rest through the Data Protection API, scoped to the Windows
// account that wrote them (CryptProtectData without CRYPTPROTECT_LOCAL_MACHINE).
// The same reach the other clients give a pairing key: dish-linux's keyring,
// dish-android's Keystore, dish-mac's Keychain all open for the user who owns
// them and nobody else. A process running as that user can undo it, which is
// the same truth for every one of those stores and why SECURITY.md keeps
// saying that code execution as you is not defended against.
//
// The optional entropy is a fixed application label, not a secret: it keeps
// another program's CryptUnprotectData call from opening a Dish blob by
// accident, and it is what makes a blob written by one install readable by
// the next version of the same app. Change it and every stored secret reads
// as absent.
//
// CRYPTPROTECT_UI_FORBIDDEN because this runs from the session manager with
// no window to parent a prompt on, and a prompt would hang a reconnect.

#pragma once

#include "source/system/SecretCipher.h"

namespace dish::source {

class DpapiSecretCipher final : public SecretCipher {
  public:
    std::optional<QByteArray> protect(const QByteArray& plain) const override;
    std::optional<QByteArray> unprotect(const QByteArray& blob) const override;
};

} // namespace dish::source
