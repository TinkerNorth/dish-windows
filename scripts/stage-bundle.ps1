<#
.SYNOPSIS
    Stage a runnable, redistributable dish.exe bundle from a Release tree.

.DESCRIPTION
    The one staging path. windows-ci.yml stages its artifact/ with it and
    release.yml stages the portable dish-windows/ zip payload with it, so the
    two cannot drift apart again. They had: windows-ci staged
    libcrypto-3-x64.dll and release.yml did not (its portable-bundle smoke
    test still passed because GitHub runners carry a libcrypto-3-x64.dll on
    PATH via Strawberry Perl, so the staged exe silently borrowed the
    runner's copy and the zip shipped unable to start on a clean machine),
    while release.yml staged the licence texts and windows-ci's artifact did
    not, even though both are downloadable Combined Works under LGPLv3 s4(b).
    This script stages the union for both.

    What gets staged, and why each piece is load-bearing:

    * dish.exe from the build tree.
    * libsodium.dll, SDL2.dll, opus.dll, libcrypto-3-x64.dll, sentry.dll,
      crashpad_handler.exe: windeployqt
      walks Qt's dependency graph only, and dish.exe imports these directly
      (vcpkg's applocal step drops them beside the freshly linked exe;
      libcrypto carries the 3-x64 soversion and serves the Moonlight crypto).
      Without them the bundle dies at start: 0xc0000135, no window.
      libssl is NOT shipped: the Moonlight path uses only libcrypto and TLS
      rides Qt's Schannel backend.
    * windeployqt --qmldir: the Quick chrome is the only UI, so a bundle
      without the QML modules opens to a white window.
      --no-compiler-runtime, with the CRT staged by hand below: what
      windeployqt otherwise deploys is vc_redist.x64.exe, an installer
      rather than a runtime. Same call, and the same five files, as
      cmake/DishSetupImage.cmake section 6.
    * msvcp140{,_1,_2}.dll + vcruntime140{,_1}.dll from VCToolsRedistDir:
      dish.exe imports three directly and Qt6Core adds the other two. Its
      absence is fatal for the reason it is fatal in the install image: the
      alternative is a bundle that borrows the build machine's System32 and
      dies elsewhere.
    * Dish/Chrome/qmldir: the app's OWN QML module entry, which windeployqt
      cannot know about. dish.exe carries a compiled-in copy, but Qt searches
      <exedir>/Dish/Chrome/qmldir BEFORE it, so the file that ships had
      better be the app's own. The asserts name a regression here instead of
      leaving it to the portable-bundle smoke test. Same staging as
      cmake/DishSetupImage.cmake section 3.
    * licenses/: LGPLv3 s4(b) requires the licence to travel with the
      Combined Work, and Qt is linked under it; OFL-1.1 s2 requires the same
      for the Inter faces compiled in as resources.

.PARAMETER BuildDir
    The Release build tree to stage from (the release preset's
    build-release).

.PARAMETER OutDir
    Bundle directory to create (windows-ci.yml: artifact, release.yml:
    dish-windows). Created if missing.

.PARAMETER QtBin
    Directory holding windeployqt.exe. Defaults to QT_ROOT_DIR/bin (what
    jurplel/install-qt-action sets in CI; v4 sets QT_ROOT_DIR but not
    Qt6_DIR), then CMAKE_PREFIX_PATH/bin, then PATH.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build-release',
    [Parameter(Mandatory = $true)]
    [string]$OutDir,
    [string]$QtBin = ''
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

if (-not (Test-Path (Join-Path $BuildDir 'dish.exe'))) {
    throw "No dish.exe in $BuildDir. Build the release preset first: cmake --build --preset release"
}

if (-not $QtBin) {
    if ($env:QT_ROOT_DIR -and (Test-Path (Join-Path $env:QT_ROOT_DIR 'bin\windeployqt.exe'))) {
        $QtBin = Join-Path $env:QT_ROOT_DIR 'bin'
    } elseif ($env:CMAKE_PREFIX_PATH -and (Test-Path (Join-Path $env:CMAKE_PREFIX_PATH 'bin\windeployqt.exe'))) {
        $QtBin = Join-Path $env:CMAKE_PREFIX_PATH 'bin'
    } else {
        $onPath = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
        if ($onPath) { $QtBin = Split-Path -Parent $onPath.Source }
    }
}
if (-not $QtBin) {
    throw 'windeployqt.exe not found. Set QT_ROOT_DIR or CMAKE_PREFIX_PATH to the Qt prefix, or pass -QtBin.'
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Copy-Item (Join-Path $BuildDir 'dish.exe') $OutDir/

foreach ($dll in 'libsodium.dll', 'SDL2.dll', 'opus.dll', 'libcrypto-3-x64.dll',
                 'sentry.dll', 'crashpad_handler.exe') {
    $src = Join-Path $BuildDir $dll
    if (-not (Test-Path $src)) { throw "Missing $dll in $BuildDir (vcpkg applocal should have staged it beside dish.exe)" }
    Copy-Item $src $OutDir/
}

& (Join-Path $QtBin 'windeployqt.exe') --release --no-translations --no-compiler-runtime --qmldir src/qml (Join-Path $OutDir 'dish.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

if (-not $env:VCToolsRedistDir) { throw 'VCToolsRedistDir is not set: import the MSVC environment (vcvars64) first; the bundle cannot ship an app-local CRT without it' }
$crt = Join-Path $env:VCToolsRedistDir 'x64/Microsoft.VC143.CRT'
foreach ($dll in 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    if (-not (Test-Path (Join-Path $crt $dll))) { throw "Missing $dll in $crt" }
    Copy-Item (Join-Path $crt $dll) $OutDir/
}

$qmldir = Join-Path $BuildDir 'Dish/Chrome/qmldir'
if (-not (Test-Path $qmldir)) { throw "No Dish.Chrome qmldir at $qmldir - the staged dish.exe could not resolve its own QML module" }
if ((Get-Content $qmldir -Raw) -notmatch '(?m)^Main 1\.0 ') { throw "$qmldir does not declare the app's Main type, so it is the wrong Dish.Chrome qmldir (the setup kit's?)" }
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'Dish/Chrome') | Out-Null
Copy-Item $qmldir (Join-Path $OutDir 'Dish/Chrome/')

New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'licenses') | Out-Null
Copy-Item LICENSE            (Join-Path $OutDir 'licenses/LICENSE.LGPL-3.0.txt')
Copy-Item COPYING.GPL3       (Join-Path $OutDir 'licenses/LICENSE.GPL-3.0.txt')
Copy-Item THIRD_PARTY.md     (Join-Path $OutDir 'licenses/')
Copy-Item packaging/fonts/Inter-LICENSE.txt (Join-Path $OutDir 'licenses/')

Write-Output "Staged bundle at $OutDir ($(@(Get-ChildItem $OutDir -Recurse -File).Count) files)"
