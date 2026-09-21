// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/DpapiSecretCipher.h"

#include <QLoggingCategory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <wincrypt.h>

#include <cstring>

namespace dish::source {

namespace {

Q_LOGGING_CATEGORY(lcDishSecrets, "dish.secrets")

// Bound to the blob as optional entropy. See the header for what it is not.
constexpr const char kEntropyLabel[] = "Dish pairing secrets";
constexpr const wchar_t* kDescription = L"Dish pairing secret";

DATA_BLOB entropyBlob() {
    DATA_BLOB entropy;
    // The label's bytes without the terminator; const_cast because the Win32
    // struct is not const-correct and the API only reads through it.
    entropy.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(kEntropyLabel));
    entropy.cbData = static_cast<DWORD>(std::strlen(kEntropyLabel));
    return entropy;
}

// Copies the API's output into a QByteArray and frees the LocalAlloc buffer
// on every path, so a throw between the call and the copy cannot leak it.
struct LocalBlob {
    DATA_BLOB blob{};
    ~LocalBlob() {
        if (blob.pbData != nullptr) { LocalFree(blob.pbData); }
    }
    QByteArray take() const {
        return QByteArray(reinterpret_cast<const char*>(blob.pbData),
                          static_cast<qsizetype>(blob.cbData));
    }
};

} // namespace

std::optional<QByteArray> DpapiSecretCipher::protect(const QByteArray& plain) const {
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB entropy = entropyBlob();
    LocalBlob out;
    if (CryptProtectData(&in, kDescription, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                         &out.blob) == FALSE) {
        qCWarning(lcDishSecrets) << "CryptProtectData failed:" << GetLastError();
        return std::nullopt;
    }
    return out.take();
}

std::optional<QByteArray> DpapiSecretCipher::unprotect(const QByteArray& blob) const {
    if (blob.isEmpty()) { return std::nullopt; }
    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()));
    in.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB entropy = entropyBlob();
    LocalBlob out;
    if (CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                           &out.blob) == FALSE) {
        // Another account's blob, a tampered value, or a machine whose master
        // key is gone: the secret is unreadable and the caller re-pairs.
        qCWarning(lcDishSecrets) << "CryptUnprotectData failed:" << GetLastError();
        return std::nullopt;
    }
    return out.take();
}

} // namespace dish::source
