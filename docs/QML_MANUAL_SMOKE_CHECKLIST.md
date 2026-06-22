# DISH_QML — Manual smoke checklist (human-at-the-machine)

These items cannot be confirmed by an automated agent (they need a live human
eye, a real input device, or window-manager interaction that screenshot tooling
cannot reliably drive under per-monitor DPI). Run `build-qml\dish.exe`.

For each: what A3 could partially verify, and exactly what YOU should look for.

| # | Item | A3 partial verification | What the human must confirm |
|---|------|--------------------------|------------------------------|
| 1 | **Mica backdrop visually present** | `chrome->applyMicaBackdrop()` returns and sets `ChromeBridge.micaActive`; the title-bar strip sampled dark (RGB ~30,30,30) on a real on-screen capture — consistent with a Mica/dark frame showing through the transparent title bar. | Look for the translucent Mica material: the desktop wallpaper/behind-window colour should faintly tint the window background, and it should shift as you drag the window across different wallpaper regions. A flat solid colour with no translucency = Mica NOT applied. |
| 2 | **Window drag via title bar** | `WindowTitleBar` MouseArea calls `bar.window.startSystemMove()`; double-click calls `toggleMaximize()`. Wiring present, not exercised live. | Click-drag the empty title strip (not the buttons) — the window should move. Double-click the strip — it should toggle maximize/restore. |
| 3 | **Snap Layouts flyout on maximize-button hover** | `ChromeBridge.setMaximizeButtonRect()` publishes the button rect to the native hit-test filter; the rect updates on resize. Not hoverable via synthetic input. | Hover the mouse over the square (maximize) caption button and HOLD ~0.5s — Windows 11 should pop the Snap Layouts flyout (the grid of layout choices). |
| 4 | **Native resize borders** | Frameless window + `FramelessWindowChrome` native filter installed (`WindowHitTest` math is unit-tested). | Move the mouse to each window edge/corner — the resize cursor should appear and dragging should resize. Confirm all 8 directions. |
| 5 | **Live telemetry ticking with a real device** | No hardware was attached; Controllers page rendered its empty/default state only. The `App.slots`/input-rate plumbing compiles and the page loads without error. | Plug in a real pad / connect a satellite, go to Controllers, and confirm the per-slot Hz / live-stats values update in real time. |
| 6 | **Light/dark follows the theme toggle** | Theme chips (Light / Dark / System) render on Settings. Synthetic clicks could NOT reliably land under 1.5x DPI, so the toggle was NOT exercised. **IMPORTANT: see the dark-mode finding below — the app launched LIGHT despite mode=System + OS=Dark.** | On Settings → Appearance, click Light, then Dark, then System. The whole window palette must repaint immediately each time. Specifically verify: with mode=**System** and Windows in **Dark** mode, the app body should be DARK (deep-blue ~#060818, light text) — A3 observed it rendering LIGHT (white body, dark text). Confirm whether this reproduces. |
| 7 | **Win10 solid-color fallback** | Code path exists: `Main.qml` uses `color: Theme.background` when `ChromeBridge.micaActive` is false; the Win11 version gate lives in `WindowHitTest.h`. Could not test (host is Win11). | On a Windows 10 machine, launch the app: there should be NO Mica translucency — the window body should be a flat solid themed colour, and chrome (drag/resize/min/max/close) should still work. |

## Dark-mode finding (Task 6) — needs human confirmation / A-ext follow-up

On this machine (`theme_mode = system` in HKCU\Software\Dish\Dish, and Windows
`AppsUseLightTheme = 0` i.e. **OS is in Dark mode**), the QML app launched and
rendered in the **LIGHT palette**: white body/cards (RGB 255,255,255) with deep-ink
text. This was confirmed on a clean topmost on-screen capture (not just the
PrintWindow buffer, which renders the transparent Mica root as white regardless).

This contradicts the intended behaviour:
- `QmlEntryPoint.cpp:53-54` pins `setActiveAppearance(Dark)` when the stored mode
  is `System` (A-ext's "Mica resolved light" fix), and
- `detectSystemAppearance()` returns Dark for `AppsUseLightTheme = 0`.

By every code path the body palette should be DARK, yet the `Theme.*` tokens the
QML reads resolved to the LIGHT palette at render time. The title-bar strip DID
sample dark (Mica frame), so this is specifically the QML content palette, not the
frame. Root cause is most likely an ordering / live-re-apply interaction between
`model.start()`'s `ThemeController` and the `QmlEntryPoint` pin (the pin runs, but
something re-applies light before/at QML bind time). This is in A2/A-ext's theme
domain — flagged here, NOT fixed by A3.
