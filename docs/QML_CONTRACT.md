# QML Contract (frozen — A2)

This is the FROZEN exposure contract the Qt Quick UI binds against. Page agents
code ONLY against this document. Do not re-read the C++; if something you need is
missing, flag it rather than reaching past this surface.

A2 (the flows redesign) extends A1 additively — every A1 binding keeps working.
The additions are listed in **§7 A2 addendum** at the end: shell-header
primitives (counts + keep-awake pill), the collapsible-rail preference, the
light-bar setting, two new slot roles, and the `Tokens` metrics singleton
beside `Theme`. The two index-based invokables A1 still listed as DEPRECATED
(`connectByIndex`, index-based `pairWithPin`) were REMOVED from the C++ in R14
and are gone from this doc.

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
| `pairingServerId` | `string` | `stateChanged` | Stable `id` of the pairing target (empty when `!pairingActive`). Pass it into the pairing sheet so BOTH paths work for a parked target: the forward submit resolves on it and the reverse PIN auto-send has a destination. Capture it (with the name) BEFORE `clearPairingTarget()`. |
| `slotModel` | `SlotListModel*` | (CONSTANT) | The controllers/slots model — see §2. Bind into a `ListView.model`. |
| `connectionModel` | `ConnectionListModel*` | (CONSTANT) | The connection-rows model — see §3. |
| `themeMode` | `int` (read/write) | `themeModeChanged` | Appearance mode, `0=Light 1=Dark 2=System` (the SettingsPage chip order). Writing forwards to `ThemePreferenceStore::setMode`; the live app + native chrome re-theme. See §1b. |
| `crashReportingEnabled` | `bool` (read/write) | `crashReportingChanged` | Crash-reporting opt-out flag (default ON). Writing forwards to `CrashReportingStore::setEnabled`. |
| `appVersion` | `string` | (CONSTANT) | The build version (the CMake project `VERSION`). |
| `onboardingNeeded` | `bool` | `onboardingNeededChanged` | `!OnboardingPreferenceStore::welcomeCompleted()`. Main.qml pushes the onboarding flow on it; flips false after `markOnboardingComplete()`. |
| `donateSponsorsUrl` | `string` | (CONSTANT) | GitHub Sponsors URL (brand default; localizable in C++). |
| `donateKofiUrl` | `string` | (CONSTANT) | Ko-fi URL. |
| `donateBmacUrl` | `string` | (CONSTANT) | Buy Me a Coffee URL. |
| `discoveredServers` | `list` | `discoveredChanged` | The FOUND list (reactive). Same JS objects the `discoveredServers()` invokable returns: `{ name, ip, udpPort, pairPort, httpPort, machineId, source, id }`. **One-spot rule**: excludes any server whose stable `id` already has a `connectionModel` row (remembered ∪ live) — that row is the box's one spot; FOUND is only the un-remembered rest. Bind a `Repeater.model` to it and it re-evaluates as a scan lands or a pair/forget moves a box between the lists — no manual re-pull. |
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
| `discoveredChanged` | — | The FOUND list moved: the discovered scan changed (folds `WifiConnectionManager::discoveredChanged`) OR the connection-row id set changed (a pair landed / a forget dropped a row — those ids are excluded from FOUND, so it re-reads then too). NOTIFY for the `discoveredServers` property — bind to that property and it re-evaluates here automatically (the legacy `discoveredServers()` invokable still works, re-pulled on this edge). |
| `scanningChanged` | — | The `scanning` flag flipped (a scan started or finished; folds `WifiConnectionManager::scanningChanged`). NOTIFY for the `scanning` property. |
| `reversePairingChanged` | — | A reverse-pairing transition (phase / PIN / server name moved; folds `WifiConnectionManager::reversePairingChanged`). NOTIFY for all three `reversePairing*` properties. |
| `themeModeChanged` | — | `themeMode` moved (the store republished). |
| `crashReportingChanged` | — | `crashReportingEnabled` moved. |
| `onboardingNeededChanged` | — | `onboardingNeeded` flipped. |
| `deadzonesChanged` | — | The deadzone device rows / their seeded values moved (a device attach/detach, or a `setDeadzones`/`setMotionEnabled` landed). Re-pull `deadzoneDevices()`. |
| `pairingSucceeded` | — | One-shot: a connection reached `Connected` after a pair (the online-count rising edge). The pairing sheet may close on it. Best-effort. |
| `rawInputCaptured` | `string slotId, int kind, int index, int value` | A raw joystick input was observed for the slot currently capturing (after `startInputCapture`). `kind` `0=axis 1=button 2=hat`; `index` the raw source index; `value` the axis int16 / `1` for a button / the `SDL_HAT_*` bitmask for a hat. Pass `slotId`/`kind`/`index` straight into `assignSlotInput` to bind the output the page is editing. Fires ONLY for the capturing slot — other devices' inputs are filtered out. |

