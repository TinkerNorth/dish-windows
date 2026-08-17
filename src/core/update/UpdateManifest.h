// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// latest.json (schema 1) parsing and validation — the ONLY reader of update
// metadata. Every rule from spec section 16.1 is enforced here, once, so the
// reducer and coordinator downstream may trust a parsed manifest completely.
// Any violation of a required field or of the dish-setup.exe asset is a typed
// ManifestError; the caller treats every error as a failed check. The zip
// asset is deliberately ignored: the updater never consumes it.

#pragma once

#include "core/update/UpdateVersion.h"

#include <QByteArray>
#include <QString>

#include <variant>

namespace dish::update {

// The permalink the app fetches (GitHub's Latest pointer; no api.github.com)
// and the only prefix a manifest asset URL may carry. sha256 makes the CDN
// host the 302 lands on irrelevant to integrity, so only the entry host pins.
inline constexpr const char* kLatestManifestUrl =
    "https://github.com/TinkerNorth/dish-windows/releases/latest/download/latest.json";
inline constexpr const char* kAssetUrlPrefix =
    "https://github.com/TinkerNorth/dish-windows/releases/download/";
inline constexpr const char* kSetupAssetName = "dish-setup.exe";

// Caps: the body is size-checked BEFORE the JSON parse (a captive portal's
// HTML splash can be arbitrarily large), and no plausible installer is half a
// gigabyte.
inline constexpr qint64 kManifestMaxBytes = 64 * 1024;
inline constexpr qint64 kAssetMaxBytes = qint64(500) * 1024 * 1024;

enum class ManifestError {
    Oversize,          // body > 64 KiB before parsing
    BadJson,           // unparseable, or the root is not an object (portal HTML)
    UnsupportedSchema, // schema != 1 (greater = newer client required)
    WrongProduct,      // product != "dish-windows"
    WrongChannel,      // channel != "stable"
    BadVersion,        // version not strict M.m.p
    BadMinimum,        // minimumSupportedVersion malformed or > version
    MissingSetupAsset, // no assets["dish-setup.exe"] object
    BadAssetUrl,       // http://, lookalike host, or wrong path prefix
    BadSha,            // not 64 lowercase hex
    BadSize,           // outside (0, 500 MB)
};

struct UpdateAsset {
    QString url;
    QString sha256; // 64 lowercase hex
    qint64 size = 0;

    bool operator==(const UpdateAsset& o) const {
        return url == o.url && sha256 == o.sha256 && size == o.size;
    }
    bool operator!=(const UpdateAsset& o) const { return !(*this == o); }
};

struct UpdateManifest {
    int schema = 1;
    QString product;
    QString version;
    QString channel;
    QString publishedAt; // display only, NEVER ordering
    QString minimumSupportedVersion;
    QString releaseNotesUrl; // "" when absent or dropped (non-https / non-github.com)
    UpdateAsset setupAsset;  // the dish-setup.exe entry (required)

    bool operator==(const UpdateManifest& o) const {
        return schema == o.schema && product == o.product && version == o.version &&
               channel == o.channel && publishedAt == o.publishedAt &&
               minimumSupportedVersion == o.minimumSupportedVersion &&
               releaseNotesUrl == o.releaseNotesUrl && setupAsset == o.setupAsset;
    }
    bool operator!=(const UpdateManifest& o) const { return !(*this == o); }

    // Unknown extra fields are ignored (additive-only policy for schema 1).
    static std::variant<UpdateManifest, ManifestError> parse(const QByteArray& body);
};

} // namespace dish::update
