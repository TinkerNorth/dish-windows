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
| `themeMode` | `int` (read/write) | `themeModeChanged` | Appearance mode, `0=Light 1=Dark 2=System` (the SettingsPage chip order). Writing forwards to `ThemePreferenceStore::setMode`; the live app + native chrome re-theme. See §1b. |
| `crashReportingEnabled` | `bool` (read/write) | `crashReportingChanged` | Crash-reporting opt-out flag (default ON). Writing forwards to `CrashReportingStore::setEnabled`. |
| `appVersion` | `string` | (CONSTANT) | The build version (the CMake project `VERSION`). |
| `onboardingNeeded` | `bool` | `onboardingNeededChanged` | `!OnboardingPreferenceStore::welcomeCompleted()`. Main.qml pushes the onboarding flow on it; flips false after `markOnboardingComplete()`. |
| `donateSponsorsUrl` | `string` | (CONSTANT) | GitHub Sponsors URL (brand default; localizable in C++). |
| `donateKofiUrl` | `string` | (CONSTANT) | Ko-fi URL. |
| `donateBmacUrl` | `string` | (CONSTANT) | Buy Me a Coffee URL. |
| `discoveredServers` | `list` | `discoveredChanged` | The FOUND list (reactive). Same JS objects the `discoveredServers()` invokable returns: `{ name, ip, udpPort, pairPort, httpPort, machineId, source, id }`. Bind a `Repeater.model` to it and it re-evaluates as a scan lands — no manual re-pull. |
| `scanning` | `bool` | `scanningChanged` | Whether a discovery scan is in flight (reactive). Flips true on `startDiscovery()` and false on completion; gate the Scan button / "Scanning…" label on it. |
| `reversePairingPhase` | `string` | `reversePairingChanged` | The host-initiated (reverse) pairing phase: `"idle"` (none in flight) / `"awaiting"` (PIN shown, waiting for the operator to approve on the satellite) / `"approved"` (operator approved — the session is opening) / `"declined"` (operator denied) / `"timedout"` (no approval inside the ~2-min budget). The sheet switches on this. |
| `reversePairingPin` | `string` | `reversePairingChanged` | The 4-digit PIN to display while `awaiting` (the operator types it on the satellite). Stays set on the terminal arms so the sheet can keep showing it; cleared on the next `requestReversePairing`/`cancelReversePairing`. |
| `reversePairingServerName` | `string` | `reversePairingChanged` | Display name of the server being reverse-paired (its `name`, or `ip` when unnamed). Empty when `idle`. |

> Note: the property is `slotModel`, NOT `slots` — `slots` is the reserved
> `Q_SLOTS` token and moc strips it.

### Signals

