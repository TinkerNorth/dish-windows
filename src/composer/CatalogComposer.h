// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CatalogComposer — a kernel Composer that PURELY derives the list of pickable
// controller types the "Emulate" picker offers, by combining the cached catalog
// (fed in as an Observable<CatalogDto> by the coordinator after a fetch) with the
// set of feature slugs this client understands. The transform is a free function
// (offerableTypes) so it is unit-testable without the Observable plumbing, and
// the composer just wraps it — no IO, no events, exactly one upstream pair.
//
// The per-slot CURRENT/remembered selection is NOT derived here: that lives in
// the ControllerTypeStore (the override map) and is read at the picker call site,
// so this composer stays a pure projection of the catalog. Mirrors the role of
// dish-android composer/MotionCapabilityComposer (a pure AbstractComposer over a
// derived Map) applied to the catalog → picker-list projection.

#pragma once

#include "Models/Models.h"
#include "architecture/Composer.h"
#include "architecture/Observable.h"

#include <QList>
#include <QString>

#include <cstdint>

namespace dish::composer {

// One row the Emulate picker renders. Carries the wire `type` id (the descriptor
// value written into ControllerTypeStore on selection) plus the server-provided,
// already-localized display strings. `slug` lets the UI swap in bundled art /
// translations for a type it recognizes; an unknown slug still renders from the
// server `name`/`shortName`/`description`. `known` is whether the client has a
// bundled identity for this slug (else it is a forward-compat server-only type).
struct PickableType {
    int type = 0;
    QString slug;
    QString name;
    QString shortName;
    QString description;
    bool known = false;

    bool operator==(const PickableType& o) const {
        return type == o.type && slug == o.slug && name == o.name && shortName == o.shortName &&
               description == o.description && known == o.known;
    }
    bool operator!=(const PickableType& o) const { return !(*this == o); }
};

// A settable snapshot of the currently-relevant catalog for the composer's
// upstream Observable. The frozen CatalogDto has no operator== (it is a Qt/QJson
// DTO owned by Wave 1), so it cannot itself be an Observable value; this thin
// wrapper supplies == keyed on the catalog's ETag — which IS its content
// identity ("<serverVersion>+<locale>" per the contract) plus reachability — so
// the Observable's distinct-until-changed suppresses no-op re-emits and a
// no-change revalidate (304) does not retrigger the picker list. The coordinator
// sets this from SatelliteCatalogRepository after a fetch.
struct CatalogSnapshot {
    models::CatalogDto catalog;

    bool operator==(const CatalogSnapshot& o) const {
        return catalog.etag == o.catalog.etag && catalog.reachable == o.catalog.reachable &&
               catalog.serverVersion == o.catalog.serverVersion &&
               catalog.locale == o.catalog.locale;
    }
    bool operator!=(const CatalogSnapshot& o) const { return !(*this == o); }
};

// The slugs the client ships bundled art/translations for. A type whose slug is
// in here is `known`; everything else renders purely from server strings. Kept
// in lockstep with the protocol-1 controller types (xbox360 / ds4).
QList<QString> knownTypeSlugs();

// Derive the offerable picker rows from a catalog. Every controllerType that
// renders is offered (a type newer than the app still gets a row from its
// server-provided name/shortName/description — forward-compat); a catalog row
// with no display name at all is dropped. Order follows the catalog order (the
// server curates it). Pure: no Qt widgets, no IO. Mirrors the contract's
// "unknown controller-TYPE id/slug DOES render" rule.
QList<PickableType> offerableTypes(const models::CatalogDto& catalog);

// The Composer: derives Observable<QList<PickableType>> from the current catalog
// snapshot Observable. Single upstream → single transform; empty until a catalog
// has loaded (an empty CatalogDto yields no rows).
class CatalogComposer : public arch::Composer<QList<PickableType>, CatalogSnapshot> {
  public:
    explicit CatalogComposer(const arch::Observable<CatalogSnapshot>& catalog)
        : arch::Composer<QList<PickableType>, CatalogSnapshot>(
              catalog, [](const CatalogSnapshot& c) { return offerableTypes(c.catalog); }) {}
};

} // namespace dish::composer
