<#
.SYNOPSIS
    End-to-end silent install / upgrade / update-apply / uninstall assertions
    for dish-setup.exe.

.DESCRIPTION
    The unit suite pins the reducers and the ops seams; this is the only thing
    that runs the real installer against a real disk, a real registry and real
    shortcuts. It is the gate that catches a payload that unpacks but does not
    install, an ARP entry with a wrong value, an uninstall that leaves a
    directory behind, or an update handoff that silently downgrades.

    Everything happens under a work directory (a path WITH A SPACE in it, on
    purpose) and, for the per-user scope, under HKCU. The machine-scope leg
    only runs when the shell is already elevated, and never prompts.

    The user-data purge leg deletes HKCU\Software\Dish\Dish,
    HKCU\Software\TinkerNorth\Dish and %LOCALAPPDATA%\Dish, which on a
    developer machine is real data. It is therefore backed up before the leg
    and restored afterwards, whatever the outcome. Pass -SkipUserDataPurge to
    leave all three untouched.

.PARAMETER Setup
    Path to the packed dish-setup.exe under test.

.PARAMETER PackTool
    dish-payload-pack.exe, used to re-pack the payload as version 99.0.0 for
    the upgrade and update-apply legs. Defaults to the file next to -Setup.

.PARAMETER Stub
    dish-setup-stub.exe, the PE half of the re-pack. Defaults to the file next
    to -PackTool.

.PARAMETER Image
    The staged install image the re-pack reads. Defaults to setup-image next
    to -Setup.

.PARAMETER WorkRoot
    Where the sandbox lives. Defaults to RUNNER_TEMP (CI) or TEMP.

.PARAMETER SkipUserDataPurge
    Skip step 7b (the --purge-user-data assertions).

.PARAMETER SkipMachineScope
    Skip the machine-scope leg even when the shell is elevated.

.EXAMPLE
    ./scripts/test-installer-roundtrip.ps1 -Setup build-release/dish-setup.exe
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Setup,
    [string]$PackTool = '',
    [string]$Stub = '',
    [string]$Image = '',
    [string]$WorkRoot = '',
    [switch]$SkipUserDataPurge,
    [switch]$SkipMachineScope
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------- helpers ---

$script:Checks = 0

function Step([string]$text) {
    Write-Host ''
    Write-Host "== $text" -ForegroundColor Cyan
}

function Ok([string]$text) {
    $script:Checks++
    Write-Host "   ok  $text"
}

function Fail([string]$text) {
    throw "ASSERTION FAILED: $text"
}

function Assert-True([bool]$condition, [string]$text) {
    if (-not $condition) { Fail $text }
    Ok $text
}

function Assert-Eq($expected, $actual, [string]$text) {
    if ("$expected" -ne "$actual") { Fail "$text (expected '$expected', got '$actual')" }
    Ok "$text = $actual"
}

function Assert-Exists([string]$path, [string]$text) {
    if (-not (Test-Path -LiteralPath $path)) { Fail "$text - missing: $path" }
    Ok $text
}

function Assert-Absent([string]$path, [string]$text) {
    if (Test-Path -LiteralPath $path) { Fail "$text - still present: $path" }
    Ok $text
}

# Windows PowerShell's Start-Process joins -ArgumentList with spaces and adds
# no quoting of its own, so an install directory with a space in it arrives at
# the installer as two arguments. Quote here, once, by the CommandLineToArgvW
# rules the stub and CliOptions both parse by.
function Format-Argument([string]$value) {
    if ($value -notmatch '[\s"]') { return $value }
    $escaped = $value -replace '(\\*)"', '$1$1\"'
    $escaped = $escaped -replace '(\\+)$', '$1$1'
    return '"' + $escaped + '"'
}

# Both dish-setup.exe and uninstall.exe are GUI-subsystem binaries: the shell
# does not wait for them and $LASTEXITCODE is meaningless. Start-Process -Wait
# is the documented way to script them (spec section 9).
function Invoke-Exe {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$Arguments = @(),
        [string]$StdOutFile = ''
    )
    $splat = @{ FilePath = $Path; Wait = $true; PassThru = $true; NoNewWindow = $true }
    if ($Arguments.Count -gt 0) {
        $splat['ArgumentList'] = (($Arguments | ForEach-Object { Format-Argument $_ }) -join ' ')
    }
    if ($StdOutFile) { $splat['RedirectStandardOutput'] = $StdOutFile }
    $process = Start-Process @splat
    return $process.ExitCode
}

