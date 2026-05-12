# Dish Windows

Native Windows desktop client for the Satellite wireless-gamepad server.
Mirrors the functionality of the Dish Android, Mac, and Linux clients: LAN
discovery, PIN pairing, encrypted UDP input streaming (ChaCha20-Poly1305),
heartbeats, and multiple parallel server sessions.

Physical controllers only — no virtual on-screen touch gamepad (matches
dish-mac; the touch overlay belongs to dish-android where the form factor
makes sense).

## Architecture

```
Qt6 Widgets (MainWindow, ConnectionsDialog, PairingDialog, SlotCard)
  └── AppModel (QObject, UI thread)
        ├── ConnectionHub      ── aggregates live + remembered sessions
        ├── WifiConnectionManager
        │     └── WifiConnection (per-server)
        │           └── SatelliteClient  ── encrypted UDP + heartbeat + ACK loop
        ├── LANDiscovery       ── UDP broadcast listener on :9879
        ├── PairingClient      ── TCP pair handshake on :9878
        ├── HTTPClient         ── POST/DELETE /api/connections on :9877
        └── SDLGamepadBridge   ── SDL_GameController event pump (own thread)
              └── GamepadInputProcessor → SatelliteClient.sendReport()
```

### Hot path (input → wire)

The hot path is the only thing that runs at gamepad polling rate; every
other subsystem is bookkeeping and stays off it.

```
  ┌────────────────┐      ┌───────────────────────┐      ┌──────────────────────┐
  │ SDL gamepad    │ ───► │ GamepadInputProcessor │ ───► │ SatelliteClient      │
  │ thread         │      │  • XUSB packing       │      │  • ChaCha20-Poly1305 │
  │ (own std::thr) │      │  • axis/trigger scale │      │  • IP_TOS = DSCP EF  │
  └────────────────┘      │  • atomic counter     │      │  • raw sendto()      │
                          └───────────────────────┘      └──────────┬───────────┘
                                                                    │
                                                                    ▼
                                                              UDP :9876 → server
```

No queue, no Qt event hop, no cross-thread signal. The UI thread never
appears on this path; it only updates 1 Hz telemetry from counters the
SDL thread publishes lock-free.

## Low-latency strategies (mirrored from Android / Mac / Linux)

- **Direct `sendto()` from the SDL gamepad thread.** Each `SDL_CONTROLLER*`
  event fires the native Winsock send inline — no queue, no Qt event hop,
  no cross-thread signal. Same pattern as the POSIX siblings.
- **Raw Winsock UDP socket** (not `QUdpSocket`) so we can set `IP_TOS = 0xB8`
  (DSCP EF class, expedited forwarding) and bypass framework-level queueing.
  *Caveat:* Windows since Vista strips `IP_TOS` from outbound packets by
  default for non-admin processes. The `setsockopt` call doesn't fail, so
  we set it anyway in case the user has the `DisableUserTOSSetting=0`
  registry override on; meanwhile the rest of the latency win still holds.
  A future enhancement could wire up qWAVE.dll (`QOSCreateHandle`) for
  proper DSCP marking — see `HANDOFF.md`.
- **SDL2 → XInput on Windows.** SDL_GameController uses XInput natively for
  Xbox-class controllers (the lowest-latency Windows gamepad API) and falls
  back to the SDL HID layer for everything else.
- **libsodium `crypto_aead_chacha20poly1305_ietf`** produces the exact same
  wire format as the Android JNI / CryptoKit.ChaChaPoly / libsodium used by
  the other clients and the Satellite server.
- **Per-session heartbeat + ACK threads** so the hot input path is never
  contended by book-keeping traffic.
- **Lock-free `AtomicCounter` for the nonce** and a single short-held mutex
  on the routing-table lookup keep the hot path branch-free and
  allocation-free.
- **No SIGPIPE on Windows.** Winsock doesn't raise signals on a remote
  disconnect, so `MSG_NOSIGNAL` is `#define`d to 0 and we get the same
  "send to a closed peer survives" guarantee for free.

## Cross-platform behaviour parity

The following behaviours mirror dish-android, dish-mac, and dish-linux, so
user-visible behaviour stays predictable across platforms:

- **Display-sleep inhibitor while streaming.** A `ScreenWakeController` reads
  `hub.bindings × hub.connections`, derives a streaming-slot count, and
  calls `SetThreadExecutionState(ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED |
  ES_CONTINUOUS)` on every 0↔positive transition. Released on the last
  unbind / disconnect, so a forgotten session doesn't pin the display awake
  forever. Works against Modern Standby / "S0 low-power idle" on laptops
  (Windows uses the same flag for both screen-off and system-sleep).
