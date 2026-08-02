# QML UI Kit (frozen — A2)

This is the FROZEN UI-kit + shell contract the Qt Quick page agents build
against. Together with `docs/QML_CONTRACT.md` (the `App` model surface) it is the
ONLY surface a page agent codes to. Do not re-read the C++ or invent new styling;
if a component you need is missing, flag it.

All of this lives in the `Dish.Chrome` QML module (one import). Colors come from
the `Theme` singleton (`import Dish.Chrome`) documented in A0 — never hard-code a
hex. Controls are the **Basic** style; every kit control is custom-painted from
`Theme` tokens. Do NOT import Material / Universal / FluentWinUI3.

## Conventions

- **Style:** `QtQuick.Controls.Basic`. Import it in every file that uses Controls.
- **Mica:** the window is frameless and (on Win11) its background is transparent
  so the OS Mica backdrop shows through. **Never paint an opaque full-area
  background.** Page roots are transparent; only `Kit.Card` (and dialogs) paint a
  `Theme.surface` panel. If you add a background, justify why with a comment.
- **Imports:** kit components live in `src/qml/kit/`. From a page in
  `src/qml/pages/` import them as:
  ```qml
  import "../kit" as Kit
  ```
  Then use `Kit.Card`, `Kit.KitButton`, etc. (Files at `src/qml/` root use
  `import "kit" as Kit`.)
- **Theme access:** `import Dish.Chrome` then `Theme.primary`, `Theme.surface`,
  `Theme.onSurface`, `Theme.muted`, `Theme.outline`, `Theme.success`,
  `Theme.warning`, `Theme.error`, `Theme.background`, `Theme.surfaceDim`.
- **qmllint note:** the module declares `DEPENDENCIES QtQuick`
  (CMakeLists.txt), so `Theme.*`'s `QColor` properties resolve statically and
  `unresolved-type` is a real error again — keep QML warning-clean
  (`missing-property`/`unused-imports`/`unresolved-type` all gate CI). The one
  remaining accepted limitation is `unqualified` on `App`: it is a runtime
  context property (see QML_CONTRACT.md), invisible to static analysis until
  it moves to a compiled singleton, so CI downgrades only that category.

---

## 0. Library rules

Eight rules, each closing a hole a design system dies through. They are review-
blocking, not advisory.

| # | Rule |
|---|---|
| C1 | **A page may not declare an inline `component`.** If two files would draw it, it is a kit component; if one file draws it and it has a variant, it is *still* a kit component. `HomePage.qml`'s private `WireLine` is the cautionary tale — it was promoted. |
| C2 | **A kit component owns its own state machine.** Pages never set `opacity` to express disabled, never paint a hover wash, never draw a focus ring. |
| C3 | **A kit component never reads the domain.** No `App.*`, no model roles, no `AppViewModel`. It takes primitives and emits signals. `CapabilityTable` takes the rows `App.capabilityForCandidate()` returns **as data** — the caller passes them in. |
| C4 | **One name per colour.** No aliasing layer. `Theme.primaryHover` **is** the 12 % accent wash; there is no second name for it. |
| C5 | **No component may stringify a `Theme` colour.** `String(Theme.outline)` yields `#AARRGGBB`, which Canvas 2D parses as `#RRGGBBAA` — silently wrong channels. Always `Qt.rgba(c.r, c.g, c.b, c.a)`. |
| C6 | **Every state a component can be in appears in `KitGallery.qml`.** A state that is not in the gallery is not shipped. |
| C7 | **Two components may not draw the same thing.** Check the inventory before adding one. Adding a second pill style is review-blocking — the "as DualSense" pill and the Verified / Best-fit / Layout-guessed badges are all `CapabilityChip`. |
| C8 | **Glyphs are never re-coloured by state.** `BrandGlyph` re-tints by **palette** (`Theme.glyph`); state colour lives in the dot and the chip. |

Two more that bind every file:

- **Disabled vs unavailable are different mechanisms.** A dead *control* is
  `enabled: false` + `Tokens.disabledOpacity` + `Theme.disabledFg`, and that is
  legal **only** on an `AbstractButton`. Unavailable *information* gets **no
  opacity at all** — full-opacity `Theme.mutedStrong` plus the outlined absent
  chip. Never multiply an already-muted colour by an opacity.
