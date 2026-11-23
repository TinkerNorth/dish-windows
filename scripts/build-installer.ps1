<#
.SYNOPSIS
    Compile dist\dish-setup.exe from the staged install image with Inno Setup.

.DESCRIPTION
    Thin local wrapper over the two-step recipe the workflows run:

        cmake --build <build> --target Dish dish_setup_image
        iscc /DMyAppVersion=<M.m.p> /DImageDir=<build>\setup-image installer.iss

    The version is read from project(Dish VERSION ...) in CMakeLists.txt, the
    single source release.yml also asserts against. The image must already be
    staged; this script points at the cmake target rather than staging it
    itself so there is exactly one path that decides the payload's contents.

.PARAMETER BuildDir
    The build tree whose setup-image to pack. Defaults to build-release.

.PARAMETER Version
    The MAJOR.MINOR.PATCH to stamp. Defaults to project(Dish VERSION ...);
    release.yml passes the tag's, and then asserts the two agree.

.PARAMETER Iscc
    Path to ISCC.exe. Defaults to the Inno Setup scripts/install-inno-setup.ps1
    installs, which must be the version scripts/tools.ps1 pins; an engine
    passed here is used as given.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build-release',
    [string]$Version = '',
    [string]$Iscc = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
. (Join-Path $PSScriptRoot 'tools.ps1')

# --- Version: project(Dish VERSION x.y.z) is the source of truth ------------
if (-not $Version) {
    $cm = Get-Content CMakeLists.txt -Raw
    if ($cm -notmatch 'project\(\s*Dish[\s\r\n]+VERSION[\s]+(\d+\.\d+\.\d+)') {
        throw 'Could not read a MAJOR.MINOR.PATCH version from project(Dish VERSION ...) in CMakeLists.txt'
    }
    $Version = $Matches[1]
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version '$Version' is not MAJOR.MINOR.PATCH" }

# --- Image: staged by the dish_setup_image target ----------------------------
$imageDir = Join-Path $BuildDir 'setup-image'
if (-not (Test-Path (Join-Path $imageDir 'dish.exe'))) {
    throw "No staged image at $imageDir. Run: cmake --build $BuildDir --target Dish dish_setup_image"
}

# --- ISCC: the pinned engine, nothing else ----------------------------------
# The round-trip gate's assertions follow the engine (6.7.3 renamed a per-user
# install's ARP entry), so an installer compiled by another version is one no
# gate has tested.
if (-not $Iscc) { $Iscc = Join-Path $DishInnoSetupDir 'ISCC.exe' }
if (-not (Test-Path $Iscc)) {
    throw "ISCC.exe not found at $Iscc. Run scripts/install-inno-setup.ps1 (Inno Setup $DishInnoSetupVersion), or pass -Iscc."
}
# An -Iscc of your own is taken as given; the default location must hold the pin.
if ($Iscc -eq (Join-Path $DishInnoSetupDir 'ISCC.exe')) {
    $isccVersion = Get-InnoSetupVersion
    if ($isccVersion -ne $DishInnoSetupVersion) {
        throw "The Inno Setup at $DishInnoSetupDir is '$isccVersion'; scripts/tools.ps1 pins $DishInnoSetupVersion. Run scripts/install-inno-setup.ps1."
    }
}

& $Iscc "/DMyAppVersion=$Version" "/DImageDir=$imageDir" installer.iss
if ($LASTEXITCODE -ne 0) { throw "iscc failed with exit code $LASTEXITCODE" }
if (-not (Test-Path 'dist/dish-setup.exe')) { throw 'iscc succeeded but dist/dish-setup.exe is missing' }

$out = Get-Item 'dist/dish-setup.exe'
"Built $($out.FullName) ($([math]::Round($out.Length / 1MB, 1)) MB, version $Version)"
