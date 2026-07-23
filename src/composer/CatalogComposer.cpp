// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "composer/CatalogComposer.h"

#include "core/reducer/PickerVisibility.h"

namespace dish::composer {

QList<QString> knownTypeSlugs() {
    // The protocol-1 controller types the client bundles art/translations for.
    // Kept in lockstep with proto::kControllerType* (xbox360 = 0, ds4 = 1,
    // dualsense = 2, switchpro = 3).
    return {QStringLiteral("xbox360"), QStringLiteral("ds4"), QStringLiteral("dualsense"),
            QStringLiteral("switchpro")};
}

QList<PickableType> offerableTypes(const models::CatalogDto& catalog) {
    const auto known = knownTypeSlugs();
    QList<PickableType> out;
    out.reserve(catalog.controllerTypes.size());
    for (const auto& t : catalog.controllerTypes) {
        // A type renders (and so is offered) as long as it carries a display
        // name — a type newer than the app still gets a row from its server
        // strings. A nameless catalog row is dropped.
        if (!reducer::isTypeOfferable(t)) { continue; }
        PickableType row;
        row.type = t.id;
        row.slug = t.slug;
        row.name = t.name;
        row.shortName = t.shortName;
        row.description = t.description;
        row.known = known.contains(t.slug);
        out.push_back(row);
    }
    return out;
}

} // namespace dish::composer
