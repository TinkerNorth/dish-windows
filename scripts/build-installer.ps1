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

.PARAMETER Iscc
    Path to ISCC.exe. Defaults to the standard Inno Setup 6 location, then
    whatever is on PATH.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build-release',
    [string]$Iscc = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

# --- Version: project(Dish VERSION x.y.z) is the source of truth ------------
$cm = Get-Content CMakeLists.txt -Raw
if ($cm -notmatch 'project\(\s*Dish[\s\r\n]+VERSION[\s]+(\d+\.\d+\.\d+)') {
    throw 'Could not read a MAJOR.MINOR.PATCH version from project(Dish VERSION ...) in CMakeLists.txt'
}
$version = $Matches[1]

# --- Image: staged by the dish_setup_image target ----------------------------
$imageDir = Join-Path $BuildDir 'setup-image'
if (-not (Test-Path (Join-Path $imageDir 'dish.exe'))) {
    throw "No staged image at $imageDir. Run: cmake --build $BuildDir --target Dish dish_setup_image"
}

# --- ISCC ---------------------------------------------------------------------
if (-not $Iscc) {
    $candidates = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    $Iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Iscc) {
        $onPath = Get-Command iscc.exe -ErrorAction SilentlyContinue
        if ($onPath) { $Iscc = $onPath.Source }
    }
    if (-not $Iscc) {
        throw 'ISCC.exe not found. Install Inno Setup 6 (https://jrsoftware.org/isinfo.php or `choco install innosetup`), or pass -Iscc.'
    }
}

& $Iscc "/DMyAppVersion=$version" "/DImageDir=$imageDir" installer.iss
if ($LASTEXITCODE -ne 0) { throw "iscc failed with exit code $LASTEXITCODE" }
if (-not (Test-Path 'dist/dish-setup.exe')) { throw 'iscc succeeded but dist/dish-setup.exe is missing' }

$out = Get-Item 'dist/dish-setup.exe'
"Built $($out.FullName) ($([math]::Round($out.Length / 1MB, 1)) MB, version $version)"
