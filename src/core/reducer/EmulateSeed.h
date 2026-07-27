// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// EmulateSeed — pure seed-type resolution over the catalog's OPTIONAL
// per-type `emulates` hints (contract §Catalog): which offered virtual type is
// the natural default for a DETECTED physical pad. The mapping policy lives on
// the server so new hardware needs no client release; this only matches the
// pad's identity (SDL-type slug + USB "vid:pid") against it. dish-android has
// no equivalent yet — its picker ignores `emulates` and snaps to the first
// offered type (ConfigureBindingsViewModel.withCatalogDefault); windows
// implements the contract's matching and keeps first-offered as the EXACT
// degradation when no hints exist, so an interim/legacy catalog behaves
// bit-for-bit as before. Qt types only because the CatalogDto vocabulary
// forces them.

#pragma once

#include "Models/Models.h"

#include <QChar>
#include <QList>
#include <QString>

#include <optional>

namespace dish::reducer {

// The catalog's `usb` identities are lowercase zero-padded hex "vid:pid"
// (Models.cpp lowercases them at parse). Format a detected pad's numeric
// identity into that vocabulary. A 0/0 identity (SDL could not read the
// descriptor) is identity-LESS, not a real key, so it yields empty — which
// never matches — mirroring SlotPathFields' vid/pid-0 unsupported rule.
inline QString vidPidKey(int vendorId, int productId) {
    if (vendorId == 0 && productId == 0) { return {}; }
    return QStringLiteral("%1:%2")
        .arg(vendorId & 0xFFFF, 4, 16, QLatin1Char('0'))
        .arg(productId & 0xFFFF, 4, 16, QLatin1Char('0'));
}

// One type's hint vs one detected pad: the sdlType slug OR any usb identity
// matches. Both sides are protocol constants (lowercase, never localized), so
// the compares are verbatim. Empty detected fields match nothing — an unknown
// pad must not accidentally equal a hint whose own field is absent/empty.
inline bool emulatesMatches(const models::CatalogEmulatesDto& hint, const QString& sdlTypeSlug,
                            const QString& vidPid) {
    if (!sdlTypeSlug.isEmpty() && hint.sdlType == sdlTypeSlug) { return true; }
    return !vidPid.isEmpty() && hint.usb.contains(vidPid);
}

// The seeded type id for a detected pad, resolved against the OFFERED types
// (the caller pre-filters with isTypeOfferable — hints ride only offered types
// per the contract). First match in catalog order wins: the server curates the
// order, so a pad matching two hints gets the server's preferred one. nullopt
// means no offered type carries emulates at all (an interim/legacy catalog) OR
// nothing matched this pad — either way the caller degrades to exactly its
// first-offered default.
inline std::optional<int> seedFromEmulates(const QList<models::CatalogTypeDto>& offeredTypes,
                                           const QString& sdlTypeSlug, const QString& vidPid) {
    for (const auto& type : offeredTypes) {
        if (!type.emulates) { continue; }
        if (emulatesMatches(*type.emulates, sdlTypeSlug, vidPid)) { return type.id; }
    }
    return std::nullopt;
}

} // namespace dish::reducer
