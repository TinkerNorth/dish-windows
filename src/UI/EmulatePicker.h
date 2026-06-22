// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// EmulatePicker — the catalog-driven controller-type selector dialog (the one
// genuinely new feature surface in this wave). It lists the satellite catalog's
// controllerTypes (name / shortName / description) so the player chooses what a
// bound slot emulates on the host. A type the client recognizes (xbox360 / ds4)
// keeps its bundled glyph; a type newer than the app still renders from the
// server-provided strings (forward-compat). On accept it emits the chosen wire
// `type` id; the host (SlotCard / MainWindow / AppModel) writes it into the
// ControllerTypeStore, which the connection layer turns into the per-controller
// session PUT. Mirrors dish-android's Emulate row + picker (ControllerAdapter
// typePillLabel + the type chooser), built as a humble Qt dialog.

#pragma once

#include "composer/CatalogComposer.h"

#include <QDialog>
#include <QList>

class QButtonGroup;

namespace dish::ui {

class EmulatePicker : public QDialog {
    Q_OBJECT
  public:
    // `types` is the offerable list (CatalogComposer::offerableTypes); `slotName`
    // labels the dialog; `currentType` is the slot's current/remembered type id
    // (pre-selected if it appears in the list).
    EmulatePicker(const QList<composer::PickableType>& types, const QString& slotName,
                  int currentType, QWidget* parent = nullptr);

    // The type id the user settled on (valid after exec() returns Accepted).
    int chosenType() const { return chosenType_; }

  signals:
    // Emitted on accept with the chosen wire `type` id. The host writes it into
    // the ControllerTypeStore + drives the per-controller PUT.
    void typeChosen(int type);

  private:
    void onAccept();

    QButtonGroup* group_ = nullptr;
    int chosenType_ = 0;
};

} // namespace dish::ui
