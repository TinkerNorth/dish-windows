# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Dish contributors.
#
# The toolchain pins, in one place. Dot-sourced by every script that installs
# or checks a tool (install-deps.ps1, ci-local.ps1, ci-env.ps1, build.ps1,
# check-format.ps1, build-installer.ps1) and, through ci-env.ps1, by every
# Windows workflow, so a bump is one edit here. The vcpkg baseline is the one
# pin that lives elsewhere: vcpkg.json's builtin-baseline, which vcpkg itself
# reads and Get-VcpkgBaseline hands to CI.
#
# scripts/check-versions.ps1 fails a change whose documents (CONTRIBUTING.md,
# THIRD_PARTY.md, assets/licenses/licenses.json) stop naming these versions.
#
# Every downloaded binary carries the SHA-256 its upstream publishes; the
# fetch helpers refuse anything else.

# Qt 6.12 LTS. 6.12 is the first Qt whose online repository splits each
# architecture into its own subdirectory, which aqtinstall 3.3.0 (the newest
# on PyPI on 2026-11-23) cannot read: its Qt 6.12 support is merged on master
# (miurahr/aqtinstall#1048) but unreleased, so aqtinstall is installed from
# that commit. Move back to a PyPI version once one carries #1048.
$script:DishQtVersion = '6.12.1'
$script:DishQtArch = 'win64_msvc2022_64'
$script:DishQtArchDir = 'msvc2022_64'
$script:DishAqtSource = 'git+https://github.com/miurahr/aqtinstall.git@4c759852b1108cc801d8e24979136b895b5155d4'

# The PyPI wheel, not LLVM's own build: clang-format versions disagree on
# braced-init lists, so the gate checks the version before the files.
$script:DishClangFormatVersion = '23.1.5'

# clang-tidy, the PyPI wheel too (it carries its own clang resource headers), for
# the same reason: Visual Studio's vcvars puts its bundled LLVM ahead of
# anything else on PATH, so the gates pick the pinned one by version
# (Find-PinnedTool) instead of whichever comes first.
$script:DishClangTidyVersion = '23.1.4'

# Ninja, the release zip from ninja-build/ninja; the digest is GitHub's record
# of the asset, checked against the download.
$script:DishNinjaVersion = '1.13.2'
$script:DishNinjaUrl = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip'
$script:DishNinjaSha256 = '07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65'

# Inno Setup 6. The digest is the file-hash of upstream's
# innosetup-6.7.4.exe.issig. 7.x is a separate move.
$script:DishInnoSetupVersion = '6.7.4'
$script:DishInnoSetupUrl = 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_4/innosetup-6.7.4.exe'
$script:DishInnoSetupSha256 = 'a549929319f9120716677c685535ca4671e4b937b5fdc87e2585f32d8eb30e8c'

# sentry-cli, release.yml only (symbol upload on Windows, the Sentry release on
# Linux). Digests as GitHub records them for the release assets, one per line:
# a 64-hex value on the same line as the word "sentry" reads to gitleaks as a
# Sentry access token.
$script:DishSentryCli = @{
    Version = '3.8.2'
    WindowsSha256 =
        '50009f23c49d6e9ff7624204c7035855303b49c5e326708ee783c1fdf783ac83'
    LinuxSha256 =
        'c958865f9d4000183509ea21b1f04925d5390de6d8d87386b9e69f2b305070c3'
}

# The security scanners scripts/ci-local.ps1 runs locally, the versions
# _security.yml pins for its Linux runners; windows-amd64 digests from the
# upstream checksum files (osv-scanner_SHA256SUMS, gitleaks_8.30.1_checksums.txt).
$script:DishOsvScannerVersion = '2.7.1'
$script:DishOsvScannerUrl = 'https://github.com/google/osv-scanner/releases/download/v2.7.1/osv-scanner_windows_amd64.exe'
$script:DishOsvScannerSha256 = '7121dcf420aefbbf89a7f0050c4d3094e079ff93afd334dfc96f16b02ec85800'
$script:DishGitleaksVersion = '8.30.1'
$script:DishGitleaksUrl = 'https://github.com/gitleaks/gitleaks/releases/download/v8.30.1/gitleaks_8.30.1_windows_x64.zip'
$script:DishGitleaksSha256 = 'd29144deff3a68aa93ced33dddf84b7fdc26070add4aa0f4513094c8332afc4e'
$script:DishSecurityToolsDir = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\dish-security-tools'