- **Connection state recovery.** `PairingClient` carries a `reachable` flag
  on every `PairResponse` (true iff we received a JSON body). `classify(...)`
  splits the outcome into `Success | AuthRequired | Unreachable`; the
  manager fans those out to either `openSession`, a PIN dialog, or an error
  toast. A moved/offline server now surfaces a clear *"Server unreachable
  — has it moved networks?"* message instead of trapping the user behind an
  unanswerable PIN prompt. Mirrors dish-android PR #43.
- **Auto-reconnect fast path.** `WifiConnectionManager::pairAndConnect`
  skips the TCP pair handshake entirely when an empty PIN comes in and a
  64-char shared key is already on disk, going straight to `openSession`.
  A moved server then fails fast in the HTTP layer rather than bouncing
  through pair → `PairingRequired`.
- **Per-device deadzones.** `GamepadInputProcessor` carries a per-device
  `Deadzones { stickFlat, triggerFlat }` table; reports are filtered
  (`|v| <= flat → 0`) before they leave the processor. The default profile
  (~10 % stick / ~5 % trigger) is installed by `SDLGamepadBridge` when each
  controller attaches. Mirrors Android's per-device
  `InputDevice.getMotionRange(axis).getFlat()` pipeline.
- **Device-capability log on attach.** Every `SDL_CONTROLLERDEVICEADDED`
  logs a one-shot `DEVCAPS` line via the `dish.input` Qt logging category,
  carrying the stable id, controller name + type (SDL's
  `SDL_GameControllerType` enum), USB VID / PID, and the SDL GUID. Aimed
  at users reporting *"my pad doesn't work"* — same idea as Android's
  SatelliteJNI `DEVCAPS` log.

## Requirements

- Windows 10 (build 19041 / 20H1) or newer, 64-bit
- Visual Studio 2022 with the "Desktop development with C++" workload, or
  the standalone Build Tools for Visual Studio 2022
- CMake 3.21+
- Ninja (`winget install Ninja-build.Ninja` or `choco install ninja`)
- Qt 6.2+ (the CI workflow installs 6.7.3 via `jurplel/install-qt-action`)
- libsodium 1.0.18+ and SDL2 2.30+ (via vcpkg — see below)
- A compatible gamepad (Xbox One / Series controller via XInput, DualSense,
  DualShock 4, 8BitDo, …)
- A Satellite server reachable on your LAN

### Install build dependencies

The fastest path is the bundled installer — same idea as
`satellite/install-dependencies.bat`. From an elevated cmd / PowerShell:

```cmd
install-dependencies.bat
```

That runs five idempotent steps via winget + aqtinstall + a vcpkg
bootstrap:

1. Visual Studio 2022 Build Tools (Desktop C++ workload + Win11 SDK)
2. CMake + Ninja
3. LLVM (clang-format + clang-tidy)
4. Python 3 + aqtinstall + Qt 6.7.3 to `C:\Qt`
5. vcpkg cloned to `%USERPROFILE%\vcpkg`, with `VCPKG_ROOT` +
   `CMAKE_PREFIX_PATH` persisted to your user env

Total ~12 GB download, ~30–60 min wall-clock on a clean Win11 box.
Some installers (VS Build Tools in particular) trigger a UAC prompt —
approve them when asked.

If you'd rather drive the install by hand:

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools `
    --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621"
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install LLVM.LLVM
winget install Python.Python.3.12
python -m pip install --user --upgrade aqtinstall
python -m aqt install-qt windows desktop 6.7.3 win64_msvc2019_64 --outputdir C:\Qt
git clone https://github.com/microsoft/vcpkg.git $env:USERPROFILE\vcpkg
& $env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat -disableMetrics
[Environment]::SetEnvironmentVariable('VCPKG_ROOT', "$env:USERPROFILE\vcpkg", 'User')
[Environment]::SetEnvironmentVariable('CMAKE_PREFIX_PATH', 'C:\Qt\6.7.3\msvc2019_64', 'User')
```

`vcpkg.json` pins `libsodium` and `sdl2`; the CMake toolchain file
(`$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake`) resolves and builds
them on the first `cmake` configure.

## Build & Run

After `install-dependencies.bat` has finished, open a fresh PowerShell
window (any window — the build script auto-finds MSVC via `vswhere` and
sources `vcvars64.bat` for you):

```powershell
cd dish-windows
scripts\build.ps1 release
.\build-release\dish.exe
```

For a debug build with tests:

```powershell
scripts\build.ps1 debug test
```

Or the long form:

```powershell
cmake -S . -B build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --parallel
.\build\dish.exe
```

## Install (per-user)

`dish.exe` needs its companion Qt DLLs to run outside the build tree. The
canonical Windows tool for that is `windeployqt.exe`, shipped with Qt:

```powershell
mkdir $env:USERPROFILE\Apps\Dish
Copy-Item build-release\dish.exe $env:USERPROFILE\Apps\Dish\
& "$env:QT_DIR\bin\windeployqt.exe" --release --no-translations `
    $env:USERPROFILE\Apps\Dish\dish.exe
