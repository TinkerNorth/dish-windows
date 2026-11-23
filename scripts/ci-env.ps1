<#
.SYNOPSIS
    Set up a Windows workflow job's environment: the MSVC toolchain and the
    pins every later step reads.

.DESCRIPTION
    The first step after checkout in windows-ci.yml, codeql.yml,
    measurements.yml and release.yml. It replaces a third-party action that
    did the MSVC half: the same vswhere + vcvars64 import scripts/build.ps1
    and scripts/ci-local.ps1 do locally (Get-MsvcEnvironment in
    scripts/tools.ps1), handed to the job through GITHUB_ENV and GITHUB_PATH.

    Exported for later steps:
      * every variable vcvars64 adds or changes; new PATH entries go through
        GITHUB_PATH so they stay ahead of the runner's own (Git's usr\bin
        carries a link.exe of its own);
      * QT_VERSION, QT_ARCH, QT_ARCH_DIR, AQT_SOURCE, CLANG_FORMAT_VERSION
        from scripts/tools.ps1;
      * VCPKG_COMMITID, read from vcpkg.json's builtin-baseline, its one copy;
      * IMAGE_VERSION, the runner image, which keys the vcpkg binary cache
        because an image update can carry a new MSVC and so new ABI hashes.
    It also creates vcpkg-bincache\, which the files binary source needs to
    exist before vcpkg first writes to it.

    Outside a workflow (no GITHUB_ENV) it prints what it would export.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'tools.ps1')

$exports = [ordered]@{}
$newPathEntries = @()

$msvc = Get-MsvcEnvironment
$currentPath = @($env:PATH -split ';' | Where-Object { $_ })
foreach ($name in $msvc.Keys) {
    $value = $msvc[$name]
    if ($name -ieq 'PATH') {
        $newPathEntries = @($value -split ';' | Where-Object { $_ -and ($currentPath -notcontains $_) })
        continue
    }
    $existing = [Environment]::GetEnvironmentVariable($name)
    if ($existing -cne $value) { $exports[$name] = $value }
}

$exports['QT_VERSION'] = $DishQtVersion
$exports['QT_ARCH'] = $DishQtArch
$exports['QT_ARCH_DIR'] = $DishQtArchDir
$exports['AQT_SOURCE'] = $DishAqtSource
$exports['CLANG_FORMAT_VERSION'] = $DishClangFormatVersion
$exports['VCPKG_COMMITID'] = Get-VcpkgBaseline -RepoRoot $repoRoot
$exports['IMAGE_VERSION'] = if ($env:ImageVersion) { $env:ImageVersion } else { 'local' }

New-Item -ItemType Directory -Force (Join-Path $repoRoot 'vcpkg-bincache') | Out-Null

if (-not $env:GITHUB_ENV) {
    foreach ($name in $exports.Keys) { Write-Output "$name=$($exports[$name])" }
    foreach ($entry in $newPathEntries) { Write-Output "PATH+=$entry" }
    return
}

# One line per variable: none of vcvars64's values spans lines, and a value
# that did would be refused here rather than written half.
$envLines = foreach ($name in $exports.Keys) {
    $value = [string]$exports[$name]
    if ($value -match "[`r`n]") { throw "$name has a multi-line value; GITHUB_ENV needs the delimiter form for it" }
    "$name=$value"
}
Add-Content -Path $env:GITHUB_ENV -Value $envLines -Encoding utf8

# GITHUB_PATH prepends each line ahead of the ones before it, so the entries go
# in reverse to keep vcvars64's order.
if ($newPathEntries.Count -gt 0) {
    [array]::Reverse($newPathEntries)
    Add-Content -Path $env:GITHUB_PATH -Value $newPathEntries -Encoding utf8
}

Write-Output "Exported $($exports.Count) variables and $($newPathEntries.Count) PATH entries."
Write-Output "Qt $DishQtVersion ($DishQtArch), vcpkg $($exports['VCPKG_COMMITID']), image $($exports['IMAGE_VERSION'])"