# Where Install-Ninja puts it, per user like Inno Setup.
$script:DishNinjaDir = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "Programs\Ninja $DishNinjaVersion"

# Where Install-InnoSetup puts it: a per-user install, so the same call works
# on a runner and on a developer box without elevation.
$script:DishInnoSetupDir = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\Inno Setup 6'

function Get-VcpkgBaseline {
    param([string]$RepoRoot = (Split-Path -Parent $PSScriptRoot))
    $manifest = Get-Content (Join-Path $RepoRoot 'vcpkg.json') -Raw | ConvertFrom-Json
    $baseline = $manifest.'builtin-baseline'
    if ($baseline -notmatch '^[0-9a-f]{40}$') { throw "vcpkg.json builtin-baseline is not a 40-character commit: '$baseline'" }
    return $baseline
}

# Downloads $Url to $OutFile and refuses it unless its SHA-256 is $Sha256.
function Get-PinnedFile {
    param(
        [Parameter(Mandatory)][string]$Url,
        [Parameter(Mandatory)][string]$Sha256,
        [Parameter(Mandatory)][string]$OutFile
    )
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -UseBasicParsing
    $actual = (Get-FileHash -Algorithm SHA256 $OutFile).Hash.ToLowerInvariant()
    if ($actual -ne $Sha256) {
        Remove-Item $OutFile -Force
        throw "checksum mismatch for $Url`: expected $Sha256, got $actual"
    }
}

# The vcvars64 environment of the newest Visual Studio with the x64 C++ tools,
# as a name -> value map. vswhere's -products * is load-bearing: without it a
# machine with only the standalone Build Tools reports no compiler at all.
function Get-MsvcEnvironment {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere; install Visual Studio 2022 or its Build Tools (scripts/install-deps.ps1)." }
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $install) { throw 'vswhere found no Visual Studio with the x64 C++ tools.' }
    $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "No vcvars64.bat at $vcvars" }
    Write-Information "MSVC environment from $vcvars" -InformationAction Continue
    $environment = [ordered]@{}
    foreach ($line in (cmd.exe /c "`"$vcvars`" >nul && set")) {
        if ($line -match '^([^=]+)=(.*)$') { $environment[$Matches[1]] = $Matches[2] }
    }
    if ($LASTEXITCODE -ne 0 -or -not $environment.Contains('VCToolsInstallDir')) { throw "$vcvars did not produce an MSVC environment" }
    return $environment
}

# Imports that environment into this process, with the pinned Ninja ahead of the
# one vcvars64 puts on PATH (scripts/install-deps.ps1 installs it).
function Import-MsvcEnvironment {
    $environment = Get-MsvcEnvironment
    foreach ($name in $environment.Keys) { Set-Item -Path "Env:$name" -Value $environment[$name] }
    if (Test-Path (Join-Path $DishNinjaDir 'ninja.exe')) { $env:PATH = "$DishNinjaDir;$env:PATH" }
}

# The version of the Inno Setup installed per user at $DishInnoSetupDir, from
# its uninstall entry ('' when there is none). ISCC.exe carries no version
# resource (it reads 0.0.0.0), so the entry its installer wrote is the record.
function Get-InnoSetupVersion {
    $uninstall = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
    if (-not (Test-Path $uninstall)) { return '' }
    $wanted = $DishInnoSetupDir.TrimEnd('\')
    foreach ($key in Get-ChildItem $uninstall) {
        $entry = Get-ItemProperty $key.PSPath
        $location = $entry.PSObject.Properties['InstallLocation']
        $version = $entry.PSObject.Properties['DisplayVersion']
        if ($location -and $version -and ([string]$location.Value).TrimEnd('\') -ieq $wanted) {
            return [string]$version.Value
        }
    }
    return ''
}

# Installs the pinned Inno Setup per user unless that version is already there,
# and returns the path of its ISCC.exe.
function Install-InnoSetup {
    $iscc = Join-Path $DishInnoSetupDir 'ISCC.exe'
    if ((Test-Path $iscc) -and ((Get-InnoSetupVersion) -eq $DishInnoSetupVersion)) {
        Write-Information "Inno Setup $DishInnoSetupVersion already at $DishInnoSetupDir" -InformationAction Continue
        return $iscc
    }
    $setup = Join-Path ([IO.Path]::GetTempPath()) "innosetup-$DishInnoSetupVersion.exe"
    Get-PinnedFile -Url $DishInnoSetupUrl -Sha256 $DishInnoSetupSha256 -OutFile $setup
    # Start-Process joins its arguments with spaces and quotes none of them, so
    # the directory, which has a space in it, carries its own quotes.
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/CURRENTUSER', "/DIR=`"$DishInnoSetupDir`"")
    $process = Start-Process -FilePath $setup -ArgumentList $arguments -Wait -PassThru
    Remove-Item $setup -Force
    if ($process.ExitCode -ne 0) { throw "the Inno Setup $DishInnoSetupVersion installer exited with $($process.ExitCode)" }
    if (-not (Test-Path $iscc)) { throw "Inno Setup installed but $iscc is missing" }
    $installed = Get-InnoSetupVersion
    if ($installed -ne $DishInnoSetupVersion) { throw "the Inno Setup at $DishInnoSetupDir records version '$installed', not $DishInnoSetupVersion" }
    Write-Information "Inno Setup $installed at $DishInnoSetupDir" -InformationAction Continue
    return $iscc
}

# The first $Name on PATH whose `--version` reports $Version, or '' when none
# does. The versions found on the way go to $Seen, for the error message.
function Find-PinnedTool {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Version,
        [ref]$Seen
    )
    $found = @()
    foreach ($candidate in @(Get-Command $Name -All -CommandType Application -ErrorAction SilentlyContinue)) {
        $line = (& $candidate.Source --version 2>&1 | Out-String)
        $candidateVersion = if ($line -match 'version (\d+\.\d+\.\d+)') { $Matches[1] } else { '' }
        $found += "$($candidate.Source) ($candidateVersion)"
        if ($candidateVersion -eq $Version) {
            if ($Seen) { $Seen.Value = $found }
            return $candidate.Source
        }
    }
    if ($Seen) { $Seen.Value = $found }
    return ''
}