```

The release CI workflow does the same thing and uploads a zip you can
unpack anywhere on `%PATH%`.

## Project Layout

```
dish-windows/
├── CMakeLists.txt
├── vcpkg.json                 # libsodium + SDL2 deps for vcpkg manifest mode
├── install-dependencies.bat   # one-shot toolchain installer (mirrors satellite)
├── scripts/build.ps1
├── packaging/
│   ├── dish.ico               # embedded into dish.exe via dish.rc
│   ├── dish.png               # 256 × 256 source asset
│   ├── dish.svg               # vector source
│   └── dish.rc                # version info + icon resource
└── src/
    ├── main.cpp               # QApplication entry + WSAStartup + sodium_init
    ├── AppModel.{h,cpp}       # top-level QObject
    ├── Models/                # DiscoveredServer, PairResponse, …
    ├── Network/               # Winsock sockets, crypto, discovery, pairing, HTTP
    ├── Input/                 # SDL bridge + XUSB mapping
    ├── Util/                  # AtomicCounter, hex, endian helpers, sleep inhibitor
    └── UI/                    # Qt widgets + theme
```

## Protocol parity

All message types, byte layouts, port numbers and JSON shapes match the
other Dish clients verbatim so all four can talk to the same server and
appear identical to it:

| Field            | Value            |
| ---------------- | ---------------- |
| Discovery port   | UDP 9879 (listen)|
| Pairing port     | TCP 9878         |
| HTTP API port    | TCP 9877         |
| Streaming port   | UDP 9876         |
| AEAD             | ChaCha20-Poly1305 IETF |
| Nonce            | counter, BE, left-padded to 12 bytes |
| Packet layout    | `token(4) \| counter(4) \| ciphertext+tag` |
| AAD              | token (4 bytes)  |
| XUSB report      | 12 bytes, little-endian |
| Heartbeat period | 2 s              |
| Miss threshold   | 5 consecutive    |

## Testing

```powershell
scripts\build.ps1 debug test
# or
ctest --test-dir build-debug --output-on-failure
```

Unit tests cover the hex/byte-packing utilities, the big-endian helpers,
the XUSB input mapping (axis and trigger scaling, button bitfield,
per-device deadzone application, zero-on-disconnect fan-out), the
lock-free atomic counter under contention, the lenient beacon JSON
decoder, the model codable round-trips, the `PairingClient::classify`
outcome arms (Success / AuthRequired / Unreachable), and the
`ScreenWakeController` acquire/release lifecycle via a fake
`DisplaySleepInhibitor` (so the suite never has to actually flip
`SetThreadExecutionState`). They run in well under a second and do not
open sockets.

## Development

Install the build dependencies (see above), then enable the pre-commit
hook:

```powershell
scripts\build.ps1 debug          # generates build-debug/compile_commands.json
scripts\setup-hooks.ps1          # points core.hooksPath at .githooks/
```

Format / lint manually (Git for Windows ships the bash shell the hook
runs under; the CLI binaries below come from LLVM):

```powershell
clang-format -i $(git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp')
clang-tidy -p build-debug $(git ls-files 'src/*.cpp')
```

See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the full workflow, header
policy, and review expectations.

## License

Distributed under the terms of the **GNU Lesser General Public License v3.0
or later**. See [`LICENSE`](LICENSE) (LGPL) and [`COPYING.GPL3`](COPYING.GPL3)
(the GPL v3 the LGPL incorporates by reference).

## Contributing

Changes should land on `main` through a pull request. The `Windows CI`
workflow (`.github/workflows/windows-ci.yml`) runs the `clang-format`
check, the debug build + ctest, `clang-tidy`, and a release build on every
PR and on `main` pushes. The `Security` workflow
(`.github/workflows/security.yml`) and `CodeQL` workflow
(`.github/workflows/codeql.yml`) run alongside it — action-pin lint,
OSV-Scanner, gitleaks, dependency review, allowlist-expiry check, and
CodeQL `cpp` analysis. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the
LGPL header policy, branching, hook setup, and the local-equivalent
security commands.

> **Note on branch protection.** GitHub's branch-protection and repository-
> ruleset features are not available for private repositories on the free
> org plan this repo lives under, so direct pushes to `main` are not
> blocked at the platform level. Treat the PR-based flow as a convention
> and rely on the CI workflows as the quality gate.

## Security

Vulnerability disclosure: [`SECURITY.md`](SECURITY.md). The release
pipeline produces SHA256SUMS and an SBOM (SPDX) — see
[`CONTRIBUTING.md#security`](CONTRIBUTING.md#security) for the
verification recipe. Cosign keyless signing + SLSA L3 provenance are
on the roadmap (see [`HANDOFF.md`](HANDOFF.md)).