- **Focus is global.** Every focusable control: `focusPolicy: Qt.StrongFocus`, a
  1 px solid `Theme.primary` border **and** a 2 px `Theme.focusRing` ring outside
  it, on `visualFocus` only.

---

## 1. Component inventory (`src/qml/kit/` — 38 components)

Raw literals are legal **only** here: the kit is the layer that turns tokens into
pixels, and a token defined in terms of itself is not a token.
`scripts/qml-lint-literals.ps1` skips this directory and nothing else.

### 1.1 Primitives

| Component | Base | Key API |
|---|---|---|
| `DishButton` | `Button` | `variant: Primary \| Outline \| Destructive`, `size: Normal \| Small`. **The one button type** — new code uses this. |
| `KitButton` | `DishButton` | Alias for `variant: Primary`. Kept so 40+ call sites keep working. |
| `OutlineButton` | `DishButton` | Alias for `variant: Outline`. |
| `RowButton` | `AbstractButton` | A full-width, focusable, keyboard-activatable list row. Donation rails and licence rows are these, never cursor-pointer divs. |
| `ComboButton` | `AbstractButton` | The drop-down trigger. |
| `SegmentedControl` | `Control` | Two or more segments; thumb radius is derived, never literal. **Disabled drops the accent thumb** for a neutral `Theme.surface` chip with a hairline — `disabledFg` on a saturated accent at `disabledOpacity` is unreadable, and a dead control still has to report which segment is selected. |
| `LabeledSwitch` | `Item` | `label`, `description`, `checked`, `toggled(bool)`. |
| `RadioMark` | `Item` | The selection mark used inside `SelectRow` / `OptionCard`. |
| `SliderRow` | `Item` | `committed(int)` **and** `moved(int)` — push live on `moved`, persist on `committed`. |
| `KitTextField` | `TextField` | Plus `hasError` / `errorText`: an 11 px `Theme.error` message beneath the field, and `Accessible.description`. |
| `StatusDot` | `Rectangle` | `token` → `"success"`/`"warning"`/`"muted"`/`"primary"`. 10 px with a 1 px darker ring. **Never drawn without its chip.** |
| `CapabilityChip` | `Item` | `text`, `tone ∈ Present \| Absent \| Low \| Ok \| Warn \| Neutral`. **Always renders** — an absent capability is drawn, never hidden. |
| `Eyebrow` | `Label` | The mono, tracked, uppercase micro-label. |
| `SectionHeader` | `Label` | `label` — natural case in, uppercase out (`font.capitalization`, never an uppercase string). |
| `BrandGlyph` | `Item` | `glyph`, `glyphForToken(token)`, `tinted`, `accessibleName`. Palette-tinted per C8. **Never references a `*-animated` name** — those files contain no `<animate>` and Qt runs no SMIL. |
| `LiveStat` | `Item` | `live`, `rateText(hz, measured)`, `latencyText(ms, samples)`. **The only formatter for a rate in the app.** `~` means derived or estimated; a sub-millisecond latency reads `"<1 ms"`, never `"~0.0 ms"` (mirrors `reducer::formatLatencyMs`, which is what the models render). |
| `DishProgressBar` | `Item` | Track `Theme.surfaceDim`, height fixed at 3, no caller-settable height. Static filled track under `Tokens.reducedMotion`. |
| `LoadingSpinner` | `Item` | The apply-overlay step spinner's source of truth, and the type-catalog loader. |

### 1.2 Containers and surfaces

