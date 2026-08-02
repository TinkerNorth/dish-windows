// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The app shell (v3): a COLLAPSIBLE left rail (Home / Controllers / Connections
// up top; the compact "Set up" action, then Support Dish and Settings pinned to
// the footer with the version line) and a content column with a per-page header
// (back chevron · title · streaming pill · dot+sub line) over a StackView.
// Collapsed, the rail is a 48px icon strip continuing the title bar's hamburger
// cell; expanded it is a 236px labeled pane. The state persists via
// App.railCollapsed (the hamburger toggles it).
//
// Overlays are IN-SCENE: dialogs are Kit.ContentDialog popups over a scrim and
// transient errors land in the one toast host below — no extra OS windows.
//
// Per-page header contract: a page (or pushed detail) may declare
//   readonly property string headerTitle / headerSub / headerDot
// and the shell renders them; absent properties fall back to the rail label /
// empty. The STREAMING pill is the shell's own — one signal, one surface.
// Pages reach the shell through `StackView.view.shellApi`:
//   selectDestination(index) · pushDetail(url, title, props) ·
//   openSetupWizard(slotId) · requestNavigation(action) · toast(msg, severity)
//   · currentTitle
//
// Leave guard: a pushed page may declare `suppressBack` (the header chevron is
// hidden — the wizard owns the only control named Back) and `blocksLeave` +
// `requestLeave(proceed)` (a rail switch REPLACES the stack, so it must confirm
// first). Everything that would replace or unwind the stack from outside the
// page goes through requestNavigation().

// Bound component behavior: lets the Repeater delegate reference the shell's id
// and its `required` model props without the engine falling back to dynamic
// (unqualified) lookup — keeps binding resolution static and qmllint quiet.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import "kit" as Kit

