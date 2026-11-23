<#
.SYNOPSIS
    clang-format gate: the exact file set and invocation windows-ci.yml runs.

.DESCRIPTION
    One script so the workflow and scripts/ci-local.ps1 cannot drift on the
    file set or the invocation. Check-only; the pre-commit hook is the autofix
    path.

    The version is the one scripts/tools.ps1 pins (the PyPI wheel, installed
    with pipx in CI and pip by scripts/install-deps.ps1). Another version
    disagrees on braced-init lists, so a different one fails here before it
    can pass or fail the files for the wrong reason.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot 'tools.ps1')

$versionLine = (& clang-format --version | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'clang-format --version failed' }
if ($versionLine -notmatch 'version (\d+\.\d+\.\d+)') { throw "could not read a version out of '$($versionLine.Trim())'" }
if ($Matches[1] -ne $DishClangFormatVersion) {
    throw "clang-format $($Matches[1]) on PATH; the gate is pinned to $DishClangFormatVersion (pip install clang-format==$DishClangFormatVersion)."
}

$files = git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h'
if (-not $files) { throw 'git ls-files found no sources for the format gate' }
clang-format --dry-run --Werror $files
if ($LASTEXITCODE -ne 0) { throw 'clang-format check failed' }
Write-Output "clang-format: OK ($(@($files).Count) files)"
