# QML Contract (frozen — A1)

This is the FROZEN exposure contract the Qt Quick UI binds against. Page agents
code ONLY against this document. Do not re-read the C++; if something you need is
missing, flag it rather than reaching past this surface.

The whole surface is reached through one context property, **`App`** (an
`AppViewModel`), registered by `QmlEntryPoint::runQmlApp` on the root context. It
wraps the existing `dish::AppModel` and its stores/composers; it adds NO new
behavior — every property is a re-projection and every method forwards verbatim.

Two `QML_IMPORT`-free list models are vended through `App`; their concrete types
are also registered uncreatable under `import Dish.Chrome 1.0` as
`SlotListModel` / `ConnectionListModel` (you generally never name them — you bind
`App.slotModel` / `App.connectionModel` directly into a `ListView.model`).

The theme palette is the separate `Theme` singleton from A0 (`import Dish.Chrome`)
— unchanged; use it for colors. This doc does not re-list it.

---

## 1. `App` — top-level singleton (context property)

Access: `App` (already in every QML scope; no import needed).
Type: `dish::qml::AppViewModel` (a `QObject`, context property, app-lifetime).

### Properties

| Property | Type | NOTIFY | Meaning |
|---|---|---|---|
| `statusText` | `string` | `stateChanged` | Dashboard headline ("No connections yet" / "<n> remembered" / the single live server label / "<n> online"). Mirrors the Widgets header verbatim. |
| `summaryText` | `string` | `stateChanged` | Header sub-line ("Tap Manage to add one" / "<n> remembered" / "<n> of <m> online"). |
| `onlineCount` | `int` | `stateChanged` | Number of connections whose link is `Connected`. |
| `connectionCount` | `int` | `stateChanged` | Total remembered+live connections (dashboard `ConnectionSummary` count). |
| `busy` | `bool` | `stateChanged` | True while a controller is registering — drives the indeterminate dashboard spinner. |
| `eventsPerSec` | `int` | `telemetryChanged` | Input events/s sampled ~1 Hz from the processor. |
| `sendsPerSec` | `int` | `telemetryChanged` | Wire sends/s, same ~1 Hz sample. |
| `totalSent` | `qulonglong` | `telemetryChanged` | Cumulative reports sent since launch. |
| `pairingActive` | `bool` | `stateChanged` | True when the model has parked a pairing target (open the pairing sheet on the rising edge; call `clearPairingTarget()` before showing it). |
| `pairingServerName` | `string` | `stateChanged` | Display name of the pairing target (empty when `!pairingActive`). |
| `slotModel` | `SlotListModel*` | (CONSTANT) | The controllers/slots model — see §2. Bind into a `ListView.model`. |
| `connectionModel` | `ConnectionListModel*` | (CONSTANT) | The connection-rows model — see §3. |

> Note: the property is `slotModel`, NOT `slots` — `slots` is the reserved
> `Q_SLOTS` token and moc strips it.

### Signals