### Invokable methods

| Method | Args | Effect |
|---|---|---|
| `bindSlot(slotId, connectionId)` | `string, string` | Bind a slot to a connection (the SlotCard bind menu). |
| `unbindSlot(slotId)` | `string` | Unbind the slot. |
| `setSlotPath(slotId, choice)` | `string, string` | Force the slot's USB input path: `"standard"` (SDL/XInput owns the pad), `"direct"` (raw-HID claim), or `"auto"` (clear the override; the resolution policy decides). Resolves `slotId` → `(vid, pid)` (a synthetic slot's id IS the packed vpKey string; an SDL slot's identity comes from the bridge device list) and forwards to `UsbGamepadManager::setPathChoice` / `clearChoice`. No-op when the slot has no resolvable identity or no USB manager is wired. The slot roles (`pathPhase`/`desiredPath`/…) refresh on the next `stateChanged` — no new NOTIFY. Gate the control on the `pathSupported` role. |
| `availableConnectionsForSlot(slotId)` | `string` → `list` | The connections this slot may bind to, for the bind chooser, as JS objects `{ connectionId:string, label:string, dotColor:string, glyph:string }` (`dotColor`/`glyph` are the same tokens §3 exposes). Computed via the SAME pure `reducer::connectionsVisibleInPicker` the Widgets SlotCard uses: connections bound to ANOTHER slot are EXCLUDED, live-available unbound ones are offered, and the slot's OWN current binding is held over even when offline. Read it **one-shot when the chooser opens** (like `emulateTypes`) — no NOTIFY. Do NOT bind the chooser to the unfiltered `connectionModel`, and gate the Bind button on this list being non-empty (or the slot being bound), not the total `connectionModel.count`. |
| `refreshEmulate(slotId)` | `string` | Kick a best-effort catalog refresh for the slot's bound satellite. Call before reading `emulateTypes` so a later open shows fresh types. |
| `emulateTypes(slotId)` | `string` → `list` | Offerable controller types as JS objects: `{ type:int, slug:string, name:string, shortName:string, description:string, known:bool }`. Empty if the slot is unbound or no catalog cached yet. |
| `emulateCurrentType(slotId)` | `string` → `int` | The wire type id to pre-select in the picker (user override → hardware class → Xbox). |
| `setControllerType(slotId, type)` | `string, int` | Apply the Emulate choice and re-attach the slot so the new descriptor is PUT. |
| `startDiscovery()` | — | Begin a satellite discovery scan (Connections page "Scan"). |
| `isScanning()` | → `bool` | Whether a scan is in flight. Prefer the reactive `scanning` property for bindings. |
| `discoveredServers()` | → `list` | The FOUND list as JS objects: `{ name:string, ip:string, udpPort:int, pairPort:int, httpPort:int, machineId:string, source:string, id:string }`. `source` is the discovery-source label ("UDP broadcast" / "mDNS" / "mDNS + broadcast"). One-spot rule: ids that already have a `connectionModel` row are excluded (see the property row in §1). Prefer the reactive `discoveredServers` PROPERTY for bindings; this invokable is kept for explicit re-pulls. |
| `connectByServerId(serverId)` | `string` | Connect to the discovered server with that stable `id` (de-raced; resolves out of the live list — no-op if not found). Pass the `id` field from `discoveredServers`. |
| `reconnectConnection(connectionId)` | `string` | Reconnect a REMEMBERED satellite by id WITHOUT a rescan requirement and WITHOUT re-pairing (the key persists). If the id is in the current scan, connects the fresh endpoint; else kicks a discovery relearn AND attempts the last-known endpoint now. Gate the button on the row NOT being `liveLink`. |
| `disconnectConnection(connectionId)` | `string` | Graceful disconnect of a LIVE session WITHOUT forgetting — the remembered row + pairing key survive (contrast `forgetConnection`). Gate the button on the row's `liveLink` role. |
| `forgetConnection(connectionId)` | `string` | Forget a remembered connection (unbinds its slots, drops key/pin/row). |
| `pairByServerId(serverId, pin)` | `string, string` | Submit a 6-digit PIN for the discovered server with that stable `id` (de-raced; resolves out of the live list). Watch `App.errorMessage` for failure and `isPairingInFlight` for the spinner. |
| `isPairingInFlight(serverId)` | `string` → `bool` | Whether a `POST /api/pair` is in flight for that server id (use the `id` field from `discoveredServers`). Drives the Pair button's spinner+disabled state. |
| `clearPairingTarget()` | — | Drop the one-shot pairing trigger (call before opening the pairing sheet to avoid re-entry). |
| `requestReversePairing(serverId)` | `string` | Start HOST-INITIATED (reverse) pairing for the discovered server with that stable `id` (de-raced; resolves out of the live list — no-op if not found). The dish generates + shows a 4-digit PIN (`reversePairingPin`); the operator types it on the satellite. Watch `reversePairingPhase` for the outcome and `App.errorMessage` for the decline/timeout reason. A second call cancels the first. |
| `cancelReversePairing()` | — | Abort an in-flight reverse pair (stops the poll, returns `reversePairingPhase` to `"idle"` and clears the PIN/server name). Safe to call when idle. |
| `setThemeMode(mode)` | `int` | Apply an appearance mode (`0=Light 1=Dark 2=System`). Forwards to `ThemePreferenceStore`; re-themes the live QML palette + the native chrome immersive-dark attribute. |
| `setCrashReportingEnabled(on)` | `bool` | Forward to `CrashReportingStore::setEnabled`. |
| `deadzoneDevices()` | → `list` | Per-device deadzone rows as JS objects: `{ id:string, name:string, hasGyro:bool, stickFlat:int, triggerFlat:int, forwardMotion:bool }`. Re-pull on `deadzonesChanged`. |
| `setDeadzones(deviceId, stickFlat, triggerFlat)` | `string, int, int` | Persist the per-device override (`DeadzoneRepository`) AND push it into the live processor (`AppModel::applyDeadzones`) — the exact pair the Widgets view does. |
| `setMotionEnabled(deviceId, on)` | `string, bool` | Forward to `MotionEnabledStore::setEnabled`, keyed by the device id. |
| `slotRemap(slotId)` | `string` → `map` | The slot's EFFECTIVE raw-joystick remap as a JS object (stored override → else the default layout). Resolves `slotId` → `(vid,pid)` (same resolver `setSlotPath` uses); returns `{}` for a slot with no resolvable identity (e.g. an SDL-recognised game controller or USB-direct synthetic — those don't take a raw-joystick remap). Shape: `{ a, b, x, y, dpadUp, dpadDown, dpadLeft, dpadRight, leftShoulder, rightShoulder, back, start, leftThumb, rightThumb : int (raw source-button index, -1 = unassigned), leftStickX, leftStickY, rightStickX, rightStickY, hatIndex : int (raw source/hat index, -1 = none), leftTrigger, rightTrigger : { kind:"axis"\|"button", index:int }, invertLeftY, invertRightY : bool }`. Re-pull after an `assignSlotInput`/`setSlotInvert`/`resetSlotRemap`. |
| `assignSlotInput(slotId, target, kind, index)` | `string, string, int, int` | Apply a CAPTURE result to the slot's remap via the pure `withAssignment` helper and persist it (the store pushes into the live bridge, so it takes effect on the next report — no re-attach). `target` is the logical output being assigned, one of: `"a"`/`"b"`/`"x"`/`"y"`/`"dpadUp"`/`"dpadDown"`/`"dpadLeft"`/`"dpadRight"`/`"leftShoulder"`/`"rightShoulder"`/`"back"`/`"start"`/`"leftThumb"`/`"rightThumb"`/`"leftStickX"`/`"leftStickY"`/`"rightStickX"`/`"rightStickY"`/`"leftTrigger"`/`"rightTrigger"`. `kind` is the captured input kind `0=axis 1=button 2=hat` and `index` the raw source index — pass through the `kind`/`index` from `rawInputCaptured` verbatim. A trigger target tags its source kind from `kind` (axis-capture → analogue axis, button-capture → digital full-scale-on-press). A `hat`-kind capture to a dpad target routes the dpad to that hat; a `button`-kind capture routes that direction to the button. No-op for an unknown `target` (forward-compat) or a slot with no resolvable identity. |
| `setSlotInvert(slotId, which, on)` | `string, string, bool` | Flip a stick Y-invert flag and persist (→ live). `which` is `"leftY"` or `"rightY"`. No-op for an unknown flag or an unresolvable slot. |
| `resetSlotRemap(slotId)` | `string` | Drop the slot's stored remap override (revert to the default DirectInput layout) and clear it in the live bridge. No-op for an unresolvable slot. |
| `startInputCapture(slotId)` | `string` | Arm the bridge's input-capture mode and remember `slotId` as the capturing slot. While active, raw inputs from THAT slot's device arrive on `rawInputCaptured`; assign one with `assignSlotInput`. Calling it again for another slot re-points the filter. An axis only fires on a deliberate move (idle jitter is rejected); buttons on press; hats on a non-centered direction — so a resting pad never self-assigns. |
| `stopInputCapture()` | — | Disarm capture and clear the capturing slot. Call when the user finishes assigning (or leaves the page). Safe to call when idle. |
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
* **SetupGuideDialog (the setup wizard)** — step 1 IS the live connect flow
  (the android guided-setup connection step, ported): `App.startDiscovery` on
  open, `App.scanning` / `App.discoveredServers` / `App.foundCount` drive the
  host list, each row opens the shared `PairingDialog`, and the step
  auto-advances on `pairingSucceeded` gated by the wizard's own pending host
  (android's "never advance on a background reconnect" rule). Step 2 lists
  `App.slotModel` rows live; step 3 summarizes `App.onlineCount` /
  `App.slotCount`.

### Configure-controls (raw-joystick remap) page

For a generic pad whose DirectInput button order is scrambled, the page corrects
the routing per `(vid,pid)`. Bind it like:

* On open: `let r = App.slotRemap(slotId)` to render the current routing (which
  raw source each output reads + the invert toggles). If `r` is `{}` the slot
  has no raw-joystick remap (an SDL game controller / USB-direct synthetic) —
  hide the page or show a "not remappable" note.
* To (re)assign an output: call `App.startInputCapture(slotId)`, prompt the user
  to press the input, and on `rawInputCaptured(slotId, kind, index, value)` call
  `App.assignSlotInput(slotId, target, kind, index)` for the output being edited.
  Then `App.stopInputCapture()` and re-pull `App.slotRemap(slotId)`.
* Invert toggles: `App.setSlotInvert(slotId, "leftY"|"rightY", on)`.
* "Reset to defaults": `App.resetSlotRemap(slotId)`.

All four persist AND push into the live bridge, so the change takes effect on the
next report with no re-attach. ALWAYS call `stopInputCapture()` when leaving the
page so capture doesn't keep streaming.

### The pairing sheet: both paths at once

Forward pairing (`pairByServerId`) is the operator-reads-the-PIN-off-the-dish
flow. The REVERSE flow is the inverse: the dish SHOWS a PIN and the operator
approves on the satellite. The sheet runs BOTH simultaneously (android
`PairPinDialog` parity) — no tap gates either path:

* On open: `App.cancelReversePairing()` (a stale phase from an earlier sheet
  must not leak in), then `App.requestReversePairing(server.id)` immediately —
  the operator is notified the moment the sheet opens. The 6-digit PIN field
  stays typeable throughout as the live fallback; its submit is
  `pairByServerId` as before.
* Show the reverse block only while `App.reversePairingPhase !== "idle"` (an
  unresolvable id leaves the phase idle, degrading to forward-only — e.g. a
  parked target whose satellite has left the scan).
* While `App.reversePairingPhase === "awaiting"`: show `App.reversePairingPin`
  (the 4 digits) and `App.reversePairingServerName`, with a spinner.
* On `"approved"`: close the sheet (the session is opening — the connection row
  will go live). On `"declined"` / `"timedout"`: show the reason (also arrives on
  `App.errorMessage`) and offer a "New code" retry.
* The sheet's Cancel calls `App.cancelReversePairing()`; whichever path
  completes first pairs the box.

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
| `remappable` | `bool` | Slot is a RAW-joystick SDL pad whose DirectInput routing the "Configure controls" page may remap (the `mapJoystick` path). `false` for synthetics (USB-direct), the virtual slot, and SDL-recognised game controllers (they use SDL's own mapping and ignore a remap). Gate the "Configure controls" entry on this — and `slotRemap(slotId)` returns `{}` for a non-remappable slot. |
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
| `pathPhase` | `string` | USB-path FSM phase token: `"routed"` / `"claiming"` / `"direct"` / `"awaitingFramework"` / `"restoreStuck"` / `"needsReplug"`. |
| `desiredPath` | `string` | Resolved desired path the toggle reads as selected: `"standard"` / `"direct"`. (`"auto"` is a `setSlotPath` INPUT only — it resolves to one of these, so it never appears here.) |
| `pathSupported` | `bool` | The device is a raw-HID-claimable controller (a `UsbController` exists for it). Show the Standard/Direct/Auto control ONLY when true — an Xbox/XInput pad has none and hides it. |
| `claimInProgress` | `bool` | `pathPhase == "claiming"` — a Direct claim is in flight. Disable the toggle + show a spinner while true. |
| `directFailure` | `string` | Last Direct-claim failure reason token (`"permissionDenied"` / `"busy"` / `"initFailed"` / `"dropped"`), or `""` when none. Drives the inline note (together with the `needsReplug` / `restoreStuck` phases). |

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
| `latencyText` | `string` | Pre-formatted one-way latency, e.g. `"~3.4 ms"` (median heartbeat-RTT/2 over a sliding 64-ping window, ~1 Hz refresh). `""` until a live session has RTT samples. |
| `latencySamples` | `int` | RTT samples currently in the window (0–64). Gate the latency caption on `linkState === "connected" && latencySamples > 0` and show the count beside the figure (`"~3.4 ms · last 64 pings"`). |

> `connectionModel` carries the REMEMBERED/derived rows. The FOUND list of
> not-yet-remembered discovered servers comes from the `App.discoveredServers`
> property (reactive) / `App.discoveredServers()` invokable. The two lists are
> disjoint by construction (the one-spot rule): an id with a row here never
> appears in `discoveredServers` — its reachability shows as this row's chip
> (`ready`/`online`/…) instead of a duplicate FOUND row.

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
    onClicked: App.pairByServerId(App.discoveredServers()[0].id, pinField.text)
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

---

## 7. A2 addendum — the flows redesign surface

Additive over A1. Everything below exists and is wired; page agents bind these
exactly like the A1 surface.

### 7.1 New `App` properties

| Property | Type | NOTIFY | Meaning |
|---|---|---|---|
| `slotCount` | `int` | `stateChanged` | Rows in `slotModel` (mirrored so bindings never poke the model for a count). |
| `boundSlotCount` | `int` | `stateChanged` | Slots currently bound to a connection — drives the "· nothing bound" suffix. |
| `firstOnlineName` | `string` | `stateChanged` | Label of the first `Connected` connection ("Living-room Satellite online · …"); empty when none. |
| `foundCount` | `int` | `discoveredChanged` | Size of the FOUND list ("2 found · nothing remembered yet") — after the one-spot exclusion, so it counts only un-remembered boxes, matching the rows the FOUND card renders. |
| `keepAwakeActive` | `bool` | `stateChanged` | True while the display-sleep inhibitor is held — render the header pill `STREAMING · DISPLAY KEPT AWAKE`. |
| `railCollapsed` | `bool` (RW) | `railCollapsedChanged` | The nav rail's persisted collapse state (48px icons vs 236px labels). The title-bar hamburger calls `App.setRailCollapsed(!App.railCollapsed)`. |
| `lightbarFollowGame` | `bool` (RW) | `lightbarChanged` | Light-bar forwarding preference (design combo: true = "Follow game", false = "Off"). Settings page binds `App.setLightbarFollowGame(x)`. |

Header sub-lines are ASSEMBLED IN QML from these primitives (each page owns its
wording via `qsTr`):
- Controllers: no connections → "No connections yet — pair a Satellite to get
  started" (dot muted); one online, nothing bound → `firstOnlineName` + " online
  · nothing bound" (dot primary); else `onlineCount of connectionCount online`
  (+ " · nothing bound" when `boundSlotCount === 0`; dot success).
- Connections: nothing remembered → `foundCount` + " found · nothing remembered
  yet" (dot muted); else `onlineCount` streaming · `connectionCount` remembered
  (dot success when any online, warning when none).

### 7.2 New `slotModel` roles

| Role | Type | Meaning |
|---|---|---|
| `emulateName` | `string` | Resolved emulation type short name for the bound sub-line's "· as DualShock 4" suffix. Empty → omit the suffix. |
| `registering` | `bool` | Attach in flight — render the busy card (glyph + "Registering controller…" + indeterminate bar) instead of chips/actions. |

### 7.3 `Tokens` singleton (`import Dish.Chrome`)

Non-color design tokens beside `Theme`: type scale (`textStatus 17, textTitle
20, textHeading 16, textBase 13, textSummary 12, textMeta 11, textChip 10`),
`sectionLetterSpacing 1.5`, `monoFamily` (platform monospace), spacing `s1..s9 =
2,4,6,8,10,12,14,16,20` + `pagePadding 24`, radii (`radiusChip 5, radiusButton
6, radiusCard 8, radiusBar 2`), shell metrics (`titleBarHeight 44,
captionButtonWidth 46, railCompact 48, railExpanded 236, navItemHeight 40,
hitRow 44, dotSize 8`), `disabledOpacity 0.4`. Never hard-code these numbers in
pages.

### 7.4 `Theme` singleton additions

`primaryDark`, `onPrimary`, and the derived washes `primaryHover`,
`primaryPress`, `primaryFill`, `warningFill` (all NOTIFY `paletteChanged`).
Text on a filled accent control is ALWAYS `Theme.onPrimary` (not
`Theme.background`).

### 7.5 Kit inventory for the redesign

New: `Eyebrow`, `SegmentedControl` (options/value/small/busy + picked),
`ComboButton` (options/value + picked), `SliderRow` (label/value/maxValue +
committed), `RadioMark` (selected), `RowButton` (title/subtitle + clicked),
`CapabilityChip` (text/present/low), `LiveStat` (text/live),
`DishProgressBar` (indeterminate/value).
Restyled to the ds spec (API unchanged unless noted): `KitButton` (primary
fill), `OutlineButton` (accent border), `Card` (radius 8, 12/14 inset),
`SectionHeader` (now a Row with optional `glyph`; accent mono), `KitTextField`,
`LabeledSwitch`, `EmptyState` (+ `glyph` property), `ContentDialog` (+ `eyebrow`,
`preferredWidth`; empty `acceptText`/`rejectText` hides that button),
`NotificationToastHost` (+ "warning" severity).