Item {
    id: shell

    // The destinations. `source` is the page component file the StackView roots
    // to when the rail item is chosen. The first three render at the rail top;
    // the last two (Support Dish above Settings) pin to the rail footer per the
    // design. Support Dish draws the pulse-pink heart instead of a brand glyph
    // (`heart` flag) — the one hue Dish uses beyond cyan, reserved for
    // donations.
    //
    // Glyphs: no two entries may share a family+state pair, or the collapsed
    // 48px icon-only rail is unnavigable (Controllers and Connections were both
    // a satellite until v3).
    //
    // The rule is about the PICTURE, not the file name: `dish` and
    // `dish-connected` are two names for one silhouette (they differ by a
    // 3-unit dot in a 64-unit viewBox — under one pixel at the rail's 16 px),
    // so Home and Controllers read identically the moment the labels fade.
    // Controllers therefore draws `pad`, the glyph SCR §12.1 asked for: the
    // shipped set only ever held three separable silhouettes (dish · satellite
    // · gear) and the rail needs four.
    readonly property var destinations: [
        { key: "home",        label: qsTr("Home"),         glyph: "dish-connected", heart: false, source: "pages/HomePage.qml" },
        { key: "controllers", label: qsTr("Controllers"),  glyph: "pad",            heart: false, source: "pages/ControllersPage.qml" },
        { key: "connections", label: qsTr("Connections"),  glyph: "satellite",      heart: false, source: "pages/ConnectionsPage.qml" },
        { key: "support",     label: qsTr("Support Dish"), glyph: "",               heart: true,  source: "pages/DonatePage.qml" },
        { key: "settings",    label: qsTr("Settings"),     glyph: "gear",           heart: false, source: "pages/SettingsPage.qml" }
    ]

    property int currentIndex: 0
    readonly property bool collapsed: App.railCollapsed

    // The header title fallback. The root of each destination shows the rail
    // label unless the page supplies headerTitle; a pushed detail supplies its
    // own via pushDetail(url, title, props) or its headerTitle property.
    property string currentTitle: destinations[0].label

    function selectDestination(index) {
        if (index === shell.currentIndex && contentStack.depth <= 1)
            return;                       // already on that root; nothing to do
        shell.currentIndex = index;
        shell.currentTitle = shell.destinations[index].label;
        // replace() clears any pushed detail pages — a rail switch is a fresh root.
        contentStack.replace(null, Qt.resolvedUrl(shell.destinations[index].source));
    }

    // Pages push a detail view with an explicit breadcrumb title; pop() (the
    // back chevron) restores the destination root and its label. `props` is the
    // optional initial-property map (ConfigureBindingPage needs its slotId
    // before the component is loaded, not after).
    function pushDetail(url, title, props) {
        shell.currentTitle = title;
        contentStack.push(Qt.resolvedUrl(url), props || {});
    }

    // The one setup surface. It is a PUSHED PAGE, never a dialog, and it always
    // lands on Home's stack — its result is a Home row, so popping (on success
    // or cancel) must never strand the user in Settings. `slotId` pre-answers
    // page 1 for a dangling pad; "" starts at the beginning.
    function openSetupWizard(slotId) {
        shell.selectDestination(0);
        contentStack.push(Qt.resolvedUrl("wizard/SetupWizardPage.qml"),
                          { seedSlotId: slotId || "" });
        shell.currentTitle = qsTr("Set up Dish");
    }

    // Anything that would replace or unwind the content stack from OUTSIDE the
    // current page runs through here: the page gets first refusal (the wizard
    // raises its discard confirm and calls back only if the user agrees).
    // Read through `_cur` (a `var`), never `contentStack.currentItem` (a typed
    // QQuickItem): the guard properties are OPTIONAL page declarations, and a
    // typed read of one makes qmllint's missing-property gate fire.
    function requestNavigation(action) {
        const cur = shell._cur;
        if (cur && cur.blocksLeave === true && typeof cur.requestLeave === "function")
            cur.requestLeave(action);
        else
            action();
    }

    // The one toast host is the shell's (below), so a page raises a transient
    // notice through here instead of dropping a second host into its own tree.
    // `severity` is "error" | "warning" | "success".
    function toast(message, severity) {
        toastHost.show(message, severity);
    }

    // What the header renders for the current page: the page's own header*
    // properties when declared, else the fallbacks.
    readonly property var _cur: contentStack.currentItem
    readonly property string headerTitle: _cur && _cur.headerTitle !== undefined
                                          ? _cur.headerTitle : currentTitle
    readonly property string headerSub: _cur && _cur.headerSub !== undefined ? _cur.headerSub : ""
    readonly property string headerDot: _cur && _cur.headerDot !== undefined ? _cur.headerDot : ""
    // The ONE streaming surface. Owned by the shell, not by a page, so it reads
    // the same on every destination and can never disagree with itself.
    readonly property string headerPill: App.keepAwakeActive
                                         ? qsTr("Streaming · display kept awake") : ""
    // The wizard suppresses the chevron: it draws its own footer `‹ Back`, and
    // two controls named Back 500px apart is the defect this closes.
    readonly property bool backVisible: contentStack.depth > 1
                                        && !(_cur && _cur.suppressBack === true)

    // Which half of the shell holds the keyboard. F6 hops between them (the
    // Windows pane-cycle convention); named ids beat probing the focus chain.
    readonly property bool railFocused: railHome.activeFocus || railControllers.activeFocus
                                        || railConnections.activeFocus || railAdd.activeFocus
                                        || railSupport.activeFocus || railSettings.activeFocus

    Shortcut {
        sequence: "F6"
        onActivated: {
            if (shell.railFocused) {
                if (contentStack.currentItem)
                    contentStack.currentItem.forceActiveFocus(Qt.TabFocusReason);
            } else {
                railHome.forceActiveFocus(Qt.TabFocusReason);
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: shell.requestNavigation(function () { shell.selectDestination(4); })
    }
    // The Windows Back gesture, bound to the same action as the header chevron
    // and DISABLED exactly when the chevron is hidden. The wizard suppresses the
    // chevron and binds Alt+Left to its own footer Back; two enabled Shortcuts
    // on one sequence is an ambiguous activation, so only one may be live.
    Shortcut {
        sequence: "Alt+Left"
        enabled: shell.backVisible
        onActivated: shell.requestNavigation(function () { contentStack.pop(); })
    }

    RowLayout {
        anchors.fill: parent
        // Flush: the rail's own hairline is the seam between the two panes.
        spacing: Tokens.s0

        // ---- Left navigation rail (design: a solid surface pane with a
        // hairline right edge; 236px labeled / 48px icon strip). ----
        Rectangle {
            id: rail
            Layout.fillHeight: true
            Layout.preferredWidth: shell.collapsed ? Tokens.railCompact : Tokens.railExpanded
            color: Theme.surface

            Behavior on Layout.preferredWidth {
                NumberAnimation {
                    duration: Tokens.reducedMotion ? 0 : Tokens.durFast
                    easing.type: Easing.OutCubic
                }
            }

            Rectangle {                   // right-edge hairline divider
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.outline
            }

            // One rail item; used for the top destinations and the pinned
            // Settings entry.
            component RailItem: ItemDelegate {
                id: railItem
                required property int destIndex
                readonly property var dest: shell.destinations[destIndex]
                readonly property bool active: destIndex === shell.currentIndex

                implicitHeight: Tokens.navItemHeight
                // The content is positioned in absolute cell coordinates below;
                // control padding would double-inset it.
                padding: Tokens.s0
                focusPolicy: Qt.StrongFocus

                Accessible.role: Accessible.Button
                Accessible.name: railItem.dest.label

                onClicked: shell.requestNavigation(function () {
                    shell.selectDestination(railItem.destIndex);
                })

                // Icon-only navigation without tooltips is not shippable; the
                // expanded rail carries its own labels.
                //
                // Declared, not attached: the ToolTip ATTACHED property resolves
                // its delegate through QtQuick.Controls (the style-selecting
                // module), which this file does not import — the attached form
                // logs "QQmlComponent: Component is not ready" at RUNTIME and no
                // tip ever appears. Kit.DishToolTip is the app's themed tooltip;
                // a bare ToolTip would resolve Basic's system-palette delegate
                // and paint an unthemed white slab in both appearances.
                Kit.DishToolTip {
                    id: railTip
                    visible: (railItem.hovered || railItem.visualFocus) && shell.collapsed
                    delay: 500
                    text: railItem.dest.label
                    x: railItem.width + Tokens.s3
                    y: (railItem.height - railTip.implicitHeight) / 2
                }

                background: Rectangle {
                    radius: Tokens.radiusChip
                    color: railItem.active ? Theme.primaryFill
                         : railItem.hovered ? Theme.primaryHover
                         : "transparent"
                    border.width: railItem.visualFocus ? 1 : 0
                    border.color: Theme.primary

                    Rectangle {   // active-destination accent bar, hugging the edge
                        visible: railItem.active
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: Tokens.glyphSm
                        radius: Tokens.radiusBar
                        color: Theme.primary
                    }

                    Rectangle {   // keyboard focus ring, outside the fill
                        visible: railItem.visualFocus
                        anchors.fill: parent
                        anchors.margins: -Tokens.s1
                        radius: Tokens.radiusChip + Tokens.s1
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.focusRing
                    }
                }

                contentItem: Item {
                    // Fixed icon cell (railCompact minus the item margins) so the
                    // glyph column doesn't shift as the rail animates.
                    Kit.BrandGlyph {
                        visible: !railItem.dest.heart
                        glyph: railItem.dest.glyph
                        width: Tokens.glyphSm
                        height: Tokens.glyphSm
                        anchors.verticalCenter: parent.verticalCenter
                        x: (Tokens.railCompact - Tokens.s4) / 2 - width / 2
                    }
                    // Support Dish draws the pulse-pink heart (the design's
                    // FHeartNav) — a text glyph, not a brand SVG, so it rides
                    // the palette's pulse token in both appearances.
                    Label {
                        visible: railItem.dest.heart === true
                        text: "♥"   // ♥ — brand glyph, not localized
                        color: Theme.pulse
                        font.pixelSize: Tokens.textHeading
                        anchors.verticalCenter: parent.verticalCenter
                        x: (Tokens.railCompact - Tokens.s4) / 2 - width / 2
                    }
                    Label {
                        text: railItem.dest.label
                        color: railItem.active ? Theme.onSurface : Theme.muted
                        font.pixelSize: Tokens.textBase
                        font.weight: railItem.active ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                        x: Tokens.railCompact - Tokens.s4
                        opacity: shell.collapsed ? 0 : 1
                        visible: opacity > 0
                        Behavior on opacity {
                            NumberAnimation { duration: Tokens.reducedMotion ? 0 : Tokens.durFast }
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: Tokens.s2
                anchors.bottomMargin: Tokens.s4
                anchors.leftMargin: Tokens.s2
                anchors.rightMargin: Tokens.s2
                // Flush: each RailItem carries its own Layout.margins.
                spacing: Tokens.s0

                RailItem { id: railHome;        destIndex: 0; Layout.fillWidth: true; Layout.margins: Tokens.s1 }
                RailItem { id: railControllers; destIndex: 1; Layout.fillWidth: true; Layout.margins: Tokens.s1 }
                RailItem { id: railConnections; destIndex: 2; Layout.fillWidth: true; Layout.margins: Tokens.s1 }

                Item { Layout.fillHeight: true }   // pin the footer cluster

                // The pane-density "Set up" action (design frame 18's compact
                // sibling of the dashed action card): a solid accent outline
                // over the primary-fill wash, deepening 18 → 24 % on hover /
                // press; collapsed it is the bare centered +. It is an ACTION,
                // not a destination — it opens the wizard on Home's stack.
                AbstractButton {
                    id: railAdd
                    Layout.fillWidth: true
                    Layout.leftMargin: Tokens.s3
                    Layout.rightMargin: Tokens.s3
                    Layout.topMargin: Tokens.s1
                    Layout.bottomMargin: Tokens.s4
                    implicitHeight: 36
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Set up")

                    onClicked: shell.requestNavigation(function () { shell.openSetupWizard(""); })

                    background: Rectangle {
                        radius: Tokens.radiusButton
                        color: railAdd.down ? Theme.accentWash24
                             : railAdd.hovered ? Theme.primaryPress
                             : Theme.primaryFill
                        border.width: 1
                        border.color: Theme.primary

                        Rectangle {   // keyboard focus ring, outside the fill
                            visible: railAdd.visualFocus
                            anchors.fill: parent
                            anchors.margins: -Tokens.s1
                            radius: Tokens.radiusButton + Tokens.s1
                            color: "transparent"
                            border.width: 2
                            border.color: Theme.focusRing
                        }
                    }
                    contentItem: Item {
                        // Fixed + cell (railCompact minus the button margins) so
                        // the glyph column holds as the rail animates; the label
                        // fades exactly like a RailItem's.
                        Text {
                            text: "+"
                            color: Theme.primary
                            font.pixelSize: Tokens.textHeading
                            anchors.verticalCenter: parent.verticalCenter
                            x: (Tokens.railCompact - Tokens.s6) / 2 - width / 2
                        }
                        Label {
                            text: qsTr("Set up")
                            color: Theme.primary
                            font.pixelSize: Tokens.textBase
                            font.weight: Font.DemiBold
                            anchors.verticalCenter: parent.verticalCenter
                            x: Tokens.railCompact - Tokens.s6
                            opacity: shell.collapsed ? 0 : 1
                            visible: opacity > 0
                            Behavior on opacity {
                                NumberAnimation { duration: Tokens.reducedMotion ? 0 : Tokens.durFast }
                            }
                        }
                    }

                    // Declared, not attached — see RailItem above.
                    Kit.DishToolTip {
                        id: railAddTip
                        visible: (railAdd.hovered || railAdd.visualFocus) && shell.collapsed
                        delay: 500
                        text: qsTr("Set up")
                        x: railAdd.width + Tokens.s3
                        y: (railAdd.height - railAddTip.implicitHeight) / 2
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: Tokens.s2
                    Layout.rightMargin: Tokens.s2
                    Layout.topMargin: Tokens.s2
                    Layout.bottomMargin: Tokens.s2
                    implicitHeight: 1
                    color: Theme.outline
                }

                RailItem { id: railSupport;  destIndex: 3; Layout.fillWidth: true; Layout.margins: Tokens.s1 }
                RailItem { id: railSettings; destIndex: 4; Layout.fillWidth: true; Layout.margins: Tokens.s1 }

                Label {
                    text: qsTr("Dish %1").arg(App.appVersion)
                    font.family: Tokens.monoFamily
                    font.pixelSize: Tokens.textChip
                    color: Theme.muted
                    Layout.leftMargin: Tokens.s4
                    Layout.topMargin: Tokens.s3
                    Layout.bottomMargin: Tokens.s1
                    opacity: shell.collapsed ? 0 : 1
                    visible: opacity > 0
                    Behavior on opacity {
                        NumberAnimation { duration: Tokens.reducedMotion ? 0 : Tokens.durFast }
                    }
                }
            }
        }

        // ---- Content column: per-page header + StackView. ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Flush: the header owns its own bottom margin, and the StackView
            // starts where it ends.
            spacing: Tokens.s0

            // Header: back ‹ (when a detail is pushed and the page allows it) ·
            // title · pill, then the dot + sub line underneath.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tokens.pagePadding
                Layout.rightMargin: Tokens.pagePadding
                Layout.topMargin: Tokens.s5
                Layout.bottomMargin: Tokens.s2
                spacing: Tokens.s2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.s6

                    AbstractButton {
                        id: backChevron
                        visible: shell.backVisible
                        implicitWidth: Tokens.minTouch
                        implicitHeight: Tokens.minTouch
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus

                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Back")

                        onClicked: shell.requestNavigation(function () { contentStack.pop(); })

                        background: Rectangle {
                            visible: backChevron.visualFocus
                            radius: Tokens.radiusChip
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.primary

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -Tokens.s1
                                radius: Tokens.radiusChip + Tokens.s1
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.focusRing
                            }
                        }
                        contentItem: Text {
                            text: "‹"
                            // The single guillemets are missing from some mono
                            // faces; pin the family that has them.
                            font.family: Tokens.sansFamily
                            font.pixelSize: Tokens.textTitle
                            color: backChevron.hovered ? Theme.primary : Theme.muted
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    Label {
                        text: shell.headerTitle
                        color: Theme.onSurface
                        font.pixelSize: Tokens.textTitle
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: shell.headerPill.length > 0
                        implicitWidth: pillText.implicitWidth + Tokens.s8
                        implicitHeight: pillText.implicitHeight + Tokens.s3
                        radius: Tokens.radiusChip
                        color: Theme.primaryFill
                        border.width: 1
                        border.color: Theme.outline

                        Accessible.role: Accessible.StaticText
                        Accessible.name: shell.headerPill

                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            text: shell.headerPill
                            font.family: Tokens.monoFamily
                            font.pixelSize: Tokens.textChip
                            font.letterSpacing: Tokens.sectionLetterSpacing
                            font.capitalization: Font.AllUppercase
                            color: Theme.primary
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    visible: shell.headerSub.length > 0
                    spacing: Tokens.s4

                    Kit.StatusDot {
                        token: shell.headerDot
                        visible: shell.headerDot.length > 0
                    }
                    Label {
                        text: shell.headerSub
                        color: Theme.muted
                        font.pixelSize: Tokens.textSummary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            // The content host. Transparent: pages sit on the window surface.
            StackView {
                id: contentStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                background: null
                // Pages reach the shell (destination switch, detail push, the
                // wizard) through this — `page.StackView.view.shellApi`.
                readonly property var shellApi: shell
                // Confine the slide transition to the content bounds so an
                // outgoing page disappears at the rail divider (slides "behind"
                // it) instead of painting over the rail to the edge.
                clip: true
                // Back at the root, the header reads the destination's own label
                // again: a pushed page's title was only ever a breadcrumb.
                // Keyed on depth rather than on each pop site so a page that
                // pops ITSELF — the wizard, on a successful bind — heals the
                // header too.
                onDepthChanged: {
                    if (contentStack.depth <= 1)
                        shell.currentTitle = shell.destinations[shell.currentIndex].label;
                }
                // Push the first page AFTER the StackView has a layout size rather
                // than via initialItem: initialItem is created during construction
                // when this is still 0x0 inside the Layout, so the first page (the
                // default Home destination) renders blank until a later
                // navigation forces a relayout. Deferring to onCompleted creates it
                // already sized.
                Component.onCompleted: contentStack.push(Qt.resolvedUrl(shell.destinations[0].source))
            }
        }
    }

    // ---- Global transient-notification host. Dropped ONCE here so every
    // one-shot failure in the app (a failed connect/reconnect, an external-link
    // open that couldn't launch, a USB path-switch notice) surfaces as a toast
    // instead of being silently dropped. It overlays the whole shell but only
    // paints at the bottom-center, so it never blocks interaction elsewhere.
    Kit.NotificationToastHost {
        id: toastHost
        anchors.fill: parent
    }
    Connections {
        target: App
        function onErrorMessage(message) { toastHost.show(message); }
    }
}
