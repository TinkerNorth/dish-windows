// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PLACEHOLDER. The Settings destination — a later page agent replaces the body
// with the real settings (theme, telemetry, crash reporting, donate, licenses).
// Proves navigation only for now.

import QtQuick.Controls.Basic
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    title: qsTr("Settings")

    Kit.SectionHeader { label: qsTr("Settings") }

    Label {
        text: qsTr("TODO: settings (theme / telemetry / about)")
        color: Theme.muted
        font.pixelSize: 13
    }
}
