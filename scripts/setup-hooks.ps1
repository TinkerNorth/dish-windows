#!/usr/bin/env pwsh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# Point this repo's git hooks at the tracked .githooks/ directory so the
# pre-commit lint/format checks run for every contributor after a single
# one-time setup. Idempotent — safe to re-run.

$ErrorActionPreference = 'Stop'

Set-Location (Join-Path $PSScriptRoot '..')

if (-not (Test-Path '.git')) {
    Write-Host "✗ not a git repository (run 'git init' first, or run from inside the repo)"
    exit 1
}

git config core.hooksPath .githooks
# Windows ignores executable bits; Git for Windows runs the hook via the
# shebang. Nothing to chmod here.

Write-Host "✓ core.hooksPath → .githooks"
Write-Host ""
Write-Host "Recommended tooling (install once):"
Write-Host "  winget install LLVM.LLVM       # provides clang-format + clang-tidy"
Write-Host "  winget install Kitware.CMake"
Write-Host "  winget install Ninja-build.Ninja"
Write-Host ""
Write-Host "Note: clang-tidy needs build-debug/compile_commands.json — generate it with:"
Write-Host "  scripts/build.ps1 debug"