| Component | Base | Key API |
|---|---|---|
| `Card` | `Control` | Plus `filled` (false ⇒ transparent fill, hairline kept) and `dense` (tighter vertical padding). The one deliberately opaque element — content reads against a panel, not bare Mica. |
| `ActionCard` | `AbstractButton` | `title`, `subtitle`, `showPlus`, plus `placeholder` (dashed `Theme.outline` border, no wash, no hover, no focus — an ActionCard that is not an action). |
| `Page` | `Page` | Transparent background, one page-level `ScrollView`. `scrollable: false` when the page owns its own layout (the wizard). |
| `Callout` | `Item` | `tone ∈ Info \| Warning \| Error`, `text`, optional leading `glyph`, trailing action slot. |
| `EmptyState` | `Item` | Every list has one. |
| `ErrorBanner` | `Item` | Plus `detail` — an error is a diagnosis **and** a next step. The inline unsteady-link banner and the catalog-retry row. |
| `NotificationToastHost` | `Item` | `show(message, severity)`, tones `error \| warning \| success` only. A stray `"info"` maps to `success` and warns. The only elevated surface in the app. |
| `DishToolTip` | `ToolTip` | The **only** tooltip. A bare `ToolTip` paints Basic's `palette.toolTipBase`/`toolTipText` — Qt's system defaults, which read as an unthemed white slab in both appearances (and as white-on-white over a light card). This one is `Theme.surface` + hairline `Theme.outline` + `radiusButton`, text at `Theme.onSurface`/`Tokens.textSummary`. **Declare it, never attach it**: the attached `ToolTip.text` property resolves its delegate through `QtQuick.Controls` (the style-selecting module) which the pages do not import, so it logs `Component is not ready` and no tip appears. |
| `SelectRow` | `AbstractButton` | The one selectable radio row (pad picker, host picker, type card, bind destination). Selected = **1 px** `Theme.primary` + `Theme.primaryFill`. |
| `OptionCard` | `AbstractButton` | The two-up choice card (Standard / Direct), with an optional badge chip. |

### 1.3 Composites

| Component | Draws |
|---|---|
| `WireLine` | The pad→host wire. `live` (solid) / idle (dashed) / `transmitting` (dashes crawling toward the host during an apply). Promoted out of `HomePage.qml`; Home and `WizardBanner` both compose it. |
| `BindingStrip` | The `BINDING` chip flow + `Edit ›`. Overflows into a real focusable `+N` chip that opens a popup listing the remainder **with their reasons** — never a bare count. |
| `CapabilityTable` | The four-layer matrix. `rows` come from the caller (C3). `✓` success / `✕` error / `—` `Theme.mutedStrong` for Pending. Every layer chip shows its **true** state; the first failing one is heavier. |
| `WizardBanner` | The pad slot, wire and host slot, plus the ①②③ stage markers and the stage-3 sub-step dots. `compact` for a short window. |
| `StepList` | The apply overlay's steps: `done` ✓ / `active` rotating ring / `pending` hollow / `failed` ✕. |
| `ApplyOverlay` | `ContentDialog` + `StepList` + an optional `Cancel` and slow hint. Two callers: the wizard's Review page and Configure binding. |
| `ContentDialog` | The dialog shell: `Theme.scrim`, 1 px `Theme.outline`, `Tokens.radiusDialog`, **no shadow**. Does not auto-close on accept. |
| `ConfirmDialog` | `ContentDialog` + `bodyText` + `bulletLines`. **Reject has default focus.** The discard confirm, Forget host, Forget controller, the keep-awake close confirm. |
| `BlockerDialog` | The two **terminal** blockers only: `ConnectionLost`, `ControllerUnplugged`. An unsteady link is an inline `ErrorBanner`, not a modal. |
| `KitGallery` | Every component × every state, with a theme switcher. No runtime entry point — reached only by a developer editing `Main.qml`. The C6 artifact. |

### 1.4 Selected component detail

### `Kit.SectionHeader`
The monospace, letter-spaced, uppercased section label (mirrors the Widgets
`sectionHeaderQss`). A `Label` subtype.

| Property | Type | Default | Meaning |
|---|---|---|---|
| `label` | string | `""` | Natural-case text; rendered uppercased. |

```qml
Kit.SectionHeader { label: qsTr("Controllers") }
```

### `Kit.KitButton`
The PRIMARY action button — filled, primary-tinted pill, dark on-primary text.
A `Button` subtype, so all `Button` API (`text`, `enabled`, `onClicked`, …)
applies. Disabled drops to 0.4 opacity (the design-system rule).

```qml
Kit.KitButton {
    text: qsTr("Scan")
    enabled: !App.isScanning()
    onClicked: App.startDiscovery()
}
```

### `Kit.OutlineButton`
The SECONDARY / outlined button — transparent fill, themed outline, primary
text. Use for quieter actions (Forget, Cancel, Manage). Same `Button` API.

```qml
Kit.OutlineButton { text: qsTr("Forget"); onClicked: App.forgetConnection(connectionId) }
```

### `Kit.Card`
The surface container — a rounded, outlined `Theme.surface` panel. A `Control`
subtype; put a layout inside, `padding` (default 16) insets it. This is the one
kit element that is deliberately opaque (content reads against a panel, not bare
Mica).

