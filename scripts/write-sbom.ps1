<#
.SYNOPSIS
    Write the SPDX bill of materials of what a staged bundle ships:
    dish-windows-components.spdx.json.

.DESCRIPTION
    Syft, which the release hardening runs over the release directory, sees a
    zip and an installer and names almost nothing inside them. This document
    is written from the build instead, at the versions the build used:

      * every port vcpkg.json declares, from the vcpkg.spdx.json vcpkg writes
        for it under <build>/vcpkg_installed/<triplet>/share/<port>/ (name,
        version with port revision, licence);
      * Qt, at the version scripts/tools.ps1 pins;
      * ENet, at the commit CMakeLists.txt fetches;
      * Crashpad, as vendored by the sentry-native above;
      * the Inter faces compiled into dish.exe;
      * the Microsoft binaries the bundle carries (the Visual C++ runtime and
        the Windows SDK's shader compiler), at the file versions in the bundle.

    windows-ci.yml and release.yml run it over the same staged bundle, and
    scripts/check-sbom.ps1 fails a document that misses a library
    assets/licenses/licenses.json lists.

.PARAMETER BuildDir
    The release build tree (its vcpkg_installed folder).

.PARAMETER Bundle
    The staged bundle the document describes (for the Microsoft file versions).

.PARAMETER Out
    Where to write the document.

.PARAMETER Version
    The Dish version the document describes. Defaults to project(Dish VERSION ...).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$Bundle,
    [Parameter(Mandatory)][string]$Out,
    [string]$Version = '',
    [string]$Triplet = 'x64-windows'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'tools.ps1')

$cmake = [IO.File]::ReadAllText((Join-Path $repoRoot 'CMakeLists.txt'))
if (-not $Version) {
    if ($cmake -notmatch 'project\(\s*Dish\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'no project(Dish VERSION X.Y.Z) in CMakeLists.txt' }
    $Version = $Matches[1]
}
if ($cmake -notmatch 'GIT_REPOSITORY\s+https://github.com/cgutman/enet\.git\s+GIT_TAG\s+([0-9a-f]{40})') { throw 'no ENet GIT_TAG in CMakeLists.txt' }
$enetCommit = $Matches[1]

$packages = [System.Collections.Generic.List[object]]::new()
function Add-Package {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$PackageVersion,
        [Parameter(Mandatory)][string]$License,
        [Parameter(Mandatory)][string]$Download,
        [string]$Purl = '',
        [string]$Cpe = ''
    )
    $refs = @()
    if ($Purl) { $refs += [ordered]@{ referenceCategory = 'PACKAGE-MANAGER'; referenceType = 'purl'; referenceLocator = $Purl } }
    if ($Cpe) { $refs += [ordered]@{ referenceCategory = 'SECURITY'; referenceType = 'cpe23Type'; referenceLocator = $Cpe } }
    $id = 'SPDXRef-Package-' + ($Name -replace '[^A-Za-z0-9.-]', '-')
    $package = [ordered]@{
        name             = $Name
        SPDXID           = $id
        versionInfo      = $PackageVersion
        downloadLocation = $Download
        filesAnalyzed    = $false
        licenseConcluded = $License
        licenseDeclared  = $License
        copyrightText    = 'NOASSERTION'
    }
    if ($refs.Count -gt 0) { $package['externalRefs'] = $refs }
    $packages.Add($package)
}

