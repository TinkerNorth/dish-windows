// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Controllers destination — the slot dashboard, restyled to the flows
// redesign (design FControllersBody / FSlotCard / FPathSeg / FTelemetry /
// FBindDlg). One Kit.Card per App.slotModel row: brand glyph + status dot +
// name and bound sub-line, the capability pills and mono live-rate readouts,
// the per-slot actions (Bind… / Emulate… / Unbind) and — under a hairline —
// the USB-path segmented control with its claiming / note states plus the
// "Controls…" remap entry. A registering slot renders the busy variant. The
// page header (title · dot+sub · keep-awake pill) is DECLARED here and drawn
// by the shell (AppShell per-page header contract) — the body renders no title
// of its own. A mono telemetry strip pins to the page footer. All data and
// actions come solely from the frozen App contract (docs/QML_CONTRACT.md,
// §7 A2 addendum); no business logic lives here.

// Bound so delegates and the dialog body reference outer ids (page, card,
// bindList) statically and their model bindings resolve via `required
// property`. `App` stays unqualified: it is a runtime context property the
// linter cannot resolve (downgraded to info by the lint invocation), the same
// accepted limitation ConnectionsPage notes.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: page
    title: qsTr("Controllers")

    // ---- Shell header contract (rendered by AppShell, not by this body).
    // Sub-line composition per QML_CONTRACT §7.1: no connections → the
    // getting-started nudge (muted); exactly one online with nothing bound →
    // the named-satellite line (primary); else the online count, suffixed
    // "· nothing bound" while no slot is bound (success).
    readonly property string headerTitle: qsTr("Controllers")
    readonly property string headerSub: {
        if (App.connectionCount === 0)
            return qsTr("No connections yet — pair a Satellite to get started");
        if (App.onlineCount === 1 && App.boundSlotCount === 0)
            return App.firstOnlineName + qsTr(" online · nothing bound");
        return qsTr("%1 of %2 online").arg(App.onlineCount).arg(App.connectionCount)
               + (App.boundSlotCount === 0 ? qsTr(" · nothing bound") : "");
    }
    readonly property string headerDot: App.connectionCount === 0 ? "muted"
                                        : App.onlineCount === 1 && App.boundSlotCount === 0
                                              ? "primary"
                                              : "success"
    readonly property string headerPill: App.keepAwakeActive
                                         ? qsTr("STREAMING · DISPLAY KEPT AWAKE") : ""

    // The enclosing shell StackView, type-erased through `var` so the shell's
    // dynamic `shellApi` property (an instance property AppShell declares on
    // its StackView — invisible to the linter's static type) resolves without
    // a missing-property warning.
    readonly property var shellStack: StackView.view
    readonly property var shellApi: shellStack ? shellStack.shellApi : null

    // The slot whose emulate picker is open, captured on row click (the shared
    // picker dialog is declared once, not per-delegate).
    property string emulateSlotId: ""

    // ---- Empty state --------------------------------------------------------
    // Centered in the content viewport (the wrapper spans the ScrollView's
    // height; with the list hidden it is the only laid-out child, so nothing
    // scrolls and the state sits dead-center like the design's flexed column).
    Item {
        visible: App.slotCount === 0
        width: parent ? parent.width : implicitWidth
        height: page.contentItem.height

        Kit.EmptyState {
            anchors.centerIn: parent
            width: parent.width
            glyph: "dish-off"
            title: qsTr("No controllers connected")
            body: qsTr("Plug in an Xbox, PlayStation, or generic pad over USB or Bluetooth — Windows detects it and Dish lists it here automatically.")
            actionText: qsTr("Open Connections")
            showAction: true
            // Destination 2 is the Connections rail entry (AppShell order —
            // Home / Controllers / Connections).
            onActionRequested: if (page.shellApi) page.shellApi.selectDestination(2)
        }
    }

    // ---- Slot list ----------------------------------------------------------
    // Sized to its contents (the page's Column lays children out vertically)
    // so it grows with the model rather than scrolling internally; the page's
    // single scroller owns all overflow.
    ListView {
        id: slotList
        visible: App.slotCount > 0
        width: parent ? parent.width : implicitWidth
        height: contentHeight
        interactive: false
        spacing: Tokens.s5
        model: App.slotModel

        delegate: Kit.Card {
            id: card
            width: ListView.view ? ListView.view.width : implicitWidth

            // Slot roles consumed by this delegate (contract §2 + §7.2).
            // Declared required so they resolve qualified and the ListView
            // injects them.
            required property string slotId
            required property string name
            required property bool bound
            required property string boundLabel
            // "· as DualShock 4" suffix source; empty → suffix omitted (§7.2).
            required property string emulateName
            // Attach in flight → the busy variant replaces chips/actions (§7.2).
            required property bool registering
            required property bool live
            required property string dotColor
            // True for a raw-joystick pad whose DirectInput routing the
            // "Controls…" page may remap; gates that action's visibility.
            required property bool remappable
            required property bool hasMotion
            required property bool hasLightbar
            required property int batteryLevel
            required property int batteryStatus
            required property bool batteryKnown
            required property int gamepadHz
            required property bool gamepadHzLive
            required property bool gamepadHzShown
            required property int motionHz
            required property bool motionHzShown
            required property int pollHz
            required property bool pollHzShown
            // USB input-path roles (contract §2). The segmented control
            // reflects these; picking calls App.setSlotPath.
            required property string pathPhase
            required property string desiredPath
            required property bool pathSupported
            required property bool claimInProgress
            required property string directFailure

            readonly property bool hasPathRow: !registering
                                               && (pathSupported || remappable)

            contentItem: ColumnLayout {
                spacing: 0

                // ── Busy variant: attach in flight (design registering card) ─
                RowLayout {
                    visible: card.registering
                    Layout.fillWidth: true
                    spacing: Tokens.s6

                    Kit.BrandGlyph {
                        glyph: "dish"
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        Layout.alignment: Qt.AlignVCenter
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.s1

                        Label {
                            text: card.name
                            color: Theme.onSurface
                            font.pixelSize: 14   // design slot-name size (no type-scale token)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: qsTr("Registering controller…")
                            color: Theme.muted
                            font.pixelSize: Tokens.textMeta
                        }
                    }
                }
                Kit.DishProgressBar {
                    visible: card.registering
                    indeterminate: true
                    Layout.fillWidth: true
                    Layout.topMargin: Tokens.s5
                }

                // ── Normal variant: glyph · dot · name/sub/chips · actions ──
                RowLayout {
                    visible: !card.registering
                    Layout.fillWidth: true
                    spacing: Tokens.s6

                    Kit.BrandGlyph {
                        glyph: card.live ? "satellite-connected" : "satellite"
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Kit.StatusDot {
                        token: card.dotColor
                        Layout.alignment: Qt.AlignVCenter
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.s1

                        Label {
                            text: card.name
                            color: Theme.onSurface
                            font.pixelSize: 14   // design slot-name size (no type-scale token)
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            // "Bound to Living-room Satellite · as DualShock 4"
                            // — the emulate suffix only when the resolved short
                            // name is known (§7.2: empty → omit).
                            text: card.bound
                                  ? qsTr("Bound to %1").arg(card.boundLabel)
                                    + (card.emulateName.length > 0
                                           ? qsTr(" · as %1").arg(card.emulateName) : "")
                                  : qsTr("Unbound")
                            color: Theme.muted
                            font.pixelSize: Tokens.textMeta
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        // Capability pills + mono rate readouts, one leading
                        // cluster (design chip row). The absent gyro pill IS
                        // drawn — absence is visible, not merely missing.
                        RowLayout {
                            spacing: Tokens.s3
                            Layout.topMargin: Tokens.s2

                            Kit.CapabilityChip {
                                text: card.hasMotion ? qsTr("Gyro") : qsTr("No gyro")
                                present: card.hasMotion
                            }
                            // Lightbar shown ONLY when the pad has an RGB LED.
                            Kit.CapabilityChip {
                                visible: card.hasLightbar
                                text: qsTr("Lightbar")
                                present: true
                            }
                            // Battery only once a real reading landed (an
                            // unknown level 255 hides the chip via batteryKnown).
                            Kit.CapabilityChip {
                                visible: card.batteryKnown
                                text: page.batteryLabel(card.batteryLevel, card.batteryStatus)
                                present: true
                                low: page.batteryLow(card.batteryLevel, card.batteryStatus)
                            }

                            // Rate readouts: live measurements in the success
                            // tone, estimates/IMU sample rates muted.
                            Kit.LiveStat {
                                visible: card.gamepadHzShown
                                live: card.gamepadHzLive
                                text: card.gamepadHzLive ? qsTr("%1 Hz").arg(card.gamepadHz)
                                                         : qsTr("~%1 Hz").arg(card.gamepadHz)
                                Layout.leftMargin: Tokens.s1
                            }
                            Kit.LiveStat {
                                visible: card.motionHzShown
                                live: false
                                text: qsTr("Motion %1 Hz").arg(card.motionHz)
                                Layout.leftMargin: Tokens.s1
                            }
                            Kit.LiveStat {
                                visible: card.pollHzShown
                                live: true
                                text: qsTr("Poll %1 Hz").arg(card.pollHz)
                                Layout.leftMargin: Tokens.s1
                            }
                        }
                    }

                    // Right-edge actions, vertically centered on the card.
                    RowLayout {
                        spacing: Tokens.s4
                        Layout.alignment: Qt.AlignVCenter

                        Kit.KitButton {
                            visible: !card.bound
                            text: qsTr("Bind…")
                            // Dimmed when the slot has nothing it may bind to
                            // — gated on the SLOT's filtered pick-list
                            // (contract §1), not the raw connection total. The
                            // App.connectionCount read (NOTIFY stateChanged)
                            // enlists this binding in the state graph so the
                            // NOTIFY-less invokable re-evaluates on each move.
                            enabled: App.connectionCount >= 0
                                     && App.availableConnectionsForSlot(card.slotId).length > 0
                            onClicked: page.openBind(card.slotId, card.name)
                        }
                        // Emulate is offered only on a bound slot (nothing to
                        // emulate a pad on until it routes to a satellite).
                        Kit.OutlineButton {
                            visible: card.bound
                            text: qsTr("Emulate…")
                            onClicked: page.openEmulate(card.slotId)
                        }
                        Kit.OutlineButton {
                            visible: card.bound
                            text: qsTr("Unbind")
                            onClicked: App.unbindSlot(card.slotId)
                        }
                    }
                }

                // ── Path row: USB PATH segments + claim/note states, and the
                // remap entry — below a hairline, per the design's FSlotCard
                // footer. Shown for a raw-HID-claimable pad and/or a
                // remappable one; an Xbox/XInput pad has neither and hides it.
                Rectangle {
                    visible: card.hasPathRow
                    implicitHeight: 1
                    color: Theme.outline
                    Layout.fillWidth: true
                    Layout.topMargin: Tokens.s5
                }
                RowLayout {
                    visible: card.hasPathRow
                    Layout.fillWidth: true
                    Layout.topMargin: Tokens.s5
                    spacing: Tokens.s5

                    Kit.Eyebrow {
                        visible: card.pathSupported
                        mutedTone: true
                        text: qsTr("USB PATH")
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Reflects desiredPath ("auto" is an INPUT only — the
                    // resolver collapses it to standard/direct, so it never
                    // reads selected); picking forwards the wire token. Inert
                    // while a Direct claim is in flight so a second pick can't
                    // race the transition.
                    Kit.SegmentedControl {
                        visible: card.pathSupported
                        small: true
                        busy: card.claimInProgress
                        options: [qsTr("Standard"), qsTr("Direct"), qsTr("Auto")]
                        value: card.desiredPath === "direct" ? qsTr("Direct") : qsTr("Standard")
                        Layout.alignment: Qt.AlignVCenter
                        onPicked: function(option) {
                            App.setSlotPath(card.slotId,
                                            option === qsTr("Direct") ? "direct"
                                            : option === qsTr("Auto") ? "auto"
                                                                      : "standard");
                        }
                    }

                    // Claim-in-flight: the thin sweep + status line.
                    Kit.DishProgressBar {
                        visible: card.pathSupported && card.claimInProgress
                        indeterminate: true
                        Layout.preferredWidth: 70   // design's fixed inline sweep width
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Label {
                        visible: card.pathSupported && card.claimInProgress
                        text: qsTr("Claiming controller…")
                        color: Theme.muted
                        font.pixelSize: Tokens.textMeta
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Live Direct: the measured URB completion rate.
                    Kit.LiveStat {
                        visible: card.pathSupported && card.pathPhase === "direct"
                                 && card.pollHzShown
                        live: true
                        text: qsTr("Poll %1 Hz").arg(card.pollHz)
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // Inline note for the non-happy path states (needs-replug /
                    // restore-stuck in the attention tone, a claim-failure
                    // reason in the error tone). Fills the remaining width, so
                    // it doubles as the row's flexible spacer that pushes
                    // "Controls…" to the trailing edge when empty.
                    Label {
                        text: card.pathSupported
                              ? page.pathNoteText(card.pathPhase, card.directFailure) : ""
                        color: page.pathNoteIsError(card.pathPhase, card.directFailure)
                               ? Theme.error : Theme.warning
                        font.pixelSize: Tokens.textMeta
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                    }

                    // The raw-joystick remap detail ("Configure controls").
                    // Shown ONLY for a remappable slot — an SDL game controller
                    // / USB-direct synthetic uses its own mapping and hides it.
                    Kit.OutlineButton {
                        visible: card.remappable
                        text: qsTr("Controls…")
                        Layout.alignment: Qt.AlignVCenter
                        onClicked: page.openRemap(card.slotId, card.name)
                    }
                }
            }
        }
    }

    // ---- Telemetry strip (design FTelemetry) --------------------------------
    // Pinned to the page bottom via the Page footer slot — it holds while the
    // slot list scrolls, exactly the design's out-of-flow footer row.
    footer: Item {
        implicitHeight: telemetryRow.implicitHeight + Tokens.s8

        RowLayout {
            id: telemetryRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Tokens.pagePadding
            anchors.rightMargin: Tokens.pagePadding
            spacing: Tokens.s4

            // LiveStat's non-live face IS the strip's spec (mono chip-size
            // muted); telemetryChanged re-evaluates these ~1 Hz.
            Kit.LiveStat {
                text: qsTr("events/s %1   sends/s %2")
                          .arg(App.eventsPerSec).arg(App.sendsPerSec)
            }
            Item { Layout.fillWidth: true }
            Kit.LiveStat {
                text: qsTr("total %1").arg(App.totalSent)
            }
        }
    }

    // ---- Bind chooser (design FBindDlg) -------------------------------------
    // The shared chooser dialog (also instantiated by HomePage); openFor pulls
    // the slot's filtered pick-list and accept applies the bind.
    BindChooserDialog {
        id: bindDialog
    }

    // ---- Emulate picker -----------------------------------------------------
    EmulatePicker {
        id: emulatePicker
        onChosen: function(type) {
            App.setControllerType(page.emulateSlotId, type);
            emulatePicker.close();
        }
        // Retry re-kicks the async catalog fetch for the open slot; the
        // spinner/error/list then reflect the new AsyncState as it resolves.
        onRetryRequested: App.refreshEmulate(page.emulateSlotId)
    }

    // When the catalog fetch lands (Loading→Success/Error), re-pull the offer
    // list into the open picker so a slow/late catalog populates the rows
    // without the user reopening. Harmless when the picker is closed (the next
    // open reloads).
    Connections {
        target: App
        function onEmulateStateChanged() {
            if (page.emulateSlotId.length > 0) {
                emulatePicker.load(App.emulateTypes(page.emulateSlotId),
                                   App.emulateCurrentType(page.emulateSlotId));
            }
        }
    }

    // ---- Helpers (presentation only; no business logic) ---------------------

    function openBind(slotId, slotName) {
        bindDialog.openFor(slotId, slotName);
    }

    // Push the raw-joystick remap detail. shellApi.pushDetail(url, title)
    // cannot seed initial properties and ControlsRemapPage needs slotId /
    // slotName BEFORE load — so reproduce pushDetail here: push with the
    // params on the shell stack, then hand the pushed page's own title to the
    // shell breadcrumb (what pushDetail's `title` argument does).
    function openRemap(slotId, slotName) {
        if (!page.shellStack) {
            return;
        }
        var detail = page.shellStack.push(Qt.resolvedUrl("ControlsRemapPage.qml"),
                                          { slotId: slotId, slotName: slotName });
        if (detail && page.shellApi) {
            page.shellApi.currentTitle = detail.title;
        }
    }

    function openEmulate(slotId) {
        page.emulateSlotId = slotId;
        // Kick a best-effort catalog refresh before reading the offer list so
        // a freshly-opened picker shows current types (contract requirement).
        App.refreshEmulate(slotId);
        emulatePicker.load(App.emulateTypes(slotId), App.emulateCurrentType(slotId));
        emulatePicker.open();
    }

    // Battery wire-status mapping (contract §2: 2=charging, 3=full, 4=wired;
    // batteryKnown already gates visibility, so 255 never reaches here).
    function batteryLabel(level, status) {
        if (status === 4) { return qsTr("Battery wired"); }
        if (status === 2) { return qsTr("Battery %1% ↑").arg(level); }
        if (status === 3) { return qsTr("Battery full"); }
        return qsTr("Battery %1%").arg(level);
    }
    // Low = the pack is at/under the threshold and not charging (design rule).
    function batteryLow(level, status) {
        return level <= 15 && status !== 2;
    }

    // The inline note beside the path segments for the non-happy FSM states.
    // The phase drives the needs-replug / restore-stuck lines (attention); a
    // directFailure token (present when Direct couldn't claim) drives the
    // failure-reason line (error). Empty for the steady routed/direct/claiming
    // states. Presentation only — the tokens come straight from the roles.
    function pathNoteText(phase, failure) {
        if (phase === "needsReplug") {
            return qsTr("Unplug and replug the controller to finish switching.");
        }
        if (phase === "restoreStuck") {
            return qsTr("Standard isn't responding — pick Direct, retry, or replug.");
        }
        if (failure === "permissionDenied") {
            return qsTr("Direct access denied — another app owns this device.");
        }
        if (failure === "busy") {
            return qsTr("Direct claim is busy — another app or driver holds the device.");
        }
        if (failure === "initFailed") {
            return qsTr("Direct claim couldn't start the controller's report stream.");
        }
        if (failure === "dropped") {
            return qsTr("The device dropped during the claim — a replug is needed.");
        }
        return "";
    }
    // The phase notes are attention-toned; only a claim-failure reason (with
    // no phase note taking precedence) reads in the error tone.
    function pathNoteIsError(phase, failure) {
        return phase !== "needsReplug" && phase !== "restoreStuck"
               && failure.length > 0;
    }
}