| Signal | Args | Meaning |
|---|---|---|
| `stateChanged` | — | Header / slot / pairing state moved (folds AppModel's `stateChanged`). |
| `telemetryChanged` | — | Telemetry footer numbers moved (~1 Hz). |
| `errorMessage` | `string message` | Transient one-shot error — surface it as a toast. |
| `discoveredChanged` | — | The discovered-servers list moved (folds `WifiConnectionManager::discoveredChanged`). NOTIFY for the `discoveredServers` property — bind to that property and it re-evaluates here automatically (the legacy `discoveredServers()` invokable still works, re-pulled on this edge). |
| `scanningChanged` | — | The `scanning` flag flipped (a scan started or finished; folds `WifiConnectionManager::scanningChanged`). NOTIFY for the `scanning` property. |
| `reversePairingChanged` | — | A reverse-pairing transition (phase / PIN / server name moved; folds `WifiConnectionManager::reversePairingChanged`). NOTIFY for all three `reversePairing*` properties. |
| `themeModeChanged` | — | `themeMode` moved (the store republished). |
| `crashReportingChanged` | — | `crashReportingEnabled` moved. |
| `onboardingNeededChanged` | — | `onboardingNeeded` flipped. |
| `deadzonesChanged` | — | The deadzone device rows / their seeded values moved (a device attach/detach, or a `setDeadzones`/`setMotionEnabled` landed). Re-pull `deadzoneDevices()`. |
| `pairingSucceeded` | — | One-shot: a connection reached `Connected` after a pair (the online-count rising edge). The pairing sheet may close on it. Best-effort. |

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
| `isScanning()` | → `bool` | Whether a scan is in flight. Prefer the reactive `scanning` property for bindings. |
| `discoveredServers()` | → `list` | The FOUND list as JS objects: `{ name:string, ip:string, udpPort:int, pairPort:int, httpPort:int, machineId:string, source:string, id:string }`. `source` is the discovery-source label ("UDP broadcast" / "mDNS" / "mDNS + broadcast"). Prefer the reactive `discoveredServers` PROPERTY for bindings; this invokable is kept for explicit re-pulls. |
| `connectByServerId(serverId)` | `string` | Connect to the discovered server with that stable `id` (de-raced; resolves out of the live list — no-op if not found). Pass the `id` field from `discoveredServers`. **Prefer this over `connectByIndex`.** |
| `connectByIndex(discoveredIndex)` | `int` | **DEPRECATED** (racy if the list reorders between read and call). Connect to the discovered server at that index (no-op if out of range). Use `connectByServerId`. |
| `reconnectConnection(connectionId)` | `string` | Reconnect a REMEMBERED satellite by id WITHOUT a rescan requirement and WITHOUT re-pairing (the key persists). If the id is in the current scan, connects the fresh endpoint; else kicks a discovery relearn AND attempts the last-known endpoint now. Gate the button on the row NOT being `liveLink`. |
| `disconnectConnection(connectionId)` | `string` | Graceful disconnect of a LIVE session WITHOUT forgetting — the remembered row + pairing key survive (contrast `forgetConnection`). Gate the button on the row's `liveLink` role. |
| `forgetConnection(connectionId)` | `string` | Forget a remembered connection (unbinds its slots, drops key/pin/row). |
| `pairByServerId(serverId, pin)` | `string, string` | Submit a 6-digit PIN for the discovered server with that stable `id` (de-raced; resolves out of the live list). **Prefer this over `pairWithPin`.** Watch `App.errorMessage` for failure and `isPairingInFlight` for the spinner. |
| `pairWithPin(discoveredIndex, pin)` | `int, string` | **DEPRECATED** (racy if the list reorders). Submit a 6-digit PIN for the discovered server at that index. Use `pairByServerId`. |
| `isPairingInFlight(serverId)` | `string` → `bool` | Whether a `POST /api/pair` is in flight for that server id (use the `id` field from `discoveredServers`). Drives the Pair button's spinner+disabled state. |
| `clearPairingTarget()` | — | Drop the one-shot pairing trigger (call before opening the pairing sheet to avoid re-entry). |
| `requestReversePairing(serverId)` | `string` | Start HOST-INITIATED (reverse) pairing for the discovered server with that stable `id` (de-raced; resolves out of the live list — no-op if not found). The dish generates + shows a 4-digit PIN (`reversePairingPin`); the operator types it on the satellite. Watch `reversePairingPhase` for the outcome and `App.errorMessage` for the decline/timeout reason. A second call cancels the first. |
| `cancelReversePairing()` | — | Abort an in-flight reverse pair (stops the poll, returns `reversePairingPhase` to `"idle"` and clears the PIN/server name). Safe to call when idle. |
| `setThemeMode(mode)` | `int` | Apply an appearance mode (`0=Light 1=Dark 2=System`). Forwards to `ThemePreferenceStore`; re-themes the live QML palette + the native chrome immersive-dark attribute. |
| `setCrashReportingEnabled(on)` | `bool` | Forward to `CrashReportingStore::setEnabled`. |
| `deadzoneDevices()` | → `list` | Per-device deadzone rows as JS objects: `{ id:string, name:string, hasGyro:bool, stickFlat:int, triggerFlat:int, forwardMotion:bool }`. Re-pull on `deadzonesChanged`. |
| `setDeadzones(deviceId, stickFlat, triggerFlat)` | `string, int, int` | Persist the per-device override (`DeadzoneRepository`) AND push it into the live processor (`AppModel::applyDeadzones`) — the exact pair the Widgets view does. |
| `setMotionEnabled(deviceId, on)` | `string, bool` | Forward to `MotionEnabledStore::setEnabled`, keyed by the device id. |
| `licenses()` | → `list` | The bundled third-party manifest as JS objects: `{ name:string, version:string, license:string, url:string }` (unnamed entries dropped). |
| `markOnboardingComplete()` | — | Persist the welcome-completed flag (`OnboardingPreferenceStore::markWelcomeCompleted`). `onboardingNeeded` then flips false. |
| `openExternalUrl(url)` | `string` | Open a URL via the shared `ExternalLink` path; a failure raises `errorMessage` (the QML toast channel), matching the Widgets warning. NOT a raw `Qt.openUrlExternally`. |

---

## 1b. Settings / About / Onboarding surfaces (A-ext)

These extend §1 over the EXISTING, already-tested stores — `AppViewModel` only
re-projects/forwards (no new behaviour). The pages bind them directly:

* **SettingsPage** — `App.themeMode` / `App.setThemeMode` (Appearance chips),
  `App.crashReportingEnabled` / `App.setCrashReportingEnabled` (Diagnostics),
  `App.appVersion` (About).
* **DeadzoneSettingsPage** — `App.deadzoneDevices()` rows (re-pulled on
  `deadzonesChanged`), `App.setDeadzones`, `App.setMotionEnabled`.
* **LicensesPage** — `App.licenses()` rows, `App.openExternalUrl`.
* **DonatePage** — `App.donateSponsorsUrl` / `-KofiUrl` / `-BmacUrl`,
  `App.openExternalUrl`.
* **Main.qml first-run** — if `App.onboardingNeeded`, push
  `onboarding/OnboardingFlow.qml`; on its `completed()`, pop and call
  `App.markOnboardingComplete()`.

### Reverse (host-initiated) pairing sheet

Forward pairing (`pairByServerId`) is the operator-reads-the-PIN-off-the-dish
flow. The REVERSE flow is the inverse: the dish SHOWS a PIN and the operator
approves on the satellite. Bind a sheet like:

* On the user's "Pair this way" tap: `App.requestReversePairing(server.id)`,
  then open the sheet.
* While `App.reversePairingPhase === "awaiting"`: show `App.reversePairingPin`
  (the 4 digits) and `App.reversePairingServerName`, with a spinner.
* On `"approved"`: close the sheet (the session is opening — the connection row
  will go live). On `"declined"` / `"timedout"`: show the reason (also arrives on
  `App.errorMessage`) and offer retry.
* The sheet's Cancel calls `App.cancelReversePairing()`.

The poll budget is ~2 minutes; a momentary network blip during the wait does NOT
abort (it keeps polling until the deadline).

### Dark-mode / Mica wiring

The app DEFAULTS to its deep-space DARK palette: `QmlEntryPoint` pins
`Appearance::Dark` when the theme store sits at its System default (so a fresh
launch is dark rather than following the OS to light), and the native chrome
applies immersive-dark by default. `setThemeMode` forwards to the store (the
`ThemeController` swaps the active `dish::ui::Theme` palette + re-applies the
global QSS off its Observable), then pushes the resolved appearance to the QML
side: the `Theme` singleton (`ThemeBridge`) is `refresh()`-ed — its tokens are
now `NOTIFY paletteChanged` (was `CONSTANT`) so bindings re-read live — and the
chrome's `DWMWA_USE_IMMERSIVE_DARK_MODE` is flipped to match, so the frame never
drifts light while the body re-darks.

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
| `liveLink` | `bool` | Link is actively streaming (`Connected` or `Unstable`). **Gates the per-row Disconnect/Reconnect buttons**: enable `disconnectConnection(connectionId)` only when `liveLink`; enable `reconnectConnection(connectionId)` only when NOT `liveLink`. |

> `connectionModel` carries the REMEMBERED/derived rows. The FOUND list of
> not-yet-remembered discovered servers comes from the `App.discoveredServers`
> property (reactive) / `App.discoveredServers()` invokable.

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
