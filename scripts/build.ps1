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
# Prereqs:
#   - Visual Studio 2022 with the "Desktop development with C++" workload
#     (or just the Build Tools), so cl.exe is on PATH
#   - Ninja (`winget install Ninja-build.Ninja` or `choco install ninja`)
#   - Qt 6 + libsodium + SDL2 (we expect vcpkg — see CONTRIBUTING.md)
#   - $env:VCPKG_ROOT pointing at your vcpkg checkout, or pass
#     `-DCMAKE_TOOLCHAIN_FILE=<path>` yourself.

[CmdletBinding()]
param(
    [ValidateSet('release','debug')]
    [string]$BuildType = 'release',
    [string]$Action    = ''
)

$ErrorActionPreference = 'Stop'

$cmakeBuildType = if ($BuildType -eq 'debug') { 'Debug' } else { 'Release' }
$buildDir       = "build-$BuildType"

if (-not $env:VCPKG_ROOT) {
    Write-Warning '$env:VCPKG_ROOT is not set; CMake will rely on system find_package paths and probably fail to find SDL2 / libsodium. See CONTRIBUTING.md for vcpkg setup.'
}

$toolchain = if ($env:VCPKG_ROOT) {
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
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
