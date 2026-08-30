// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Wizard page 4 - Binding · session. What a Moonlight host will run, or what it
// is already running that this controller joins. Shown ONLY for a Moonlight
// destination; the step is skipped entirely for anything else.
//
// A host carries ONE session for up to four controllers, so the app is a
// question only for whoever creates it. A binding that joins one is told what it
// is joining and offered no picker, because a picker would imply a choice that
// does not exist.
//
// Every state the host can be in renders here, and NONE of them blocks the
// binding: pairing is remembered trust verified late, so the session is
// attempted when the controller is used rather than when the binding is saved.
// The one exception is a host already carrying its four controllers.

// Bound: the app-row delegate reads the outer `page` id alongside its modelData.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import "../kit" as Kit

ColumnLayout {
    id: page

    property BindingDraft draft

    // The host carries its four controllers already, and releasing one is not
    // this page's job. The wizard owns the navigation.
    signal bindingsRequested()

    // ── The wizard's step contract ──────────────────────────────────────────
    // Always true. See the header: a binding is a durable intent, so no host
    // state may stand between the user and a bound controller.
    readonly property bool canAdvance: true
    readonly property string primaryLabel: qsTr("Continue ›")
    readonly property string hint: page.state === "hostFull"
                                   ? qsTr("Unbind a controller on this host to make room.")
                                   : ""

    function primaryActivated() {
        // The pick rides the draft to Review, and the remembered-host record
        // learns it when the session actually starts.
        return true;
    }

    function activated() {
        page.reload();
    }

    // ── Host state ──────────────────────────────────────────────────────────
    // One token, resolved in C++ from what a probe answered. QML localizes and
    // colours from it and never re-derives it.
    property string state: "checking"
    property var apps: []
    property string pin: ""
    // Bumped on every host move so the invokable-backed readouts re-evaluate: a
    // call is not a binding dependency.
    property int accounting: 0

    readonly property bool isMoonlight: page.draft.hostIsMoonlight && page.draft.hasDestination

    function reload() {
        if (!page.isMoonlight)
            return;
        // Re-probed on entering the step, never polled.
        App.probeMoonlightHost(page.draft.hostId);
        if (page.state !== "notPaired")
            App.refreshMoonlightApps(page.draft.hostId);
        page.refresh();
    }

    function refresh() {
        if (!page.isMoonlight)
            return;
        page.state = App.moonlightSessionState(page.draft.hostId, page.draft.slotId);
        page.accounting += 1;
    }

    function startPairing() {
        page.pin = App.moonlightPairingPin();
        App.pairMoonlightHost(page.draft.hostId, page.pin);
        page.refresh();
    }

    // Backing out has to reach the exchange, not just the screen: phase 1 parks on
    // the host until a human types the PIN.
    function cancelPairing() {
        App.cancelMoonlightPairing(page.draft.hostId);
        page.refresh();
    }

    // ASK FOR A SESSION, not just for the host's news. A link that dropped and a
    // host that ended the session both leave a binding pointing at nothing
    // running, and re-probing tells the user what they already know. This is what
    // Reconnect and Start a session actually do; a host that ended one has
    // nothing left to resume, so what comes back is a new session.
    function startSession() {
        App.connectMoonlightHost(page.draft.hostId, page.draft.appId);
        page.refresh();
    }

    // The banner covers three states and they do not recover the same way. A list
    // that could not be read is re-read; a session the host refused or could not
    // finish is asked for again.
    function retryBanner() {
        if (page.state === "appsFailed")
            page.reload();
        else
            page.startSession();
    }

    // The one-based ordinal this binding will show as. Arithmetic on two counts
    // C++ vends, never a third number it has to keep consistent with them: a host
    // that is full says so in its own state rather than promising a fifth slot.
    function controllerNumber(revision) {
        if (revision < 0)
            return 1;
        const taken = App.moonlightBoundSlotCount(page.draft.hostId);
        return Math.min(taken + 1, App.moonlightMaxControllers());
    }

    function joinedAppName(revision) {
        return revision >= 0 ? App.moonlightRunningAppName(page.draft.hostId) : "";
    }

    spacing: Tokens.s6

    Connections {
        target: App

        function onMoonlightHostsChanged() { page.refresh(); }
        function onMoonlightPairingFinished(hostId) {
            if (hostId === page.draft.hostId)
                page.refresh();
        }
        function onMoonlightAppsChanged(hostId) {
            if (hostId !== page.draft.hostId)
                return;
            page.apps = App.moonlightApps();
            page.refresh();
        }
    }

    // ── Token to copy ───────────────────────────────────────────────────────
    // Section order matches the state list, so a reader can walk the two side by
    // side. The host name is the argument almost everywhere, which is why it is
    // read once here.
    readonly property string hostName: page.draft.hostName

    function titleText() {
        switch (page.state) {
        case "notPaired":
            return qsTr("Not paired yet");
        case "pairingRefused":
            return qsTr("%1 did not accept the PIN").arg(page.hostName);
        case "unreachable":
        case "remembered":
            return qsTr("%1 is not answering").arg(page.hostName);
        case "trustLost":
            return qsTr("%1 no longer recognises this device").arg(page.hostName);
        case "hostReplaced":
            return qsTr("%1 was reset").arg(page.hostName);
        case "newSession":
            return qsTr("New session");
        case "joining":
            return page.joinedAppName(page.accounting).length > 0
                   ? qsTr("Joining %1").arg(page.joinedAppName(page.accounting))
                   : qsTr("Joining the session on %1").arg(page.hostName);
        case "hostFull":
            return qsTr("%1 is full").arg(page.hostName);
        case "busyOther":
            return qsTr("Another device is using %1").arg(page.hostName);
        case "resumeFailed":
            return qsTr("Could not rejoin the session on %1").arg(page.hostName);
        case "live":
            return qsTr("Streaming to %1").arg(page.hostName);
        case "dropped":
            return qsTr("Session on %1 ended").arg(page.hostName);
        case "endedByHost":
            return qsTr("%1 ended the session").arg(page.hostName);
        }
        return "";
    }

    function bodyText() {
        switch (page.state) {
        case "notPaired":
            return qsTr("%1 needs a one time PIN before Dish can start a session. Pair now, or add the controller and pair later.").arg(page.hostName);
        case "pairingPin":
            return qsTr("Type %1 into the Moonlight or Sunshine page on %2.").arg(page.pin).arg(page.hostName);
        case "pairingRefused":
            return qsTr("Check that the code went into the right host, then try again.");
        case "unreachable":
            return qsTr("Check that the host is switched on and on this network, then try again.");
        case "remembered":
            return qsTr("Dish remembers the pairing with %1 and will start a session when the host is back.").arg(page.hostName);
        case "trustLost":
            return qsTr("The host removed the pairing. Pair again to start a session.");
        case "hostReplaced":
            return qsTr("This host has a new identity, so the old pairing no longer works. Pair again to start a session.");
        case "newSession":
            return qsTr("This is the first controller on %1, so it picks what the host runs.").arg(page.hostName);
        case "joining":
            return qsTr("%1 is already running a session for this device. This controller joins it as controller %2.")
                     .arg(page.hostName).arg(page.controllerNumber(page.accounting));
        case "hostFull":
            return qsTr("A session carries four controllers at most, and %1 already has four. Unbind one to make room.").arg(page.hostName);
        case "busyOther":
            return qsTr("%1 is running an app for a different device and will not hand that session over. Close it to start a new one, or add the controller and try again later.").arg(page.hostName);
        case "resumeFailed":
            return qsTr("The host has a session but would not hand it back. Close the app on %1 and start a new one.").arg(page.hostName);
        case "live":
            return qsTr("%1 · controller %2 of 4")
                     .arg(page.joinedAppName(page.accounting)).arg(page.controllerNumber(page.accounting));
        case "dropped":
            return qsTr("The link dropped. Dish will rejoin the next time you use this controller.");
        case "endedByHost":
            return qsTr("The app closed on the host. Start a new session to keep using this controller.");
        }
        return "";
    }

    function spinnerText() {
        if (page.state === "pairingPin")
            return qsTr("Waiting for the host to accept the PIN…");
        if (page.state === "appsLoading")
            return qsTr("Reading the app list from %1…").arg(page.hostName);
        return qsTr("Checking %1…").arg(page.hostName);
    }

    // Amber is the problem colour, never the working one. C++ owns the rule so
    // the hosts screen and this page cannot disagree about it.
    readonly property bool problem: page.accounting >= 0
                                    && App.moonlightSessionIsProblem(page.draft.hostId,
                                                                     page.draft.slotId)

    readonly property bool spinning: page.state === "checking" || page.state === "pairingPin"
                                     || page.state === "appsLoading"
    readonly property bool banner: page.state === "appsFailed" || page.state === "refused"
                                   || page.state === "setupFailed"
    readonly property bool calloutState: page.state === "notPaired" || page.state === "pairingRefused"
                                         || page.state === "unreachable"
                                         || page.state === "remembered"
                                         || page.state === "trustLost"
                                         || page.state === "hostReplaced"
                                         || page.state === "hostFull"
                                         || page.state === "busyOther"
                                         || page.state === "resumeFailed"
                                         || page.state === "dropped"
                                         || page.state === "endedByHost"

    // ── Head ────────────────────────────────────────────────────────────────
    Kit.Eyebrow {
        mutedTone: true
        text: qsTr("Session")
    }

    Label {
        visible: page.titleText().length > 0
        text: page.titleText()
        color: Theme.onSurface
        font.pixelSize: Tokens.textStatus
        font.bold: true
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    // The body sits outside the callout for the states that are not a problem;
    // the rest carry it inside one, next to what to do about it.
    Label {
        visible: !page.calloutState && !page.banner && page.state !== "noApps"
                 && page.bodyText().length > 0
        text: page.bodyText()
        color: Theme.muted
        font.pixelSize: Tokens.textSummary
        lineHeight: 1.5
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    // ── In progress: never a bare spinner ───────────────────────────────────
    Kit.LoadingSpinner {
        visible: page.spinning
        running: page.spinning
        text: page.spinnerText()
        Layout.fillWidth: true
        Layout.topMargin: Tokens.s2
    }

    RowLayout {
        visible: page.state === "pairingPin"
        spacing: Tokens.s4
        Layout.fillWidth: true

        Kit.DishButton {
            text: qsTr("New code")
            variant: Kit.DishButton.Outline
            size: Kit.DishButton.Small
            onClicked: page.startPairing()
        }
        Kit.DishButton {
            text: qsTr("Cancel")
            variant: Kit.DishButton.Outline
            size: Kit.DishButton.Small
            onClicked: page.cancelPairing()
        }
    }

    // ── Live ────────────────────────────────────────────────────────────────
    RowLayout {
        visible: page.state === "live"
        spacing: Tokens.s4
        Layout.fillWidth: true

        Kit.LiveStat {
            live: true
            text: qsTr("streaming")
        }
        Kit.CapabilityChip {
            text: qsTr("Paired")
            tone: Kit.CapabilityChip.Ok
        }
        Item { Layout.fillWidth: true }
        Kit.DishButton {
            text: qsTr("Close the app on %1").arg(page.hostName)
            variant: Kit.DishButton.Outline
            size: Kit.DishButton.Small
            onClicked: page.quitApp()
        }
    }

    // ── Nothing to pick ─────────────────────────────────────────────────────
    Kit.EmptyState {
        visible: page.state === "noApps"
        glyph: "satellite-off"
        title: qsTr("No apps on this host")
        body: qsTr("%1 has no apps set up yet. Add one on the host, or add the controller and Dish will start whatever the host lists first.").arg(page.hostName)
        actionText: qsTr("Retry")
        showAction: true
        Layout.fillWidth: true
        Layout.topMargin: Tokens.s5

        onActionRequested: page.reload()
    }

    // ── Could not read it ───────────────────────────────────────────────────
    Kit.ErrorBanner {
        visible: page.banner
        tone: page.state === "appsFailed" ? Kit.ErrorBanner.Warning : Kit.ErrorBanner.Error
        text: page.state === "appsFailed"
              ? qsTr("Could not read the app list from %1").arg(page.hostName)
              : page.state === "setupFailed"
                ? qsTr("Could not finish the session on %1").arg(page.hostName)
                : qsTr("%1 refused the session: %2")
                    .arg(page.hostName).arg(App.moonlightRefusalMessage(page.draft.hostId))
        detail: page.state === "appsFailed"
                ? qsTr("Dish will start whatever the host lists first. Retry once %1 is reachable.").arg(page.hostName)
                : page.state === "setupFailed"
                  ? qsTr("The app started but the stream did not come up, so Dish closed it again.")
                  : qsTr("Add the controller anyway and Dish will try again the next time you use it.")
        showRetry: true
        Layout.fillWidth: true

        onRetryRequested: page.retryBanner()
    }

    // ── Needs an answer ─────────────────────────────────────────────────────
    Kit.Callout {
        visible: page.calloutState
        tone: page.problem ? Kit.Callout.Warning : Kit.Callout.Info
        text: page.bodyText()
        Layout.fillWidth: true

        Kit.DishButton {
            visible: page.state === "notPaired"
            text: qsTr("Pair now")
            variant: Kit.DishButton.Primary
            size: Kit.DishButton.Small
            onClicked: page.startPairing()
        }
        Kit.DishButton {
            visible: page.state === "trustLost" || page.state === "hostReplaced"
            text: qsTr("Pair again")
            variant: Kit.DishButton.Primary
            size: Kit.DishButton.Small
            onClicked: page.startPairing()
        }
        Kit.DishButton {
            visible: page.state === "pairingRefused"
            text: qsTr("Try again")
            variant: Kit.DishButton.Primary
            size: Kit.DishButton.Small
            onClicked: page.startPairing()
        }
        Kit.DishButton {
            visible: page.state === "busyOther" || page.state === "resumeFailed"
            text: qsTr("Close the app on %1").arg(page.hostName)
            variant: Kit.DishButton.Destructive
            size: Kit.DishButton.Small
            onClicked: page.quitApp()
        }
        Kit.DishButton {
            visible: page.state === "dropped"
            text: qsTr("Reconnect")
            variant: Kit.DishButton.Primary
            size: Kit.DishButton.Small
            onClicked: page.startSession()
        }
        Kit.DishButton {
            visible: page.state === "endedByHost"
            text: qsTr("Start a session")
            variant: Kit.DishButton.Primary
            size: Kit.DishButton.Small
            onClicked: page.startSession()
        }
        Kit.DishButton {
            visible: page.state === "unreachable" || page.state === "remembered"
                     || page.state === "busyOther" || page.state === "resumeFailed"
            text: qsTr("Retry")
            variant: Kit.DishButton.Outline
            size: Kit.DishButton.Small
            onClicked: page.reload()
        }
        Kit.DishButton {
            visible: page.state === "hostFull"
            text: qsTr("See controllers on %1").arg(page.hostName)
            variant: Kit.DishButton.Outline
            size: Kit.DishButton.Small
            onClicked: page.showBindings()
        }
    }

    // ── The pick, and only when there is one to make ────────────────────────
    Repeater {
        model: page.state === "newSession" ? page.apps : []

        delegate: Kit.SelectRow {
            id: appRow

            required property var modelData

            Layout.fillWidth: true
            selected: page.draft.appId === appRow.modelData.id
            title: appRow.modelData.title.length > 0 ? appRow.modelData.title
                                                     : appRow.modelData.id

            onPicked: page.draft.chooseApp(appRow.modelData.id, appRow.modelData.title)
        }
    }

    Label {
        visible: page.state === "newSession" && page.draft.appId.length === 0
        text: qsTr("Without a pick, Dish starts whatever %1 lists first.").arg(page.hostName)
        color: Theme.mutedStrong
        font.pixelSize: Tokens.textMeta
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    Item {
        Layout.fillHeight: true
        Layout.minimumHeight: Tokens.s5
    }

    function quitApp() {
        App.quitMoonlightApp(page.draft.hostId);
        page.refresh();
    }

    function showBindings() {
        page.bindingsRequested();
    }
}
