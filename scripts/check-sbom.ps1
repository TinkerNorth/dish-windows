<#
.SYNOPSIS
    Fail an SPDX document that does not name every library the app ships.

.DESCRIPTION
    The list of what ships is assets/licenses/licenses.json, the manifest the
    in-app Licenses screen renders and scripts/check-versions.ps1 ties to the
    vcpkg baseline, the FetchContent pins and the Qt pin. Every entry but
    Catch2 (test-only, never in a bundle) must appear in the document as a
    package of that name, case aside, and its version must be the manifest's
    (a vcpkg port's version may carry its port revision after a #, and a
    commit the manifest shortens matches its full form).

        scripts/check-sbom.ps1 -Sbom dist/dish-windows-components.spdx.json

.PARAMETER Sbom
    The SPDX-JSON document to check.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Sbom
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot

$document = [IO.File]::ReadAllText((Resolve-Path $Sbom)) | ConvertFrom-Json
$libraries = ([IO.File]::ReadAllText((Join-Path $repoRoot 'assets/licenses/licenses.json')) | ConvertFrom-Json).libraries

$missing = @()
foreach ($library in $libraries) {
    $artifact = [string]$library.artifact
    if ($artifact -eq 'Catch2') { continue }
    $named = @($document.packages | Where-Object { ([string]$_.name) -ieq $artifact })
    if ($named.Count -eq 0) { $missing += "$artifact (no package of that name)"; continue }
    $wanted = [string]$library.version
    $versions = @($named | ForEach-Object { ([string]$_.versionInfo -split '#')[0] })
    # A commit the manifest names by its short form matches the full one.
    $matching = @($versions | Where-Object { $_ -eq $wanted -or ($wanted -match '^[0-9a-f]{7,39}$' -and $_.StartsWith($wanted)) })
    if ($matching.Count -eq 0) { $missing += "$artifact $wanted (the document says $($versions -join ', '))" }
}

if ($missing.Count -gt 0) {
    foreach ($entry in $missing) { Write-Output "::error::$Sbom does not name $entry" }
    exit 1
}
Write-Output "${Sbom}: every library assets/licenses/licenses.json lists is named, at its version ($(@($libraries).Count - 1))"
