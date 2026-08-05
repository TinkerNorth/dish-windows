// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The installer window: frameless custom chrome exactly like the app
// (FramelessWindowChrome is installed by SetupMain's objectCreated hook), the
// 236px hero rail on the left drawing the install AS a transmission, and the
// content column — stage bar pinned, page body scrolling, footer pinned — on
// the right. All pages are always instantiated and toggled by `visible`
// (the SetupWizardPage pattern); this file owns the step state machine, the
// footer rules, every dialog, and the mapping from engine phases to steps.
// Silent mode never instantiates any of this: no QML precondition gates
// engine behavior.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

ApplicationWindow {
    id: root

    readonly property bool uninstall: Setup.mode === Setup.Uninstall

    // 760x560 is both the default AND the minimum: the content is all
    // fill-width, so growth is free — the rail keeps its fixed width and the
    // content column absorbs the rest. Maximize stays allowed (Snap Layouts).
    width: 760
    height: 560
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    title: root.uninstall ? qsTr("Uninstall Dish") : qsTr("Install Dish")

    // The C++ FramelessWindowChrome filter restores the native snap/resize/
    // shadow these flags strip; the button HINTS are load-bearing Win32 style
    // bits (Main.qml documents why).
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint
           | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
           | Qt.WindowCloseButtonHint

    // ALWAYS the themed solid, never a transparent Mica body.
    color: Theme.background

    // ── Step state ──────────────────────────────────────────────────────────
    // Install: 0 Welcome · 1 License · 2 Location · 3 Shortcuts ·
    // 4 Installing · 5 Done. Uninstall: 0 Confirm · 1 Removing · 2 Removed.
    property int step: 0
    property bool hadBlockers: false
    property bool closeApproved: false

    readonly property bool failedFace: Setup.phase === Setup.Failed

    // Engine work in flight: the footer freezes, Back stands down.
    readonly property bool working: Setup.phase === Setup.Preflight
                                    || Setup.phase === Setup.AwaitingBlockers
                                    || Setup.phase === Setup.AwaitingElevation
                                    || Setup.phase === Setup.Copying
                                    || Setup.phase === Setup.Committing
                                    || Setup.phase === Setup.Finalizing
                                    || Setup.phase === Setup.RollingBack

    readonly property var activePage: root.uninstall
        ? (root.step === 0 ? confirmPage : root.step === 1 ? removingPage : removedPage)
        : (root.step === 0 ? welcomePage
         : root.step === 1 ? licensePage
         : root.step === 2 ? locationPage
         : root.step === 3 ? shortcutsPage
         : root.step === 4 ? installingPage
         : donePage)

    readonly property bool dialogOpen: blockerDialog.visible || cancelConfirm.visible
                                       || downgradeConfirm.visible || staleConfirm.visible

    // ── Footer copy ─────────────────────────────────────────────────────────
    // Positions are translated WHOLE, never composed: word order varies by
    // language.
    readonly property string positionText: root.uninstall
        ? (root.step === 0 ? qsTr("Step 1 of 2 · Remove")
         : root.step === 1 ? qsTr("Step 2 of 2 · Removing")
         : qsTr("Step 2 of 2 · Removed"))
        : (root.step === 0 ? qsTr("Step 1 of 3 · Welcome")
         : root.step === 1 ? qsTr("Step 1 of 3 · License")
         : root.step === 2 ? qsTr("Step 2 of 3 · Location")
         : root.step === 3 ? qsTr("Step 2 of 3 · Shortcuts")
         : root.step === 4 ? qsTr("Step 3 of 3 · Installing")
         : qsTr("Step 3 of 3 · Done"))

    readonly property string hintText: {
        if (Setup.phase === Setup.AwaitingElevation)
            return qsTr("Waiting for Windows approval…");
        if (Setup.phase === Setup.Committing || Setup.phase === Setup.Finalizing
            || Setup.phase === Setup.RollingBack) {
            return root.uninstall ? qsTr("Removing…")
                                  : qsTr("Finishing up, this only takes a moment");
        }
        if (Setup.phase === Setup.Copying)
            return root.uninstall ? qsTr("Removing…") : qsTr("Copying…");
        const advice = root.activePage && root.activePage.hint ? root.activePage.hint : "";
        if (advice.length === 0)
            return root.positionText;
        return qsTr("%1 · %2").arg(root.positionText).arg(advice);
    }

    readonly property bool backEnabled: !root.uninstall && root.step >= 1 && root.step <= 3
                                        && !root.working

    readonly property bool cancelEnabled: {
        if (Setup.phase === Setup.AwaitingElevation)
            return false;
        if (root.uninstall) {
            if (root.step === 0)
                return true;
            if (root.step === 1)
                return root.failedFace; // Close after a removal failure
            return false;
        }
        if (root.step < 4)
            return true;
        if (root.step === 4)
            return Setup.phase === Setup.Copying; // commit/finalize are atomic
        return root.failedFace; // Done: nothing left to cancel unless it failed
    }

    readonly property string cancelLabel: root.failedFace
                                          && ((root.uninstall && root.step === 1)
                                              || (!root.uninstall && root.step === 5))
                                          ? qsTr("Close") : qsTr("Cancel")

    // ── The hero rail's readings ────────────────────────────────────────────
    function shortDir(path) {
        if (!path || path.length === 0)
            return "";
        const parts = path.split(/[\\/]+/).filter(function (s) { return s.length > 0; });
        if (parts.length <= 2)
            return path;
        return "…\\" + parts[parts.length - 2] + "\\" + parts[parts.length - 1];
    }

    // Same set LocationPage lets Continue through: a folder that merely has
    // files in it is an answer, so the rail must not keep saying "—" under a
    // primary the user can press.
    readonly property bool dirAnswered: Setup.dirStatus === Setup.DirOk
                                        || Setup.dirStatus === Setup.DirIsExistingInstall
                                        || Setup.dirStatus === Setup.DirNotEmpty

    readonly property string heroState: {
        if (root.uninstall) {
            if (root.step === 2)
                return "removed";
            if (root.step === 1)
                return root.failedFace ? "idle" : "removing";
            // The wire exists; this flow unmakes it.
            return "done";
        }
        if (root.step === 4) {
            if (Setup.phase === Setup.Done)
                return "done";
            if (Setup.phase === Setup.Copying || Setup.phase === Setup.Committing
                || Setup.phase === Setup.Finalizing)
                return "installing";
            // Error rule: red never enters the scene; the beam just goes idle.
            return "idle";
        }
        if (root.step === 5)
            return root.failedFace ? "idle" : "done";
        return "idle";
    }

    readonly property string heroDestination: root.uninstall
        ? root.shortDir(Setup.existingDir)
        : (root.step >= 2 && root.dirAnswered ? root.shortDir(Setup.installDir) : "")

    readonly property string heroOnDisk: root.uninstall
        ? Setup.requiredText
        : (root.step >= 2 && root.dirAnswered ? Setup.requiredText : "")

    // ── Navigation ──────────────────────────────────────────────────────────
    function goTo(n) {
        if (n === root.step)
            return;
        const forward = n > root.step;
        root.step = n;
        const page = root.activePage;
        if (page) {
            // Clean slate first, so a reduced-motion (or interrupted) entry
            // never inherits a stale offset.
            page.x = 0;
            page.opacity = 1;
            if (typeof page.activated === "function")
                page.activated();
            root.playEnter(page, forward);
        }
        root.focusStep();
    }

    function goBack() {
        if (root.backEnabled)
            root.goTo(root.step - 1);
    }

    function primaryPressed() {
        const page = root.activePage;
        if (!page || !page.canAdvance || root.working)
            return;
        // A page that handles its own primary returns false and does not
        // advance (Shortcuts hands the commit to us; Done finishes).
        if (typeof page.primaryActivated === "function" && page.primaryActivated() === false)
            return;
        if (!root.uninstall && root.step < 3)
            root.goTo(root.step + 1);
    }

    function cancelPressed() {
        if (root.uninstall) {
            if (root.step === 1) {
                if (root.failedFace)
                    Setup.quitSetup();
                return; // removal is not resumable — no mid-removal cancel
            }
            if (root.working) {
                Setup.cancel();
                return;
            }
            Setup.quitSetup();
            return;
        }
        if (root.step === 4) {
            if (Setup.phase === Setup.Copying && !cancelConfirm.visible)
                cancelConfirm.open();
            return;
        }
        if (root.step === 5) {
            if (root.failedFace)
                Setup.quitSetup();
            return;
        }
        if (root.working) {
            Setup.cancel();
            return;
        }
        // Steps that have written nothing quit without ceremony.
        Setup.quitSetup();
    }

    // M1: only the incoming side animates — the outgoing page hides
    // instantly, so there is no two-page overlap to manage.
    function playEnter(page, forward) {
        if (Tokens.reducedMotion || !page)
            return;
        enterAnim.stop();
        enterAnim.target = page;
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

    // Focus follows the step (visual spec §13): Welcome lands on the primary,
    // Installing on Cancel, every other page on its first interactive control.
    function focusStep() {
        if (!root.uninstall && root.step === 0) {
            footer.primaryButton.forceActiveFocus(Qt.TabFocusReason);
            return;
        }
        if (!root.uninstall && root.step === 4) {
            footer.cancelButton.forceActiveFocus(Qt.TabFocusReason);
            return;
        }
        const page = root.activePage;
        if (page && typeof page.focusFirst === "function" && page.focusFirst())
            return;
        if (page) {
            const it = page.nextItemInFocusChain(true);
            let walk = it;
            while (walk && walk !== page)
                walk = walk.parent;
            if (walk === page && it) {
                it.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
        footer.primaryButton.forceActiveFocus(Qt.TabFocusReason);
    }

    // M9: one clean settle — loops stop, the beam reads solid — then advance.
    // Under reduced motion the moment is skipped entirely.
    Timer {
        id: settleTimer
        interval: 2 * Tokens.durNormal
        repeat: false
        onTriggered: root.goTo(5)

        function arm() {
            if (Tokens.reducedMotion)
                root.goTo(5);
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
                root.hadBlockers = true;
                if (!blockerDialog.visible)
                    blockerDialog.open();
            } else if (blockerDialog.visible) {
                blockerDialog.close();
            }
            const running = ph === Setup.Copying || ph === Setup.Committing
                          || ph === Setup.Finalizing || ph === Setup.RollingBack;
            if (root.uninstall) {
                if (running && root.step === 0)
                    root.goTo(1);
            } else if (running && root.step === 3) {
                root.goTo(4);
            }
        }

        function onProbeChanged() {
            if (Setup.staleJournalFound && !staleConfirm.visible)
                staleConfirm.open();
        }

        function onElevationDeclined() {
            shortcutsPage.showElevationDeclined();
        }

        function onInstallFinished(ok, error) {
            if (ok) {
                settleTimer.arm();
                return;
            }
            if (error === Setup.Cancelled) {
                // Rollback already ran: everything is intact, resume choosing.
                root.goTo(3);
                return;
            }
            if (error === Setup.NeedElevation)
                return; // surfaced as the Shortcuts page's ErrorBanner
            root.goTo(5); // the Done page's failure face
        }

        function onUninstallFinished(ok, error) {
            if (ok) {
                root.goTo(2);
                return;
            }
            if (error === Setup.Cancelled)
                root.goTo(0);
            // other failures surface on the removing page itself
        }
    }

    // Windows sends no broadcast a Quick app can bind to for the "animate
    // controls inside windows" preference; re-sample whenever we regain focus.
    onActiveChanged: {
        if (root.active)
            Tokens.refreshMotionPreference();
    }

    // Closing is an intent: pages that wrote nothing quit silently, work in
    // flight routes through the cancel confirm, the commit window cannot be
    // interrupted, and Done treats close as Finish-without-launch.
    onClosing: function (close) {
        if (root.closeApproved)
            return;
        if (!root.uninstall && root.step === 4) {
            close.accepted = false;
            if (Setup.phase === Setup.Copying && !cancelConfirm.visible)
                cancelConfirm.open();
            return;
        }
        if (root.uninstall && root.step === 1 && !root.failedFace) {
            close.accepted = false; // removal is not resumable
            return;
        }
        if (!root.uninstall && root.step === 5 && !root.failedFace) {
            close.accepted = false;
            root.closeApproved = true;
            Setup.finishOnly();
            return;
        }
        // Everything else: nothing (new) is on disk — just go.
    }

    // Esc = Cancel (through the confirm where destructive); Alt+Left = Back.
    // Both stand down while a dialog owns the keyboard.
    Shortcut {
        sequence: "Esc"
        enabled: !root.dialogOpen
        onActivated: root.cancelPressed()
    }
    Shortcut {
        sequence: "Alt+Left"
        enabled: !root.dialogOpen
        onActivated: root.goBack()
    }

    Component.onCompleted: root.focusStep()

    // ── Chrome ──────────────────────────────────────────────────────────────
    SetupTitleBar {
        id: titleBar
        window: root
        titleText: root.title
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    SetupHeroRail {
        id: heroRail
        anchors.left: parent.left
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        width: Tokens.railExpanded
        sceneState: root.heroState
        versionText: Setup.appVersion
        destinationText: root.heroDestination
        onDiskText: root.heroOnDisk
        // Only reachable at extreme text scale; facts move, they never vanish.
        compact: root.height < root.minimumHeight * 0.8
    }

    // ── Content column ──────────────────────────────────────────────────────
    Kit.Page {
        id: content
        scrollable: false
        anchors.left: heroRail.right
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom

        ColumnLayout {
            width: parent.width
            height: parent.height
            spacing: Tokens.s6

            SetupStageBar {
                id: stageBar
                // A three-marker bar over a one-question flow is decoration:
                // the uninstaller carries its place in the footer instead.
                visible: !root.uninstall
                stage: root.step <= 1 ? 1 : root.step <= 3 ? 2 : 3
                subStep: root.step % 2
                locked: root.step >= 4
                Layout.fillWidth: true

                onStageClicked: function (stage) {
                    const target = stage === 1 ? 0 : 2;
                    if (root.step < 4 && target < root.step)
                        root.goTo(target);
                }
            }

            ScrollView {
                id: bodyScroll
                clip: true
                contentWidth: availableWidth
                Layout.fillWidth: true
                Layout.fillHeight: true

                Item {
                    id: stepHost
                    width: bodyScroll.availableWidth
                    // The viewport, unless the step outgrows it — then it
                    // scrolls (200% text scale).
                    height: Math.max(bodyScroll.availableHeight,
                                     root.activePage ? root.activePage.implicitHeight : 0)

                    WelcomePage {
                        id: welcomePage
                        visible: !root.uninstall && root.step === 0
                        width: stepHost.width
                        height: stepHost.height
                    }
                    LicensePage {
                        id: licensePage
                        visible: !root.uninstall && root.step === 1
                        width: stepHost.width
                        height: stepHost.height
                    }
                    LocationPage {
                        id: locationPage
                        visible: !root.uninstall && root.step === 2
                        width: stepHost.width
                        height: stepHost.height
                    }
                    ShortcutsPage {
                        id: shortcutsPage
                        visible: !root.uninstall && root.step === 3
                        width: stepHost.width
                        height: stepHost.height

                        onInstallRequested: {
                            // An older-over-newer install is destructive:
                            // explicit confirm first, silent never gets here.
                            if (Setup.isDowngrade)
                                downgradeConfirm.open();
                            else
                                Setup.beginInstall();
                        }
                    }
                    InstallingPage {
                        id: installingPage
                        visible: !root.uninstall && root.step === 4
                        width: stepHost.width
                        height: stepHost.height
                        hadBlockers: root.hadBlockers
                    }
                    DonePage {
                        id: donePage
                        visible: !root.uninstall && root.step === 5
                        width: stepHost.width
                        height: stepHost.height

                        onRetryRequested: {
                            Setup.retry();
                            root.goTo(3);
                        }
                        onFinishRequested: {
                            root.closeApproved = true;
                            if (Setup.wantLaunch)
                                Setup.finishAndLaunch();
                            else
                                Setup.finishOnly();
                        }
                    }

                    UninstallConfirmPage {
                        id: confirmPage
                        visible: root.uninstall && root.step === 0
                        width: stepHost.width
                        height: stepHost.height

                        onRemoveRequested: Setup.beginUninstall()
                    }
                    UninstallProgressPage {
                        id: removingPage
                        visible: root.uninstall && root.step === 1
                        width: stepHost.width
                        height: stepHost.height
                        hadBlockers: root.hadBlockers
                    }
                    UninstallDonePage {
                        id: removedPage
                        visible: root.uninstall && root.step === 2
                        width: stepHost.width
                        height: stepHost.height
                    }
                }
            }

            SetupFooter {
                id: footer
                Layout.fillWidth: true
                backEnabled: root.backEnabled
                cancelEnabled: root.cancelEnabled
                cancelText: root.cancelLabel
                hintText: root.hintText
                primaryLabel: root.activePage ? root.activePage.primaryLabel : ""
                primaryEnabled: root.activePage
                                ? root.activePage.canAdvance && !root.working : false
                primaryDestructive: root.uninstall && root.step === 0

                onBackClicked: root.goBack()
                onCancelClicked: root.cancelPressed()
                onPrimaryClicked: root.primaryPressed()
            }
        }
    }

    // ── Dialogs (centred over the WHOLE window, rail included) ──────────────
    BlockerCloseDialog {
        id: blockerDialog
        uninstallMode: root.uninstall
    }

    Kit.ConfirmDialog {
        id: cancelConfirm
        eyebrow: qsTr("Cancel")
        heading: qsTr("Stop installing?")
        bodyText: qsTr("Files copied so far are removed, and this PC is left as it was.")
        acceptText: qsTr("Stop")
        rejectText: qsTr("Keep going")
        destructiveAccept: true

        onAccepted: {
            cancelConfirm.close();
            Setup.cancel();
        }
    }

    Kit.ConfirmDialog {
        id: downgradeConfirm
        eyebrow: qsTr("Downgrade")
        heading: qsTr("Install an older version?")
        bodyText: qsTr("This PC has Dish %1, newer than this installer (%2). Continuing replaces it with the older version.").arg(Setup.existingVersion).arg(Setup.appVersion)
        acceptText: qsTr("Replace it")
        rejectText: qsTr("Cancel")
        destructiveAccept: true

        onAccepted: {
            downgradeConfirm.close();
            Setup.beginInstall();
        }
    }

    Kit.ConfirmDialog {
        id: staleConfirm
        eyebrow: qsTr("Recovery")
        heading: qsTr("Finish cleaning up?")
        bodyText: qsTr("A previous setup attempt was interrupted and left recovery files behind. Clean them up before continuing.")
        acceptText: qsTr("Clean up")
        rejectText: qsTr("Not now")

        onAccepted: {
            staleConfirm.close();
            Setup.cleanStaleJournal();
        }
    }
}
