// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// BundledCatalog — the OFFLINE per-slug capability sets for the controller
// types this client ships art/translations for. Port of dish-android
// composer/BundledCatalog: the fallback when no server catalog has ever been
// fetched, and the single feature source LegacyCatalogTranslator rebuilds a
// recognized legacy catalog from — so neither path re-lists feature slugs ad
// hoc. Unknown slugs return nullopt so a richer REMOTE type is never masked by
// a stale bundled guess.
//
// Android's sets carry its Feature enum incl. GAMEPAD/MOUSE/KEYBOARD, which
// have NO catalog slug (they are intrinsic / host-injected, so only the host
// layer gates them and android filters them out wherever catalog slugs are
// built). Windows has no Feature model yet (A2's Capability.h), so this
// exposes exactly the CATALOG-slug subset — the keys a CatalogTypeDto.features
// map carries. That DTO vocabulary is also why Qt containers appear under
// core/: QString slugs / QStringList sets ARE the data being produced
// (core/catalog uses Qt only where the DTOs force it).

#pragma once

#include "core/model/Protocol.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace dish::catalog {

// The bundled type slugs, in wire-id order (proto::kControllerType*).
inline const QString kSlugXbox360 = QStringLiteral("xbox360");
inline const QString kSlugDs4 = QStringLiteral("ds4");
inline const QString kSlugDualSense = QStringLiteral("dualsense");
inline const QString kSlugSwitchPro = QStringLiteral("switchpro");

// Catalog feature-slug vocabulary (protocol constants, never localized).
inline const QString kFeatureAnalogTriggers = QStringLiteral("analogTriggers");
inline const QString kFeatureRumble = QStringLiteral("rumble");
inline const QString kFeatureMotion = QStringLiteral("motion");
inline const QString kFeatureLightbar = QStringLiteral("lightbar");
inline const QString kFeatureTouchpad = QStringLiteral("touchpad");

// The per-type feature slugs this client has code for — the `known` whitelist
// reducer::isFeatureOffered gates on, owned here so every caller passes the
// same protocol-1 vocabulary instead of re-listing it.
inline QStringList knownFeatureSlugs() {
    return {kFeatureRumble, kFeatureAnalogTriggers, kFeatureMotion, kFeatureLightbar,
            kFeatureTouchpad};
}

// The supported catalog features of one bundled type. Every emulated pad
// carries analog triggers and a rumble motor in the bundled view; the richer
// pads add their extras. Deterministic order (triggers, rumble, extras) so the
// list is ==-comparable in tests and downstream snapshots.
inline std::optional<QStringList> typeFeatureSlugs(const QString& slug) {
    const QStringList base{kFeatureAnalogTriggers, kFeatureRumble};
    if (slug == kSlugXbox360) { return base; }
    if (slug == kSlugDs4 || slug == kSlugDualSense) {
        return base + QStringList{kFeatureMotion, kFeatureTouchpad, kFeatureLightbar};
    }
    if (slug == kSlugSwitchPro) { return base + QStringList{kFeatureMotion}; }
    return std::nullopt;
}

// Wire-id flavor for callers that only hold a descriptor `type`. An id outside
// the bundled range degrades to the xbox360 set (the least-capable baseline —
// mirrors android typeCapabilitiesById's else arm) rather than claiming
// features an unknown type may not have.
inline QStringList typeFeatureSlugsById(int typeId) {
    switch (typeId) {
    case proto::kControllerTypePlayStation:
        return *typeFeatureSlugs(kSlugDs4);
    case proto::kControllerTypeDualSense:
        return *typeFeatureSlugs(kSlugDualSense);
    case proto::kControllerTypeSwitchPro:
        return *typeFeatureSlugs(kSlugSwitchPro);
    default:
        return *typeFeatureSlugs(kSlugXbox360);
    }
}

} // namespace dish::catalog
