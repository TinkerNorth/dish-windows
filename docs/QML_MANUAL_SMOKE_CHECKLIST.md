# Manual smoke checklist

The automated suite covers every pure decision in the app, and `qmllint` plus
the literal scanner cover the QML. Neither can cover a window manager, a real
pad, or a human eye. This is the list a person runs before a release.

Build and launch: `scripts/build.ps1 release`, then run `build-release\dish.exe`.

Mark an item pass only if you saw it happen. "The code looks right" is what the
unit tests are for.

## Window chrome

The window is frameless: Windows draws no title bar and
`FramelessWindowChrome` restores the native behaviours by hand. The hit-test
maths is unit-tested (`test_window_hit_test`), the interaction is not.

- [ ] **Drag.** Click-drag the empty part of the title strip (not the buttons,
      not the hamburger). The window moves.
- [ ] **Double-click maximize.** Double-click the same strip. It toggles
      maximize and restore.
- [ ] **Caption buttons.** Minimize, maximize, restore and close all respond to
      a single click, and each highlights on hover.
- [ ] **Snap Layouts.** Hover the maximize button and hold about half a second.
      Windows 11 opens the Snap Layouts flyout. Picking a layout snaps the
      window.
- [ ] **Resize borders.** All eight edges and corners show a resize cursor and
      resize on drag.
- [ ] **Minimum size.** Drag the window as small as it will go. It stops at
      900x620 with nothing clipped.
- [ ] **Windows 10.** On a Windows 10 host the app launches, the body is the
      flat themed colour, and drag, resize, maximize and close all still work.
      The Mica frame extension is a Windows 11 path and is skipped.

## Navigation

- [ ] **Rail collapse.** The title-bar hamburger collapses the rail to the
      48 px icon strip and expands it back to 236 px. The choice survives a
      restart.
- [ ] **Collapsed tooltips.** With the rail collapsed, hovering each entry
      shows its label after about half a second. All five entries (Home,
      Controllers, Connections, Support Dish, Settings) are distinguishable
      from their icon alone.
- [ ] **F6.** Moves focus between the rail and the content pane.
- [ ] **Ctrl+,** opens Settings.
- [ ] **Alt+Left** goes back exactly when the header back chevron is visible,
      and does nothing when it is not. In the setup wizard it steps one page
      back instead.
- [ ] **Header.** Every destination and every pushed detail page shows a title,
      and a sub-line with a status dot where it has one to show.

## Setup wizard

Five pages over three stages: Input, Destination, then Type, Feel and Review.
Needs a real pad and a reachable satellite.

- [ ] **Entry points.** The rail's `Set up` action, Home's `+ Add` card, and an
      unbound pad's `Bind…` all open the wizard, and all of them land on
      Home's stack so cancelling returns to Home.
- [ ] **Seeded pad.** Opening from an unbound pad's `Bind…` pre-answers page 1
      and starts on page 2. Back to page 1 still works.
- [ ] **Banner fills in.** The pad, wire and host slots of the banner fill as
      answers land, and the stage markers advance.
- [ ] **Nothing writes early.** Walk pages 1 to 4 and cancel. No binding was
      created, no type changed, no USB path switched.
- [ ] **Discard confirm.** With answers entered, press Esc, click a rail entry,
      and close the window. Each raises the discard confirm, and Cancel keeps
      the draft intact.
- [ ] **Apply, Standard path.** Finish the wizard with the Standard path. The
      apply overlay shows its steps, the binding lands, the wizard pops to Home
      and the new row is there.
- [ ] **Apply, Direct path.** Finish with the Direct path on a raw-HID capable
      pad. The Connection step can sit for up to 20 seconds; past 4 seconds the
      slow hint appears, and Cancel is offered only while that step is active.
- [ ] **Direct fallback.** If the claim does not land, the run continues, the
      pad streams over Standard, and you get a **warning** toast, not an error.
- [ ] **Failure keeps the draft.** Bind against an unreachable host. The wizard
      stays put, the draft survives, the reason arrives as a toast, and the
      primary button is live again.

## Live input

Needs a pad and a satellite. Nothing here can be faked in CI.

- [ ] **Telemetry ticks.** On Controllers, the per-slot Hz and the footer
      events/s and sends/s move while you move the sticks, and settle when you
      stop.
- [ ] **Latency.** A connected row shows a latency figure with its sample
      count, and it updates about once a second. It never reads `~0.0 ms`.
- [ ] **Bluetooth pad.** A pad connected over Bluetooth shows the Bluetooth
      transport chip, the bluetooth glyph family, and offers no USB path
      control.
