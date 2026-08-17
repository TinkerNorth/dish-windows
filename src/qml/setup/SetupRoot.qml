// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The installer window: one fixed 460×420 face-swapper. No pages, no stage
// bar, no rail, no modal dialogs — the window swaps FACES (welcome, options,
// confirms, blocker gate, progress, done/failed, and the uninstall trio),
// each with one full-width verb and a link row. All faces are always
// instantiated and toggled by `visible` with the standard enter animation;
// this file owns the face map, the engine-phase reactions, and the keyboard
// contract: Enter is the active face's verb, Esc is its safe exit. Silent
// mode never instantiates any of this: no QML precondition gates engine
// behavior.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome
import Dish.Setup

ApplicationWindow {
    id: root

    readonly property bool uninstall: Setup.mode === Setup.Uninstall

    // Fixed: installers don't resize. Minimum == maximum keeps the native
    // frame from ever offering a resize, and the title bar publishes no
    // maximize rect, so Snap Layouts stays away too.
    width: 460
    height: 420
    minimumWidth: 460
    minimumHeight: 420
    maximumWidth: 460
    maximumHeight: 420
    visible: true
    title: root.uninstall ? qsTr("Uninstall Dish") : qsTr("Dish Setup")

    // The C++ FramelessWindowChrome filter restores the native snap/shadow
    // these flags strip; the button HINTS are load-bearing Win32 style bits
    // (Main.qml documents why). No maximize hint: the window is fixed.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint
           | Qt.WindowMinimizeButtonHint | Qt.WindowCloseButtonHint

    // ALWAYS the themed solid, never a transparent Mica body.
    color: Theme.background

    // ── Face state ──────────────────────────────────────────────────────────
    // Install: welcome → (options) → progress → done/failed, with the
    // confirm/blocker/elevation/recovery faces on their edges. Uninstall:
    // remove → progress → removed.
    property string face: "welcome"
    // The face whose verb started the run: a cancelled run resumes there.
    property string commitFace: "welcome"
    property bool closeApproved: false

    readonly property var faceItems: ({
        "welcome": welcomeFace,
        "options": optionsFace,
        "confirmDowngrade": downgradeFace,
        "recovery": recoveryFace,
        "blocker": blockerFace,
        "progress": progressFace,
        "confirmCancel": cancelFace,
        "elevation": elevationFace,
        "done": doneFace,
        "failed": failedFace,
        "remove": removeFace,
        "removed": removedFace
    })
    // `var`, not `Item`: faces expose `verbButton` by convention, which a
    // QQuickItem-typed property would hide from the dynamic lookups below.
    readonly property var activeFace: root.faceItems[root.face] || null

    // Engine work in flight: no verb may start a second run under it.
    readonly property bool working: Setup.phase === Setup.Preflight
                                    || Setup.phase === Setup.AwaitingBlockers
                                    || Setup.phase === Setup.AwaitingElevation
                                    || Setup.phase === Setup.Copying
                                    || Setup.phase === Setup.Committing
                                    || Setup.phase === Setup.Finalizing
                                    || Setup.phase === Setup.RollingBack

    // ── Navigation ──────────────────────────────────────────────────────────
    function setFace(name, forward) {
        if (name === root.face)
            return;
        root.face = name;
        const item = root.activeFace;
        if (item) {
            // Clean slate first, so a reduced-motion (or interrupted) entry
            // never inherits a stale offset.
            item.x = 0;
            item.opacity = 1;
            root.playEnter(item, forward !== false);
        }
        root.focusFace();
    }

    // Focus follows the face: the verb (confirm faces expose their SAFE
    // primary as the verb), else the face itself so keys still bubble here.
    function focusFace() {
        const item = root.activeFace;
        if (!item)
            return;
        const target = item.verbButton;
        if (target && target.visible && target.enabled)
            target.forceActiveFocus(Qt.TabFocusReason);
        else
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    // Enter anywhere on a face is its verb — reached only when the focused
    // control didn't consume Return itself (buttons and links do).
    function verbPressed() {
        const item = root.activeFace;
        if (!item)
            return;
        const target = item.verbButton;
        if (target && target.visible && target.enabled)
            target.clicked();
    }

    // Esc is the face's safe exit; work that cannot stop refuses it.
    function escapePressed() {
        if (root.uninstall) {
            if (root.face === "blocker") {
                Setup.cancel();
                return;
            }
            if (root.face === "elevation") {
                root.setFace("remove", false);
                return;
            }
            if (root.face === "progress")
                return; // removal is not resumable
            Setup.quitSetup(); // remove, removed, failed: nothing left to stop
            return;
        }
        switch (root.face) {
        case "options":
        case "confirmDowngrade":
            root.setFace("welcome", false);
            return;
        case "recovery":
            root.setFace("welcome");
            return;
        case "blocker":
            // A dismissed gate is a cancel, never a silent hang.
            Setup.cancel();
            return;
        case "progress":
            if (Setup.phase === Setup.Copying)
                root.setFace("confirmCancel");
            return; // elevation wait and the atomic tail refuse it
        case "confirmCancel":
            root.setFace("progress", false);
            return;
        case "elevation":
            root.setFace(root.commitFace, false);
            return;
        case "done":
            root.finishWithoutOpening();
            return;
        default:
            Setup.quitSetup(); // welcome, failed: nothing (new) is on disk
        }
    }

    function commitInstall(from) {
        if (root.working)
            return;
        root.commitFace = from;
        Setup.beginInstall();
    }

    function commitUninstall() {
        if (root.working)
            return;
        Setup.beginUninstall();
    }

    function retryInstall() {
        if (root.working)
            return;
        // Re-arms to Idle, then re-runs with the same choices — no page
        // detour, exactly what the failed face's sentence promised.
        Setup.retry();
        Setup.beginInstall();
    }

    function finishWithoutOpening() {
        root.closeApproved = true;
        Setup.finishOnly();
    }

    // M1: only the incoming side animates — the outgoing face hides
    // instantly, so there is no two-face overlap to manage.
    function playEnter(item, forward) {
        if (Tokens.reducedMotion || !item)
            return;
        enterAnim.stop();
        enterAnim.target = item;
        enterAnim.fromX = forward ? Tokens.s5 : -Tokens.s5;
        enterAnim.start();
    }

    ParallelAnimation {
        id: enterAnim

        property Item target: null
        property real fromX: 0

        NumberAnimation {
            target: enterAnim.target
            property: "opacity"
            from: 0
            to: 1
            duration: Tokens.durNormal
            easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: enterAnim.target
            property: "x"
            from: enterAnim.fromX
            to: 0
            duration: Tokens.durNormal
            easing.type: Easing.OutQuad
        }
    }

    // M9: one clean settle — the pulse stops, the mark rests — then the done
    // face. Under reduced motion the moment is skipped entirely.
    Timer {
        id: settleTimer
        interval: 2 * Tokens.durNormal
        repeat: false
        onTriggered: root.setFace("done")

        function arm() {
            if (Tokens.reducedMotion)
                root.setFace("done");
            else
                settleTimer.restart();
        }
    }

    // ── Engine reactions ────────────────────────────────────────────────────
    Connections {
        target: Setup

        function onPhaseChanged() {
            const ph = Setup.phase;
            if (ph === Setup.AwaitingBlockers) {
                root.setFace("blocker");
                return;
            }
            if (ph === Setup.AwaitingElevation) {
                root.setFace("progress");
                return;
            }
            if (ph === Setup.Copying) {
                // The cancel confirm rides ON TOP of a live copy; don't yank
                // it away while the question is still valid.
                if (root.face !== "confirmCancel")
                    root.setFace("progress");
                return;
            }
            if (ph === Setup.Committing || ph === Setup.Finalizing
                || ph === Setup.RollingBack) {
                // The cancel window has closed; an open confirm has expired.
                root.setFace("progress");
            }
        }

        function onProbeChanged() {
            if (root.face === "recovery" && !Setup.staleJournalFound)
                root.setFace("welcome");
        }

        function onElevationDeclined() {
            root.setFace("elevation");
        }

        function onInstallFinished(ok, error) {
            if (ok) {
                settleTimer.arm();
                return;
            }
            if (error === Setup.Cancelled) {
                // Rollback already ran: everything is intact, resume choosing
                // on the face the run was committed from.
                root.setFace(root.commitFace, false);
                return;
            }
            if (error === Setup.NeedElevation)
                return; // the elevation face, via onElevationDeclined
            root.setFace("failed");
        }

        function onUninstallFinished(ok, error) {
            if (ok) {
                root.setFace("removed");
                return;
            }
            if (error === Setup.Cancelled) {
                root.setFace("remove", false);
                return;
            }
            root.setFace("failed");
        }
    }

    // Windows sends no broadcast a Quick app can bind to for the "animate
    // controls inside windows" preference; re-sample whenever we regain focus.
    onActiveChanged: {
        if (root.active)
            Tokens.refreshMotionPreference();
    }

    // Closing is an intent: faces that wrote nothing quit silently, a live
    // copy routes through the cancel confirm, the atomic tail cannot be
    // interrupted, and Done treats close as finish-without-opening.
    onClosing: function (close) {
        if (root.closeApproved)
            return;
        if (root.face === "progress" || root.face === "confirmCancel") {
            if (root.uninstall) {
                close.accepted = false; // removal is not resumable
                return;
            }
            if (Setup.phase === Setup.Copying) {
                close.accepted = false;
                if (root.face !== "confirmCancel")
                    root.setFace("confirmCancel");
                return;
            }
            if (Setup.phase === Setup.Committing || Setup.phase === Setup.Finalizing
                || Setup.phase === Setup.RollingBack) {
                close.accepted = false; // atomic
                return;
            }
            // AwaitingElevation: closing declines the whole run — just go.
            return;
        }
        if (root.face === "blocker") {
            // A dismissed gate is a cancel. Before anything is on disk the
            // window may go with it; a mid-run gate stays to show the undo.
            Setup.cancel();
            close.accepted = Setup.bytesCopied === 0;
            return;
        }
        if (root.face === "done") {
            close.accepted = false;
            root.finishWithoutOpening();
            return;
        }
        // Everything else: nothing (new) is on disk — just go.
    }

    // Esc = the safe exit, everywhere. Stand down while the language menu
    // owns the keyboard, so Esc closes it instead of navigating underneath.
    Shortcut {
        sequence: "Esc"
        enabled: !optionsFace.popupOpen
        onActivated: root.escapePressed()
    }
    Shortcut {
        sequence: "Alt+Left"
        enabled: root.face === "options"
        onActivated: root.setFace("welcome", false)
    }

    // The elevation relaunch carries every choice the first window collected,
    // so this instance commits instead of asking again: approving the UAC
    // prompt WAS the confirmation.
    Component.onCompleted: {
        if (root.uninstall) {
            root.face = "remove";
        } else if (Setup.resumeInstall) {
            root.face = "progress";
            Setup.beginInstall();
        } else if (Setup.staleJournalFound) {
            // The probe ran in the controller's constructor, before this
            // window existed — ask now, not on a signal that already fired.
            root.face = "recovery";
        }
        root.focusFace();
    }

    // ── Chrome ──────────────────────────────────────────────────────────────
    SetupTitleBar {
        id: titleBar
        window: root
        titleText: root.title
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    // ── The faces ───────────────────────────────────────────────────────────
    Item {
        id: faceHost
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.margins: Tokens.pagePadding
        anchors.topMargin: Tokens.s6

        // The Enter fallback: fires only when the focused control didn't
        // consume Return itself (a check row, the face root).
        Keys.onReturnPressed: root.verbPressed()
        Keys.onEnterPressed: root.verbPressed()

        WelcomeFace {
            id: welcomeFace
            visible: !root.uninstall && root.face === "welcome"
            width: faceHost.width
            height: faceHost.height

            onInstallRequested: root.commitInstall("welcome")
            onDowngradeRequested: root.setFace("confirmDowngrade")
            onOptionsRequested: root.setFace("options")
        }
        OptionsFace {
            id: optionsFace
            visible: !root.uninstall && root.face === "options"
            width: faceHost.width
            height: faceHost.height

            onInstallRequested: root.commitInstall("options")
            onBackRequested: root.setFace("welcome", false)
        }
        ConfirmFace {
            id: downgradeFace
            visible: root.face === "confirmDowngrade"
            width: faceHost.width
            height: faceHost.height
            eyebrowText: qsTr("Downgrade")
            heading: qsTr("Install an older version?")
            sentence: qsTr("Continuing replaces Dish %1 with %2.").arg(Setup.existingVersion).arg(Setup.appVersion)
            safeText: qsTr("Keep %1").arg(Setup.existingVersion)
            destructiveText: qsTr("Replace it")

            onSafeChosen: root.setFace("welcome", false)
            onDestructiveChosen: root.commitInstall("welcome")
        }
        RecoveryFace {
            id: recoveryFace
            visible: root.face === "recovery"
            width: faceHost.width
            height: faceHost.height

            onSkipRequested: root.setFace("welcome")
        }
        BlockerFace {
            id: blockerFace
            visible: root.face === "blocker"
            width: faceHost.width
            height: faceHost.height
            uninstallMode: root.uninstall

            onCancelRequested: Setup.cancel()
        }
        ProgressFace {
            id: progressFace
            visible: root.face === "progress"
            width: faceHost.width
            height: faceHost.height
            uninstallMode: root.uninstall

            onCancelRequested: root.setFace("confirmCancel")
        }
        ConfirmFace {
            id: cancelFace
            visible: root.face === "confirmCancel"
            width: faceHost.width
            height: faceHost.height
            eyebrowText: qsTr("Cancel")
            heading: qsTr("Stop installing?")
            sentence: qsTr("Files copied so far are removed, and this PC is left as it was.")
            safeText: qsTr("Keep going")
            destructiveText: qsTr("Stop and undo")

            onSafeChosen: root.setFace("progress", false)
            // The copy continues underneath until this lands; the rollback
            // then swaps the progress face back in via the phase change.
            onDestructiveChosen: Setup.cancel()
        }
        ElevationFace {
            id: elevationFace
            visible: root.face === "elevation"
            width: faceHost.width
            height: faceHost.height
            uninstallMode: root.uninstall

            onCancelRequested: root.setFace(root.uninstall ? "remove" : root.commitFace, false)
        }
        DoneFace {
            id: doneFace
            visible: !root.uninstall && root.face === "done"
            width: faceHost.width
            height: faceHost.height

            onFinishRequested: root.finishWithoutOpening()
        }
        FailedFace {
            id: failedFace
            visible: root.face === "failed"
            width: faceHost.width
            height: faceHost.height
            uninstallMode: root.uninstall

            onRetryRequested: root.retryInstall()
        }
        RemoveFace {
            id: removeFace
            visible: root.uninstall && root.face === "remove"
            width: faceHost.width
            height: faceHost.height

            onRemoveRequested: root.commitUninstall()
        }
        RemovedFace {
            id: removedFace
            visible: root.uninstall && root.face === "removed"
            width: faceHost.width
            height: faceHost.height
        }
    }
}
