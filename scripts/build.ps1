#!/usr/bin/env pwsh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# Convenience wrapper around CMake + Ninja for local dev. Mirrors
# dish-linux/scripts/build.sh argument shape so muscle memory transfers.
#
# Usage:
#   scripts/build.ps1                  # release, no tests
#   scripts/build.ps1 debug            # debug, no tests
#   scripts/build.ps1 debug test       # debug + run ctest after building
#   scripts/build.ps1 release test     # release + run ctest after building
#
# The script auto-finds:
#   * VS Build Tools / Visual Studio (via vswhere + vcvars64.bat)
#   * Qt 6  (via $env:CMAKE_PREFIX_PATH set by install-dependencies.bat,
#            or by scanning C:\Qt\* for the newest msvc install)
#   * vcpkg (via $env:VCPKG_ROOT set by install-dependencies.bat, or by
#            falling back to %USERPROFILE%\vcpkg)
#
# If you ran install-dependencies.bat the way we document, this script
# Just Works from any PowerShell / cmd window — you don't need to launch
# a "Developer Command Prompt for VS 2022" first.

[CmdletBinding()]
param(
    [ValidateSet('release','debug')]
    [string]$BuildType = 'release',
    [string]$Action    = ''
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Toolchain discovery — make the script self-bootstrapping so the user
# never has to remember to open a "Developer Command Prompt".
# ---------------------------------------------------------------------------

function Find-VcvarsBat {
    # vswhere ships with every VS install >= 2017 and points at the exact
    # VC tools directory for any flavour (Community / Pro / Enterprise /
    # BuildTools). Without it we'd have to guess; this is the same lookup
    # ilammy/msvc-dev-cmd does in CI.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    $install = & $vswhere -latest -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $install) { return $null }
    $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    if (Test-Path $vcvars) { return $vcvars }
    return $null
}

function Import-Vcvars {
    param([string]$VcvarsPath)
    # vcvars64.bat sets ~50 env vars (INCLUDE, LIB, PATH, LIBPATH, etc.) that
    # the MSVC toolchain needs. The trick is running it in a child cmd.exe
    # and re-importing every `KEY=VALUE` line into the current PowerShell
    # process — same idea every CI msvc-setup action uses.
    Write-Output "==> Importing MSVC env via $VcvarsPath"
    $output = cmd.exe /c "`"$VcvarsPath`" >nul && set"
    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

function Find-QtPrefix {
    # 1) Explicit override wins.
    if ($env:CMAKE_PREFIX_PATH -and (Test-Path (Join-Path $env:CMAKE_PREFIX_PATH 'lib/cmake/Qt6'))) {
        return $env:CMAKE_PREFIX_PATH
    }
    # 2) Scan C:\Qt\<version>\msvc2019_64 (the layout aqtinstall produces
    #    and what install-dependencies.bat installs). Newest version wins.
    $qtRoot = 'C:\Qt'
    if (-not (Test-Path $qtRoot)) { return $null }
    $candidates = Get-ChildItem $qtRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+(\.\d+)?$' } |
        ForEach-Object {
            $msvc = Join-Path $_.FullName 'msvc2019_64'
            if (Test-Path (Join-Path $msvc 'lib/cmake/Qt6')) { $msvc }
        }
    if ($candidates) { return ($candidates | Sort-Object -Descending | Select-Object -First 1) }
    return $null
}

function Find-VcpkgRoot {
    if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT 'scripts/buildsystems/vcpkg.cmake'))) {
        return $env:VCPKG_ROOT
    }
    $default = Join-Path $env:USERPROFILE 'vcpkg'
    if (Test-Path (Join-Path $default 'scripts/buildsystems/vcpkg.cmake')) { return $default }
    return $null
}

# Only run vcvars import if cl.exe isn't already on PATH (saves ~3s on
# warm shells / Developer Command Prompts).
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vcvars = Find-VcvarsBat
    if (-not $vcvars) {
        throw "cl.exe not on PATH and no VS install found by vswhere. Run install-dependencies.bat first."
    }
    Import-Vcvars -VcvarsPath $vcvars
}

$qtPrefix = Find-QtPrefix
if ($qtPrefix) {
    $env:CMAKE_PREFIX_PATH = $qtPrefix
    Write-Output "==> Qt prefix: $qtPrefix"
} else {
    Write-Warning 'No Qt install found under C:\Qt and CMAKE_PREFIX_PATH not set. Run install-dependencies.bat.'
}

$vcpkgRoot = Find-VcpkgRoot
if ($vcpkgRoot) {
    $env:VCPKG_ROOT = $vcpkgRoot
    Write-Output "==> vcpkg root: $vcpkgRoot"
} else {
    Write-Warning 'vcpkg not found. Run install-dependencies.bat or set $env:VCPKG_ROOT.'
}

# ---------------------------------------------------------------------------
# Configure + build
# ---------------------------------------------------------------------------

$cmakeBuildType = if ($BuildType -eq 'debug') { 'Debug' } else { 'Release' }
$buildDir       = "build-$BuildType"

$toolchain = if ($vcpkgRoot) {
    "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake"
} else { '' }

# Configure if the build dir doesn't exist; otherwise rely on CMake's
# incremental-regeneration to pick up CMakeLists.txt changes.
if (-not (Test-Path $buildDir)) {
    Write-Output "==> Configuring ($cmakeBuildType, $buildDir)"
    $cmakeArgs = @(
        '-S', '.',
        '-B', $buildDir,
        '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$cmakeBuildType",
        '-DDISH_BUILD_TESTS=ON'
    )
    if ($toolchain) { $cmakeArgs += $toolchain }
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
}

Write-Output "==> Building $buildDir"
& cmake --build $buildDir --parallel
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

if ($Action -eq 'test') {
    Write-Output "==> Running tests"
    Push-Location $buildDir
    try {
        & ctest --output-on-failure --parallel
        if ($LASTEXITCODE -ne 0) { throw "ctest failed" }
    } finally {
        Pop-Location
    }
}

Write-Output "==> Done"
