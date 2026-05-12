# dish-windows — Continuation Prompt

Use this file as the handoff prompt when resuming work on the
`dish-windows` client. It captures the current state of the tree, how to
verify it, and the outstanding follow-ups.

---

## Context

`dish-windows` is the Windows-native port of the Dish wireless gamepad
client. It mirrors `dish-android`, `dish-mac`, and `dish-linux` in
protocol, theme, and latency strategy:

- **Language / UI:** C++17 + Qt6 Widgets.
- **Input:** SDL2 on a dedicated polling thread (hot-plug aware). SDL2's
  Windows backend uses XInput natively for Xbox-class controllers and the
  SDL HID layer for everything else.
- **Crypto:** libsodium ChaCha20-Poly1305 over UDP. `IP_TOS = DSCP EF` on
  the hot path (best-effort — Windows often strips it for non-admin
  processes; documented in README).
- **Sockets:** Raw Winsock 2.2 (`ws2_32.lib`) instead of POSIX sockets —
  same algorithmic structure as dish-linux, different API surface.
- **Display-sleep:** `SetThreadExecutionState(ES_DISPLAY_REQUIRED |
  ES_SYSTEM_REQUIRED | ES_CONTINUOUS)`.
- **Server parity:** XUSB report layout matching `satellite`.
- **License:** LGPL-3.0 (`LICENSE`, `COPYING.GPL3`, source headers).
- **Config:** `QSettings` → Windows registry under
  `HKCU\Software\TinkerNorth\Dish` (the OS-native equivalent of XDG /
  UserDefaults that the sibling clients use).

