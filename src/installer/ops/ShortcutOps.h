// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The abstract shortcut seam. Specs carry a LOCATION, not a resolved folder:
// only the Win32 implementation may ask the shell where Programs or Desktop
// live, which keeps the reducers pure and the fakes trivial. create() reports
// the resolved absolute .lnk path back through OpResult::path so the installed
// manifest can record it.

#pragma once

#include "installer/InstallPlan.h"
#include "installer/ops/FileOps.h"

#include <QString>

#include <optional>

namespace dish::installer {

enum class ShortcutLocation { StartMenu, Desktop };

// Journal token spellings for a shortcut location ("startmenu" | "desktop"),
// shared by the reducers and the journal undo path.
inline QString shortcutLocationToken(ShortcutLocation location) {
    return location == ShortcutLocation::Desktop ? QStringLiteral("desktop")
                                                 : QStringLiteral("startmenu");
}
inline std::optional<ShortcutLocation> shortcutLocationFromToken(const QString& token) {
    if (token == QLatin1String("startmenu")) { return ShortcutLocation::StartMenu; }
    if (token == QLatin1String("desktop")) { return ShortcutLocation::Desktop; }
    return std::nullopt;
}

struct ShortcutSpec {
    ShortcutLocation location = ShortcutLocation::StartMenu;
    Scope scope = Scope::PerUser;
    QString targetAbs;
    QString workingDir;
    QString iconAbs;
    int iconIndex = 0;
    QString description;

    bool operator==(const ShortcutSpec& o) const {
        return location == o.location && scope == o.scope && targetAbs == o.targetAbs &&
               workingDir == o.workingDir && iconAbs == o.iconAbs && iconIndex == o.iconIndex &&
               description == o.description;
    }
    bool operator!=(const ShortcutSpec& o) const { return !(*this == o); }
};

class ShortcutOps {
  public:
    virtual ~ShortcutOps() = default;
    virtual OpResult create(const ShortcutSpec& spec) = 0;
    virtual OpResult remove(const QString& linkAbs) = 0;
    virtual bool exists(const QString& linkAbs) = 0;
};

} // namespace dish::installer
