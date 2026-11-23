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

# The pinned one wherever it sits on PATH: Visual Studio's vcvars puts its own
# bundled clang-format (19.x with VS 17.14) ahead of pip's, and so does LLVM.
function Get-ClangFormatVersion([string]$Exe) {
    $line = (& $Exe --version | Out-String)
    if ($LASTEXITCODE -ne 0 -or $line -notmatch 'version (\d+\.\d+\.\d+)') { return '' }
    return $Matches[1]
}
$found = @(Get-Command clang-format -All -CommandType Application -ErrorAction SilentlyContinue)
$clangFormat = $null
$seen = @()
foreach ($candidate in $found) {
    $candidateVersion = Get-ClangFormatVersion $candidate.Source
    $seen += "$($candidate.Source) ($candidateVersion)"
    if ($candidateVersion -eq $DishClangFormatVersion) { $clangFormat = $candidate.Source; break }
}
if (-not $clangFormat) {
    $onPath = if ($seen) { $seen -join ', ' } else { 'none' }
    throw "no clang-format $DishClangFormatVersion on PATH (found: $onPath). pip install clang-format==$DishClangFormatVersion."
}
Write-Output "Using $clangFormat ($DishClangFormatVersion)"

$files = git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h'
if (-not $files) { throw 'git ls-files found no sources for the format gate' }
& $clangFormat --dry-run --Werror $files
if ($LASTEXITCODE -ne 0) { throw 'clang-format check failed' }
Write-Output "clang-format: OK ($(@($files).Count) files)"
