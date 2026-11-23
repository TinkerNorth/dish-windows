<#
.SYNOPSIS
    Install the Inno Setup scripts/tools.ps1 pins, per user, verified by its
    SHA-256. windows-ci.yml, release.yml and scripts/install-deps.ps1 call it,
    so every installer is compiled by the one engine the round-trip gate ran.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'tools.ps1')
$iscc = Install-InnoSetup
Write-Output "ISCC: $iscc"