```qml
Kit.Card {
    contentItem: ColumnLayout {
        spacing: 8
        Kit.SectionHeader { label: qsTr("Living-Room") }
        Label { text: qsTr("192.168.1.20 • UDP 47811"); color: Theme.muted }
    }
}
```

### `Kit.StatusDot`
A status dot that maps a contract dot token to a `Theme` color. Bind a model
role's `dotColor` straight in. A `Rectangle` subtype (8×8 default).

| Property | Type | Default | Meaning |
|---|---|---|---|
| `token` | string | `"muted"` | `"success"`/`"warning"`/`"muted"`/`"primary"`. |

```qml
Kit.StatusDot { token: dotColor }   // dotColor from SlotListModel / ConnectionListModel
```

### `Kit.BrandGlyph`
The v6 brand SVG glyph. An `Image` subtype rendering `qrc:/brand/<glyph>.svg`
(Qt6::Svg). Set `width`/`height`; pass a bare asset name (no path/extension).

| Property | Type | Default | Meaning |
|---|---|---|---|
| `glyph` | string | `"satellite"` | Bare asset name, e.g. `"satellite-connected"`, `"dish-off"`. |
| `glyphForToken(token)` | function → string | — | Maps a `ConnectionListModel.glyph` token (`"satelliteBase"`/`"satelliteConnected"`/`"satelliteOff"`) to an asset name. |

Available brand assets (the `:/brand/` set): `dish`, `dish-connected`,
`dish-disabled`, `dish-master`, `dish-off`, `dish-receiving`, `satellite`,
`satellite-broadcasting`, `satellite-connected`, `satellite-disabled`,
`satellite-master`, `satellite-off`, `bluetooth`, `bluetooth-connected`,
`bluetooth-disabled`, `bluetooth-off`, `bluetooth-searching`, `gear`, `pad`.

```qml
Kit.BrandGlyph {
    width: Tokens.glyphSm; height: Tokens.glyphSm
    glyph: glyphForToken(glyph)   // `glyph` here = ConnectionListModel role
}
```

> **The six `*-animated.svg` variants are unreachable through `BrandGlyph`.**
> They contain no `<animate>` elements and Qt runs no SMIL, so they are static
> files that merely look like states. Express a transient in QML instead — a
> `DishProgressBar`, or an opacity/rotation animation over the **base** glyph —
> gated on `Tokens.reducedMotion` and `visible`. The qrc entries stay: deleting
> them buys nothing and risks a missing-asset regression when the brand set is
> re-synced from the sibling repos.
>
> Glyph contrast is solved: `Theme.glyph` is a palette token (dark `#8FCFE3`,
> light `#2F7E96`), and `BrandGlyph` renders the raw image on dark, routing
> through a colourisation effect only on light. The baked SVG hex computes to
> 1.7 : 1 on a white card, which is why the token exists.

### `Kit.LabeledSwitch`
A settings-row toggle: leading label (+ optional description) on the left, a
themed `Switch` on the right.

| Property | Type | Default | Meaning |
|---|---|---|---|
| `label` | string | `""` | The row title. |
| `description` | string | `""` | Optional muted sub-text (shown only when set). |
| `checked` | bool | `false` | Two-way bindable switch state. |
| `toggled(bool checked)` | signal | — | Fires on user interaction. |

```qml
Kit.LabeledSwitch {
    label: qsTr("Send telemetry")
    description: qsTr("Anonymous input/wire counters.")
    checked: someStore.enabled
    onToggled: someStore.setEnabled(checked)
}
```

### `Kit.KitTextField`
The themed single-line input (outlined, primary focus ring). A `TextField`
subtype — all `TextField` API (`placeholderText`, `maximumLength`, `validator`,
`inputMethodHints`, `text`) passes through.

```qml
Kit.KitTextField {
    id: pinField
    placeholderText: qsTr("6-digit PIN")
    maximumLength: 6
    inputMethodHints: Qt.ImhDigitsOnly
}
```

---

## 2. Page convention

Pages extend **`Kit.Page`** (a transparent-backgrounded `Page` subtype). Its
default content is a padded `Column` (spacing 16, padding 24) — just declare
children; they stack. Set `title` (read by the shell for the breadcrumb).

```qml
import QtQuick.Controls.Basic
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    title: qsTr("Controllers")

    Kit.SectionHeader { label: qsTr("Controllers") }

    Kit.Card {
        // ... page body ...
    }
}
```

