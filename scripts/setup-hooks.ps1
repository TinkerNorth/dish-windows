#!/usr/bin/env pwsh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# Sets core.hooksPath to the tracked .githooks/ directory. Repo-local, no
# machine-wide state. Idempotent, safe to re-run.

$ErrorActionPreference = 'Stop'

Set-Location (Join-Path $PSScriptRoot '..')

if (-not (Test-Path '.git')) {
    Write-Host "✗ not a git repository (run 'git init' first, or run from inside the repo)"
    exit 1
}

git config core.hooksPath .githooks
# No chmod needed: Windows ignores the executable bit and Git for Windows
# runs the hook through its shebang.

Write-Host "✓ core.hooksPath → .githooks"
Write-Host ""
Write-Host "Recommended tooling (install once):"
Write-Host "  winget install LLVM.LLVM       # provides clang-format + clang-tidy"
Write-Host "  winget install Kitware.CMake"
Write-Host "  winget install Ninja-build.Ninja"
Write-Host ""
Write-Host "Note: clang-tidy needs build-debug/compile_commands.json — generate it with:"
Write-Host "  scripts/build.ps1 debug"
