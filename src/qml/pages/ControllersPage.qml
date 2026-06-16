// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PLACEHOLDER. The Controllers destination — a later page agent replaces the
// body with the slot/controller list (App.slotModel). For now it only proves
// navigation: a SectionHeader + a todo Label. Do NOT add styling here; extend
// Kit.Page and use kit components (see docs/QML_UI_KIT.md).

import QtQuick.Controls.Basic
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    title: qsTr("Controllers")

    Kit.SectionHeader { label: qsTr("Controllers") }

    Label {
        text: qsTr("TODO: controllers list (App.slotModel)")
        color: Theme.muted
        font.pixelSize: 13
    }
}
