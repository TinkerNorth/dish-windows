// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure picker-visibility mapper: which connections a slot's "Emulate"/bind
// picker offers, and which catalog controller types are pickable for a slot.
// Pulled out of the UI so the rule is unit-testable without standing up a
// dialog. Qt-free in spirit (it only reads tiny value views), no IO, no events.
// Mirrors dish-android ui/main/ControllerAdapter.connectionsVisibleInPicker +
// isAvailableForPicker (the ConnectionsVisibleInPicker / PickerFromMainUiState
// tests) and the catalog forward-compat rule from satellite/docs/contract.md
// (unknown controller slug still renders; unknown feature slug is silently not
// offered).

#pragma once

#include "Models/Models.h"
#include "core/model/Protocol.h"
#include "core/reducer/EmulateSeed.h"

#include <QList>
#include <QString>

#include <optional>

namespace dish::reducer {

// A connection is offered in the picker only when its live link is usable right
// now: Connected, or Unstable (faltering but still routing). Connecting (no
// session yet), Ready/Found (seen, not connected), Saved (offline) and Stale
// (needs re-pairing) are not — they would be a dead pick. Mirrors android
// LinkState.isAvailableForPicker exactly; every LinkState resolves (total).
inline bool isAvailableForPicker(models::LinkState live) {
    switch (live) {
    case models::LinkState::Connected:
    case models::LinkState::Unstable:
        return true;
    case models::LinkState::Connecting:
    case models::LinkState::Ready:
    case models::LinkState::Found:
    case models::LinkState::Saved:
    case models::LinkState::Stale:
        return false;
    }
    return false;
}

// The picker list for a slot: every connection that is available right now,
// PLUS the slot's currently-bound connection even when it has gone offline (the
// "holdover" — so a flaky host doesn't vanish from under the user mid-session;
// it disappears only once they unbind or it auto-recovers). Order is preserved
// (the input order is the row order), and the result is a fresh list — the input
// is never mutated. `boundConnectionId` empty/absent matches nothing. Mirrors
// android connectionsVisibleInPicker(all, boundConnectionId) and is the body of
// the per-slot PickerFromMainUiState derivation (which is just this fn applied
// with the slot's boundConnectionId).
inline QList<models::ConnectionSummary>
connectionsVisibleInPicker(const QList<models::ConnectionSummary>& all,
                           const std::optional<QString>& boundConnectionId) {
    QList<models::ConnectionSummary> visible;
    visible.reserve(all.size());
    for (const auto& c : all) {
        const bool bound = boundConnectionId.has_value() && c.id == *boundConnectionId;
        if (isAvailableForPicker(c.live) || bound) { visible.push_back(c); }
    }
    return visible;
}

// A controller type from the catalog is OFFERABLE in the picker as long as it
// renders — which a type newer than the app still does, from the server-provided
// name/shortName/description. The only gate is that the type carries a display
// name (an empty catalog row with no name is dropped). Unknown slug → still
// offered (forward-compat); the per-feature gating happens elsewhere (a feature
// slug the client has no code for is simply not surfaced). Mirrors the contract's
// "unknown controller-TYPE id/slug DOES render" rule.
inline bool isTypeOfferable(const models::CatalogTypeDto& type) {
    return !type.name.isEmpty() || !type.shortName.isEmpty();
}

// The slot's seed/default controller type, as a pure ladder: the user's Emulate
// override when set; else the type whose `emulates` hint matches the detected
// pad (EmulateSeed — the server's mapping policy, resolved against the OFFERED
// rows only, since hints ride only offered types); else the first type the
// catalog OFFERS (isTypeOfferable — the picker's first row, in the server's
// curated order); else Xbox when no catalog is cached. An empty pad identity
// (or a hint-less catalog) matches nothing, so the ladder degrades to exactly
// the pre-emulates first-offered behavior.
inline int seedControllerType(std::optional<int> userOverride,
                              const std::optional<models::CatalogDto>& catalog,
                              const QString& sdlTypeSlug, const QString& vidPid) {
    if (userOverride) { return *userOverride; }
    if (catalog) {
        QList<models::CatalogTypeDto> offered;
        offered.reserve(catalog->controllerTypes.size());
        for (const auto& t : catalog->controllerTypes) {
            if (isTypeOfferable(t)) { offered.push_back(t); }
        }
        if (const auto match = seedFromEmulates(offered, sdlTypeSlug, vidPid)) { return *match; }
        if (!offered.isEmpty()) { return offered.front().id; }
    }
    return proto::kControllerTypeXbox;
}

// Identity-less overload for callers with no detected pad in hand (and the
// pre-emulates call sites): same ladder, hint matching short-circuits to
// first-offered.
inline int seedControllerType(std::optional<int> userOverride,
                              const std::optional<models::CatalogDto>& catalog) {
    return seedControllerType(userOverride, catalog, QString(), QString());
}

// A catalog FEATURE is offered only when the client recognises its slug AND the
// server marks it supported for this type. An unknown feature slug (a capability
// a newer server invented) is silently not offered — never an error. The set of
// slugs the client understands is the protocol-1 capability vocabulary
// (rumble/analogTriggers/motion/lightbar/touchpad); `known` is that whitelist
// supplied by the caller so this stays a pure data decision. Mirrors the
// contract's "an unknown feature/hostFeature slug is simply NOT OFFERED".
inline bool isFeatureOffered(const models::CatalogTypeDto& type, const QString& featureSlug,
                             const QList<QString>& known) {
    if (!known.contains(featureSlug)) { return false; }
    const auto it = type.features.constFind(featureSlug);
    return it != type.features.constEnd() && it->supported;
}

} // namespace dish::reducer
