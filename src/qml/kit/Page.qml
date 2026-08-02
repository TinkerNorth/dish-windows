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

// Dish.Chrome is imported UNDER A NAMESPACE on purpose: the module exports its
// own `Page` (this file), so an unqualified import would make the root `Page`
// resolve to this component and qmllint would report an inheritance cycle. This
// is the one kit file with that collision.
import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome as Chrome

Page {
    id: page

    // The default content lands in a padded column; pages just declare children.
    default property alias content: body.data

    // false -> the page owns its own layout and pins its own regions (the
    // wizard: banner and footer outside a scrolling body). The content column
    // is then exactly the viewport, so a single child sized to `parent` fills
    // it and nothing scrolls at this level.
    property bool scrollable: true

    // Read by the shell for the breadcrumb / header. `title` is the Controls
    // Page property; we keep using it.

    // Transparent: the shell sits on Mica and pages must not cover it with an
    // opaque surface — only their Cards are surfaces.
    background: null

    padding: Chrome.Tokens.pagePadding

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
        ScrollBar.vertical.policy: page.scrollable ? ScrollBar.AsNeeded
                                                   : ScrollBar.AlwaysOff

        Column {
            id: body
            width: scroller.availableWidth
            // Pinned to the viewport in non-scrolling mode so a child can bind
            // its own height to `parent.height` and own the whole page.
            height: page.scrollable ? implicitHeight : scroller.availableHeight
            spacing: page.scrollable ? Chrome.Tokens.s8 : 0
        }
    }
}
