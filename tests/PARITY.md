# Parity: dish-windows against the other Dish clients

> **What this is.** The feature-parity ledger for this client, kept as a small
> matrix rather than a per-pull-request diary. The diary form went stale the
> moment a wave was ported in one PR without a row per Android PR, and by
> 2026-09-20 it had not been touched since 2026-08-18 while 24 Android PRs
> landed. A matrix cannot go stale that way: **a PR that changes a row updates
> this file in the same change**, and a reviewer checks the row, not the diary.
>
> Test coverage is no longer mapped file-by-file here. The Catch2 suite under
> `tests/` is the coverage truth and CI runs it on every push; the old
> android-test-file map claimed 0 missing rows and said nothing a green suite
> does not.
>
> Legend: ✅ works · ⚠️ shipped, not yet verified on real hardware · ❌ not
> available · – not applicable. The Windows column was verified against the
> code on 2026-09-29; sections 5a and 5b were written with their features in
> 2026-11.

## 1. What works, per client

| Feature | Android | Windows (this) | Linux | macOS |
|---|---|---|---|---|
| Satellite host, protocol 4 | ✅ | ✅ | ✅ | ✅ |
| Moonlight host (Sunshine / Apollo / Wolf) | ✅ | ✅ | ✅ | ⚠️ |
| Mouse mode on the host | ✅ | ❌ (deferred: `reducer::kClientRoutesTouchpadAsMouse`) | ❌ | ✅ |
| Steam Controller trackpad pick (stick / pad / mouse / off) | ⚠️ | ⚠️ (mouse behind the deferral above) | ⚠️ (mouse behind the deferral above) | ⚠️ |
| Controller audio (mic + speaker) | ✅ | ✅ | ✅ | ⚠️ |
| DualSense HD haptics | ⚠️ | ⚠️ | ⚠️ | ⚠️ |
| Adaptive triggers, player LEDs, mic lamp | ✅ | ✅ | ✅ | ⚠️ |
| Lightbar, motion, touchpad, battery | ✅ | ✅ | ✅ | ✅ |
| Crash reports (opt-out) | ✅ Crashlytics | ✅ Sentry | ✅ Sentry | ✅ |
| Update notice | – Play (the store updates it) · ✅ GitHub build | ✅ | ✅ | ✅ |
| Runs in the background / window closed | ⚠️ Moonlight sessions stay up (foreground service) · satellite streaming stops when the app leaves the screen, by design: the phone is the pad | ✅ tray | ✅ tray | ✅ |
| Survives PC sleep and resume | – | ✅ Satellite · ❌ Moonlight | ✅ Satellite · ❌ Moonlight | ✅ |
| Keep-awake is user-configurable | ❌ (always on while streaming) | ✅ | ✅ | ✅ |
| Diagnostics screen | ✅ | ✅ (every item its paths carry, see §5b) | ✅ (every item its paths carry, dish-linux#122) | ✅ |
| Link-tier cue on host rows (Fastest / Fast / Basic) | ✅ (on the section header) | ✅ | ✅ | ✅ |
| Protocol chip ("Satellite update recommended / required", "Dish update required") | ✅ | ✅ | ✅ | ✅ |
| App-wide microphone chip with mute-all | ✅ | ✅ | ✅ | ✅ |
| Capability verdict word "Supported" | ✅ | ✅ | ✅ | ✅ |
| Pairing secrets stored encrypted | ✅ Moonlight key (Keystore) · ❌ Satellite key (app-private prefs) | ✅ DPAPI | ✅ Satellite key (Secret Service, else a 0600 file) · ❌ Moonlight key (0600 file) | ✅ |
| Six languages (en, bs, de, es, fr, pt-BR) | ✅ | ✅ | ✅ | ✅ |

The macOS column is the mark dish-mac's own ledger (`Tests/PARITY.md`) gives
each row on 2026-11-23; its cells there say what each mark covers. Dish for
iOS keeps its ledger in another shape (`Tests/PARITY.md` in dish-ios, one row
per feature with its build status) and is not a column here.

## 2. Input, pad → host, by path (cell = USB Direct · USB Standard · Bluetooth)

| | Android | Windows (this) | Linux |
|---|---|---|---|
| Buttons, sticks, triggers | ✅ · ✅ · ✅ | ✅ · ✅ · ✅ | ✅ · ✅ · ✅ |
| Motion | ✅ · ✅ · ✅ | ✅ · ✅ · ⚠️ | ✅ · ✅ · ⚠️ |
| Touchpad | ✅ · ⚠️ · ⚠️ | ✅ · ✅ · ⚠️ | ✅ · ✅ · ⚠️ |
| Battery | ⚠️ · ✅ · ✅ | ⚠️ · ✅ · ⚠️ | ⚠️ · ✅ · ⚠️ |
| Controller mic | ✅ · ✅ · ❌ | ✅ · ⚠️ · ❌ | ✅ · ⚠️ · ❌ |
| Extended buttons (paddles, the Steam extras, Capture; protocol 4) | ⚠️ · ❌ · ❌ | ⚠️ · ⚠️ · ⚠️ | ⚠️ · ⚠️ · ⚠️ |

The Battery row is what the host is sent: the lower of the pad's own reading
and the PC's battery, on every path and to a Moonlight host too, and the PC's
alone when the pad reports none. A wired pad that reports its own charge
competes like any other, a tie goes to the pad, and an unknown level loses to
a known one. Direct reads the pad's charge from its own report; Standard and
Bluetooth read SDL's coarse level, which SDL leaves unknown for a pad it sees
as wired. The controller card keeps showing the pad's own reading. Android
applies the same rule with the phone's battery.

## 3. Receiving, host → pad, by path (cell = USB Direct · USB Standard · Bluetooth)

| | Android | Windows (this) | Linux |
|---|---|---|---|
| Rumble | ✅ · ✅ · ✅ | ✅ · ✅ · ⚠️ | ✅ · ✅ · ⚠️ |
| Lightbar | ✅ · ❌ · ⚠️ | ✅ · ✅ · ⚠️ | ✅ · ✅ · ⚠️ |
| Adaptive triggers | ✅ · ❌ · ❌ | ✅ · ⚠️ · ⚠️ | ✅ · ⚠️ · ⚠️ |
| Player LEDs | ✅ · ❌ · ❌ | ✅ · ⚠️ · ⚠️ | ✅ · ⚠️ · ⚠️ |
| Mic lamp | ✅ · ❌ · ❌ | ✅ · ⚠️ · ❌ | ✅ · ⚠️ · ❌ |
| Controller speaker | ✅ · ✅ · ❌ | ✅ · ⚠️ · ❌ | ✅ · ⚠️ · ❌ |
| HD haptics | ⚠️ · ⚠️ · ❌ | ⚠️ · ⚠️ · ❌ | ⚠️ · ⚠️ · ❌ |

The Android Standard column is ❌ for lightbar, triggers, LEDs and the lamp
because the framework has no API for them over USB; the desktop Bluetooth
column is ⚠️ because SDL's HIDAPI driver only opens a Bluetooth Sony pad in
enhanced mode since the 2026-09-20 wave and nobody has paired one against it.

## 4. Which controllers can use USB Direct (Bluetooth is never Direct)

| Controller | Android | Windows (this) | Linux |
|---|---|---|---|
| DualShock 4 | ✅ | ✅ | ✅ |
| DualSense (+Edge on desktop) | ✅ | ✅ | ✅ |
| Switch Pro | ✅ | ✅ | ✅ |
| Generic PDP Switch pads | ✅ | ✅ | ✅ |
| 8BitDo | ✅ known XInput models | ✅ in HID mode (generic HID parser); Standard only in XInput mode (XUSB owns it) | ✅ in HID mode (generic HID parser); Standard only in XInput mode (xpad owns it) |
| Xbox 360 / One / Series wired | ✅ | ❌ Standard only (XUSB owns it) | ❌ Standard only (xpad owns it) |
| Stadia | ✅ | ⚠️ generic HID parser, manual pick | ⚠️ generic HID parser, manual pick |
| Steam Controller (2015) | ✅ | ⚠️ never verified; each controller on the receiver a pad of its own (see below) | ⚠️ never verified; each controller on the receiver a pad of its own |
| Steam Controller (2026) | ❌ | ⚠️ never verified, ported from SDL (see below) | ⚠️ never verified, ported from SDL |

The 2026 Steam Controller (wired `28DE:1302`, dongles `28DE:1304` and
`28DE:1305`) is ported from SDL's driver (`SDL_hidapi_steam_triton.c`) and no
pad has been on hand. Like the 2015 pad it is never claimed on its own, only
through a Direct pick on its card. Its output worker sends the quiet settings
(lizard mode off, raw accelerometer and gyro on) at the claim and every 3 s,
because the pad's firmware turns lizard mode back on otherwise, and repeats a
running rumble every 40 ms, because the pad stops one on its own. Its four back
paddles, quick-access button, trigger clicks and stick and grip touches ride the
protocol 4 extended-buttons word (see below), and its two trackpads go where the
slot's trackpad pick puts them (below). Open follow-up: the Bluetooth identity
(`28DE:1303`) is never Direct, like every Bluetooth pad.

The trackpad pick, per Steam Controller slot (`reducer::TrackpadPickResolve`,
`usbparse::routeSteamTrackpads`, the same ladder and router as dish-android's):
`stick | pad | mouse | off`, stored by slot id (`TrackpadPickRepository`), the
2015 pad defaulting to `stick` (its right pad the right stick and its click R3,
the behaviour before the pick) and the 2026 pad, which has a real right stick
and no `stick` choice, to `pad` when the emulated type has a touch surface,
else `off`. `pad` puts the left pad on the left half of the one wire surface
(finger 0, `x/2 - 16384`) and the right pad on the right half (finger 1,
`x/2 + 16384`), either click the surface's click; `off` drops both pads and
sends their clicks as `leftPadClick` / `rightPadClick`; `mouse` makes the right
pad the cursor and left button, the left pad's click the right button and its
vertical travel the wheel (4096 units a notch, the remainder carried), through
the protocol 2 POINTER frame's buttons byte and `scrollV`, which the Direct path
now carries end to end. The pick in force is held to what the path carries:
`pad` needs the type's surface, `mouse` the host's grant AND this client's
mouse routing (`reducer::kClientRoutesTouchpadAsMouse`, still deferred, so the
picker lists no Mouse yet). `test_steam_trackpads` pins, over every pick and
both models, that each pad position lands in at most one wire field and each
click in exactly one, and that outside `stick` the 2015 right stick stays
centred and nothing is R3. The pick reaches the claim as one atomic the read
loop copies before each decode; the slot's declared `touchpadMode` is the
pick's projection (`pad` → `ds4`, `mouse` → `mouse`, else `off`), not the
satellite's per-host touchpad pick, which a Steam slot's Apply leaves alone.
Unverified on hardware: the 2015 left pad's rotation sign follows SDL's
`RotatePad(-angle)`; the wheel's 4096 units a notch is a chosen feel.

The extended-buttons word (satellite `docs/contract.md` "Extended buttons",
SDL's names) is filled by the pads that have the buttons, each from the layout
SDL reads and none yet checked on hardware: the 2015 Steam Controller's two
grips as `leftPaddle1` / `rightPaddle1`, its full-pull trigger bits as the
trigger clicks, and a left-pad click under a finger as `leftPadClick` (with no
finger it stays the stick's L3; the right pad's click stays R3 while the right
pad is the right stick); the 2026 pad's QAM as `misc1`, R4/L4/R5/L5 as paddle
1-4, its pad and trigger clicks and its stick and grip touches; the DualSense
Edge's (`054C:0DF2`, never the plain DualSense) back and Fn buttons from input
byte 10 as paddle 1 and paddle 2; the Switch Pro's and Charging Grip's Capture
(shared byte 0x20) as `misc1`. On the Standard path SDL2's `paddle1`-`paddle4`
and `misc1` map the same way, so a pad whose SDL mapping names them (an Xbox
Elite through a HIDAPI driver, a DualSense Edge, the Steam Deck's own controls
on Linux) sources the word too. A slot claims `extendedButtons` exactly when
its path's pad has one of these (`SlotRouter::SlotHardware::hasExtendedButtons`).
What the host renders is the emulated type's business; the DualSense Edge type
and the trackpad pick are the next changes.

A Steam Controller receiver (the 2015 `28DE:1142`, the 2026 `28DE:1304` and
`28DE:1305`) serves up to four paired controllers, one HID interface each: 1 to
4 on the 2015 receiver, its keyboard on 0, and 2 to 5 on the 2026 ones (SDL's
`HIDAPI_DriverSteam_IsSupportedDevice` and
`HIDAPI_DriverSteamTriton_IsSupportedDevice`, the Linux kernel's
`steam_is_valve_interface`), read here from the `&mi_XX` part of each
collection's path. The Direct pick is the receiver's and claims every slot, all
of them or none. A slot is a pad of its own, with its own card and binding,
while its controller is there: from its connect event (the 2015 pad's
`ID_CONTROLLER_WIRELESS`, connected or newly paired, the 2026 pad's
`0x46`/`0x79`) or its first report, until its disconnect event, which the next
1 s scan turns into the card appearing or leaving. An empty slot stays claimed
to listen and is no pad; while no controller is on, the receiver keeps its own
card, which says so. The receiver's card carries the path toggle only and offers
no binding or type pick (`bindable` in `docs/QML_CONTRACT.md`): it stands for
the receiver, not for a pad, and its id is no pad's. A controller's binding and
type are picked on its own card, which keeps them across the controller turning
off and on. A slot's id is its interface, so a controller that comes back lands
on its card and its Moonlight binding; a satellite binding leaves with the
controller, as it does for any pad that goes, and never moves onto another
slot's. Its battery reading leaves with it too, and is shown again as soon as
the controller is back, changed or not. Known limitation: two receivers of the
same model collapse into one. The manager keys a receiver by (vid, pid,
interface) and the gateway re-finds a slot by vid, pid and interface, first
match wins, so only one receiver's slots are read and the other's controllers
reach nothing. The fix is to key a receiver by its device path and carry the
path in `UsbDeviceInfo`. The 2015 receiver's slot is asked what it holds
(`ID_DONGLE_GET_WIRELESS_STATE`) when it opens, the request SDL and hid-steam
both send; the answer is read off the slot's input stream the way SDL's
`HIDAPI_DriverSteam_InitDevice` reads it, where hid-steam's
`steam_get_conn_status` reads it back as a feature report. Every Steam
Controller's settings are its claim's output worker's: the 2015 pad's quiet
sequence goes when the pad is there (at the claim for a wired pad, on the
connect event or first report for a receiver's controller) and its restore
sequence when the claim is handed back while the pad still holds them; the 2026
pad's settings go on the worker's 3 s clock and are undone the same way. The
read thread only flags the arrival and the departure, so a slot's reports never
wait on a feature write. A pad that is unplugged is let go with nothing written
to it, and a feature write that fails because the pad is gone is not retried. No
receiver has been on hand: how an empty slot answers the feature reports (the
2015 state request, the 2026 pad's 3 s settings) is unverified.

## 5. Decisions, not gaps

- Android's on-screen pad takes controller audio from the phone's own mic and
  speaker. A physical pad, on every client, uses only its own USB endpoints and
  refuses on ambiguity; a Bluetooth pad gets none.
- The three audio caps are promises the host streams on alone, so every client
  advertises one only while it will carry the direction right now: the pad's
  route, the slot's switch, the host's grant as the client last read it, and
  no endpoint the platform refused to open. A refused open is kept per slot,
  lane and endpoint (`core/audio/AudioRefusal.h` here, `PadAudioRefusalStore`
  on Android, `RefusedEndpoint` on the Mac), is not retried at that endpoint,
  and lapses when the endpoint changes or the device departs and returns. A
  moved grant re-sends the bound slots' descriptors; the grant is read when a
  session opens, so a direction the host switches on reaches a connected client
  at its next session.
- Xbox Direct exists only on Android: XUSB owns those pads here.
- Virtual-pad skins, lightbar wash, cutout theme, Play billing, store
  screenshots: phone-only by nature.
- Android's keep-awake is fixed (screen and wake lock while streaming); the
  desktop setting is about PC sleep, a different problem.
- Mouse mode on desktop is a recorded deferral, not an oversight.
- The DualSense Edge type (catalog id 4, `dualsenseedge`, protocol 4) is the
  DualSense's bundled feature set plus `extendedButtons`, the four paddles the
  host renders as its back and Fn buttons. It is listed last and is never the
  type a slot defaults to: only a real Edge's own `emulates` hint or the user's
  pick lands on it. The desktops carry no per-type art or labels, so the picker
  shows the satellite catalog's name for it, as for every type.
- The Android protocol chip keys on the satellite's advertised version and
  reads "Dish update required" for a newer satellite that still accepts this
  build; this client keys the chip on the negotiation, so that case reads as
  fine. Red here means the session cannot open.
- The Diagnostics page leaves out these parts of Android's. Screen hold:
  keep-awake is the user's own setting here, and a page that held the display
  would be a second writer to the same inhibitor. The latency profiling bench:
  the desktop input path carries no per-stage timestamps, so the page shows the
  session's measured round trip instead. Host cards for Moonlight and Bluetooth
  hosts: every fact the desktop holds about a Moonlight session already renders
  on the Moonlight hosts page, the host app and GFE versions and per-pad report
  counts Android adds have no desktop source, and this client never pairs to a
  host as a Bluetooth gamepad. A controller bound to a Moonlight host still
  names it.
- Of Android's three radio cards the page shows Bluetooth, with the adapter's
  state; that card's permission, link-type and host-role rows have no desktop
  counterpart. The Wi-Fi card is left out because the desktop has no WLAN
  source, and the USB card because each controller card already shows its path
  and poll rate, while the endpoint packet facts Android adds have no desktop
  source.
- The Diagnostics bench tests every item a pad's path carries (§5b), through
  the ungated path, so a pad whose rumble switch is off can still be tested.
- The Diagnostics event log is localized like every other desktop string: the
  C++ vends tokens and the QML words them. Android keeps its log in English as
  export material for bug reports.

## 5b. The Diagnostics bench (dish-website `docs/diagnostics-bundle.md`, "The bench")

The controller inspector offers each item only where the pad's path carries it,
by the router's own answer for the game's writes (`reducer::benchOffers`), and
drives it from this client alone at the contract's strengths and times
(`core/reducer/BenchSteps.h`, dish-linux's shared core byte for byte). The light
bar, the player LEDs, the adaptive triggers and the mic lamp go back to what the
game last sent, or dark and off when it never sent one, and a value the game
sends during the test is held until the test ends (`core/reducer/BenchSurfaces.h`).
⚠️ throughout until a hardware pass: no pad has been on the bench yet. The SDL
citations below are SDL 2.32.10's sources, the version vcpkg pins here
(THIRD_PARTY.md).

| Item | USB Direct | USB Standard (SDL) | Bluetooth (SDL) |
|---|---|---|---|
| Rumble (`rumble.*`) | ⚠️ DS4, DualSense, Switch Pro, 2026 Steam Controller | ⚠️ where `SDL_GameControllerHasRumble` (g) | ⚠️ where `SDL_GameControllerHasRumble` (g) |
| Trigger rumble (`triggerRumble.*`) | ❌ (a) | ⚠️ where `SDL_GameControllerHasRumbleTriggers` (b) | ⚠️ where `SDL_GameControllerHasRumbleTriggers` (b) |
| Light bar (`lightbar.cycle`) | ⚠️ DS4, DualSense | ⚠️ where `SDL_GameControllerHasLED` | ⚠️ where `SDL_GameControllerHasLED` |
| Player LEDs (`playerLeds.cycle`) | ⚠️ DualSense, Switch Pro (h) | ⚠️ DualSense under SDL's HIDAPI driver only (c) | ⚠️ DualSense under SDL's HIDAPI driver only (c) |
| Adaptive triggers (`trigger.pulse`) | ⚠️ DualSense | ⚠️ DualSense under SDL's HIDAPI driver only (d) | ⚠️ DualSense under SDL's HIDAPI driver only (d) |
| Mic lamp (`lamp.on`, `lamp.pulse`) | ⚠️ DualSense | ⚠️ DualSense under SDL's HIDAPI driver only (d) | ⚠️ DualSense under SDL's HIDAPI driver only (d) |
| Speaker (`tone.play`) | ⚠️ the pad's own endpoint | ⚠️ the pad's own endpoint | ❌ (e) |
| HD haptics (`haptics.play`) | ⚠️ a 4-channel endpoint (f) | ⚠️ a 4-channel endpoint (f) | ❌ (e) |
| Microphone (`mic.level`) | ⚠️ the pad's own endpoint | ⚠️ the pad's own endpoint | ❌ (e) |
| Sticks (`stick.drift`, `stick.range`) | ✅ | ✅ | ✅ |

(a) No Direct family has trigger motors. The one that does, the Xbox One/Series
pad, never reaches raw HID here: Windows' Xbox driver owns it and presents only
an XInput-compatible HID collection (`IG_` in its device path), whose reports
are not the pad's own (§4, "XUSB owns it"; `usbout::FeedbackState`). SDL's own
hidapi skips those collections for the same reason ("XInput devices don't get
real HID reports", `src/hidapi/windows/hid.c:439-442`).
(b) SDL decides per pad, from its driver's `SDL_JOYCAP_RUMBLE_TRIGGERS`
(`SDL_JoystickHasRumbleTriggers`, `src/joystick/SDL_joystick.c:1511-1522`).
Every wired Xbox pad rides SDL's XInput backend here, because Dish turns
RAWINPUT off (`SDLGamepadBridge::applySdlHints`, the USER-object leak), and that
backend has no trigger motors: `WINDOWS_JoystickRumbleTriggers` returns
`SDL_Unsupported()` (`src/joystick/windows/SDL_windowsjoystick.c:718-721`), the
XInput backend reports `SDL_JOYCAP_RUMBLE` alone
(`src/joystick/windows/SDL_xinputjoystick.c:415-418`), and XInput's
`XINPUT_VIBRATION` names the two body motors only (`wLeftMotorSpeed` and
`wRightMotorSpeed`, set at lines 407-408). Windows.Gaming.Input does
carry them (`GamepadVibration.LeftTrigger` / `RightTrigger`,
`WGI_JoystickRumbleTriggers`, `src/joystick/windows/SDL_windows_gaming_input.c:859-877`),
but SDL leaves it every pad that XInput, DirectInput, RAWINPUT or HIDAPI
already holds (same file, 503-519), which on a desktop is every pad. The driver
that answers yes here is SDL's HIDAPI Xbox One driver, for every Microsoft pad
it opens (`ControllerHasTriggerRumble`, `src/joystick/hidapi/SDL_hidapi_xboxone.c:154-159`,
capability at 553-555): a pad whose HID interface is not an XInput collection.
No host message carries trigger rumble, so the bench is its only writer.
(c) The SDL path carries the player LEDs as the game's own writes do, through
`SDL_GameControllerSendEffect` for a DualSense SDL's HIDAPI driver owns, which
takes the mask byte itself (`test_bench_paths`). `SDL_GameControllerSetPlayerIndex`
is not used for any other pad. It lights nothing on SDL's XInput, DirectInput
and Windows.Gaming.Input backends (`WINDOWS_JoystickSetDevicePlayerIndex` and
`WGI_JoystickSetDevicePlayerIndex` are empty, `SDL_windowsjoystick.c:650-652`,
`SDL_windows_gaming_input.c:754-756`), and an XInput pad's ring shows the slot
Windows assigned it, which XInput has no call to set. SDL's HIDAPI drivers that
do light LEDs choose their own patterns, not the contract's masks (PS5:
`0x04, 0x0A, 0x15, 0x1B, 0x1F`, `SDL_hidapi_ps5.c:331-348`; Switch:
`1 << (index % 4)`, `SDL_hidapi_switch.c:720-730`), and the PS4 driver recolours
the light bar instead (`SetLedsForPlayerIndex`, `SDL_hidapi_ps4.c:659-664`). SDL2
has no query for whether a driver lights anything for an index
(`SDL_gamecontroller.h` offers `HasLED`, `HasRumble` and `HasRumbleTriggers`,
lines 1007-1033), and the call moves SDL's own player-index table
(`SDL_SetJoystickIDForPlayerIndex`, `SDL_joystick.c:1398`). A Switch Pro on the
SDL path is therefore not offered the test, as the game's player LEDs do not
reach it there either.
(d) Only `SDL_GameControllerSendEffect` reaches these, and on Windows only SDL's
HIDAPI drivers implement it: the XInput and DirectInput backends and
Windows.Gaming.Input return `SDL_Unsupported()`
(`SDL_windowsjoystick.c:737-740`, `SDL_windows_gaming_input.c:896-899`). For any
family but the DualSense the body would be the wrong report
(`core/reducer/FeedbackRouting.h`), and a DualSense SDL's HIDAPI driver does
not hold has no effect body (`SlotRouter::sdlHardware`).
(e) The speaker, the haptic lanes and the microphone are the pad's USB Audio
Class function, which only the USB link carries: over Bluetooth the pad is a HID
device to Windows (its HID collection's parent is the `BTHENUM` HID service,
§5a `bluetooth`), and the pad audio matcher routes USB pads only
(`AppModel::resolveAudioRoutes`; `SlotRouter::audioRouteForSlot` answers none
for a Bluetooth pad).
(f) The haptic lanes are channels 3 and 4 of the DualSense's own 4-channel
render endpoint, the mix format SDL reports for it
(`SdlAudioGateway::playbackDeviceChannels`, `SDL_GetAudioDeviceSpec`). A stereo
endpoint, a DualShock 4's headset jack, has no actuator pair, so the matcher
names no haptic route for it and `audio::benchTone` writes nothing there.
(g) A pad SDL opens as a raw joystick (no game-controller mapping) is offered
rumble where `SDL_JoystickHasRumble`, and the game's rumble and the bench's
both land on it through `SDL_JoystickRumble`, from the same mailbox the SDL
thread drains for a game controller (`SDLGamepadBridge::applyJoystickOutput`).
Rumble is all such a pad claims: its light bar, trigger motors and effect body
are never offered and never written (`test_slot_router`, over an SDL virtual
joystick that `SDL_IsGameController` refuses). dish-linux does the same.
(h) The Switch Pro has four player lights, and the claim masks the byte to them
(`usbout::buildPlayerLedsReport`), so the cycle's fifth mask, `10`, lights none
on it (`test_bench_paths`).

How the contract's per-path choices land here:

- Player LEDs are the five masks (`01` to `10`) on both paths, because both
  write the mask byte themselves: the claim's OUT report and the effect body
  `SDL_GameControllerSendEffect` takes (`test_bench_paths`, "the player LED cycle
  reaches both of a DualSense's paths as the contract's five masks"). Neither
  lights them by a player number, so the contract's players 1 to 4 apply to no
  path here.
- Rumble offers all three tests: SDL's rumble call always takes both motors,
  and every Direct family with rumble has two actuators
  (`usbout::buildRumbleReport`).
- The bench needs no host: a pad bound to none is offered what its path carries
  (`test_slot_router`, "a bar the game never set goes back dark when the bench
  gives it back").
- A surface the game never set goes back to the idle state, not to what the pad
  showed when the test began, because Windows cannot read that: the HID class
  API reads input and feature reports only (`HidD_GetInputReport`,
  `HidD_GetFeature`; `hidsdi.h` declares no output-report read and `hidclass.h`
  no `IOCTL_HID_GET_OUTPUT_REPORT`), these surfaces live only in the pad's
  output report, which neither the claim nor SDL's drivers read back, and SDL2
  has no getter for a colour, player lights or a trigger effect. Where Dish
  itself set the surface before the test (the mute lamp), that value is the one
  put back.
- A test's writes are not the game's: the `output` lanes in the file count none
  of them, and a game's value the test held back is counted when it arrives
  (`test_slot_router`). `lastWrite` is the claim's last write, a test's included.

What only a person with a pad on a Windows PC can prove: that each test is
felt, seen or heard; that the bar, the LEDs, the triggers and the lamp come back
to the game's state after a test during a game; that the tone reaches a
DualSense's speaker and its actuators on their own pairs of its WASAPI endpoint
and nothing of it is cut by the close; that the microphone's level moves with
the voice with Windows' microphone access on for desktop apps; and that a
Microsoft pad SDL's HIDAPI Xbox One driver opens answers
`SDL_GameControllerHasRumbleTriggers` and feels the trigger buzz.

## 5a. Controller database and diagnostics (dish-website `docs/controller-db.md`, `docs/diagnostics-bundle.md`)

Shipped in 2.8.0, ⚠️ throughout until a hardware pass: the suite covers every
decision, but no build of it has met a real pad or a real mail client yet. Where
this client does not do what the contracts say, the row says so and why.

| Contract item | Windows (this) |
|---|---|
| Bundled copy of dbVersion 1 (272 records), checked against its manifest at every load | ⚠️ `:/controllers/v1`, `test_controller_db_bundled` |
| Index `identity.usb` / `identity.bluetooth` → record; cached copy in force only when its `dbVersion` is greater | ⚠️ |
| Check: 10 s after launch, every 6 h, `If-None-Match`, SHA-256 + size per file, schema 1, atomic swap | ⚠️ the swap is a `current` file renamed over (`QSaveFile`) after the version's own folder is written |
| "Check for controller updates automatically" (default on) and "Check now" | ⚠️ Settings → Updates, and "Check now" on the Diagnostics card too; both "Check now" buttons are disabled while the switch is off, and the coordinator refuses one regardless, as section 6.4 has it |
| Compiled tables generated from the file, a test asserting they agree | ❌ deferred: `kKnownModels` and the fast-lane vendors (`controllerdb::vendorFastLane`) stay hand-written and `test_controller_db_bundled` requires each row to match its record (usb id, family, and for `hid` the mapping) and each fast-lane vendor to own a `verified` record; `test_pad_model` holds the model an attach pins with no database equal to the compiled answers for every compiled id |
| Served records classify ids the compiled tables lack | ⚠️ applied: a pad's model is resolved once per attach (`controllerdb::PadModelResolver`, an immutable snapshot of the active and bundled catalogs that `AppModel` installs whole when the store's active catalog changes) and pinned twice. `WinHidGateway`'s scan pins each HID collection by its interface path (`controllerdb::ModelPins`, forgotten when a scan no longer lists the path), because admission needs the served family before the manager has seen the pad; the one-second poll then only looks pins up. The manager pins the tracked device to the model its first scan carried, in `ensureTracked`, and stamps that model on every later scan's pads and on the claim, so a scan that re-resolved (a path that missed one scan while a database landed) changes nothing about a pad still tracked. The capability solve (`SlotRouter::syntheticHardware`, `AppModel`'s Direct and awaiting-claim slots, the audio routes), the claim (admission, receiver slot, button order), the read loop, the output worker, the health judgement and `DirectTarget::parser` read it; `SDLGamepadBridge` pins its pads' family at attach and `applyOutput` reads it. Order: the compiled tables, then the row in force (`directRecordFor`), then the vendor fallback. A served row's extended buttons, Steam trackpads and framework gamepad follow from its family and product id, as dish-android derives them; a served row is never a receiver |
| `requires` → inert (name and issues only), bundled row for the id stays in force | ⚠️ `controllerdb::isInert`, `directRecordFor`, which also lets a `flagged` row give way to a live bundled row for its id (docs/controller-db.md 4.1; a flagged row with no bundled twin still applies). The Xbox families are inert here by construction: XUSB hides those pads from raw HID |
| `expect.endpoints` at attach | ⚠️ against the claimed interface's interrupt endpoints, as the configuration descriptor the pad's hub returned lists them (`UsbDeviceInfo::observedEndpoints`), as dish-android's check reads its claim's endpoint pair. The hub's answer lands after the claim, so a Direct slot is matched once it has; where the hub refused, `InputReportByteLength > 0` and `OutputReportByteLength > 0` still stand for `endpoints.in` and `endpoints.out`, the one view of the pipes the HID class API gives. Checked on the Direct path only |
| "Last write" (route, report id, ok, OS error, time) | ⚠️ from each claim's output worker. `route` is `interruptOut` for an output report (`WriteFile`) and `setReport` for a feature report (`HidD_SetFeature`); which pipe the HID class driver used for a `WriteFile` is not observable, and it falls back to SET_REPORT(Output) on an interface with no OUT endpoint |
| "Last feedback" (host to client) | ⚠️ the top-level `lastFeedback` stays `null`; the same facts are now per lane under `output` (below) |
| Pad tests with "Did it work?" outcomes, last twenty, in memory | ⚠️ every test the contract names that the pad's path carries (§5b), with `worked` / `didNotWork` stamped with the time the test ended, one answer per run |
| Issue shown while `open`, for this transport on this machine model | ⚠️ `fixed` and `wontfix` never show; the model is the firmware's SystemManufacturer + SystemProductName; an `os` map matches only through its `windows` key, read by its major version (a number, or a string such as `11.0`) |
| Diagnostics: bundled / active `dbVersion`, source, last check time and result, per controller record id, verification and mismatches | ⚠️ |
| Bundle `dish-diagnostics.v1` | ⚠️ `path` is `direct` or `sdl` (the contract's `framework` is SDL here, and there is no `standard` block: SDL is the reader); a Direct pad's `hardware` carries vid, pid, `bcdDevice`, product, manufacturer, every interface with its endpoints, the claimed interface's `endpoints` and `descriptorSha256`, the SHA-256 of the device descriptor and every configuration as the hub returned them, which is the hash dish-android and dish-linux give the same pad (`test_usb_hardware` on a DualSense's real bytes); `firmware` is `null`, and the two HID report lengths stay beside them. `capture` is the row below; a Satellite's `version` is the `serverVersion` its capabilities last reported, `null` before any; `app.flavor` is `github` for the installer and the portable zip alike |
| The pad's raw hardware for `hardware` and `capture` | ⚠️ read once per attach: the manager starts `UsbDeviceGateway::readHardware` on a thread of its own when `ensureTracked` first meets a device (a hub's control transfers can take a sleeping pad's time), and the scan after it lands stamps the facts on the pad's record and on its open claim. `bcdDevice` is the device descriptor's, else `HidD_GetAttributes`' `VersionNumber`. The interfaces are the configuration descriptor's; where the hub gave none, the composite parent's children through CfgMgr32, each numbered by the `MI_xx` of its instance id and classed by its `USB\Class_xx&SubClass_xx&Prot_xx` compatible id (the forms Microsoft's "Standard USB Identifiers" lists), without endpoints; a device that is not composite is its own interface 0. The device descriptor and every configuration in index order (`bNumConfigurations` of them) come from the parent hub with `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION`, the hub opened for write and shared with no elevation, each configuration in two requests (its nine bytes, then `wTotalLength`), as USBView's `GetConfigDescriptor` does (microsoft/Windows-driver-samples `usb/usbview/enum.c` at 0250f5d, lines 751 and 1914 to 2066; libusb's `cache_config_descriptors` reads them the same way). They are kept only when the hub returned all of them, so the checksum is either the one other platforms give the pad or `null`. The port is the location information's `Port_#NNNN`, else the last `#USB(x)` of the location path, else the node's address, libusb's order (`get_dev_port_number`, `libusb/os/windows_winusb.c` at d3937ff). The capture's `strings` are the HID class driver's reading of the pad's iManufacturer and iProduct (`HidD_GetManufacturerString`, `HidD_GetProductString`), not string descriptors asked of the hub: the device descriptor's indices place them, a role whose index is 0 or the serial number's is `null`, and where the hub refused the HID strings stand alone, a role `null` when its string is empty. The HID report descriptor is the same hub request with bmRequest 0x81, wValue 0x2200 and wIndex the interface, for 4096 bytes, hidapi's fallback length, `source: device`. **The interface recipient does not reach the pad:** "the USB stack ignores the value of bmRequest and inserts a value of 0x80" (Microsoft's `USB_DESCRIPTOR_REQUEST` reference; USBView's comment says the same), so the pad sees a device-recipient request and its firmware decides whether to answer. A refusal keeps its Win32 error and no bytes, the list stays empty, and no descriptor is ever rebuilt from preparsed data, which is what libusb's HID backend hands back instead (`_hid_get_report_descriptor`). Not yet run against a pad |
| Capture, `usb-descriptors-v1` | ⚠️ *Capture this controller* on Report a problem, for the picked pad when it is a Direct claim: the claim's read loop keeps its next twenty input reports (64 bytes each at most, stamped on the steady clock and moved onto the wall clock by age) in slots fixed with the claim, then disarms itself; the page polls ten times a second for two seconds. An id-less collection's leading 0x00, which `ReadFile` prepends, is left off, so a report is the bytes the pad sent. `device` and `configuration` are the hub's, `hidReportDescriptors` the hub's answer for the claimed interface (`source: device`) or empty where the pad refused it, `strings` as the row above. Only the capture of the controller the report names enters the file. Hot path: the read loop's per-report `running` load is now one relaxed load of `diagnostics::ReadLoopWord`, which holds the capture's armed flag too |
| Email: file under the data folder, `MAPISendMailW`, else `mailto:` and the folder revealed | ⚠️ MAPI runs on its own thread with the compose window shown; a closed compose window is the person's answer and opens no fallback. A `mailto:` body is cut at 2000 characters, at a whole character |
| "Report a problem" / "Ask a question": category, own words, controller, plain-words list, "Preview file", the never-holds sentence, "Continue in your mail app" | ⚠️ one page (`ReportProblemPage.qml`) in two starting categories, reached from the Diagnostics page's "Send to support" card, which keeps *Save diagnostics*. The controller picker starts on the first attached pad. "Continue" waits for the snapshot answers (two seconds at most). Subject prefix `Dish support:` English in every locale |
| `report` in the file and first in the mail body | ⚠️ `controller` is the record id, else the slot's product string, else `null` |
| `hosts[i].snapshot` from `GET /api/connections/{id}/diagnostics` | ⚠️ every live Satellite link, through `WifiConnectionManager`'s pinned HTTPS and proof, two-second deadline. Only a JSON object of `satellite-connection-diagnostics.v1` is embedded; its host's label and address are replaced by the alias wherever they appear, keys included. 404 is `unknownConnection`, 401/403 `unauthorized`, the deadline `timeout`, a 5xx or a 200 whose body is not a snapshot `badReply`, no answer at all `offline` |
| `olderSatellite` from the version, the route not called | ⚠️ below 2.3.0 by the capabilities' `serverVersion`; a version that does not read as numbers is asked. `satellite-capabilities` carries `protocol` as `[1, <capabilities' protocolVersion>]`: the capabilities route names only the ceiling, and every Satellite speaks 1 |
| `moonlight-serverinfo` | ⚠️ `appVersion` only, from the last answered `/serverinfo`; `state` and `pairStatus` are `null`, because this client asks `/serverinfo` only over plaintext, where Sunshine reports every host free and `PairStatus` 0 for every caller. Moonlight's `session` lists its bindings (record, the emulated-device pick as `requestedType`) and is `null` for ack, round trips and counts: GameStream reports nothing back and this client counts nothing on that path |
| `hosts[i].session`: slots, ack, RTT, `sent`/`received` per kind | ⚠️ `requestedType`/`appliedType` are the satellite catalog's slug, or the wire number while no catalog is cached; `backend` is the host's active backend from its capabilities (the apply result names none per slot); `rtt` is the heartbeat window's p50/p99. `sent`/`received` are new relaxed counters in `SatelliteClient` (hot path); the heartbeat and its ack are not counted |
| Controller `input` (frames, rateHz, last) | ⚠️ the processor's existing per-device counter and seqlocked last wire report; `rateHz` the slot's current gamepad rate |
| Controller `output` per lane: count, target, last, `lastWrite` | ⚠️ new relaxed counters in `SlotRouter` (hot path), counted when the router hands a host's message for a lane to the pad, one a bench test holds back included; the bench's own writes are not counted (§5b); `target` is `directUsb` / `framework` (SDL) / `none`; `lastWrite` rides `rumble` and is the claim's last write, a test's included. `triggerRumble` is `null`: no host message carries one, and the bench's trigger buzz is not the game's. The trigger-effect lane keeps only when, not its 22 bytes |
| `speaker` / `haptics` voices | ⚠️ `open` / `closed` / `refused` from the playout engine, `device` `pad` while open (it opens only the pad's endpoint); `frames` is `null`, no per-voice count is kept; `null` for a pad with no such lane on this PC |
| `bluetooth` | ⚠️ `{ hid, classic }` or `{ ble, le }` from the HID path's service class or enumerator, read at attach on the SDL path; the path never leaves. Evidence for the rule: hidapi decides the same from the HID collection's parent node, whose compatible ids name `BTHENUM` for a classic link and `BTHLEDEVICE` for an LE one, flagged `HID_API_BUS_FLAG_BLE` (`hid_internal_detect_bus_type`, libusb/hidapi `windows/hid.c` at 16a3ce9, lines 686 to 695). That parent is the HID service (0x1124) under BTHENUM and HID over GATT (0x1812) under BTHLEDEVICE, and the collection's own instance id, which its path embeds, names the same service, so the path rule and hidapi's agree and behaviour is unchanged |
| `device.battery`, `powerSave` | ⚠️ `GetSystemPowerStatus`: no battery is `null`, an unknown level is `null`, `charging` is the battery flag's charging bit, `powerSave` is battery saver |
| `device.usbHost` | ⚠️ measured: the present device interfaces of class `GUID_DEVINTERFACE_USB_HOST_CONTROLLER` (`{3ABF6F2D-71C4-462A-8A92-1E6861E6AF27}`, Usbiodef.h) that `SetupDiGetClassDevsW(..., DIGCF_PRESENT \| DIGCF_DEVICEINTERFACE)` lists, counted to `ERROR_NO_MORE_ITEMS`. At least one is `true`, none is `false`, a set that would not open or a walk that ended on any other error is `null` (`diagnostics::usbHostFromCount`). Evidence: Microsoft's page for the GUID, "The system-supplied port driver for a USB host controller registers instances of GUID_DEVINTERFACE_USB_HOST_CONTROLLER to notify the operating system and applications of the presence of USB host controllers", and USBView enumerating host controllers with the same call (`enum.c` line 284). Read whenever a file is prepared, by *Save diagnostics* and the report page alike. Not yet run on a machine without a USB controller |
| `device.thermal`, `device.wifi` | ❌ `null`: Windows names no thermal state to an app, and this client does not link the WLAN API (`wlanapi`), so band and RSSI are not read |
| Satellite's `/api/controllers/v1` mirror | – clients do not read it in schema 1 |

## 6. Android PRs since the diary stopped (2026-08-18 → 2026-09-20)

| Android PR | Counterpart here | Disposition |
|---|---|---|
| #162 Guide bit in XInput360 decode | – | N/A (no XInput Direct) |
| #163 offer the wired path when a Bluetooth pad is plugged in | #52 | ported |
| #164, #165, #169 Play notes, donation flavour, APK name | – | N/A |
| #170 Moonlight | #60 | ported |
| #173 link-tier cues | this wave | ported |
| #174 per-type skins (+ protocol chip) | this wave (chip) | skins N/A, chip ported |
| #175 protocol 2 | #61 | ported |
| #176 lightbar wash | – | N/A |
| #177 input-path-honest capabilities | this wave ("Supported") | ported; this client's capability solver has no Unknown state |
| #178 controller audio (+ app-wide mic chip) | #66 + this wave | ported |
| #182, #183, #186, #190, #197 changelog / notes | – | N/A |
| #184 privacy policy | #73 | ported |
| #185 Diagnostics | this wave | ported; what the desktop leaves out is in §5 |
| #187 to #206 Play billing, products, screenshots, promote | – | N/A |
| #191 cutout theme | – | N/A |
| #198, #199 test / CI only | – | N/A |
| #210 haptics, framework lightbar/touchpad, Direct battery | #81 | ported (⚠️ until a hardware pass) |
| #213 fake satellite applied state | – | test infra |
| #214 to #218 Crashlytics-driven fixes | checked: not needed here | Android-only |

Desktop PRs with no Android counterpart and no action needed: keep-awake modes
(#53), release hardening (#54, #77), the shared build story (#67), tray and
suspend survival (#86), DPAPI-encrypted secrets (#87).
