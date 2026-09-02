#!/usr/bin/env pwsh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# Build Dish on Windows: scripts/build.ps1 [debug|release] [test]
#
# Thin wrapper over the CMake presets in CMakePresets.json, which are the
# single source of configure truth (the same presets windows-ci.yml, codeql.yml
# and release.yml run). It finds the MSVC toolchain, Qt and vcpkg itself, so it
# runs from any shell, not only a Developer Command Prompt.
#
#   scripts/build.ps1                  # release preset -> build-release/
#   scripts/build.ps1 debug            # debug preset   -> build/
#   scripts/build.ps1 debug test       # debug + run ctest after building
#
# Directory note: the debug preset writes to build/ (CI's name), not the
# build-debug/ this script used before the presets existed. Tests are a Debug
# concern (the release preset configures them OFF, exactly like CI), so
# `release test` is refused rather than silently testing nothing.
#
# It writes into build/ or build-release/ and nothing outside the repo.

[CmdletBinding()]
param(
    [ValidateSet('release', 'debug')]
    [string]$BuildType = 'release',
    [ValidateSet('', 'test')]
    [string]$Action = ''
)

$ErrorActionPreference = 'Stop'

function Find-VcvarsBat {
    # vswhere ships with every VS install >= 2017 and is the only reliable way
    # to locate any flavour (Community / Pro / Enterprise / BuildTools).
    # -products * is required or a BuildTools-only box reports nothing.
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
    # There is no PowerShell equivalent of vcvars64.bat, so run it in a child
    # cmd.exe and re-import every KEY=VALUE line into this process.
    Write-Output "==> Importing MSVC env via $VcvarsPath"
    $output = cmd.exe /c "`"$VcvarsPath`" >nul && set"
    foreach ($line in $output) {
        if ($line -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

function Find-QtPrefix {
    # An explicit CMAKE_PREFIX_PATH wins; otherwise fall back to the layout
    # scripts/install-deps.ps1 produces, newest version first.
    if ($env:CMAKE_PREFIX_PATH -and (Test-Path (Join-Path $env:CMAKE_PREFIX_PATH 'lib/cmake/Qt6'))) {
        return $env:CMAKE_PREFIX_PATH
    }
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

if ($BuildType -eq 'release' -and $Action -eq 'test') {
    throw "The release preset configures tests OFF, exactly like CI. Run scripts/build.ps1 debug test."
}

# Skip the ~3s vcvars import when cl.exe is already on PATH.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vcvars = Find-VcvarsBat
    if (-not $vcvars) {
        throw "cl.exe not on PATH and no VS install found by vswhere. Run scripts/install-deps.ps1 first."
    }
    Import-Vcvars -VcvarsPath $vcvars
}

$qtPrefix = Find-QtPrefix
if ($qtPrefix) {
    $env:CMAKE_PREFIX_PATH = $qtPrefix
    Write-Output "==> Qt prefix: $qtPrefix"
    # So a dish.exe launched straight from this shell finds Qt's DLLs. ctest
    # gets the same path through DL_PATHS in tests/CMakeLists.txt.
    $qtBin = Join-Path $qtPrefix 'bin'
    if (Test-Path $qtBin) {
        if (-not (($env:PATH -split ';') -contains $qtBin)) {
            $env:PATH = "$qtBin;$env:PATH"
        }
    }
} else {
    Write-Warning 'No Qt install found under C:\Qt and CMAKE_PREFIX_PATH not set. Run scripts/install-deps.ps1.'
}

$vcpkgRoot = Find-VcpkgRoot
if ($vcpkgRoot) {
    $env:VCPKG_ROOT = $vcpkgRoot
    Write-Output "==> vcpkg root: $vcpkgRoot"
} else {
    Write-Warning 'vcpkg not found. Run scripts/install-deps.ps1 or set $env:VCPKG_ROOT.'
}

$preset = $BuildType
$buildDir = if ($BuildType -eq 'debug') { 'build' } else { 'build-release' }

# Test for build.ninja, not for the directory: CMake creates the directory the
# moment it starts, so an aborted configure leaves one behind and a directory
# check would treat that poisoned tree as configured. A configured tree skips
# the (vcpkg-manifest-checking) reconfigure for a faster loop; preset flag
# changes land on the next fresh configure, or run `cmake --preset <name>`
# yourself.
$ninjaFile = Join-Path $buildDir 'build.ninja'
if (-not (Test-Path $ninjaFile)) {
    Write-Output "==> Configuring (preset $preset, $buildDir)"
    & cmake --preset $preset
    if ($LASTEXITCODE -ne 0) {
        # Wipe the half-configured tree so the next run starts clean.
        Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
        throw "cmake configure failed"
    }
}

Write-Output "==> Building (preset $preset)"
& cmake --build --preset $preset --parallel
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

if ($Action -eq 'test') {
    Write-Output "==> Running tests"
    & ctest --preset debug --parallel
    if ($LASTEXITCODE -ne 0) { throw "ctest failed" }
}

Write-Output "==> Done"