# Installs the pinned Ninja per user unless that version is already there, and
# returns the directory holding ninja.exe.
function Install-Ninja {
    $ninja = Join-Path $DishNinjaDir 'ninja.exe'
    if (Test-Path $ninja) {
        $present = (& $ninja --version | Out-String).Trim()
        if ($present -eq $DishNinjaVersion) { return $DishNinjaDir }
    }
    $zip = Join-Path ([IO.Path]::GetTempPath()) "ninja-$DishNinjaVersion-win.zip"
    Get-PinnedFile -Url $DishNinjaUrl -Sha256 $DishNinjaSha256 -OutFile $zip
    New-Item -ItemType Directory -Force -Path $DishNinjaDir | Out-Null
    Expand-Archive -Path $zip -DestinationPath $DishNinjaDir -Force
    Remove-Item $zip -Force
    $installed = (& $ninja --version | Out-String).Trim()
    if ($installed -ne $DishNinjaVersion) { throw "ninja.exe at $DishNinjaDir reports $installed, not $DishNinjaVersion" }
    Write-Information "Ninja $installed at $DishNinjaDir" -InformationAction Continue
    return $DishNinjaDir
}

# The pinned OSV-Scanner, fetched once into $DishSecurityToolsDir; returns its path.
function Get-OsvScanner {
    $exe = Join-Path $DishSecurityToolsDir "osv-scanner-$DishOsvScannerVersion.exe"
    if (-not (Test-Path $exe)) {
        New-Item -ItemType Directory -Force -Path $DishSecurityToolsDir | Out-Null
        Get-PinnedFile -Url $DishOsvScannerUrl -Sha256 $DishOsvScannerSha256 -OutFile $exe
    }
    return $exe
}

# The pinned gitleaks, fetched once into $DishSecurityToolsDir; returns its path.
function Get-GitleaksTool {
    $dir = Join-Path $DishSecurityToolsDir "gitleaks-$DishGitleaksVersion"
    $exe = Join-Path $dir 'gitleaks.exe'
    if (-not (Test-Path $exe)) {
        $zip = Join-Path ([IO.Path]::GetTempPath()) "gitleaks-$DishGitleaksVersion.zip"
        Get-PinnedFile -Url $DishGitleaksUrl -Sha256 $DishGitleaksSha256 -OutFile $zip
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Expand-Archive -Path $zip -DestinationPath $dir -Force
        Remove-Item $zip -Force
    }
    return $exe
}