The shell loads exactly these three files by path — a page agent REPLACES the
body of its file, keeps the filename and the `Kit.Page { title: … }` root:

| Destination | File | Model surface |
|---|---|---|
| Controllers | `src/qml/pages/ControllersPage.qml` | `App.slotModel` |
| Connections | `src/qml/pages/ConnectionsPage.qml` | `App.connectionModel` + `App.discoveredServers()` |
| Settings | `src/qml/pages/SettingsPage.qml` | (settings stores) |

> If a page needs real logic, it belongs in a tested C++/store, not QML. Flag it.

New page files (detail sub-pages, etc.) MUST be registered in
`CMakeLists.txt` under `qt_add_qml_module(... QML_FILES ...)`. Page agents:
**ask the spine agent (A2) to add the file** — parallel page agents do not edit
CMakeLists.txt.

---

## 3. App shell (`src/qml/AppShell.qml`)

`AppShell` is the NavigationView layout: a left rail (Controllers / Connections /
Settings) + a `StackView` content area with a breadcrumb header. It is loaded by
`Main.qml` under the title bar and on the Mica surface. Page agents normally do
NOT touch AppShell — they only fill their page files. Two shell APIs are exposed
for detail navigation:

- **`shell.pushDetail(url, title)`** — push a detail page onto the content
  StackView with an explicit breadcrumb title. The Back affordance (shown when
  the stack has depth > 1) pops it and restores the destination's rail label.
  Reach the shell from a page via its ancestor; the supported pattern is to have
  the page expose a signal the shell connects, OR (simpler) push onto the
  enclosing `StackView.view` directly:
  ```qml
  // From inside a page, open a detail view:
  StackView.view.push(Qt.resolvedUrl("SomeDetailPage.qml"))
  ```
  (Use `pushDetail` when you also want the breadcrumb title to change.)

The rail selection resets the content StackView to the chosen destination's root
(`replace()`), clearing any pushed detail pages.

### 3.1 Rail glyphs — four separable silhouettes, not four file names

Collapsed, the rail is a 48 px icon strip with no labels, so each entry has to be
identifiable from its picture alone at `Tokens.glyphSm` (16 px). The shipped
mapping:

| Entry | glyph | why |
|---|---|---|
| Home | `dish-connected` | the app's own mark — the whole signal path |
| Controllers | `pad` | the pads attached to this PC |
| Connections | `satellite` | the remote hosts |
| Support Dish | ♥ (`Theme.pulse` text glyph) | the one non-cyan hue, reserved for donations |
| Settings | `gear` | — |

