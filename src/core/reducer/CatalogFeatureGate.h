// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CatalogFeatureGate — pure per-type capability gating against the
// satellite's catalog: (a) the descriptor caps rule — a client must not claim
// a proto::kCap* bit the type's catalog features do not offer (the satellite
// delta headline: switchpro advertises analogTriggers supported:false, so a
// pad with real triggers still must not claim the cap on that type) — and
// (b) the touchpad DS4-mode offer check the auto-resolve reads. The per-slug
// drop rule mirrors dish-android CapabilityResolver.typeCapabilities (absent
// or supported:false ⇒ the feature is not in the type layer); the caps-word
// application is AHEAD of android, whose wireCaps still hardcodes
// ANALOG_TRIGGERS|RUMBLE unconditionally — windows follows the latest
// contract here.

#pragma once

#include "Models/Models.h"
#include "core/catalog/BundledCatalog.h"
#include "core/model/Protocol.h"
#include "core/reducer/PickerVisibility.h"
#include "core/reducer/TouchpadModeResolve.h"

#include <QString>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dish::reducer {

// The caps word a descriptor may claim for `type`: each proto::kCap* bit
// survives only when the pad DETECTED it and the catalog type offers the
// matching feature slug (isFeatureOffered: known + present + supported —
// absent or supported:false drops the cap, and an unknown slug never grants
// one). Bits outside the protocol-1 vocabulary pass through untouched: this
// fn owns the per-slug intersection, not the word's full shape. Touchpad has
// no caps bit (it rides the descriptor's `touchpadMode`), hence its absence
// from the table.
inline std::uint16_t allowedCapsForType(std::uint16_t detectedCaps,
                                        const models::CatalogTypeDto& type) {
    const auto known = catalog::knownFeatureSlugs();
    const std::pair<std::uint16_t, QString> capSlugs[] = {
        {proto::kCapAnalogTriggers, catalog::kFeatureAnalogTriggers},
        {proto::kCapRumble, catalog::kFeatureRumble},
        {proto::kCapMotion, catalog::kFeatureMotion},
        {proto::kCapLightbar, catalog::kFeatureLightbar},
    };
    std::uint16_t allowed = detectedCaps;
    for (const auto& [bit, slug] : capSlugs) {
        if ((allowed & bit) != 0 && !isFeatureOffered(type, slug, known)) {
            allowed = static_cast<std::uint16_t>(allowed & ~bit);
        }
    }
    return allowed;
}

// Whether `type` offers the touchpad DS4 pad-render mode. Bridges the DTO
// into A1's typeOffersDs4Touchpad so the mode rule keeps ONE owner: supported
// + (absent `modes` = a pre-modes catalog → the prior assumption,
// pad-capable; else the "ds4" slug must be listed). A type without the
// touchpad feature at all offers nothing.
inline bool typeOffersTouchpadDs4(const models::CatalogTypeDto& type) {
    const auto it = type.features.constFind(catalog::kFeatureTouchpad);
    if (it == type.features.constEnd()) { return false; }
    std::vector<std::string> modes;
    modes.reserve(static_cast<std::size_t>(it->modes.size()));
    for (const auto& mode : it->modes) { modes.push_back(mode.toStdString()); }
    return typeOffersDs4Touchpad(it->supported, modes);
}

} // namespace dish::reducer
