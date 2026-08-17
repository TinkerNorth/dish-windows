# Third-party notices

Dish for Windows is licensed [LGPL-3.0-or-later](LICENSE). This file lists every
third-party component that is linked into `dish.exe`, shipped beside it in the
release zip or inside `dish-setup.exe`, embedded in either as a resource, or
used only to build and test it. It also states what someone redistributing the
binaries has to do.

Two artifacts ship: `dish-windows.zip`, the portable bundle, and
`dish-setup.exe`, the installer (Inno Setup, compiled from `installer.iss`).
Both carry the same install image: the app, its runtime DLLs, the Visual C++
runtime, and the licence texts. Where a component reaches a user through only
one of the two, the table below says which.

The app has an in-app version of this list at Settings, Licenses, rendered from
[`assets/licenses/licenses.json`](assets/licenses/licenses.json). That manifest
and this file describe the same set of components. See
[Keeping this in sync](#keeping-this-in-sync) for the divergences that exist
today.

---

## 1. Summary

| Component | Version | SPDX | How it reaches the user | Attribution obligation |
|---|---|---|---|---|
| [Qt 6](#2-qt-6) | 6.7.3 (CI and release); CMake requires >= 6.7 | `LGPL-3.0-only` | Dynamically linked. The Qt DLLs and QML plugin modules are staged into the release zip by `windeployqt`. `Qt6EntryPoint` is static. | Notice, license text, relink freedom. See section 2. |
| [SDL2](#sdl2) | 2.32.10#1 (vcpkg baseline `9e593bb1`) | `Zlib` | Dynamically linked. `SDL2.dll` ships in the release zip and in the installer payload. | Keep the notice, do not claim authorship |
| [libsodium](#libsodium) | 1.0.22#1 (vcpkg baseline `9e593bb1`) | `ISC` | Dynamically linked. `libsodium.dll` ships in the release zip and in the installer payload. | Keep the copyright and permission notice |
| [Inter](#4-inter) | 4.001 | `OFL-1.1` | Four `.ttf` faces embedded in `dish.exe` as Qt resources under `:/fonts/`. | Ship the license text with every copy. See section 4. |
| [Catch2](#5-catch2) | 3.5.4 | `BSL-1.0` | Test binary only. Not linked into `dish.exe`, not in the release zip. | None for redistributors of the app |
| [Visual C++ runtime](#visual-c-runtime) | 14.4x (`Microsoft.VC143.CRT`) | Proprietary, Microsoft | Five DLLs staged app-local, in the installer payload and in the release zip alike. | Microsoft Visual Studio redistributable terms |
| [Windows SDK shader binaries](#windows-sdk-shader-binaries) | `dxcompiler.dll` 1.6.2112.16, `dxil.dll` 101.6.2112.13 | Proprietary, Microsoft | Copied into the release zip and the installer payload by `windeployqt` from the installed Windows SDK. | Microsoft Windows SDK redistribution terms |
| [Windows system libraries](#windows-system-libraries) | OS | Proprietary, Microsoft | Import libraries only. Nothing is redistributed. | None |

Two further items are reuse of published facts rather than of code, and are
covered in [section 7](#7-reused-facts-not-reused-code): SDL's default Switch Pro
motion scaling constants, and the HID input-report byte layouts documented in the
Linux kernel's PlayStation and Nintendo HID drivers.

Everything under `packaging/brand/` and `packaging/dish.svg` is original
TinkerNorth artwork, covered by this repository's own license. See
[section 8](#8-first-party-artwork).

---

## 2. Qt 6

Upstream: <https://www.qt.io/>. Source: <https://code.qt.io/cgit/qt/qtbase.git/>.

Qt is offered under a commercial license, GPLv2, GPLv3, and LGPLv3. **This
project uses Qt under the GNU Lesser General Public License version 3
(`LGPL-3.0-only`).** No commercial Qt license is used, and no Qt source is
modified. The full LGPLv3 text is in [`LICENSE`](LICENSE); LGPLv3 incorporates
GPLv3 by reference, and that text is in [`COPYING.GPL3`](COPYING.GPL3).

### Modules linked

Declared in [`CMakeLists.txt`](CMakeLists.txt):

- `Qt6::Core`, `Qt6::Gui`, `Qt6::Network`, `Qt6::Svg`
- `Qt6::Qml`, `Qt6::Quick`, `Qt6::QuickControls2`
- `Qt6::EntryPoint`, auto-linked for the `WIN32` subsystem executable

The Qt Quick runtime pulls in further Qt DLLs and QML plugin modules at deploy
time (`Qt6QmlModels`, `Qt6QmlWorkerScript`, `Qt6QuickControls2Basic`,
`Qt6QuickControls2BasicStyleImpl`, `Qt6QuickControls2Impl`, `Qt6QuickEffects`,
`Qt6QuickLayouts`, `Qt6QuickShapes`, `Qt6QuickTemplates2`, `Qt6OpenGL`,
`Qt6Widgets`, plus the `qml/QtQml` and `qml/QtQuick` trees). All are part of Qt
and carry the same license.

Build-time only, not redistributed: `Qt6::LinguistTools` (`lupdate`, `lrelease`),
`qmllint`, `qmlcachegen`, `qmltyperegistrar`, `windeployqt`.

Qt modules that are GPL-only rather than LGPL, such as Qt Charts, are not used.

### TLS

`Qt6::Network` reaches the satellite's HTTPS API through Qt's Schannel TLS
backend, which is the Windows default from Qt 6.2 on. No OpenSSL is bundled or
deployed. If you build against a Qt configured for the OpenSSL backend instead,
you take on OpenSSL's own attribution obligations, which this file does not
cover.

### Third-party code inside Qt

The shipped Qt DLLs statically embed further third-party libraries (FreeType,
HarfBuzz, PCRE2, zlib, libpng, libjpeg-turbo, Brotli, double-conversion, md4c,
and others, depending on how the Qt binary was configured). Their notices belong
to the Qt build you deploy, and The Qt Company documents them at
<https://doc.qt.io/qt-6/licenses-used-in-qt.html>. Attribution for those is
inherited from the Qt binaries you ship, not restated here.

### The LGPL position, stated precisely

`dish.exe` is a "Combined Work" in the sense of LGPLv3 section 4: our own code
plus the Qt libraries. Linking is dynamic. The Qt DLLs are separate files that
`windeployqt` copies next to the executable, and the executable resolves them at
load time.

`Qt6EntryPoint` is the one exception. It is a small static library providing the
Win32 `WinMain` shim that forwards into `main()`, and it is linked into
`dish.exe` itself.

**If you redistribute `dish.exe`, or the release zip, or any build of it, you
must:**

1. Give prominent notice with each copy that Qt is used and is covered by the
   LGPL. Shipping this file alongside the binary does that.
2. Ship a copy of the GNU LGPLv3 and the GNU GPLv3 with the binary. Those are
   [`LICENSE`](LICENSE) and [`COPYING.GPL3`](COPYING.GPL3) in this repository.
   They are not currently inside the release zip, so add them if you build your
   own distribution.
3. Preserve the copyright notices in the material you redistribute, and include
   a reference to the LGPL in your documentation.
4. Let the recipient replace Qt. Two ways satisfy LGPLv3 section 4(d), and this
   project satisfies both:
   - Section 4(d)(1): Qt is used through a shared library mechanism. A recipient
     can drop in their own build of a compatible Qt 6 by replacing the DLLs next
     to `dish.exe`, without touching our code.
   - Section 4(d)(0): the complete corresponding source for `dish.exe` is this
     public repository under LGPL-3.0-or-later, so a recipient can rebuild and
     relink the whole thing themselves. This is what discharges the obligation
     for the statically linked `Qt6EntryPoint`.
5. Not strip or obscure the license notices, and not add terms that restrict
   these rights.

If you fork this project and make the fork's source unavailable, you break
condition 4 for the statically linked part and you break this repository's own
license at the same time. Do not do that.

Nothing here requires a user of the released binary to do anything. These are
obligations on redistribution.

---

## 3. Bundled runtime libraries

SDL2 and libsodium are declared in [`vcpkg.json`](vcpkg.json) and resolved
against the pinned `builtin-baseline`
`9e593bb18ea69cc5095e012465dcd675a822ed0d` (vcpkg 2026.07.29), which is the same
commit the CI workflow pins. Both are built as DLLs on the `x64-windows` triplet
and both are dynamically linked.

### SDL2

Simple DirectMedia Layer 2.32.10 (vcpkg port revision 1). SPDX `Zlib`. Upstream:
<https://github.com/libsdl-org/SDL>.

Used for controller enumeration, input polling, motion, rumble and light-bar
output, and battery reporting on every path other than the raw-HID USB-direct
path.

```
Simple DirectMedia Layer
Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

### libsodium

libsodium 1.0.22 (vcpkg port revision 1). SPDX `ISC`. Upstream:
<https://libsodium.org/>.

Used for the pairing key derivation, the HKDF-SHA256 per-session key schedule,
and the ChaCha20-Poly1305 AEAD on the UDP data plane.

```
ISC License

Copyright (c) 2013-2026 Frank Denis <j at pureftpd dot org>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```


## 4. Inter

Inter 4.001, by Rasmus Andersson and the Inter Project Authors. SPDX `OFL-1.1`.
Upstream: <https://rsms.me/inter/>.

Four faces are bundled and embedded into `dish.exe` as Qt resources by
`packaging/dish.qrc`: Regular, Medium, SemiBold, Bold. They are loaded at startup
through `QFontDatabase` so the UI matches the design system on machines without
Inter installed.

The full license is at
[`packaging/fonts/Inter-LICENSE.txt`](packaging/fonts/Inter-LICENSE.txt) and is
embedded in the binary alongside the fonts at `:/fonts/Inter-LICENSE.txt`.

```
Copyright (c) 2016 The Inter Project Authors (https://github.com/rsms/inter)
```

What the OFL requires here:

- The fonts may be bundled and redistributed with this software, including
  commercially, because they are not being sold on their own (OFL condition 1).
- **Every copy that includes the fonts must include the copyright notice and the
  license text** (OFL condition 2). Keep `Inter-LICENSE.txt` with any
  distribution you make. The `.txt` is embedded in the executable as a resource,
  but a copy as a plain file next to the binary is the safer reading of the
  requirement and is what a redistributor should ship.
- "Inter" is not a Reserved Font Name in this license file, but the faces are
  shipped unmodified, so condition 3 does not bite either way. If you modify
  them, rename them.
- The fonts must stay under the OFL. Being bundled with LGPL software does not
  relicense them, and the OFL does not reach the application code.

---

## 5. Catch2

Catch2 v3.5.4. SPDX `BSL-1.0`. Upstream: <https://github.com/catchorg/Catch2>.

**Test-only. Not linked into `dish.exe` and not present in the release zip.** It
is resolved by `tests/CMakeLists.txt` through `find_package(Catch2 3)` with a
`FetchContent` fallback pinned to tag `v3.5.4`, and it links only into the
`DishTests` executable, which is built when `DISH_BUILD_TESTS=ON` and is switched
off for the release configuration.

It is listed here because it appears in the in-app licenses manifest. Someone
redistributing the application binary has no Catch2 obligation.

---

## 6. Microsoft components

### Windows SDK shader binaries

`windeployqt` copies `dxcompiler.dll` and `dxil.dll` out of the installed Windows
SDK (observed: SDK 10.0.22621.0, `dxcompiler.dll` 1.6.2112.16, `dxil.dll`
101.6.2112.13) into the deployment directory, because Qt Quick's RHI can use the
Direct3D backend. They therefore end up in the release zip.

These are Microsoft binaries. `dxcompiler` is built from the open-source
DirectX Shader Compiler, but the binary Microsoft ships is distributed under the
Windows SDK license terms, and `dxil.dll` is proprietary. Redistribution is
permitted by the Windows SDK's redistributable list, and it is governed by
Microsoft's terms, not by any license in this file.

### Visual C++ runtime

`dish.exe` is built with MSVC and links the Visual C++ runtime dynamically. The
runtime is not vendored in this repository, and the dev build passes
`--no-compiler-runtime` to `windeployqt`, so a locally built tree does not stage
it.

**Both shipped artifacts stage it app-local.** "Runs on a machine where nothing
is installed" is a hard requirement for `dish-setup.exe`, and it is the whole
claim the portable zip makes. `windeployqt --compiler-runtime` answers neither:
it copies `vc_redist.exe` for the user to find and run, which is not the same
thing as a runtime, and a zip that needs an installer run first is not portable.
`cmake/DishSetupImage.cmake` for the install image, and the bundling steps in
`.github/workflows/release.yml` and `.github/workflows/windows-ci.yml` for the
zip, therefore copy the same five DLLs out of
`$env:VCToolsRedistDir/x64/Microsoft.VC143.CRT` next to `dish.exe`:

```
msvcp140.dll  msvcp140_1.dll  msvcp140_2.dll  vcruntime140.dll  vcruntime140_1.dll
```

App-local deployment of these files is explicitly permitted by the Visual Studio
redistributable terms, and it is what keeps the install to one directory with no
machine-wide side effects. The version is whatever the build machine's toolset
ships; the staging script and both workflow steps hard-error when
`VCToolsRedistDir` is unset rather than producing an artifact that cannot start.

The two vcpkg runtimes travel in the same directory for the same reason:
`libsodium.dll` and `SDL2.dll` are staged beside the CRT DLLs, before
`windeployqt` runs, because `windeployqt` only knows about Qt's own
dependencies.

The SFX stub itself is the exception that makes all of this work: it is built
`/MT`, with the CRT linked statically, precisely so the one binary that must run
before anything is installed depends on nothing but `kernel32`, `user32`,
`shell32`, `comctl32` and `advapi32`.

Redistributing the Visual C++ runtime, in any form, is governed by Microsoft's
redistributable terms, not by any license in this file.

### Windows system libraries

Linked as import libraries against the operating system, and never redistributed:
`ws2_32`, `setupapi`, `hid`, `bthprops`, `dbghelp`, `dwmapi`, `shell32`, `ole32`.

---

## 7. Reused facts, not reused code

Two places in this repository reuse published device-protocol facts. No
third-party source is compiled in either case, and the values were remapped onto
this project's own decoders.

### SDL default motion scaling

`switchGyroToWire` and `switchAccelToWire` in
[`src/core/input/UsbReportParsers.h`](src/core/input/UsbReportParsers.h) follow
SDL's default IMU scaling for controllers whose factory calibration this project
does not read: gyro raw divided by 14.2842 to get degrees per second, accel raw
divided by 4096 to get g. In the source those appear pre-multiplied into the wire
scale as the divisors 28568 and 16384. That is reuse of two constants. SDL's zlib
notice is reproduced in [section 3](#sdl2), and SDL is a linked dependency of this
project in any case.

### Linux kernel HID drivers

The per-model input-report byte layouts in the same header, for DualShock 4,
DualSense and Switch Pro, follow the layouts documented in the upstream Linux
kernel HID drivers `drivers/hid/hid-playstation.c` and `drivers/hid/hid-nintendo.c`.
Only offsets and field meanings were used. The Linux kernel is licensed
`GPL-2.0-only`; upstream is <https://github.com/torvalds/linux>. No kernel code
is compiled into this project, and none of the kernel's rumble or init-packet
output sequences are used here, because the Windows raw-HID path is input-only.

The dish-android repository carries the same attribution for the parsers these
were mirrored from.

---

## 8. First-party artwork

The brand iconography under `packaging/brand/` (the dish, satellite, bluetooth,
gear and pad glyph families and their state variants), `packaging/dish.svg`,
`packaging/dish.png` and `packaging/dish.ico` are original TinkerNorth work,
shared with the sibling Dish clients. They are covered by this repository's
license and carry no third-party attribution.

---

## Keeping this in sync

[`assets/licenses/licenses.json`](assets/licenses/licenses.json) is the manifest
the in-app Licenses screen renders, parsed by `src/UI/licenses/LicenseManifest.*`.
It is hand-authored, not generated, so it can drift. It currently lists Qt 6,
SDL2, libsodium, Catch2 and Inter, which is the same set of open-source
components as this file, with the same licenses. The Microsoft components in section 6 are deliberately absent from the
manifest: their terms are Microsoft's and there is no license text to render.
One further difference is worth knowing about:

- The manifest lists Catch2, which is test-only and is not in the shipped binary.
  Showing it to a user is harmless but inaccurate.

When a dependency is added, changed or dropped, update
[`vcpkg.json`](vcpkg.json), the manifest, and this file together.

---

## Reporting an attribution problem

If something is missing, misattributed, or wrong here, open an issue or email
`security@tinkernorth.com`. See [`SECURITY.md`](SECURITY.md) for disclosure
handling and [`CONTRIBUTING.md`](CONTRIBUTING.md) for the license-header policy
applied to contributions.