- [ ] **Battery.** A pad that reports battery shows the chip; one that does not
      shows no chip rather than a wrong number.
- [ ] **Rumble and lightbar.** A game on the satellite drives rumble and the
      light bar on a capable pad.
- [ ] **Configure controls.** On a generic DirectInput pad, capture and assign
      each output, flip the Y inverts, and confirm the change takes effect on
      the next report with no re-plug. Reset to defaults restores the stock
      layout.
- [ ] **Capture does not self-assign.** Arm capture and leave the pad at rest.
      Nothing is captured from idle stick jitter.

## Appearance

- [ ] **Toggle repaints live.** Settings, Appearance: click Light, then Dark,
      then System. The whole window, including the title bar and the caption
      buttons, repaints immediately each time. No surface is left on the old
      palette.
- [ ] **System resolves correctly.** With mode System, set Windows to Dark and
      restart the app: the body is dark. Set Windows to Light and restart: the
      body is light. There is no live OS-appearance watcher, so a flip while
      the app is running is expected to take effect only on the next launch or
      the next explicit toggle.
- [ ] **Light palette is readable.** In Light, walk every page. No text or
      glyph washes out; brand glyphs stay legible on white cards.
- [ ] **Reduced motion.** Turn off "Show animations in Windows" in Windows
      accessibility settings, then re-focus the app window. Indeterminate bars
      become a static filled track, the rail collapse stops animating, and no
      glyph animates.

## Transients and guards

- [ ] **Overflow menus open with a visible width.** On Connections, click the
      `⋯` on a host card: the Forget menu appears, sized to its text. On Home,
      open a slot row's context menu the same way. A Menu takes its width from
      its background, so a restyled background with no `implicitWidth` opens the
      menu at zero width — it takes focus and draws nothing, which looks exactly
      like a dead button. Check this after any change to a Menu background.
- [ ] **One toast host.** Trigger a failure (unplug the network mid-connect).
      Exactly one toast appears, bottom-centre, and it does not block the
      controls under it.
- [ ] **Streaming pill.** With a pad streaming, the header shows the streaming
      pill on every destination, and it clears when streaming stops.
- [ ] **Close while streaming.** Close the window while a pad is streaming. The
      quit confirm appears; Cancel keeps the app running and streaming.
- [ ] **First run.** With a fresh profile (clear
      `HKCU\Software\TinkerNorth\Dish`) the
      onboarding flow opens full-screen over the shell. Both Skip and finishing
      the flow mark it done, and it does not reappear on the next launch.

## Keep awake

`powercfg /requests` (elevated) is the oracle: it names the process holding a
SYSTEM or DISPLAY request. None of this is reachable from CI.

- [ ] **Default reach is the machine, not the screen.** With a pad streaming on
      a fresh config, `powercfg /requests` shows Dish under SYSTEM and **not**
      under DISPLAY, and the screen still blanks on its normal schedule. The
      pill reads "Streaming · computer kept awake".
- [ ] **Configure lands on the setting.** Click **Configure** on the pill from
      any destination. Settings opens at the rail root, not as a pushed detail
      with a back chevron.
- [ ] **Never means never.** Set *Keep the computer awake* to Never. The
      request disappears immediately, the pill drops to "Streaming", and the
      quit confirm still appears when closing with a pad streaming.
- [ ] **The idle window lets go and takes back.** Set While playing with a
      1-minute window. Leave the pad untouched: within ~1 minute the request
      disappears and the pill drops to "Streaming". Move a stick: both come
      back within a second.
- [ ] **A resting pad does not hold it.** With a USB-direct DualSense or DS4
      claimed (which gets no deadzone), leave it on the desk under While
      playing. The request must still expire — a drifting stick must not read
      as activity.
- [ ] **While connected ignores stillness.** Set While connected and leave the
      pad untouched well past the window. The request stays.
- [ ] **The display opt-in widens, never creates.** Turn *Keep the display
      awake too* on: Dish appears under DISPLAY as well and the pill reads
      "display kept awake". Turn the mode to Never with the switch still on: no
      request at all.

## Accessibility

- [ ] **Keyboard only.** Unplug the mouse. Reach every destination, open the
      wizard, complete a binding, and open and dismiss a dialog.
- [ ] **Focus is visible.** Every focused control draws the 1 px accent border
      and the 2 px ring outside it. No control takes focus without showing it.
- [ ] **Screen reader.** With Narrator on, each rail entry, caption button and
      status pill announces something meaningful.