function New-Log([string]$name) {
    return (Join-Path $script:LogDir "$name.log")
}

function Get-Arp([string]$scope) {
    $hive = if ($scope -eq 'machine') { 'HKLM:' } else { 'HKCU:' }
    $path = "$hive\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\TinkerNorth.Dish"
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    return Get-ItemProperty -LiteralPath $path
}

function Get-ShortcutPaths([string]$scope) {
    if ($scope -eq 'machine') {
        return @{
            StartMenu = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Dish.lnk'
            Desktop   = Join-Path ([Environment]::GetFolderPath('CommonDesktopDirectory')) 'Dish.lnk'
        }
    }
    return @{
        StartMenu = Join-Path ([Environment]::GetFolderPath('Programs')) 'Dish.lnk'
        Desktop   = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Dish.lnk'
    }
}

# The uninstaller's exit code covers everything except the helper's tail (the
# working set, the directory itself and the ARP key), which lands within
# seconds of the process exiting. Callers poll; so do we (spec section 9).
function Wait-Until {
    param([scriptblock]$Condition, [int]$TimeoutSeconds = 30, [string]$What = 'condition')
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (& $Condition) { return $true }
        Start-Sleep -Milliseconds 300
    }
    return [bool](& $Condition)
}

# Windows PowerShell turns a native command's stderr into ErrorRecords, which
# under 'Stop' kills the script over reg.exe's cheerful "The operation
# completed successfully." on stdout's sibling stream.
function Invoke-Native {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Arguments[0] @($Arguments[1..($Arguments.Count - 1)]) 2>$null | Out-Null
    } finally {
        $ErrorActionPreference = $previous
    }
}

function Remove-TreeHard([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return }
    for ($i = 0; $i -lt 5; $i++) {
        try {
            Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop
            return
        } catch {
            Start-Sleep -Milliseconds 400
        }
    }
}

# --------------------------------------------------------------- fixtures ---

$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path -LiteralPath $Setup)) { throw "No installer at $Setup" }
$Setup = (Resolve-Path -LiteralPath $Setup).Path
$setupDir = Split-Path -Parent $Setup
if (-not $PackTool) { $PackTool = Join-Path $setupDir 'dish-payload-pack.exe' }
# The stub follows the pack tool, not -Setup: release.yml copies dish-setup.exe
# out to the repo root for upload while both build inputs stay in build/.
if (-not $Stub) { $Stub = Join-Path (Split-Path -Parent $PackTool) 'dish-setup-stub.exe' }
if (-not $Image) { $Image = Join-Path $setupDir 'setup-image' }
if (-not $WorkRoot) {
    $WorkRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
}
# %TEMP% is often the 8.3 form (C:\Users\SAMUSA~1\...), while every path .NET
# hands back is expanded. Normalise once, or half the comparisons below are
# between two spellings of the same directory.
$WorkRoot = (Get-Item -LiteralPath $WorkRoot).FullName

# The space is deliberate: an installer that mis-quotes one path fails here
# rather than on a user's "C:\Program Files" machine.
$sandbox = Join-Path $WorkRoot 'dish e2e'
$installDir = Join-Path $sandbox 'App dir'
$script:LogDir = Join-Path $sandbox 'logs'
$scratch = Join-Path $sandbox 'scratch'
$backupDir = Join-Path $sandbox 'userdata-backup'

$localAppData = $env:LOCALAPPDATA
$dishData = Join-Path $localAppData 'Dish'
$updatesDir = Join-Path $dishData 'updates'
$markerFile = Join-Path $dishData 'dish-e2e-marker.txt'

