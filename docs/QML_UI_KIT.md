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
- **qmllint note:** linting against the module reports `QColor … not found
  [unresolved-type]` for every `Theme.*` color — this is a known qmllint
  limitation (the singleton returns `QColor` without a declared QtGui qmltypes
  dependency). It is NOT an error; ignore those specific warnings. Keep your QML
  otherwise warning-clean (no `unqualified`/`missing-property`/`unused-imports`).

---

## 1. Component kit (`src/qml/kit/`)

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
`dish-disabled`, `dish-master`, `dish-off`, `dish-receiving`,
`dish-receiving-animated`, `dish-scanning-animated`, `satellite`,
`satellite-broadcasting`, `satellite-broadcasting-animated`,
`satellite-connected`, `satellite-disabled`, `satellite-master`,
`satellite-off`, `satellite-orbiting-animated`, `bluetooth`,
`bluetooth-connected`, `bluetooth-disabled`, `bluetooth-off`,
`bluetooth-pairing-animated`, `bluetooth-searching`,
`bluetooth-searching-animated`.

```qml
Kit.BrandGlyph {
    width: 18; height: 18
    glyph: glyphForToken(glyph)   // `glyph` here = ConnectionListModel role
}
```

> NOTE (unverified): the brand SVGs carry their own stroke colors and may wash
> out against a light-resolved Mica backdrop. They render; their contrast on
> Mica was not visually confirmed. Treat glyph tinting as TBD if a page needs a
> specific contrast.

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
| `contentColumn` | (alias) | — | The body `ColumnLayout`; inject fields via its `children`/`data`, or declare children of `contentColumn`. |
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

        // Body controls are children of contentColumn:
        contentColumn.children: [
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
  Main.qml                 (A0 chrome window; hosts appRoot StackView → AppShell)
  WindowTitleBar.qml       (A0)
  AppShell.qml             (nav rail + content StackView + breadcrumb)
  kit/
    SectionHeader.qml  KitButton.qml  OutlineButton.qml  Card.qml
    StatusDot.qml      BrandGlyph.qml LabeledSwitch.qml   KitTextField.qml
    Page.qml           ContentDialog.qml
  pages/
    ControllersPage.qml  ConnectionsPage.qml  SettingsPage.qml   (placeholders)
```
