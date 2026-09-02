<#
.SYNOPSIS
    clang-format gate: the exact file set and invocation windows-ci.yml runs.

.DESCRIPTION
    One script so the workflow and scripts/ci-local.ps1 cannot drift on the
    file set or the invocation. Check-only; the pre-commit hook is the autofix
    path.

    CI pins clang-format 22.1.4 (PyPI wheel via pipx). Another version can
    disagree on braced-init lists; treat a surprise verdict with suspicion.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$files = git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h'
if (-not $files) { throw 'git ls-files found no sources for the format gate' }
clang-format --dry-run --Werror $files
if ($LASTEXITCODE -ne 0) { throw 'clang-format check failed' }
Write-Output "clang-format: OK ($(@($files).Count) files)"
