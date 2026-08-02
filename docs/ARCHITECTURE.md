# dish-windows — Architecture Guide

This is the decision document for how dish-windows is structured: the layers, the
patterns, and — above all — **how state flows**. It is the Windows analogue of
the dish-android architecture guide. Read it before adding a feature; it tells you
which primitive to reach for and where the code goes so that the **core stays
swappable from the UI** and **every state (loading, error, empty, success) is
captured and bindable**.

> TL;DR — Data flows **one way**: a *source of truth* owns state → *pure
> reducers/composers* derive from it → the *UI binds* and renders it → the *UI
> emits intents* (commands) back to the source. The UI never owns domain state and
> the core never imports Qt Quick. If you find yourself reaching for a `bool busy`
> or a fire-and-forget toast to represent the result of an async operation, stop:
> that is an [`AsyncState<T>`](#async-state) or a [reducer FSM](#reducer-fsm).

---

## 1. The prime directive: core ⟂ UI

The app is split so that **you could delete `src/qml/` and write a completely
different front-end (a CLI, a tray applet, a different toolkit) without touching a
single file under `src/core/`, `src/source/`, `src/repository/`, or
`src/composer/`.** That is the test for every change. Concretely:

- **Core layers are Qt-Quick-free and mostly Qt-free.** `src/core/` is *fully*
  Qt-free (it compiles and unit-tests on the host with no Qt/SDL/sodium). The
  source/repository/composer layers may use `QObject`/`QString`/`QSettings` for IO
  and value types, but they **never** import a QML type, a widget, or `tr()` on a
  user-facing string — localization happens at the UI edge (we carry *render keys*
  and *reason codes as data*, not pre-localized text).
- **The UI is a projection.** `src/qml/` (and the legacy `src/UI/` Widgets path)
  are thin adapters: they map already-derived state onto bindings and forward user
  intents verbatim. They hold *view* state (which page is open, which row is
  expanded) but never *domain* state.

```
        ┌─────────────────────────────────────────────────────────────┐
        │  UI  (src/qml, src/UI)   — projection + intents, swappable    │
        │     binds state ▲                         │ emits commands     │
        └─────────────────┼─────────────────────────┼───────────────────┘
                          │                         ▼
        ┌─────────────────┴─────────────────────────────────────────────┐
        │  composer/   Composers (pure derive) · Controllers (effects)   │
        │              · Coordinators (imperative command surface)       │
        └─────────────────┬─────────────────────────┬───────────────────┘
              reads Observables                  drives
        ┌─────────────────┴───────────┐ ┌───────────┴───────────────────┐
        │  source/  StateSources +     │ │  repository/  durable storage  │
        │           Gateways (IO edge) │ │  (QSettings, dumb + sync)       │
        └─────────────────┬───────────┘ └────────────────────────────────┘
                          │ uses
        ┌─────────────────┴─────────────────────────────────────────────┐
        │  core/   PURE: reducers (FSMs + mappers), models, AsyncState,   │
        │          wire/crypto, input math.   No Qt. Host-testable.       │
        └─────────────────┬─────────────────────────────────────────────┘
                          │ built on
        ┌─────────────────┴─────────────────────────────────────────────┐
        │  architecture/  the UDF kernel: Observable · StateSource ·      │
        │                 Composer/Combiner · Controller · Repository     │
        └─────────────────────────────────────────────────────────────────┘
```

**Dependency rule:** arrows point *up*. A lower layer never includes a higher one.
`core/` knows nothing of `source/`; `source/` knows nothing of `composer/`;
nothing below `qml/` knows `qml/` exists. The composition root
([`AppModel`](../src/AppModel.h)) is the *only* place allowed to wire concrete
instances of every layer together.

---

## 2. The folder map (where things live)

| Folder | Role | May depend on | Qt? |
|---|---|---|---|
| `src/architecture/` | The UDF kernel (generic primitives) | nothing | **No** |
| `src/core/` | Pure domain: reducers/FSMs, mappers, models, `AsyncState`, crypto, input math | `architecture/` | **No** |
| `src/source/` | `StateSource`s (owned reactive state) + Gateways (native IO boundary) | `core/`, `repository/` | yes (IO) |
| `src/repository/` | Durable keyed storage (QSettings), dumb + synchronous | `core/` | yes (QSettings) |
| `src/composer/` | `Composer`s (pure derive), `Controller`s (side effects), Coordinators (command services) | `core/`, `source/`, `repository/` | yes (QObject) |
| `src/qml/` | QML bridge: `AppViewModel` facade + role models + chrome | everything | yes (Quick) |
| `src/UI/` | The design-token palette (`Theme`) + the font-family probes (`FontStacks`) + crash handling. No widgets remain (see [§9](#legacy)) | `core/` | yes (Gui) |
| `src/Input/`, `src/Network/` | Older capitalized homes for the SDL bridge / socket layer (IO; being folded into the layer model over time) | `core/` | yes |

When you add code, the folder is the contract: *if it has no Qt and makes a
decision, it belongs in `core/`.* If it owns a socket/timer/cache, it is a
`source/` `StateSource` or a Gateway. If it persists, it is a `repository/`. If it
purely derives one value from others, it is a `composer/` Composer. If it performs
a side effect off a state, it is a `composer/` Controller.

---

## 3. The kernel primitives — pick the right one

All five live in [`src/architecture/`](../src/architecture/README.md) and are pure
header-only C++17 (the C++ ports of dish-android's `StateFlow` /
`AbstractStateSource` / `AbstractComposer` / `AbstractController` / `Repository`).

| You have… | Use | Notes |
|---|---|---|
| a value that changes over time, observed by others | `Observable<S>` | hot, always has a value, **distinct-until-changed** via `operator==`; `subscribe()` replays the latest; RAII `Subscription` |
| state owned from a socket / timer / cache / setting | `StateSource<S>` | expose `state()` read-only; mutate via protected `setState` |
| one value **purely derived** from other Observables | `Composer<Out, Ins...>` | one pure `transform`, no IO/events; recomputes eagerly on any upstream change |
| a side effect driven by a state | `Controller<S>` | implement `apply()`; `start()` is idempotent; choose `stop()` teardown |
| durable keyed storage | `Repository<K,V>` | dumb, synchronous, thread-safe; wrap in a Source for reactive reads; passes `RepositoryContract` |

Conventions that are *classes you write per feature*, not base classes:

- **Reducer / Mapper** — a pure free function in `core/` for a `(state,event) →
  result` decision or a domain→UI shape. The unit of testability. See
  [§5](#reducer-fsm).
- **Coordinator** — a plain `QObject` service for imperative cross-cutting commands
  over several sources; it **re-exposes a child's `Observable` by reference, never
  mirrors it** (see [`ConnectionCoordinator`](../src/composer/ConnectionCoordinator.h)).
- **Gateway** — an IO/native boundary with **no domain state** (e.g.
  [`WinHidGateway`](../src/source/usb/WinHidGateway.h), `MdnsDiscovery`).

---

## 4. <a name="async-state"></a>The state-capture doctrine, part 1: `AsyncState<T>`

**Every asynchronous operation has four states, and the UI must be able to bind
all four.** Before, the app modelled them inconsistently — a `bool busy`, a
`QSet<id> inFlight`, an `std::optional<T>` that silently dropped failures, a
one-shot `errorMessage()` toast. The result: the UI could not show a spinner,
could not say *why* something failed, and could not tell *empty* from *still
loading*.

[`core/AsyncState.h`](../src/core/AsyncState.h) is the canonical container:

```cpp
template <class T, class E = std::string>
struct AsyncState {
    AsyncPhase phase;            // Idle | Loading | Success | Error
    std::optional<T> data;       // last good value, RETAINED across refresh/error
    std::optional<E> error;      // populated iff Error — a typed reason, not a string
    bool stale;                  // data carried from a prior success (revalidating / served-on-error)
};
```

with pure transitions: `asyncIdle()`, `toLoading(prev)`, `toLoadingFresh()`,
`toSuccess(prev, value)`, `toRevalidated(prev)` (HTTP 304), `toError(prev,
reason)`. It is `==`-comparable so it drops straight into an
`Observable<AsyncState<T>>` and gets distinct-until-changed for free.

**Two properties make it the right tool:**

1. **Stale-while-revalidate is built in.** `toLoading`/`toError` *keep* the prior
   `data` and flag it `stale`. A refresh shows a spinner *over* the last-known
   content; a failed refresh shows that content *with* an error chip — never a
   blank screen.
2. **Failure carries a typed reason.** `E` defaults to `std::string` but you should
   prefer an enum (`CatalogError`, `PairFailure`), so the UI localizes a *code*,
   matching the "render keys, not strings" rule.

**Exemplar:** the catalog fetch. [`SatelliteCatalogRepository`](../src/source/http/SatelliteCatalogRepository.h)
now delivers `AsyncState<CatalogDto, CatalogError>`: a fresh 200 → `Success`, a 304
→ `Success(stale)`, an unreachable/5xx/malformed reply → `Error(reason)` **still
carrying the cached catalog**. [`AppModel::refreshCatalogForSlot`](../src/AppModel.cpp)
flips it to `Loading` before the GET and stores the terminal state, so the Emulate
picker binds a spinner / a typed error / an empty-vs-content distinction instead of
the old "the fetch silently returned nothing".

**When to use `AsyncState<T>`:** a request/response with a clear in-flight →
terminal shape — catalog fetch, discovery scan, a one-shot connect attempt, a
forward-pairing submit. **When *not* to:** a multi-state domain lifecycle with more
than four states or domain-specific terminals (a live session that goes
Linking→Live→Faltering→Stale→Reconnecting). That is a reducer FSM, below.

---

## 5. <a name="reducer-fsm"></a>The state-capture doctrine, part 2: the reducer FSM

For a lifecycle richer than Idle/Loading/Success/Error, model it as a **pure,
total reducer FSM** — the pattern the USB input-path switching uses, and the bar
every stateful subsystem is held to.

**The gold standard:** [`core/reducer/UsbPathMachine.h`](../src/core/reducer/UsbPathMachine.h).

The shape:

```cpp
// state + event  →  next state + effects (as DATA, not performed here)
struct Reduction { std::optional<UsbController> next; std::vector<UsbEffect> effects; };
Reduction reduce(const UsbController& c, const UsbEvent& event);
```

The rules that make it robust:

1. **Pure & total.** `reduce` performs no IO and is defined for *every* (phase ×
   event) pair — a weird/failed transition can never silently drop state the way
   scattered imperative `if`s could. It is `std::variant` events in, `std::variant`
   effects out.
2. **Effects are returned as data.** `reduce` decides *what* should happen
   (`Claim`, `Release`, `Notify{reason}`, `MarkRestoreStuck`); the **coordinator**
   ([`UsbGamepadManager`](../src/source/usb/UsbGamepadManager.h)) turns world signals
   into events, runs `reduce`, and *executes* the effects against real subsystems.
   Decisions are testable without hardware; IO lives in one place.
3. **One unidirectional notify.** After *every* state-changing event the coordinator
   fires a single "state moved" signal; the UI rebuilds from the fresh snapshot.
   (This is the literal fix in commit `f8b9f4b`: granular per-effect callbacks
   missed held-synthetic phase changes, so the slot list never rebuilt. One signal
   off the FSM cured a whole class of "stuck toggle" bugs.)
4. **Loading & error are *derived state*, not events.** "Switching" is a pure
   predicate over the phase (`slotPathSwitching` → the `claimInProgress` role);
   failure is a phase (`RestoreStuck`/`NeedsReplug`) + a reason (`DirectClaimFailure`)
   carried *on the controller* and stamped onto the slot. The transient "what just
   happened" banner (`UsbNotice`) is the *only* thing routed to a toast.
5. **Exhaustively unit-tested.** One assertion per (phase × event); see
   `test_usb_path_machine.cpp` (+ edge cases). Because `reduce` is pure, the whole
   decision space is pinned with zero mocks.

The same pattern is now applied to **forward pairing**
([`core/reducer/PairingMachine.h`](../src/core/reducer/PairingMachine.h)):
`Idle → Submitting → Succeeded | Failed(reason)`, consuming the existing pure
`classifyPair` verdict, with `Succeeded` reached *only* on a real
`SessionConfirmedLive` event (killing the old "infer success from a rising online
count" heuristic).

### AsyncState vs FSM — how to choose

| | `AsyncState<T>` | Reducer FSM |
|---|---|---|
| Shape | request → in-flight → one terminal | many domain states + transitions |
| States | exactly Idle/Loading/Success/Error | bespoke (e.g. Routed/Claiming/Direct/AwaitingFramework/…) |
| Effects | the caller performs them inline | returned as data, executed by a coordinator |
| Examples | catalog fetch, discovery scan, connect attempt | USB path switch, pairing, **(planned)** satellite session |

---

## 6. Unidirectional flow — the rules

1. **One owner per piece of state.** It lives in exactly one `StateSource`/FSM and
   is exposed read-only. Nobody else stores a copy. (A Coordinator
   *re-exposes* `composer.state()` by reference; it does not mirror it — mirroring
   is a two-writer race.)
2. **Derive, don't mutate.** Downstream values are produced by a pure `Composer`
   from upstream Observables, recomputed on change. Do **not** hand-patch a derived
   field in place from an event handler.
3. **Effects as data, executed at the edge.** A reducer returns *what* to do; a
   coordinator/Controller does it. Keeps decisions pure and IO in one place.
4. **One notify, then rebuild.** State change → single signal → UI rebuilds from
   the snapshot. No granular per-field back-channels that can miss a transition.
5. **Distinct-until-changed everywhere.** Every state value is `==`-comparable so a
   no-op emit is suppressed (no UI thrash). This is why `AsyncState`, every
   composer row, and every FSM state implement `operator==`.
6. **Commands flow the other way, explicitly.** The UI calls an intent
   (`bindSlot`, `pairByServerId`, `setSlotPath`); that turns into an event/effect;
   the resulting state flows back out. The UI never writes state directly.

---

## 7. <a name="ui-contract"></a>The UI binding contract: bind *all* the states

A screen that can load, fail, or be empty **must render all four**. The reusable
kit pieces (in [`src/qml/kit/`](../src/qml/kit/)) exist so every page does this the
same way:

| State | Kit component | Source |
|---|---|---|
| Loading / in-flight | `LoadingSpinner` | `AsyncState.isLoading()` / a `*InProgress` role |
| Error / failure | `ErrorBanner` (message + optional Retry) | `AsyncState.isError()` + typed reason → localized string |
| Empty | `EmptyState` (title + body + optional action) | `Success && data.empty()` |
| Success / content | the real content | `AsyncState.data` |
| Transient event | `NotificationToastHost` (one host at the shell) | `App.errorMessage` and other one-shots |

Doctrine for QML pages:

- **Never represent a result as the absence of content.** "No rows" must
  distinguish *loading* (spinner) from *empty* (EmptyState) from *failed*
  (ErrorBanner) — bind the phase, not just the list length.
- **One toast host, at the shell.** `NotificationToastHost` is dropped once in
  `AppShell`/`Main.qml` and listens to `App.errorMessage`; every transient failure
  (connect, external-link open, USB notice) surfaces through it. Pages do not
  hand-roll their own toasts.
- **Bind reactive properties, not polling getters.** Expose a `Q_PROPERTY` with a
  `NOTIFY` (folded off the owning Observable/signal); do not make QML call a
  point-in-time `isFooInFlight()` and hope a broad signal re-evaluates it.
- **The exemplar:** [`ControllersPage.qml`](../src/qml/pages/ControllersPage.qml) +
  [`SlotListModel`](../src/qml/SlotListModel.h) — `claimInProgress` drives the
  spinner + disabled toggle, the phase drives the error note. Match it.

`AppViewModel` is the seam: it maps the C++ state onto `Q_PROPERTY`/role models and
forwards intents verbatim. It must stay **thin** — no derivation, no edge-detection,
no domain logic. (Derivation that creeps in here belongs in a `core/` mapper or a
`composer/` Composer so both UIs share it and can't drift.)

---

## 8. Threading & the hot path (the deliberate exception)

The UDF kernel notifies on the thread that called `set`; when binding from a
background thread, marshal to the GUI loop (`Qt::QueuedConnection` /
`QMetaObject::invokeMethod`).

**The input hot path is intentionally NOT routed through the kernel.** The
`input → decode → encode → sendto` path (the SDL bridge / USB read loop →
`GamepadInputProcessor::publish` → the per-slot sender) is plain, allocation-free
C++ with the routing tables in [`AppModel`](../src/AppModel.h)
(`routing_`/`motionRouting_`/…) as the single mutex-guarded cross-thread seam. Do
not add Observables, `tr()`, or QML to it. Everything *around* the hot path —
device hotplug, capability, capture mode, path switching — **is** modelled state.

---

## 9. <a name="legacy"></a>One front-end: the Qt Quick flows app

The Qt Quick app (`src/qml/`, entry `runQmlApp`, `QGuiApplication`) is THE app.
The parallel Widgets UI served as the migration's reference and fallback until
the flows redesign reached feature+UX parity, then was deleted for the release
(no `DISH_QML` option remains — Quick always builds, Widgets never links).

What survives under `src/UI/` is shared non-view infrastructure only: the
`Theme` palettes (pure QColor data + the OS appearance reader — the widget
QSS layer died with the Widgets tree), `CrashHandler`, the `SlotLiveStats`
mapper, `common/ExternalLink` (now a bare QDesktopServices edge; toast routing
happens in QML via `App.errorMessage`), and `licenses/LicenseManifest`. The
prime directive's proof burden moved to the tests: the view model + role
models live in `dish_core` and stay fully unit-testable without Quick.

---

## 10. How to add a feature (the recipe)

1. **Name the state.** What changes over time? Is it a request (→ `AsyncState<T>`)
   or a lifecycle (→ a reducer FSM in `core/reducer/`)? Define the value type with
   `operator==` and the *reasons/render-keys as data*.
2. **Write the pure core first.** The reducer/mapper + its exhaustive test. No Qt.
   It must be green before any wiring exists.
3. **Own it in a `source/`** `StateSource` (or a Coordinator for cross-cutting
   commands). Feed world signals in; expose `state()` out.
4. **Derive UI shape in a `composer/`** Composer if more than one upstream
   contributes. Side effects → a `Controller`.
5. **Project it in `AppViewModel`** as a reactive `Q_PROPERTY` + `NOTIFY`, and add
   the intent invokables. Keep it thin.
6. **Bind all states in QML** using the kit (`LoadingSpinner`/`ErrorBanner`/
   `EmptyState`/toast). No result-as-absence.
7. **Wire it once in `AppModel`** (the only composition root).

---

## 11. <a name="roadmap"></a>Hardening roadmap (decisions, ready to execute)

These are the audited next steps, each with the chosen pattern, so future features
land on the hardened shape rather than re-litigating it.

| # | Concern | Decision | Status |
|---|---|---|---|
| R1 | **Async-state primitive** | Introduce `core/AsyncState.h` | **Done** |
| R2 | **Catalog fetch** | Repo emits `AsyncState<CatalogDto, CatalogError>` (no dropped failures, stale-on-error) | **Done** |
| R3 | **USB transient banners** | Route `UsbNotice` → `errorMessage` toast (persistent USB error already on the slot) | **Done** |
| R4 | **Forward pairing** | Pure `PairingMachine` FSM (Idle/Submitting/Succeeded/Failed(reason)); success on `SessionConfirmedLive` only | **Done (core); wiring into `WifiConnectionManager` pending)** |
| R5 | **QML state kit** | `LoadingSpinner`/`ErrorBanner`/`EmptyState`/`NotificationToastHost`; global toast host at the shell | **Done** |
| R6 | **Discovery scan** | Empty result is now STATE (the page renders an empty-state for `discoveredServers.length === 0`), not a redundant transient toast — the "No servers found" toast was removed; `scanning` + the (maybe-empty) list are the bound state | **Done** |
| R7 | **Satellite session lifecycle** | Pure `SatelliteSessionMachine` FSM (Discovered/Pairing/Linking/Live/**Faltering**/Reconnecting/Stale/Failed(reason)) modelled on `UsbPathMachine`; `WifiConnectionManager` becomes the coordinator. Fixes the dead `Faltering` state, invisible reconnect/backoff, and uncaptured failure reason. | **Done (core FSM + exhaustive tests); live wiring into `WifiConnectionManager` is the device-tested follow-up** |
| R8 | **Reconnect/backoff as state** | `retryAttempt` + `nextRetryAtMs` modelled on the session FSM so the UI can show "Reconnecting (n)…" | **Done (in the R7 FSM); surfaced when R7 is wired** |
| R9 | **`MainUiState` as a Composer** | A top-level `MainUiStateComposer` derives `MainUiState` from upstream Observables (slots, connections, pairing, USB), replacing the hand-mutated `state_` + two-writer `onInputRatesChanged` patch. Carries per-flow `AsyncState`, not one `busy` bool. | Planned |
| R10 | **Wire `MotionCapabilityComposer`** | It exists + is tested but is dead code; feed it the device-list Observable and consume in R9. | Planned |
| R11 | **`DeviceListSource`** | Replace the SDL `devices()` getter + content-free `devicesChanged()` with a `StateSource<vector<DeviceSnapshot>>` (distinct-until-changed kills the battery-poll rebuild storm; closes the cross-thread TOCTOU). | Planned |
| R12 | **Extract from `AppModel`** | Move the USB framework-presence driving → `UsbDirectCoordinator`; catalog/Emulate → `EmulateCoordinator`; remap/deadzone/rumble push → `Controller`s. Leaves `AppModel` a pure composition root + hot-path seam. | Planned |
| R13 | **Capture mode as state** | Pure `CaptureMode` reducer (Idle/Capturing(slot,target)) — single-owner of the "press to assign" lifecycle, reusing the bridge's capture-threshold predicates; replaces the split bridge-bool + `QString capturingSlotId_`. | **Done (core FSM + tests); live wiring into AppViewModel/bridge is the follow-up** |
| R14 | **Drop racy invokables** | Removed the `connectByIndex`/`pairWithPin(int)` index-based commands (QML is fully on the de-raced `*ByServerId` variants). | **Done** |
| R15 | **Capability vocabulary as a reducer** | Pure `CapabilitySolver` (`core/reducer/`) owns `available = controller ∩ transport ∩ type ∩ host`, the four verdicts (`Available`/`Unavailable`/`Off`/`Pending`) and the first-failing-layer rule. The wizard's type table and Configure binding's matrix both render its output, so they cannot drift. | **Done (pure + tested); QML renders it via `App.capabilityForCandidate`)** |
| R16 | **Apply as a sequencer** | Pure `ApplyBindingMachine` (`core/reducer/`) drives the two-step apply overlay and the 20 s / 8 s budgets. Encodes the two rules the UI kept getting wrong: a timed-out Direct claim is a **fallback**, not a failure, and Cancel is accepted **only** during the claim. `AppViewModel` owns the timers and projects the phase as strings. | **Done (pure + tested); wired in `AppViewModel::applyBinding`)** |
| R17 | **A binding is a declaration, not a preference** | **It must not outlive its physical pad.** `ConnectionHub::bind` installs a desired descriptor and *every* session PUT re-sends the whole desired set, so a binding left behind by an unplugged controller made the satellite plug a virtual pad for hardware that does not exist. Pure `BindingPresence` (`core/reducer/`) gates the table against the slots the app currently shows: pad present → keep, pad moved to its twin id (USB-direct claim/release) → **migrate** so a path switch never tears down a live stream, pad gone → **unbind**, which DELETEs the controller on the live session now instead of at the reaper timeout. `AppModel::applyBindingPresence` applies it at the end of `rebuild()` behind a re-entrancy guard and raises the one-shot "Controller disconnected — unbound from …" toast, because a binding that vanishes unexplained reads as lost work. | **Done (pure + tested); wired in `AppModel::rebuild`)** |

| R18 | **The catalogue is part of the build, not a side artefact** | A stale `.ts` is not a compile error, so the catalogues fell 618 strings behind the code and nobody found out — the v3 redesign shipped its entire wizard, kit and token vocabulary in English to every non-English user while CI stayed green. Three things now hold it shut. `scripts/check-translations.ps1` re-runs `lupdate` in CI and fails on any diff, so a string cannot land without its catalogue entry. Counts go through `%n` rather than hand-written singular/plural pairs, because a pair expresses two forms and Bosnian needs three — and English became a real catalogue (`dish_en.ts`) since the source string can only carry one form, which is what used to force the `"(s)"` workaround. `DISH_REQUIRE_TRANSLATIONS` turns a missing Qt Linguist into a configure error for CI and release builds, where an English-only binary is a broken release rather than a contributor's convenience. Vocabulary is sourced from dish-android, whose catalogues are older and reviewed; `scripts/seed-from-android.py` copies every shared string across rather than inventing a second wording for it. | **Done (`test_translations.cpp` pins locale fallback, the per-language numerusform order, and placeholder integrity across all six catalogues)** |

### Recorded follow-ups (deliberate v3 deferrals)

| # | Concern | Why deferred | What it needs |
|---|---|---|---|
| F1 | **Windows High Contrast** | It needs a **third palette** plus a system-colour bridge (`SPI_GETHIGHCONTRAST` + the `SysColor` set), and it is not a v3 screen. Shipping a half-HC pass — some surfaces honouring system colours, some not — is worse than none, because a HC user cannot tell which surfaces to trust. | A `ThemePalette` sourced from `GetSysColor`, an `Appearance::HighContrast` arm, and a pass over every place the kit derives a wash from the accent (HC forbids alpha washes entirely). |
| F2 | **Toast `Undo`** | `NotificationToastHost` is a **queue with no per-toast action channel**. Adding one is a shared-surface change with exactly one caller (Unbind), so the cost lands on every toast in the app to serve one. Unbind ships confirm-free with a `Binding removed` success toast instead. | An action slot on `models::DishNotification` + the queue, and a callback lifetime story for a toast that outlives the page that raised it. |
| F3 | **Per-binding rumble** | No `RumbleEnabledStore` exists; rumble rides the descriptor caps. `App.rumbleEnabledFor` / `setRumbleEnabled` are honest stubs (`true` / no-op) and the Feel row still renders — the capability verdict for it is real, so hiding the row would hide true information. | A store mirroring `MotionEnabledStore`, keyed per binding, read by the descriptor assembly in `ConnectionHub::bind`. |
| F4 | **A positive bind-accepted edge** | `ConnectionHub::bind` applies the binding locally and the satellite answers asynchronously; only the FAILURE is typed (`slotRegistrationFailed`). The apply sequencer therefore reads success as "the binding held and the session is live" on a 250 ms tick, inside an 8 s budget. A slow satellite can still reject after the overlay closed — the existing rollback toast covers it. | A per-controller applied/accepted signal out of `WifiConnectionManager`'s controller PUT, fed to `ApplyBindingMachine` as `BindAccepted`. |
| F5 | **The SDL half of the emulation-type ladder** | `reducer::seedControllerType` matches a catalog `emulates` hint on **either** the pad's USB `vid:pid` **or** an SDL type slug (`ps4`/`ps5`/`switchpro`/`xbox360`…). `AppModel::currentTypeForConnection` can now answer the USB half (R17's presence oracle resolves the pad behind a slot), but `SDLGamepadBridge::Device` carries no type slug at all, so the slug half is always empty. A pad whose `vid:pid` is absent from the catalog's hint list still degrades to the catalog's **first offered type** — correct-by-fallback, not correct-by-identity. | `SDL_GameControllerGetType` → slug on the bridge's `Device`, threaded through `currentTypeForConnection` as the second key. Nothing else changes: the reducer already takes both. |

**What's a pure spec vs. a live rewrite.** R1–R6, R8, R13(core), R14 are landed and
unit-tested. The four state machines — `AsyncState`, `PairingMachine`,
`SatelliteSessionMachine`, `CaptureMode` — are **pure, exhaustively tested specs**
of the target behaviour. Their **live wiring** (turning `WifiConnectionManager` into
the session coordinator; making the bridge's capture flag follow `CaptureMode`) plus
R9/R10/R11/R12 rewrite the live UDP session loop, the SDL input threading, and the
central `AppModel` — code the unit suite **cannot** exercise (no socket, no
satellite, no controller in CI). Those must land **incrementally with a
device-in-the-loop test pass**, conforming to the FSM specs above, not in one blind
sweep. Sequencing when you do: R9 *additively* (compose `MainUiState` from a composer
while keeping the existing signal), then wire R7 (feed the manager's existing
transition points into `SatelliteSessionMachine` and expose its `SessionModel`), then
the R12 extractions last. **Keep the `routingMtx_` hot-path tables intact throughout.**

---

## 12. Testing

- **Pure core is the contract.** Every reducer/mapper/`AsyncState` transition has a
  Catch2 test that pins the *full* decision space with no mocks
  (`test_async_state`, `test_usb_path_machine`, `test_pairing_machine`,
  `test_catalog_repository`, `test_capability_solver`, `test_apply_binding_machine`,
  …). If logic isn't testable without standing up a live `QObject`/socket/SDL, it
  is in the wrong layer — move the decision into `core/`.
- **The design system is tested too.** `test_theme_store` pins palette
  completeness (every dark role has a light value, and they differ);
  `test_theme_contrast` computes WCAG 2.1 ratios over the real palette values in
  **both** palettes, so a token that reads on dark and vanishes on light fails the
  build rather than a screenshot; `test_font_stacks` pins the family probe that
  keeps mono off Courier New. `scripts/qml-lint-literals.ps1` keeps the pixel
  values inside `src/qml/kit/**`.
- **Probes for the kernel.** `StateSourceProbe`/`ComposerProbe`/`ControllerProbe`
  capture *emission sequences* (not just final values); `RepositoryContract` runs
  the 8 property tests every repository must pass.
- One `DishTests` exe links `dish_core`; `scripts/build.ps1 debug test` builds + runs
  it. Keep it green at every step.
