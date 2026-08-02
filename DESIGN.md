# dish-windows — Design tokens

All theme values live in [src/UI/Theme.h](src/UI/Theme.h) and
[src/UI/Theme.cpp](src/UI/Theme.cpp).

Token names follow the cross-repo schema documented in
`d:\TinkerNorth\BRAND.md` (TinkerNorth design system). When updating a
value, keep it in sync with the matching token in dish-android, dish-mac,
dish-linux, and the Satellite local web UI.

## Available tokens

Colors (in `dish::ui::Theme`):

Dish ships **two palettes** — a dark (default, deep-space) set and a light set.
The two are selected at runtime by the theme mode (dark / light / system; see
*Theme modes* below). Every token role below has a value in **both** palettes;
`Theme::primary` etc. resolve to the *active* palette so the whole app re-themes
on a mode switch. The light set mirrors every dark role 1:1 (a Catch2 test in
`tests/test_theme_store.cpp` asserts completeness + that they differ, and
`tests/test_theme_contrast.cpp` asserts each role clears its WCAG floor in both).

### Dark palette (default)

Palette: **cyan / deep-space** — mirrors dish-website.

| Token | Value | Role |
|---|---|---|
| `background` | `0xFF060818` | Body (`--tn-ink`) |
| `surface` | `0xFF0C1027` | Card (`--tn-night`) |
| `surfaceDim` | `0xFF131A3A` | Recessed (`--tn-deep`) |
| `primary` | `0xFF4FE3FF` | Main accent — cyan (`--tn-signal`) |
| `primaryDark` | `0xFF2C93AD` | Pressed / disabled (`--tn-signal-dim`) |
| `onPrimary` | `0xFF060818` | Text on primary |
| `onSurface` | `0xFFE6ECFF` | Body text (`--body-color`) |
| `muted` | `0xFF93A0C8` | Secondary text (`--muted`) |
| `outline` | `0x2E4FE3FF` | Borders — cyan @ ~18% alpha |
| `success` | `0xFF22C55E` | Status — success |
| `error` | `0xFFE74C3C` | Status — error |
| `warning` | `0xFFF59E0B` | Status — warning |
| `pulse` | `0xFFFF6FB5` | Donation accent — the one hue beyond cyan |
| `glyph` | `0xFF8FCFE3` | Brand-glyph tint |
| `disabledFg` | `0xFF6C7799` | Disabled-control foreground |
| `mutedStrong` | `0xFF7E8CB4` | Drawn-but-unavailable information |

### Light palette

Body/surface roles invert to near-white; the cyan accent darkens to keep
contrast on a light background (a bright cyan on white is illegible); status
hues shift to AA-contrast-on-light variants. This mirrors dish-android's
non-night `values/colors.xml` against its `values-night/` (dark). **Keep in
lockstep with the cross-client light palette** (dish-android / dish-mac /
dish-linux / the Satellite web UI) per `BRAND.md` — this is the one
design-coupled deliverable.

| Token | Value | Role |
|---|---|---|
| `background` | `0xFFF5F7FC` | Body — soft off-white |
| `surface` | `0xFFFFFFFF` | Card — white |
| `surfaceDim` | `0xFFE7ECF6` | Recessed — light grey |
| `primary` | `0xFF0E7C97` | Main accent — darkened cyan (contrast on white) |
| `primaryDark` | `0xFF0A5E73` | Pressed / disabled accent |
| `onPrimary` | `0xFFFFFFFF` | Text on primary |
| `onSurface` | `0xFF0C1430` | Body text — deep ink |
| `muted` | `0xFF5A6680` | Secondary text — slate |
| `outline` | `0x330E7C97` | Borders — darkened-cyan @ ~20% alpha |
| `success` | `0xFF1B873F` | Status — success |
| `error` | `0xFFC0392B` | Status — error |
| `warning` | `0xFFB7791F` | Status — warning |
| `pulse` | `0xFFC2417F` | Donation accent — AA-darkened pink |
| `glyph` | `0xFF2F7E96` | Brand-glyph tint — darkened cyan |
| `disabledFg` | `0xFF8A93A6` | Disabled-control foreground |
| `mutedStrong` | `0xFF4B566E` | Drawn-but-unavailable information |

### The four roles that are easy to confuse

| Role | What it is for | Opacity rule |
|---|---|---|
| `muted` | ordinary secondary text (sub-lines, captions) | full |
| `mutedStrong` | INFORMATION about something unavailable — a capability a layer refuses, and the reason for it | **full, always**. An unavailable capability is drawn, never faded |
| `disabledFg` | the foreground of an inactive **control** | paired with `Tokens.disabledOpacity` (0.55), and legal only on an `AbstractButton` |
| `glyph` | the brand-glyph tint | full. Re-tinted by **palette**, never by state — a separate coloured dot carries state |