Repository layout lives under `d:\TinkerNorth\dish-windows\` and is the
private repo `TinkerNorth/dish-windows` on GitHub.

---

## What's Done

### Build / tooling
- `CMakeLists.txt` with split warning surface:
  - **MSVC path** (`dish_warnings`): `/W4 /permissive- /Zc:__cplusplus
    /Zc:preprocessor /utf-8`; defines `UNICODE _UNICODE WIN32_LEAN_AND_MEAN
    NOMINMAX _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS`.
  - **clang-cl / MinGW path**: matches the dish-linux flag set.
  - `dish_strict` adds `/WX` (MSVC) or `-Werror` (clang) on first-party
    targets only so Catch2's headers stay buildable.
- `vcpkg.json` manifest pinning libsodium ≥ 1.0.18 + SDL2 ≥ 2.30.
- `scripts/build.ps1` — `release | debug | debug test` PowerShell wrapper.
- `scripts/setup-hooks.ps1` + `.githooks/pre-commit` (bash, runs under Git
  for Windows' bundled bash).
- `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore` carried
  over from dish-linux verbatim.
- `.github/workflows/windows-ci.yml` — installs Qt6 via
  `jurplel/install-qt-action`, vcpkg via `lukka/run-vcpkg`, MSVC env via
  `ilammy/msvc-dev-cmd`. Runs clang-format check, debug build + ctest,
  clang-tidy, release build, uploads `windeployqt`-bundled exe as a CI
  artifact.
- `.github/workflows/codeql.yml` runs CodeQL `cpp` analysis on a Windows
  runner so MSVC-only constructs are covered.
- `.github/workflows/security.yml` + `_security.yml` carried over —
  action-pin lint, allowlist expiry, OSV-Scanner, gitleaks,
  dependency-review.
- `.github/workflows/release.yml` — tag-triggered Windows build that
  emits `dish-windows.zip` (exe + Qt DLLs) + `dish-windows.spdx.json`
  (SBOM) on the Release.

### Source tree (`src/`)
- `Models/` — identical to dish-linux except `ControllerSlot` drops the
  `SlotInputType` enum and `physicalDeviceId` field (Windows is
  physical-controllers-only; matches dish-mac PR #7).
- `Util/` — `AtomicCounter`, `Hex`, `Endian` (verbatim);
  `DisplaySleepInhibitor` wraps `SetThreadExecutionState` instead of
  QtDBus / IOPMAssertion; `ScreenWakeController` identical to siblings.
- `Network/` — Winsock 2.2 ports of `SatelliteClient`, `LANDiscovery`,
  `PairingClient`. New `WinsockInit` RAII guard wrapping `WSAStartup` /
  `WSACleanup`. `HTTPClient`, `WifiConnection`, `WifiConnectionManager`,
  `ConnectionHub`, `ConnectionStore` are all Qt-based and inherited
  verbatim.
- `Input/` — `SDLGamepadBridge` + `GamepadInputProcessor` inherited
  verbatim (SDL2's API is identical across platforms; XInput is selected
  internally on Windows). DEVCAPS log uses the same Qt logging category
  shape as dish-linux.
- `UI/` — `Theme`, `MainWindow`, `ConnectionsDialog`, `PairingDialog`,
  `SlotCard` carried over verbatim.
- `AppModel.{h,cpp}` — same shape as dish-linux; injects a
  `SetThreadExecutionStateInhibitor` instead of `FreedesktopScreenSaverInhibitor`.
  Virtual-slot append removed.
- `main.cpp` — installs `WinsockInit` first (RAII guard lives across the
  whole app lifetime), then `sodium_init` → `QApplication` →
  `applyDishTheme` → `AppModel` → `MainWindow` → `model.start()`. No
  `setDesktopFileName` (that's an XDG-only concept).

### Packaging
- `packaging/dish.rc` — Windows resource file embedding the icon +
  version-info block (Explorer / Task Manager / `Get-ItemProperty` reads).
- `packaging/dish.ico` — multi-resolution icon generated from `dish.png`
  via the PowerShell `System.Drawing` script in HANDOFF item 4 below.
- `packaging/dish.png` — 256 × 256 source asset (from the v5 icon export).
- `packaging/dish.svg` — vector source.
- CMake `add_executable(Dish WIN32 ...)` to build a windowed-subsystem
  binary; Qt's `EntryPoint` shim forwards into the standard `int main()`.

### Tests (`tests/`, Catch2 v3)
All carried over from dish-linux verbatim — same Catch2 cases since the
underlying types are identical:
- `test_atomic_counter.cpp` — init / next / reset + 8-thread contention.
- `test_hex.cpp` — round-trip, mixed case, rejects odd-length / non-hex.
- `test_endian.cpp` — `putU{16,32,64}Be` / `readU*Be` round-trips.
- `test_models.cpp` — DTO JSON round-trips.
- `test_beacon_parser.cpp` — valid beacon, wrong service, malformed, etc.
- `test_gamepad_input_processor.cpp` — XUSB packing, scale, deadzones,
  publish fan-out, telemetry reset.
- `test_pairing_client_classify.cpp` — Success / AuthRequired /
  Unreachable arms + the `fromJson sets reachable=true` invariant.
- `test_screen_wake_controller.cpp` — streamingCount derivation +
  0↔positive transition contract via `FakeInhibitor`.

---

## What's Left

### Verification (do once on a Windows box)
- [ ] `scripts\build.ps1 debug test` clean on a fresh Win11 box with the
      tooling listed in README. CI is the source of truth but a local
      pass catches vcpkg / Qt / SDL drift early.
- [ ] `clang-tidy -p build-debug` triaged. Expect roughly the same set
      of stylistic findings as dish-linux (catalogued in
      `CONTRIBUTING.md`). Anything Windows-specific (e.g. MSVC's
      different reporting of `__declspec` warnings) should be triaged
      into the table or fixed.
- [ ] Live integration test against a running `satellite`:
      - LAN discovery → pair → connect → bind a real Xbox or DualSense
        pad → confirm XUSB reports land on the server with correct axis
        polarity and trigger range.
      - Confirm latency vs dish-linux on the same hardware (target:
        within 1 ms median; budget is dominated by Wi-Fi + server, not
        the client stack).
- [ ] Confirm display-sleep inhibitor actually keeps the screen on
      during a streaming session (`powercfg /requests` lists "Display"
      held by `dish.exe` while a slot is bound + connected).
- [ ] Soak test: 8-hour run, watch `drainTelemetry` for drops; verify the
      SDL thread never blocks on the UI thread under load.

### Tooling
- [ ] Code-signing for release exes. Self-signed for dev; eventually a
      proper Authenticode cert (`signtool sign /fd SHA256 /f cert.pfx`).
      Until then `SmartScreen` will warn on first-run downloads.

### Packaging / distribution
- [ ] Optional MSIX or WiX installer (currently shipping a portable
      zip via `windeployqt`).
- [ ] Optional winget manifest (`microsoft/winget-pkgs`).
- [ ] qWAVE.dll wiring for proper DSCP marking on the hot-path UDP
      socket (current `IP_TOS` call is best-effort — see README).

### Documentation
- [ ] README screenshots (main window, connections dialog, pairing dialog).
- [ ] DESIGN.md verified against the rendered theme on Windows (font
      metrics differ from Linux's fontconfig defaults).

### Release hardening
- [ ] Port the dish-linux `harden` job — cosign keyless signatures, SLSA
      L3 provenance, CycloneDX SBOM alongside the SPDX one.
- [ ] Authenticode signing on the exe before zipping (above).

---

## How to Resume

```powershell
cd d:\TinkerNorth\dish-windows
# Prereqs (one-time):
#   winget install Microsoft.VisualStudio.2022.BuildTools `
#       --override "--add Microsoft.VisualStudio.Workload.VCTools"
#   winget install Kitware.CMake Ninja-build.Ninja LLVM.LLVM
#   git clone https://github.com/microsoft/vcpkg $env:USERPROFILE\vcpkg
#   & $env:USERPROFILE\vcpkg\bootstrap-vcpkg.bat
#   [Environment]::SetEnvironmentVariable('VCPKG_ROOT', "$env:USERPROFILE\vcpkg", 'User')

scripts\build.ps1 debug test     # full build + ctest
scripts\build.ps1 release        # optimized binary at build-release\dish.exe
```

Pinned constraints (do not relax without asking):
- LGPL-3.0 only.
- Wire-protocol parity with `satellite` (XUSB layout, ChaCha20-Poly1305).
- Input thread must never block on the UI thread — keep the hot path
  routed through the lock-free routing-table snapshot directly.
- Theme must remain visually identical to dish-android / dish-mac /
  dish-linux.
- No virtual on-screen gamepad (physical controllers only — explicit
  user requirement).
