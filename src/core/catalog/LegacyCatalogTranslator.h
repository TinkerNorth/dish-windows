// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// LegacyCatalogTranslator — owns every catalog-version→canonical mapping so
// the rest of the app stays version-agnostic. Port of dish-android
// repository/LegacyCatalogTranslator. A current-or-newer catalog passes
// through untouched; a legacy one (older schema, or the field absent → parsed
// as 1) is REPLACED with this client's own known representation of that
// version, per the contract's "a client MAY substitute its own known
// representation for a recognized legacy version". The legacy body's
// controllerTypes are NOT trusted — only its version is read; locale, server
// version, host features and the transport metadata (etag/status/reachable)
// pass through. Applied at the repository boundary (200 fill AND stale-serve)
// so the cache and every caller see the normalized shape — a pure, instant
// substitution, never a loader.
//
// Windows deviation from the android port: the substituted rows carry
// canonical display strings. Android renders legacy rows from ViewModel-owned
// bundled labels, but the windows projection (isTypeOfferable/offerableTypes)
// drops nameless rows, so a nameless substitution would blank the picker for
// a legacy satellite. The slugs are `known`, so the UI still swaps in bundled
// translations over these English fallbacks. Qt types throughout because the
// CatalogDto vocabulary forces them (core/catalog rule).

#pragma once

#include "Models/Models.h"
#include "core/catalog/BundledCatalog.h"
#include "core/model/Protocol.h"

#include <QHash>
#include <QString>
#include <QStringList>

namespace dish::catalog {

// The catalog SCHEMA version this client is written against (contract v2: up
// to four types per backend + per-type emulates). Bump ONLY together with a
// new legacy*() representation of the version being left behind.
inline constexpr int kCatalogVersionCurrent = 2;

// v1 is the sole legacy schema: xbox360 + ds4 (ds4 touchpad in "ds4" mode),
// no emulates / dualsense / switchpro.
inline constexpr int kCatalogVersionLegacyV1 = 1;

namespace detail {

// Only controllerTypes are hardcoded; their capability sets reuse
// BundledCatalog rather than re-listing feature slugs here.
inline QHash<QString, models::CatalogFeatureDto> legacyFeatures(const QString& slug) {
    QHash<QString, models::CatalogFeatureDto> out;
    const auto slugs = typeFeatureSlugs(slug);
    if (!slugs) { return out; }
    for (const auto& featureSlug : *slugs) {
        models::CatalogFeatureDto feature;
        feature.supported = true;
        if (featureSlug == kFeatureTouchpad) {
            // Touchpad is the DS4 pad mode: the resolver gates it on the
            // "ds4" mode slug, so the substitution must advertise it.
            const auto ds4 = proto::touchpadModeName(proto::kTouchpadModeDs4);
            feature.modes =
                QStringList{QString::fromUtf8(ds4.data(), static_cast<qsizetype>(ds4.size()))};
        }
        out.insert(featureSlug, feature);
    }
    return out;
}

inline models::CatalogTypeDto legacyType(int id, const QString& slug, const QString& name,
                                         const QString& shortName) {
    models::CatalogTypeDto type;
    type.id = id;
    type.slug = slug;
    type.name = name;
    type.shortName = shortName;
    type.features = legacyFeatures(slug);
    return type;
}

inline models::CatalogDto legacyV1(const models::CatalogDto& fetched) {
    models::CatalogDto out = fetched;
    out.catalogVersion = kCatalogVersionLegacyV1;
    out.controllerTypes = {
        legacyType(proto::kControllerTypeXbox, kSlugXbox360, QStringLiteral("Xbox 360 Controller"),
                   QStringLiteral("Xbox")),
        legacyType(proto::kControllerTypePlayStation, kSlugDs4, QStringLiteral("DualShock 4"),
                   QStringLiteral("PlayStation")),
    };
    return out;
}

} // namespace detail

// The version dispatch. Deliberately >= (not per-version cases): a
// NEWER-than-current catalog is additive within protocolVersion 1, so it must
// pass through for forward-compat — substitution is only for versions old
// enough that this client knows them better than the wire body. Idempotent: a
// substituted v1 re-normalizes to the same value, so re-applying on
// stale-serve is safe by construction.
inline models::CatalogDto normalizeCatalog(const models::CatalogDto& fetched) {
    if (fetched.catalogVersion >= kCatalogVersionCurrent) { return fetched; }
    return detail::legacyV1(fetched);
}

} // namespace dish::catalog