# --- vcpkg ports, from vcpkg's own documents --------------------------------------
$manifest = [IO.File]::ReadAllText((Join-Path $repoRoot 'vcpkg.json')) | ConvertFrom-Json
$share = Join-Path $BuildDir "vcpkg_installed/$Triplet/share"
$sentryVersion = ''
foreach ($dependency in $manifest.dependencies) {
    $port = if ($dependency -is [string]) { $dependency } else { $dependency.name }
    $document = Join-Path $share "$port/vcpkg.spdx.json"
    if (-not (Test-Path $document)) { throw "no ${document}: vcpkg did not install $port into $BuildDir" }
    $spdx = [IO.File]::ReadAllText($document) | ConvertFrom-Json
    $own = @($spdx.packages | Where-Object { $_.name -eq $port })
    if ($own.Count -lt 1) { throw "$document names no package called $port" }
    $portVersion = [string]$own[0].versionInfo
    $upstream = ($portVersion -split '#')[0]
    if ($port -eq 'sentry-native') { $sentryVersion = $upstream }
    Add-Package -Name $port -PackageVersion $portVersion -License ([string]$own[0].licenseConcluded) `
        -Download ([string]$own[0].downloadLocation) -Purl "pkg:generic/$port@$upstream"
}
if (-not $sentryVersion) { throw 'vcpkg.json declares no sentry-native, so Crashpad has no version' }

# --- What vcpkg does not install -----------------------------------------------------
Add-Package -Name 'qtbase' -PackageVersion $DishQtVersion -License 'LGPL-3.0-only' `
    -Download 'https://download.qt.io/official_releases/qt/' `
    -Purl "pkg:generic/qt/qtbase@$DishQtVersion" -Cpe "cpe:2.3:a:qt:qt:${DishQtVersion}:*:*:*:*:*:*:*"
Add-Package -Name 'enet' -PackageVersion $enetCommit -License 'MIT' `
    -Download "git+https://github.com/cgutman/enet@$enetCommit" -Purl "pkg:github/cgutman/enet@$enetCommit"
Add-Package -Name 'crashpad' -PackageVersion "as vendored by sentry-native $sentryVersion" `
    -License 'Apache-2.0 AND BSD-3-Clause' -Download 'https://github.com/getsentry/crashpad'
Add-Package -Name 'inter' -PackageVersion '4.001' -License 'OFL-1.1' -Download 'https://github.com/rsms/inter'

foreach ($file in 'vcruntime140.dll', 'dxcompiler.dll') {
    $path = Join-Path $Bundle $file
    if (-not (Test-Path $path)) { throw "no $file in $Bundle" }
    $fileVersion = (Get-Item $path).VersionInfo.FileVersion
    $name = if ($file -eq 'vcruntime140.dll') { 'Microsoft Visual C++ runtime' } else { 'Microsoft DirectX Shader Compiler (Windows SDK)' }
    Add-Package -Name $name -PackageVersion "$fileVersion" -License 'NOASSERTION' -Download 'NOASSERTION'
}

# --- The document ---------------------------------------------------------------------
$root = 'SPDXRef-Package-dish-windows'
$relationships = @([ordered]@{ spdxElementId = 'SPDXRef-DOCUMENT'; relationshipType = 'DESCRIBES'; relatedSpdxElement = $root })
foreach ($package in $packages) {
    $relationships += [ordered]@{ spdxElementId = $root; relationshipType = 'CONTAINS'; relatedSpdxElement = $package.SPDXID }
}
$all = @([ordered]@{
        name             = 'dish-windows'
        SPDXID           = $root
        versionInfo      = $Version
        downloadLocation = 'https://github.com/TinkerNorth/dish-windows'
        filesAnalyzed    = $false
        licenseConcluded = 'LGPL-3.0-or-later'
        licenseDeclared  = 'LGPL-3.0-or-later'
        copyrightText    = 'Copyright (C) 2026 Dish contributors.'
    }) + $packages
$document = [ordered]@{
    spdxVersion       = 'SPDX-2.3'
    dataLicense       = 'CC0-1.0'
    SPDXID            = 'SPDXRef-DOCUMENT'
    name              = "dish-windows-$Version-components"
    documentNamespace = "https://github.com/TinkerNorth/dish-windows/sbom/$Version/$([Guid]::NewGuid())"
    creationInfo      = [ordered]@{
        created  = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
        creators = @('Organization: TinkerNorth', 'Tool: dish-windows-scripts/write-sbom.ps1')
    }
    packages          = $all
    relationships     = $relationships
}
$json = ($document | ConvertTo-Json -Depth 8) -replace "`r`n", "`n"
[IO.File]::WriteAllText($Out, $json + "`n", (New-Object Text.UTF8Encoding $false))
Write-Output "Wrote ${Out}: $($packages.Count) components of dish-windows $Version"