| Signal | Args | Meaning |
|---|---|---|
| `stateChanged` | — | Header / slot / pairing state moved (folds AppModel's `stateChanged`). |
| `telemetryChanged` | — | Telemetry footer numbers moved (~1 Hz). |
| `errorMessage` | `string message` | Transient one-shot error — surface it as a toast. |

### Invokable methods

| Method | Args | Effect |
|---|---|---|
| `bindSlot(slotId, connectionId)` | `string, string` | Bind a slot to a connection (the SlotCard bind menu). |
| `unbindSlot(slotId)` | `string` | Unbind the slot. |
| `refreshEmulate(slotId)` | `string` | Kick a best-effort catalog refresh for the slot's bound satellite. Call before reading `emulateTypes` so a later open shows fresh types. |
| `emulateTypes(slotId)` | `string` → `list` | Offerable controller types as JS objects: `{ type:int, slug:string, name:string, shortName:string, description:string, known:bool }`. Empty if the slot is unbound or no catalog cached yet. |
| `emulateCurrentType(slotId)` | `string` → `int` | The wire type id to pre-select in the picker (user override → hardware class → Xbox). |
| `setControllerType(slotId, type)` | `string, int` | Apply the Emulate choice and re-attach the slot so the new descriptor is PUT. |
| `startDiscovery()` | — | Begin a satellite discovery scan (Connections page "Scan"). |
| `isScanning()` | → `bool` | Whether a scan is in flight. |
| `discoveredServers()` | → `list` | The FOUND list as JS objects: `{ name:string, ip:string, udpPort:int, pairPort:int, httpPort:int, machineId:string, id:string }`. Index into this for `connectByIndex` / `pairWithPin`. |
| `connectByIndex(discoveredIndex)` | `int` | Connect to the discovered server at that index (no-op if out of range). |
| `forgetConnection(connectionId)` | `string` | Forget a remembered connection (unbinds its slots, drops key/pin/row). |
| `pairWithPin(discoveredIndex, pin)` | `int, string` | Submit a 6-digit PIN for the discovered server at that index. Watch `App.errorMessage` for failure and `isPairingInFlight` for the spinner. |
| `isPairingInFlight(serverId)` | `string` → `bool` | Whether a `POST /api/pair` is in flight for that server id (use the `id` field from `discoveredServers()`). Drives the Pair button's spinner+disabled state. |
| `clearPairingTarget()` | — | Drop the one-shot pairing trigger (call before opening the pairing sheet to avoid re-entry). |

---

## 2. `SlotListModel` — `App.slotModel`

A `QAbstractListModel`, one row per controller slot. A delegate reproduces the
Widgets `SlotCard`. The model is a thin mapping over `AppModel`'s slot list; it
emits minimal `rowsInserted`/`rowsRemoved` on a count change and `dataChanged`
for surviving rows on an in-place update (e.g. a 1 Hz Hz nudge), so a `ListView`
never resets on a quiet telemetry tick.

### Roles

| Role name | Type | Meaning |
|---|---|---|
| `slotId` | `string` | Stable slot id (pass to `bindSlot`/`unbindSlot`/`refreshEmulate`/`setControllerType`). |
| `name` | `string` | Display name ("DualSense", "Xbox Pad"). |
| `bound` | `bool` | Slot is bound to a connection. |
| `boundConnectionId` | `string` | The bound connection id ("" when unbound). |
| `boundLabel` | `string` | Bound server label for "Bound to <x>" ("" when unbound). |
| `live` | `bool` | Bound session is `Connected` (green dot iff true). |
| `dotColor` | `string` | Status-dot token: `"success"` / `"warning"` / `"muted"`. Resolve to a `Theme` color. |
| `usbDirect` | `bool` | Slot is a USB-direct (raw-HID) synthetic. |
| `hasMotion` | `bool` | Hardware has a gyro/accelerometer (drives the Gyro/No-gyro chip). |
| `hasLightbar` | `bool` | Hardware has an RGB LED (show the Lightbar chip ONLY when true). |
| `batteryLevel` | `int` | 0..100 percent, or `255` (0xFF) = unknown. |
| `batteryStatus` | `int` | Wire status: 2=charging, 3=full, 4=wired (others=discharging/unknown). |
| `batteryKnown` | `bool` | `batteryLevel != 255` — show the battery chip only when true. |
| `gamepadHz` | `int` | Report-rate value for the gamepad chip. |
| `gamepadHzLive` | `bool` | The gamepad Hz is a live measurement (USB-direct) vs. a "~peak" estimate. |
| `gamepadHzShown` | `bool` | Whether to show the gamepad-rate chip at all. |
| `motionHz` | `int` | IMU sample-rate value. |
| `motionHzShown` | `bool` | Whether to show the motion-rate chip (only for motion-capable pads with a reading). |
| `pollHz` | `int` | Measured USB-direct poll rate (URB completion rate). |
| `pollHzShown` | `bool` | Whether to show the poll-rate chip (USB-direct pads with a reading). |

> The `*Shown` / `*Live` booleans come from the SAME pure `SlotLiveStats` mapper
> the Widgets card uses, so the two UIs never disagree about which chip renders.
> Format Hz strings yourself: `gamepadHzLive ? "<n> Hz" : "~<n> Hz"`.

---

## 3. `ConnectionListModel` — `App.connectionModel`

A `QAbstractListModel`, one row per derived connection (the Connections page;
mirrors `ConnectionsDialog` rows). Same minimal-signal behavior as §2.

### Roles

| Role name | Type | Meaning |
|---|---|---|
| `connectionId` | `string` | Stable connection id (pass to `forgetConnection`). |
| `label` | `string` | Server name (or ip when the name is empty). |
| `ip` | `string` | IPv4 address. |
| `udpPort` | `int` | UDP stream port (compose detail "<ip> • UDP <port>"). |
| `linkState` | `string` | One of `"found"`/`"stale"`/`"saved"`/`"ready"`/`"connecting"`/`"connected"`/`"unstable"`. |
| `chip` | `string` | Status-chip key: `"found"`/`"needsPairing"`/`"offline"`/`"ready"`/`"connecting"`/`"online"`/`"unstable"`. Localize the chip text yourself. |
| `dotColor` | `string` | `"success"`/`"primary"`/`"warning"`/`"muted"` — resolve to a `Theme` color. |
| `glyph` | `string` | `"satelliteBase"`/`"satelliteConnected"`/`"satelliteOff"` — pick the brand glyph variant. |
| `boundSlotId` | `string` | Slot id bound to this connection ("" when unbound). |
| `liveLink` | `bool` | Link is actively streaming (`Connected` or `Unstable`). |

> `connectionModel` carries the REMEMBERED/derived rows. The FOUND list of
> not-yet-remembered discovered servers comes from `App.discoveredServers()`.

---

## 4. Binding examples

### Read a streaming telemetry value
```qml
Label {
    text: qsTr("events/s %1   sends/s %2").arg(App.eventsPerSec).arg(App.sendsPerSec)
    color: Theme.muted
}
// `telemetryChanged` fires ~1 Hz; the binding above re-evaluates automatically.
```

### Iterate the controllers model in a ListView
```qml
ListView {
    anchors.fill: parent
    model: App.slotModel
    spacing: 8
    delegate: ItemDelegate {
        width: ListView.view.width
        contentItem: RowLayout {
            Rectangle {                       // status dot
                width: 8; height: 8; radius: 4
                color: dotColor === "success" ? Theme.success
                     : dotColor === "warning" ? Theme.warning : Theme.muted
            }
            ColumnLayout {
                Label { text: name; color: Theme.onSurface }
                Label {
                    text: bound ? qsTr("Bound to %1").arg(boundLabel) : qsTr("Unbound")
                    color: Theme.muted
                }
                RowLayout {
                    visible: gamepadHzShown
                    Label {
                        text: gamepadHzLive ? qsTr("%1 Hz").arg(gamepadHz)
                                            : qsTr("~%1 Hz").arg(gamepadHz)
                    }
                }
            }
            Button {
                text: bound ? qsTr("Unbind") : qsTr("Bind…")
                onClicked: bound ? App.unbindSlot(slotId)
                                 : App.bindSlot(slotId, /* chosen connectionId */ "")
            }
        }
    }
}
```

### Trigger a pairing / emulate action
```qml
// Pairing: submit a PIN for the first discovered server.
TextField { id: pinField; maximumLength: 6 }
Button {
    text: qsTr("Pair")
    enabled: !App.isPairingInFlight(App.discoveredServers()[0].id)
    onClicked: App.pairWithPin(0, pinField.text)
}
Connections {
    target: App
    function onErrorMessage(message) { /* show a toast; keep the sheet open */ }
}

// Emulate: open a type picker for a bound slot.
function openEmulate(slotId) {
    App.refreshEmulate(slotId);
    let types = App.emulateTypes(slotId);     // [{type,slug,name,shortName,description,known}, ...]
    let current = App.emulateCurrentType(slotId);
    // ... build a picker from `types`, pre-select `current` ...
    // on accept:
    App.setControllerType(slotId, chosenType);
}
```
