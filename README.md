# Dish for Windows

Dish for Windows runs on your Windows PC and sends the controllers plugged into it, or paired with it over Bluetooth, to your gaming PC as wireless controllers.

Dish is made of two parts. The Dish apps run on the device that holds your controller, or that is the controller: Dish for Android, Dish for iOS, Dish for macOS, Dish for Windows and Dish for Linux. Satellite is the small server on the PC you play on, and it makes each Dish controller look to your games like a controller plugged into that PC. You need one of each. Everything is free and open source, the website is [dish.tinkernorth.com](https://dish.tinkernorth.com), and I maintain all of it on my own.

## Download

Dish for Windows 2.8.0 is the current release. Every link below always points at the newest one.

Windows 10 version 1809 or later, or Windows 11, on x64: download **[dish-setup.exe](https://github.com/TinkerNorth/dish-windows/releases/latest/download/dish-setup.exe)** and run it. It installs Dish for you alone, with no administrator prompt, and carries everything Dish needs, the Visual C++ runtime included. Once installed, Dish keeps itself up to date: it reads `latest.json` from this repository's newest release about 15 seconds after it starts and every four hours while it runs, downloads the new `dish-setup.exe` in the background, checks it against the SHA-256 that `latest.json` gives, and installs it the next time you start Dish, or straight away when you press Restart to update.

If you'd rather not install anything, [dish-windows.zip](https://github.com/TinkerNorth/dish-windows/releases/latest/download/dish-windows.zip) holds the same app. Unzip it anywhere and run `dish.exe`. The zip never updates itself; it tells you when there is a new version, and you download it again.

### Running it the first time

I don't have a code-signing certificate yet, so the installer isn't signed, and Microsoft Defender SmartScreen stops it the first time you run a fresh download. It says "Windows protected your PC". Click More info, then Run anyway. The updates Dish installs itself don't show this, because Windows doesn't mark a file an app downloads for itself. Check the download against its checksum first; how is under Trust and privacy.

All files and older versions are on the [releases page](https://github.com/TinkerNorth/dish-windows/releases).

### Satellite and the Dish apps

| App | Where to get it |
|---|---|
| Satellite (Windows, Linux) | [GitHub releases](https://github.com/TinkerNorth/satellite/releases/latest) |
| Dish for Android | [Google Play](https://play.google.com/store/apps/details?id=com.tinkernorth.dish), or the APK from [GitHub releases](https://github.com/TinkerNorth/dish-android/releases/latest) |
| Dish for iOS | Not yet released |
| Dish for macOS | [GitHub releases](https://github.com/TinkerNorth/dish-mac/releases/latest) |
| Dish for Windows | [GitHub releases](https://github.com/TinkerNorth/dish-windows/releases/latest) |
| Dish for Linux | [GitHub releases](https://github.com/TinkerNorth/dish-linux/releases/latest) |

## Getting started

1. Install Satellite on the PC you play on and start it. Its dashboard shows a 4-digit PIN under Device Pairing, with a new one every five minutes.
2. Install Dish for Windows on the PC your controller is connected to, plug the controller in or pair it in Windows' Bluetooth settings, and start Dish. Both PCs have to be on the same local network. On a first install, the setup asks about the three things that can leave your PC (they are listed under Trust and privacy); Windows may also ask once to let Dish through its firewall.
3. Pair them. Open Connections: the PC appears under Found once Satellite answers (Scan looks again). Press Pair… and type the PIN from the dashboard. If Dish shows you a PIN instead, approve the request on the dashboard. Dish remembers the PC, so you only pair once.
4. Play. On Home, Set up a controller walks you through the controller, the PC, how the PC should see it (from the types Satellite offers) and how it should feel, and Bind on its last step makes it live. Bind… on a controller on Home does the same for that one, and Configure binding… changes it later: Emulate as picks the type the game sees, and Apply makes the change live.

Closing the window keeps your controllers streaming. Quit from the tray icon.

## What it does

Dish reads each controller on your PC and sends it to the PC you bound it to, where Satellite turns it into a virtual controller. Input travels over UDP on your own network, encrypted with a key the two PCs agree on when they pair. Several controllers can go to several PCs at once. Dish reads real controllers only; the on-screen touch controller is Dish for Android's.

Every controller Windows and SDL recognise works over USB and Bluetooth on the Standard path, through SDL. Over USB, Dish can also read a DualSense or DualSense Edge, DualShock 4, Switch Pro Controller, PDP Switch pad, 8BitDo pad in HID mode, or Steam Controller itself on the Direct path; Input path on each controller's card, on the Controllers page, switches between the two. Bluetooth is never Direct, and wired Xbox pads stay on Standard because Windows' own Xbox driver holds them. Dish carries the list of controllers the whole Dish family recognises, which decides the pads that go Direct by themselves, and checks for a newer one, so a new controller doesn't have to wait for a new release.

Motion, touchpad and battery go to the PC; the battery level sent is the lower of the controller's and, on a laptop, the laptop's own. Rumble, the light bar, player lights, adaptive triggers and the mute lamp come back from the game, and over USB a DualSense or DualShock 4 can carry the PC's sound and your voice through its own speaker and microphone. What reaches the game also depends on the type you emulate and on the PC; Satellite's README has that table.

I don't own every controller. The 2015 and 2026 Steam Controllers and their receivers, the back paddles and extra buttons, DualSense HD haptics, and most of what comes back to a controller over Bluetooth are built but not yet confirmed on real hardware. A Steam Controller has to be on USB, or on its receiver, for Dish to read it Direct.

The Diagnostics page shows what each connection and controller is doing. Its controller inspector tests what a controller's path carries: rumble, trigger rumble, the light bar, the player lights, the adaptive triggers, the mic lamp, a tone on the speaker and on the haptics, the microphone and the sticks, all from your PC alone, and asks after each test whether it worked. Between that and Satellite's own view, you can tell whether a problem is the controller, Dish or the PC. No controller has been through these tests on Windows yet.

Dish doesn't need Satellite to play on a Sunshine, Apollo or Wolf host. Under Connections, Moonlight hosts finds them on your network or takes an address, and Dish shows a PIN to type into the host's page. Dish is then the controller for that host; it holds the session open but never shows the stream.

Settings can keep the PC awake while you play, or for as long as a controller is streaming. Closing a laptop's lid still puts it to sleep, as the Windows power plan says, and Settings explains how to change that in Windows. After sleep, Dish reconnects to Satellite by itself; Moonlight sessions don't come back by themselves yet. Dish follows Windows' light or dark mode, or the one you pick, and speaks English, Bosnian, German, Spanish, French and Brazilian Portuguese. The setup speaks the same languages except Bosnian.

## Requirements

Windows 10 version 1809 (build 17763) or later, or Windows 11, on x64. The setup refuses an older Windows. There is no ARM64 build; the setup accepts Windows on ARM, which runs the x64 build under emulation.

The setup installs Dish in one folder, `%LOCALAPPDATA%\Programs\Dish`, with a Start menu entry, a desktop shortcut if you tick it, and an entry in Installed apps. It also offers to install for all users, in `C:\Program Files\Dish`, which asks for administrator rights once, and once more for each update. It installs no service and no scheduled task, and nothing of Dish runs while Dish is closed. Uninstalling from Installed apps removes the folder, the shortcuts, the setup's privacy answers and any downloaded update.

Your settings and paired PCs live in the registry under `HKEY_CURRENT_USER`, with the pairing keys encrypted for your Windows account. If Dish crashes, it writes `crash.log` and `crash.dmp` to `%LOCALAPPDATA%\Dish`.

Network: both PCs on the same local network. Dish finds Satellite over mDNS (UDP 5353) and Satellite's older UDP beacon on port 9879, pairs and talks to it over HTTPS on TCP 9443, and streams on UDP 9876. A Moonlight host is paired over HTTP on port 47989 and called over TLS on 47984.

## Trust and privacy

Every release has a `SHA256SUMS` file signed with cosign by the release workflow. To check a download on Linux, put `SHA256SUMS` beside it and run:

```bash
sha256sum --ignore-missing -c SHA256SUMS
```

On Windows, `Get-FileHash .\dish-setup.exe` in PowerShell prints the hash, in capitals, to compare with the `dish-setup.exe` line of `SHA256SUMS`. [SECURITY.md](SECURITY.md) has the signature and build provenance checks, and how to report a vulnerability.

The installer, `dish.exe` and the crash reporter beside it carry no Authenticode signature, because I don't have a certificate yet. The release workflow signs the installer as soon as one is configured; until then every release ships unsigned, which is why SmartScreen asks you to let it through once (the steps are under Download). The checksum and its cosign signature are how you can tell a file came from this repository's release. An update Dish installs itself is checked against the SHA-256 in `latest.json` before it runs.

Dish for Windows works on your own network and has no account, no analytics and no ads. Three things can leave your PC, and each has a switch in Settings: a check for a new version, which reads `latest.json` from this repository's releases on GitHub (Check for updates automatically); a check for a newer controller database at dish.tinkernorth.com, about ten seconds after Dish starts and every six hours (Check for controller updates automatically); and, if Dish crashes, one crash report to Sentry (Share crash reports). All three are on by default, and the setup asks about each on a first install. Neither check sends an identifier. Controller input and the controller's microphone go only to the PC you paired with. The diagnostics file from Report a problem is something you send yourself; Dish never sends it. [PRIVACY.md](PRIVACY.md) says exactly what is stored and sent.

Dish for Windows is licensed under the GNU Lesser General Public License v3.0 or later ([LICENSE](LICENSE), with [COPYING.GPL3](COPYING.GPL3)). The third-party components and their licences are in [THIRD_PARTY.md](THIRD_PARTY.md).

## For developers

Dish for Windows is C++20 with a Qt Quick interface on Qt 6.12.1, built with CMake, MSVC and vcpkg. The presets in `CMakePresets.json` (`debug` and `release`) carry the same flags CI uses, warnings as errors included, and the scripts drive them from any PowerShell window:

```powershell
scripts\install-deps.ps1        # once: the toolchain CI uses, at the versions scripts\tools.ps1 pins
scripts\build.ps1 debug test    # build, then run the tests
scripts\ci-local.ps1            # every CI gate, in CI's order
```

`scripts\build-installer.ps1` compiles `dist\dish-setup.exe` with Inno Setup 6.7.4 from a release build, and `scripts\ci-local.ps1 -WithInstaller` also installs, checks and uninstalls it on your own machine.

Where things are written down:

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the layers, the state primitives, the threads and the input hot path, sleep and the lid, and what is not built yet.
- [docs/contract.md](docs/contract.md): where each part of the Satellite protocol lives in this tree.
- [docs/PACKAGING.md](docs/PACKAGING.md): the installer, silent installs, the portable zip and the update that installs itself.
- [docs/QML_CONTRACT.md](docs/QML_CONTRACT.md) and [docs/QML_UI_KIT.md](docs/QML_UI_KIT.md): the interface's binding surface and its component kit.
- [docs/QML_MANUAL_SMOKE_CHECKLIST.md](docs/QML_MANUAL_SMOKE_CHECKLIST.md): what is checked by hand before a release.
- [tests/PARITY.md](tests/PARITY.md): what this client has compared with the others, and what is confirmed on hardware.
- [CHANGELOG.md](CHANGELOG.md): what changed in each release.

[CONTRIBUTING.md](CONTRIBUTING.md) covers setting up, where code goes, the interface, translations, licence headers, the code style, the tests, branching, what CI runs, security checks, how a release is cut, and changing the hot path or the wire protocol. Please read it before opening a pull request.

## Getting help

If something doesn't work, open an issue at [github.com/TinkerNorth/dish-windows/issues](https://github.com/TinkerNorth/dish-windows/issues/new/choose). The most useful thing you can attach is the diagnostics file: in Settings, open Diagnostics…, press Report a problem, say what happened and pick the controller, and press Continue in your mail app; Preview file shows you the file first. The file leaves out pairing keys, addresses, Wi-Fi names and the names of your PCs and devices; read it before you attach it, it's plain JSON. Continue in your mail app starts a mail to support@tinkernorth.com with the file attached, so if you'd rather write to me than open an issue, send that mail; nothing leaves your PC until you press Send. The new Outlook and the Mail app can't take the file, so Dish also shows it in Explorer for you to attach.

Please also say which Dish for Windows version you run and whether you installed it or use the zip, your Windows version, which Satellite or Moonlight host and version you use, and which controller. If the problem is in Satellite rather than here, its own repository is the better place. For a security problem, don't open a public issue; [SECURITY.md](SECURITY.md) says how to report it privately.
