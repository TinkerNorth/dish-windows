// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The base every nav destination extends. It is a TRANSPARENT-backgrounded
// container (no opaque panel) so the Mica backdrop shows through the content
// area; pages place their own Cards on top. It provides a consistent content
// inset and a `title` the shell can read for the breadcrumb/header. Pages put a
// SectionHeader + body inside its default content.
//
// Usage:
//   import "../kit" as Kit
//   Kit.Page {
//       title: qsTr("Controllers")
//       Kit.SectionHeader { label: qsTr("Controllers") }
//       // ... body ...
//   }

import QtQuick
import QtQuick.Controls.Basic

Page {
    id: page

    // The default content lands in a padded column; pages just declare children.
    default property alias content: body.data

    // Read by the shell for the breadcrumb / header. `title` is the Controls
    // Page property; we keep using it.

    // Transparent: the shell sits on Mica and pages must not cover it with an
    // opaque surface — only their Cards are surfaces.
    background: null

    padding: 24

    // The content pane scrolls vertically when it outgrows the viewport — the
    // Windows-Settings pattern: the nav rail + breadcrumb (in the shell) stay
    // fixed, only this pane scrolls. Pages are authored to grow vertically (list
    // pages use a content-sized, non-interactive list), so this single page-level
    // scroller owns all overflow. contentWidth = availableWidth pins the width so
    // only the vertical axis ever scrolls.
    contentItem: ScrollView {
        id: scroller
        contentWidth: availableWidth
        clip: true

        Column {
            id: body
            width: scroller.availableWidth
            spacing: 16
        }
    }
}
