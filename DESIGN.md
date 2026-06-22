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
`tests/test_theme_store.cpp` asserts completeness + that they differ).

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

### Light palette

Authored for Workstream 3d (the cross-client light token set). Body/surface
roles invert to near-white; the cyan accent darkens to keep contrast on a light
background (a bright cyan on white is illegible); status hues shift to
AA-contrast-on-light variants. This mirrors dish-android's non-night
`values/colors.xml` against its `values-night/` (dark). **Keep in lockstep with
the cross-client light palette** (dish-android / dish-mac / dish-linux / the
Satellite web UI) per `BRAND.md` — this is the one design-coupled deliverable.

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
`System`, sets the active palette, and re-runs `applyDishTheme(app)` — so the
persisted value and the rendered palette cannot drift.

## How to use

Inside QSS strings, embed via `hex()`:

```cpp
label->setStyleSheet(
    QStringLiteral("color: %1;").arg(hex(Theme::muted)));
```

Pre-built QSS snippets are also available:

- `sectionHeaderQss()` — monospace section labels
- `outlinedButtonQss()` — outlined button styling
- `dotQss(QRgb)` — small colored status dot

## Outliers

None as of this commit. All previously hardcoded color literals
(`#EAEAEA`, `#6B7280`) in [src/UI/SlotCard.cpp](src/UI/SlotCard.cpp) and
[src/UI/ConnectionsDialog.cpp](src/UI/ConnectionsDialog.cpp) have been
migrated to `Theme::onSurface` / `Theme::muted`.

The Qt palette / global stylesheet in [src/UI/Theme.cpp](src/UI/Theme.cpp)
needs inline-alpha tints for hover / pressed / chip fills (QSS has no variable
references and no half-alpha token). As of Workstream 3d these are no longer
hardcoded literals — a private `rgba(QRgb, alpha)` helper derives them from the
**active** palette token, so a light re-theme retints them with the light accent
automatically. No hand-sync needed when the palette changes.
