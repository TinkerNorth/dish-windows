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
. (Join-Path $PSScriptRoot 'tools.ps1')

function Find-QtPrefix {
    # An explicit CMAKE_PREFIX_PATH wins; otherwise the pinned Qt where
    # scripts/install-deps.ps1 puts it (C:\Qt\<version>\<arch dir>).
    if ($env:CMAKE_PREFIX_PATH -and (Test-Path (Join-Path $env:CMAKE_PREFIX_PATH 'lib/cmake/Qt6'))) {
        return $env:CMAKE_PREFIX_PATH
    }
    $pinned = Join-Path 'C:\Qt' (Join-Path $DishQtVersion $DishQtArchDir)
    if (Test-Path (Join-Path $pinned 'lib/cmake/Qt6')) { return $pinned }
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
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { Import-MsvcEnvironment }

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
    Write-Warning "Qt $DishQtVersion not found under C:\Qt and CMAKE_PREFIX_PATH not set. Run scripts/install-deps.ps1."
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