`glyph` exists because the shipped SVGs bake the dark tint, which computes to
1.7 : 1 on a white card — below the 3 : 1 floor for graphical objects, and
visually simply gone. `BrandGlyph` renders the raw image on dark and routes
through a colourisation effect on light.

`disabledFg` and `mutedStrong` are two roles because the old single rule
(drop the whole control to 0.4 opacity) conflated "you cannot press this yet"
with "this hardware can never do this", and landed both near 2.6 : 1.

### Derived washes (QML `Theme` singleton only)

These are not palette entries: `ThemeBridge` derives them from the **active**
accent at read time, so a palette swap re-tints them with no second table to
forget. Alphas are 0–255.

| Name | Derivation |
|---|---|
| `primaryHover` | `primary` @ 31 dark / 26 light (12 %) |
| `primaryPress` | `primary` @ 46 dark / 41 light (18 %) |
| `accentWash24` | `primary` @ 61 dark / 56 light (24 %) |
| `primaryFill` | `primary` @ 36 dark / 31 light |
| `successFill` | `success` @ 36 dark / 31 light |
| `warningFill` | `warning` @ 41 |
| `errorFill` | `error` @ 41 |
| `focusRing` | `primary` @ 77 (30 %) |
| `outlineSubtle` | `primary` @ 23 (9 %) |
| `scrim` | `background` @ 153 (60 %) |
| `pulseFill` / `pulseEdge` | `pulse` @ 31 / 89 |

`primaryHover` and `primaryPress` **are** the 12 % and 18 % accent washes. There
is deliberately no second name for either — one colour, one name.
`Theme.alpha(c, a)` re-alphas any token for the rare one-off.

## Theme modes

The active palette is chosen by a `ThemeMode` (`dish::source::ThemePreferenceStore`,
storage values `"system" | "light" | "dark"` under `user_preferences/theme_mode`,
verbatim from dish-android for cross-client schema continuity):

- **Dark** / **Light** pin the corresponding palette.
- **System** follows the OS: `detectSystemAppearance()` reads
  `HKCU\…\Themes\Personalize\AppsUseLightTheme` (0 → dark, 1 → light), falling
  back to Qt's `QStyleHints::colorScheme()`, then to dark.

The split is SoC-clean: the **Source** (`ThemePreferenceStore`) derives the mode;
a **Controller** (`dish::composer::ThemeController`) subscribes it, resolves
`System`, and sets the active palette — so the persisted value and the rendered
palette cannot drift.

**System follows the OS live.** `AppViewModel` subscribes
`QGuiApplication::styleHints()::colorSchemeChanged`; while the stored mode is
`System` it re-resolves, re-applies the palette and fires the theme-applied sink,
so an OS theme flip while the app is running re-themes it. An explicit Light or
Dark pick ignores the OS.

## How to use

There is no stylesheet layer any more — the UI is Qt Quick only. C++ reads the
tokens directly (`dish::ui::Theme::surface`); QML reads them through the `Theme`
singleton (`ThemeBridge`), which re-emits `paletteChanged` on a mode switch so
every binding re-evaluates.

```cpp
// C++ (chrome / native window attributes)
const bool dark = dish::ui::activeAppearance() == dish::ui::Appearance::Dark;
```

```qml
// QML — never a hex literal outside src/qml/kit/**
Rectangle { color: Theme.surface; border.color: Theme.outline }
```

`hex(QRgb)` remains for diagnostics and log lines.

## Enforcement

- `tests/test_theme_store.cpp` — palette completeness: every dark role has a
  light value, and they differ.
- `tests/test_theme_contrast.cpp` — WCAG 2.1 ratios over the real palette values,
  in **both** palettes. A token that reads on dark and vanishes on light fails
  the build.
- `scripts/qml-lint-literals.ps1` — no raw `#rrggbb`, `Qt.rgba(...)`,
  `font.family: "…"` or bare numeric `font.pixelSize` / `radius` / spacing
  outside `src/qml/kit/**`. Error mode for `src/qml/wizard/**` and
  `src/qml/shared/**`; warnings elsewhere.
  A flush layout is `Tokens.s0`, not `0`: zero gap is a decision the design
  makes (a seam drawn with a divider or a colour change instead of a space),
  and naming it keeps the scale continuous and the scanner silent.