**Substitution recorded (plan D8 / SCR §12.1).** D8's table gave Controllers
`dish` while Home kept `dish-connected`. Those are two names for **one
silhouette**: they differ by a 3-unit dot in a 64-unit viewBox, which is under
one pixel at 16 px, so the two top rail entries were the same picture the moment
the labels faded — the exact defect D8 was written to close, one family over.
The `:/brand/` set held only three separable shapes (the tilted dish ellipse, the
satellite's wide panel bar, the gear disc) and the rail needs four, so
`brand/pad.svg` was drawn: the `pad-*` glyph SCR §12.1 asked for, in the same
64 × 64 / `#8FCFE3` + white-at-55 % language as the rest of the set, default
state only. It tints through `Theme.glyph` like every other brand asset, so both
appearances resolve from the palette.

Rule going forward: **no two rail entries may resolve to the same silhouette** —
a different *state* of the same family (`-connected`, `-receiving`, `-off`) is
not a different rail glyph.

---

## 4. Overlay / ContentDialog convention

Modal tasks (pairing, emulate picker) present as **`Kit.ContentDialog`** — a
centered, dim-scrim `Popup` painting a `Theme.surface` card with a heading, a
body slot, and an accept/reject footer. Declare one anywhere in the page tree
and call `.open()`; a `Popup` reparents to the window overlay layer, so it floats
above the shell automatically.

| Property | Type | Default | Meaning |
|---|---|---|---|
| `heading` | string | `""` | Dialog title. |
| `acceptText` | string | `"OK"` | Primary footer button label. |
| `rejectText` | string | `"Cancel"` | Outline footer button label. |
| `acceptEnabled` | bool | `true` | Enable/disable the accept button (e.g. until input valid). |
| `body` | list<QtObject> (alias) | — | The dialog body: assign the field items (`body: [ ... ]`). Aliases the internal column's `data` list, so the type is statically known to tooling. |
| `accepted()` | signal | — | Accept clicked. **The dialog does NOT auto-close on accept** — call `close()` yourself after a successful action (keep open on error). |
| `rejected()` | signal | — | Reject clicked (auto-closes). |

Working example — a pairing dialog (uses the `App` pairing surface from
`QML_CONTRACT.md`):

```qml
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    title: qsTr("Connections")

    Kit.KitButton { text: qsTr("Pair…"); onClicked: pairDialog.open() }

    Kit.ContentDialog {
        id: pairDialog
        heading: qsTr("Enter pairing PIN")
        acceptText: qsTr("Pair")
        acceptEnabled: pinField.text.length === 6

        // Body controls:
        body: [
            Kit.KitTextField {
                id: pinField
                placeholderText: qsTr("6-digit PIN")
                maximumLength: 6
                inputMethodHints: Qt.ImhDigitsOnly
            }
        ]

        onAccepted: App.pairWithPin(0, pinField.text)   // close on success below
        onRejected: pinField.clear()
    }

    Connections {
        target: App
        // Keep the dialog open on error; surface a toast. Close it when pairing
        // is no longer in flight and no error arrived (page decides the policy).
        function onErrorMessage(message) { /* show toast; pairDialog stays open */ }
    }
}
```

For a custom dialog beyond accept/reject, build a `Popup` directly using
`Theme.surface`/`Theme.outline` + `Kit.Card` styling and the same
`anchors.centerIn: Overlay.overlay` + `modal: true` recipe.

---

## 5. Onboarding convention (first-run, outside the nav shell)

A first-run flow is shown FULL-SCREEN OVER the nav shell, not inside it. `Main.qml`
hosts a top-level `StackView` (id `appRoot`) below the title bar whose
`initialItem` is the `AppShell`. The onboarding agent shows its flow by pushing a
full-screen page onto `appRoot`, and pops it (or replaces back to the shell) when
done:

```qml
// In Main.qml the host is:
//   StackView { id: appRoot; initialItem: AppShell {} ; background: null }
//
// Show onboarding over the shell (e.g. from a first-run check at startup):
appRoot.push("onboarding/OnboardingFlow.qml")
// ... when finished:
appRoot.pop()        // reveals the AppShell beneath
```

The onboarding flow itself is a normal full-bleed item (transparent background so
Mica shows; use `Kit.Card`/`Kit.KitButton`/`Kit.SectionHeader` for its surfaces).
It runs ABOVE the title bar's content area but the title bar (drag / Mica /
caption buttons) stays live, so the window remains movable/closable during
onboarding. New onboarding QML files must be registered in CMake (ask A2).

---

## File map

```
src/qml/
  Main.qml                 (chrome window; hosts appRoot StackView → AppShell)
  WindowTitleBar.qml
  AppShell.qml             (nav rail + content StackView + breadcrumb + leave guard)
  kit/                     (37 components — see §1)
    DishButton  KitButton  OutlineButton  RowButton  ComboButton
    SegmentedControl  LabeledSwitch  RadioMark  SliderRow  KitTextField
    StatusDot  CapabilityChip  Eyebrow  SectionHeader  BrandGlyph
    LiveStat  DishProgressBar  LoadingSpinner
    Card  ActionCard  Page  Callout  EmptyState  ErrorBanner
    NotificationToastHost  SelectRow  OptionCard
    WireLine  BindingStrip  CapabilityTable  WizardBanner  StepList
    ApplyOverlay  ContentDialog  ConfirmDialog  BlockerDialog  KitGallery
  shared/
    BindingDraft.qml       (the ONE binding draft; two editors instantiate it)
  wizard/
    SetupWizardPage.qml    WizardInputPage.qml   WizardDestinationPage.qml
    WizardTypePage.qml     WizardFeelPage.qml    WizardReviewPage.qml
  pages/
    HomePage  ControllersPage  ConnectionsPage  ConfigureBindingPage
    SettingsPage  DonatePage  LicensesPage  DeadzoneSettingsPage
    ControlsRemapPage  PairingDialog
  onboarding/
    OnboardingFlow  WelcomeScreen  HelpScreen
```

Every new `.qml` must be listed in `qt_add_qml_module(... QML_FILES ...)` or it
will not be in the module at runtime.