$isElevated = ([Security.Principal.WindowsPrincipal] `
    [Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

$expectedVersion = $null
$cmakeText = Get-Content (Join-Path $repoRoot 'CMakeLists.txt') -Raw
if ($cmakeText -match 'project\(\s*Dish[\s\r\n]+VERSION[\s]+(\d+\.\d+\.\d+)') {
    $expectedVersion = $Matches[1]
} else {
    throw 'Could not read project VERSION out of CMakeLists.txt'
}

Write-Host "installer   : $Setup"
Write-Host "version     : $expectedVersion"
Write-Host "sandbox     : $sandbox"
Write-Host "elevated    : $isElevated"

Remove-TreeHard $sandbox
New-Item -ItemType Directory -Force -Path $sandbox, $script:LogDir, $scratch | Out-Null

$holder = $null
$userDataBackedUp = $false

function Restore-UserData {
    if (-not $script:userDataBackedUp) { return }
    foreach ($pair in @(
            @{ File = 'hkcu-dish.reg';        Key = 'HKCU\Software\Dish\Dish' },
            @{ File = 'hkcu-tinkernorth.reg'; Key = 'HKCU\Software\TinkerNorth\Dish' })) {
        $file = Join-Path $backupDir $pair.File
        if (Test-Path -LiteralPath $file) {
            Invoke-Native reg.exe import $file
        }
    }
    $dataBackup = Join-Path $backupDir 'LocalAppData-Dish'
    if (Test-Path -LiteralPath $dataBackup) {
        New-Item -ItemType Directory -Force -Path $dishData | Out-Null
        Copy-Item -Path (Join-Path $dataBackup '*') -Destination $dishData -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
    Write-Host "   (restored the pre-test HKCU keys and %LOCALAPPDATA%\Dish)"
}

try {
    # -- 1. Command line ---------------------------------------------------
    Step '1. Command line'
    $versionOut = Join-Path $scratch 'version.txt'
    $code = Invoke-Exe -Path $Setup -Arguments @('--version') -StdOutFile $versionOut
    Assert-Eq 0 $code '--version exits 0'
    $printed = (Get-Content $versionOut -Raw).Trim()
    Assert-Eq $expectedVersion $printed '--version matches the project version'

    $code = Invoke-Exe -Path $Setup -Arguments @('/S', '--bogus')
    Assert-Eq 2 $code 'an unknown flag is a usage error'

    # -- 2. Fresh silent install ------------------------------------------
    Step '2. Fresh silent install (per-user, desktop shortcut on)'
    $log = New-Log 'install'
    $code = Invoke-Exe -Path $Setup -Arguments @(
        '/S', '--dir', $installDir, '--scope', 'user',
        '--start-menu', 'on', '--desktop', 'on', '--log', $log)
    if ($code -ne 0 -and (Test-Path $log)) { Get-Content $log | Select-Object -Last 40 }
    Assert-Eq 0 $code 'silent install exits 0'

    foreach ($relative in @('dish.exe', 'uninstall.exe', 'uninstall-helper.exe',
            '.dish-manifest.json',
            'licenses/LICENSE.LGPL-3.0.txt', 'licenses/LICENSE.GPL-3.0.txt',
            'licenses/THIRD_PARTY.md', 'licenses/Inter-LICENSE.txt')) {
        Assert-Exists (Join-Path $installDir $relative) "installed $relative"
    }

    $manifest = Get-Content (Join-Path $installDir '.dish-manifest.json') -Raw | ConvertFrom-Json
    Assert-Eq $expectedVersion $manifest.version 'recorded manifest version'
    Assert-Eq 'user' $manifest.scope 'recorded scope'
    # Three real hashes, not a file count: a truncated copy has the right name.
    $sample = $manifest.files | Get-Random -Count 3
    foreach ($entry in $sample) {
        $file = Join-Path $installDir ($entry.path -replace '/', '\')
        $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLower()
        Assert-Eq $entry.sha256 $hash "sha256 of $($entry.path)"
    }

    $arp = Get-Arp 'user'
    Assert-True ($null -ne $arp) 'ARP key TinkerNorth.Dish exists under HKCU'
    Assert-Eq 'Dish' $arp.DisplayName 'ARP DisplayName'
    Assert-Eq $expectedVersion $arp.DisplayVersion 'ARP DisplayVersion'
    Assert-Eq 'TinkerNorth' $arp.Publisher 'ARP Publisher'
    Assert-Eq "`"$installDir\uninstall.exe`"" $arp.UninstallString 'ARP UninstallString'
    Assert-True ([int]$arp.EstimatedSize -gt 0) 'ARP EstimatedSize is positive'
    Assert-Eq 'user' $arp.InstallScope 'ARP InstallScope'

    $links = Get-ShortcutPaths 'user'
    Assert-Exists $links.StartMenu 'Start Menu shortcut at the per-user location'
    Assert-Exists $links.Desktop 'Desktop shortcut at the per-user location'

    # -- 3. Repair (idempotence) -------------------------------------------
    Step '3. Same command again is a repair'
    $code = Invoke-Exe -Path $Setup -Arguments @(
        '/S', '--dir', $installDir, '--scope', 'user',
        '--start-menu', 'on', '--desktop', 'on', '--log', (New-Log 'repair'))
    Assert-Eq 0 $code 'repeat install exits 0'
    Assert-Absent (Join-Path $installDir '.dish-stage') 'no .dish-stage residue'
    Assert-Absent (Join-Path $installDir '.dish-old') 'no .dish-old residue'

    # -- 4. Upgrade --------------------------------------------------------
    Step '4. Silent upgrade to 99.0.0'
    if (-not (Test-Path -LiteralPath $PackTool)) { throw "No pack tool at $PackTool" }
    if (-not (Test-Path -LiteralPath $Image)) { throw "No install image at $Image" }
    $setup99 = Join-Path $scratch 'dish-setup-99.exe'
    if (-not (Test-Path -LiteralPath $Stub)) { throw "No stub at $Stub" }
    $packLog = Join-Path $script:LogDir 'pack99.log'
    $code = Invoke-Exe -Path $PackTool -Arguments @(
        '--stub', $Stub, '--image', $Image, '--out', $setup99,
        '--version', $expectedVersion, '--version-override', '99.0.0') -StdOutFile $packLog
    Assert-Eq 0 $code 're-pack as 99.0.0 succeeds'

    $code = Invoke-Exe -Path $setup99 -Arguments @(
        '/S', '--dir', $installDir, '--scope', 'user', '--log', (New-Log 'upgrade'))
    Assert-Eq 0 $code 'silent upgrade exits 0'
    Assert-Eq '99.0.0' (Get-Arp 'user').DisplayVersion 'ARP DisplayVersion after upgrade'
    Assert-Absent (Join-Path $installDir '.dish-stage') 'no .dish-stage after upgrade'
    Assert-Absent (Join-Path $installDir '.dish-old') 'no .dish-old after upgrade'

    # -- 5. Blocked by a running app --------------------------------------
    Step '5. A process running from the install dir blocks a silent uninstall'
    $holderExe = Join-Path $installDir 'holder.exe'
    Copy-Item (Join-Path $env:SystemRoot 'System32\PING.EXE') $holderExe -Force
    $holder = Start-Process -FilePath $holderExe -ArgumentList '-n', '60', '127.0.0.1' `
        -PassThru -WindowStyle Hidden
    Start-Sleep -Milliseconds 700
    $code = Invoke-Exe -Path (Join-Path $installDir 'uninstall.exe') `
        -Arguments @('/S', '--log', (New-Log 'uninstall-blocked'))
    Assert-Eq 5 $code 'silent uninstall without --closeapps exits 5'
    Assert-Exists (Join-Path $installDir 'dish.exe') 'the install survived the refusal'
    Stop-Process -Id $holder.Id -Force -ErrorAction SilentlyContinue
    $holder = $null
    Start-Sleep -Milliseconds 500
    Remove-Item -LiteralPath $holderExe -Force -ErrorAction SilentlyContinue

    # -- 6. Update-apply handoff -------------------------------------------
    Step '6. --update-apply from a staged update'
    # The apply refuses payload <= installed (H3), so go back to the shipped
    # version first: this is exactly the state a real app is in when it stages
    # a newer release.
    $code = Invoke-Exe -Path (Join-Path $installDir 'uninstall.exe') `
        -Arguments @('/S', '--log', (New-Log 'uninstall-before-apply'))
    Assert-Eq 0 $code 'uninstall before the apply leg exits 0'
    Wait-Until { -not (Test-Path -LiteralPath $installDir) } 30 | Out-Null
    Remove-TreeHard $installDir
    $code = Invoke-Exe -Path $Setup -Arguments @(
        '/S', '--dir', $installDir, '--scope', 'user',
        '--start-menu', 'on', '--desktop', 'on', '--log', (New-Log 'install-before-apply'))
    Assert-Eq 0 $code 'reinstall of the shipped version exits 0'

    $ready = Join-Path $updatesDir 'ready\99.0.0'
    New-Item -ItemType Directory -Force -Path $ready | Out-Null
    $stagedExe = Join-Path $ready 'dish-setup.exe'
    Copy-Item $setup99 $stagedExe -Force
    $stagedHash = (Get-FileHash $stagedExe -Algorithm SHA256).Hash.ToLower()
    $stagedSize = (Get-Item $stagedExe).Length
    $stamp = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
    $marker = "schema=1`nversion=99.0.0`nsha256=$stagedHash`nsize=$stagedSize`nstagedUtc=$stamp`n"
    [IO.File]::WriteAllText((Join-Path $ready 'ready.marker'), $marker,
        (New-Object Text.UTF8Encoding $false))

    # Delete the Desktop shortcut by hand first, so the apply's effect on
    # shortcuts is observable. Spec 11.2 ("never re-create a deleted shortcut")
    # and spec 12.2 step 6 both say it stays gone, and that is what 6c asserts;
    # H1's "every install parameter comes from the recorded manifest verbatim"
    # is about the installer never INVENTING a choice, not about resurrecting
    # an icon the user threw away during an unattended update.
    $links = Get-ShortcutPaths 'user'
    Remove-Item -LiteralPath $links.Desktop -Force
    $targetExe = Join-Path $installDir 'dish.exe'

    # A pid that has certainly exited: the wait is then instantaneous and the
    # leg tests the apply, not the wait.
    $deadPid = (Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\PING.EXE') `
            -ArgumentList '-n', '1', '127.0.0.1' -Wait -PassThru -WindowStyle Hidden).Id

    Step '6a. mismatched --expect-version'
    $code = Invoke-Exe -Path $stagedExe -Arguments @(
        '--update-apply', '--waitpid', "$deadPid", '--target-exe', $targetExe,
        '--expect-version', '98.0.0', '--no-relaunch', '--log', (New-Log 'apply-mismatch'))
    Assert-Eq 14 $code 'a payload that is not --expect-version exits 14'
    Assert-Eq "version-mismatch 14" `
        ((Get-Content (Join-Path $ready 'apply-result.txt') -Raw).Trim()) `
        'apply-result.txt records the mismatch'
    Assert-Eq $expectedVersion (Get-Arp 'user').DisplayVersion 'the install is untouched'

    Step '6b. tampered staged installer'
    $tampered = Join-Path $ready 'tampered.exe'
    Copy-Item $stagedExe $tampered -Force
    $bytes = [IO.File]::ReadAllBytes($tampered)
    # Deep inside the appended zip, well past the stub image: the trailer still
    # points at a range whose CRC-32 no longer matches.
    $offset = [int]($bytes.Length * 0.7)
    $bytes[$offset] = $bytes[$offset] -bxor 0xFF
    [IO.File]::WriteAllBytes($tampered, $bytes)
    $code = Invoke-Exe -Path $tampered -Arguments @(
        '--update-apply', '--waitpid', "$deadPid", '--target-exe', $targetExe,
        '--expect-version', '99.0.0', '--no-relaunch', '--log', (New-Log 'apply-tampered'))
    Assert-Eq 7 $code 'a tampered payload exits 7 before touching the install'
    Assert-Eq $expectedVersion (Get-Arp 'user').DisplayVersion 'the install is still intact'
    Remove-Item -LiteralPath $tampered -Force

    Step '6c. the real apply'
    $code = Invoke-Exe -Path $stagedExe -Arguments @(
        '--update-apply', '--waitpid', "$deadPid", '--target-exe', $targetExe,
        '--expect-version', '99.0.0', '--no-relaunch', '--log', (New-Log 'apply'))
    Assert-Eq 0 $code '--update-apply exits 0'
    Assert-Eq 'ok 0' ((Get-Content (Join-Path $ready 'apply-result.txt') -Raw).Trim()) `
        'apply-result.txt records success'
    Assert-Eq '99.0.0' (Get-Arp 'user').DisplayVersion 'ARP DisplayVersion after the apply'
    Assert-Exists $links.StartMenu 'the Start Menu shortcut survived the apply'
    Assert-Absent $links.Desktop 'a shortcut deleted before the apply stays absent'
    # The corrected set is what gets recorded, so the NEXT upgrade agrees and
    # the uninstaller never hunts for a link that is not there.
    $applied = Get-Content (Join-Path $installDir '.dish-manifest.json') -Raw | ConvertFrom-Json
    Assert-True $applied.startMenu 'the record still claims the Start Menu shortcut'
    Assert-True (-not $applied.desktop) 'the record no longer claims the deleted shortcut'
    Assert-True ($applied.shortcutPaths.Count -eq 1) 'exactly one shortcut path is recorded'

    # -- 7. Uninstall ------------------------------------------------------
    Step '7a. Silent uninstall leaves user data alone'
    New-Item -ItemType Directory -Force -Path $dishData | Out-Null
    Set-Content -LiteralPath $markerFile -Value 'dish e2e marker' -Encoding ASCII
    $code = Invoke-Exe -Path (Join-Path $installDir 'uninstall.exe') `
        -Arguments @('/S', '--log', (New-Log 'uninstall'))
    Assert-Eq 0 $code 'silent uninstall exits 0'
    Assert-True (Wait-Until { -not (Test-Path -LiteralPath $installDir) } 30) `
        'the install directory is gone within 30 s'
    Assert-True (Wait-Until { $null -eq (Get-Arp 'user') } 30) 'the ARP key is gone'
    Assert-Absent $links.StartMenu 'the Start Menu shortcut is gone'
    Assert-Exists $markerFile 'user data survives an uninstall without --purge-user-data'
    Assert-Absent $updatesDir 'the updater cache is removed regardless (D13)'

    if ($SkipUserDataPurge) {
        Write-Host '   (skipped 7b: -SkipUserDataPurge)' -ForegroundColor Yellow
    } else {
        Step '7b. --purge-user-data removes it'
        New-Item -ItemType Directory -Force -Path $backupDir | Out-Null
        Invoke-Native reg.exe export 'HKCU\Software\Dish\Dish' `
            (Join-Path $backupDir 'hkcu-dish.reg') /y
        Invoke-Native reg.exe export 'HKCU\Software\TinkerNorth\Dish' `
            (Join-Path $backupDir 'hkcu-tinkernorth.reg') /y
        if (Test-Path -LiteralPath $dishData) {
            Copy-Item $dishData (Join-Path $backupDir 'LocalAppData-Dish') -Recurse -Force
        }
        $script:userDataBackedUp = $true

        $code = Invoke-Exe -Path $Setup -Arguments @(
            '/S', '--dir', $installDir, '--scope', 'user', '--log', (New-Log 'install-purge'))
        Assert-Eq 0 $code 'reinstall before the purge leg exits 0'
        New-Item -ItemType Directory -Force -Path (Join-Path $updatesDir 'ready\98.0.0') | Out-Null
        Set-Content -LiteralPath $markerFile -Value 'dish e2e marker' -Encoding ASCII
        $code = Invoke-Exe -Path (Join-Path $installDir 'uninstall.exe') `
            -Arguments @('/S', '--purge-user-data', '--log', (New-Log 'uninstall-purge'))
        Assert-Eq 0 $code 'silent uninstall --purge-user-data exits 0'
        Assert-True (Wait-Until { -not (Test-Path -LiteralPath $installDir) } 30) `
            'the install directory is gone within 30 s'
        Assert-Absent $markerFile 'the purge removed %LOCALAPPDATA%\Dish'
        Assert-Absent $updatesDir 'the purge removed the updater cache too'
    }

    Step '7c. Nothing left to uninstall'
    $code = Invoke-Exe -Path $Setup -Arguments @(
        '/S', '--uninstall', '--log', (New-Log 'uninstall-empty'))
    Assert-Eq 11 $code 'uninstalling nothing exits 11'

    # -- 8. --extract-only -------------------------------------------------
    Step '8. --extract-only matches the payload manifest'
    $extract = Join-Path $scratch 'extract'
    $code = Invoke-Exe -Path $Setup -Arguments @(
        '/S', '--extract-only', $extract, '--log', (New-Log 'extract'))
    Assert-Eq 0 $code '--extract-only exits 0'
    $extractRoot = (Get-Item -LiteralPath $extract).FullName.TrimEnd('\')
    # Not [IO.Path]::GetRelativePath: it does not exist in .NET Framework, and
    # this script has to run under Windows PowerShell as well as CI's pwsh.
    # @() around every pipeline: under Set-StrictMode a Where-Object that
    # matches nothing yields $null, and $null.Count is a hard error.
    $extracted = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File | ForEach-Object {
            $_.FullName.Substring($extractRoot.Length + 1).Replace('\', '/')
        })
    # The payload manifest is the same one the install wrote, so read it back
    # out of a manifest-shaped file the extract produced nothing of: compare
    # against the installed record from step 2 instead.
    $expectedFiles = @($manifest.files | ForEach-Object { $_.path })
    $missing = @($expectedFiles | Where-Object { $_ -notin $extracted })
    $extra = @($extracted | Where-Object { $_ -notin $expectedFiles })
    if ($missing.Count -gt 0) { Fail "not extracted: $($missing -join ', ')" }
    Ok "every one of the $($expectedFiles.Count) manifest entries was extracted"
    if ($extra.Count -gt 0) { Fail "extracted but not in the manifest: $($extra -join ', ')" }
    Ok 'nothing outside the manifest was extracted'

    # -- 9. Machine scope --------------------------------------------------
    if ($SkipMachineScope -or -not $isElevated) {
        Write-Host ''
        Write-Host '   (skipped the machine-scope leg: needs an elevated shell)' -ForegroundColor Yellow
    } else {
        Step '9. Machine scope (HKLM + all-users shortcuts)'
        $machineDir = Join-Path $sandbox 'Machine dir'
        $code = Invoke-Exe -Path $Setup -Arguments @(
            '/S', '--dir', $machineDir, '--scope', 'machine',
            '--start-menu', 'on', '--desktop', 'off', '--log', (New-Log 'install-machine'))
        Assert-Eq 0 $code 'machine-scope silent install exits 0'
        $machineArp = Get-Arp 'machine'
        Assert-True ($null -ne $machineArp) 'ARP key exists under HKLM'
        Assert-Eq 'machine' $machineArp.InstallScope 'ARP InstallScope is machine'
        $machineLinks = Get-ShortcutPaths 'machine'
        Assert-Exists $machineLinks.StartMenu 'Start Menu shortcut at the all-users location'
        Assert-Absent $machineLinks.Desktop 'no Desktop shortcut was created'
        Assert-True ($null -eq (Get-Arp 'user')) 'machine scope wrote nothing to HKCU'

        $code = Invoke-Exe -Path (Join-Path $machineDir 'uninstall.exe') `
            -Arguments @('/S', '--log', (New-Log 'uninstall-machine'))
        Assert-Eq 0 $code 'machine-scope silent uninstall exits 0'
        Assert-True (Wait-Until { -not (Test-Path -LiteralPath $machineDir) } 30) `
            'the machine install directory is gone within 30 s'
        Assert-True (Wait-Until { $null -eq (Get-Arp 'machine') } 30) 'the HKLM ARP key is gone'
        Assert-Absent $machineLinks.StartMenu 'the all-users shortcut is gone'
    }

    Write-Host ''
    Write-Host "Installer round-trip PASSED ($script:Checks assertions)." -ForegroundColor Green
    exit 0
}
catch {
    Write-Host ''
    Write-Host $_.Exception.Message -ForegroundColor Red
    Write-Host "Logs: $script:LogDir"
    exit 1
}
finally {
    if ($holder) { Stop-Process -Id $holder.Id -Force -ErrorAction SilentlyContinue }
    Restore-UserData
    foreach ($leftover in @($installDir, (Join-Path $sandbox 'Machine dir'))) {
        if (Test-Path -LiteralPath $leftover) {
            Write-Host "   (cleaning up $leftover)"
            Remove-TreeHard $leftover
        }
    }
    Remove-TreeHard (Join-Path $updatesDir 'ready\99.0.0')
}
