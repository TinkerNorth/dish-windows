<#
.SYNOPSIS
    Silent install / repair / uninstall assertions for dish-setup.exe.

.DESCRIPTION
    The installer is Inno Setup, so this no longer pins a bespoke engine; it
    asserts the CONTRACT the app and the docs rely on, against a real disk and
    a real HKCU:

      1. A fresh /VERYSILENT install into a directory WITH A SPACE lands
         dish.exe, the Qt runtime beside it, the licence texts, and the
         uninstaller whose presence is the app's installed-vs-portable probe
         (src/update/UpdateCoordinator.cpp: any unins*.exe sibling).
      2. The per-user ARP entry exists and points at the install, and the
         installed dish.exe starts in place and completes its controller
         database check over HTTPS (scripts/test-portable-bundle.ps1
         -InPlace -RequireOnlineCheck).
      3. A silent re-run over the same version (Inno's repair/upgrade path,
         which is also the shape of every OTA apply) exits 0 and leaves the
         install intact.
      4. A silent uninstall removes the files, the directory and the ARP
         entry. The uninstaller's tail is asynchronous (it deletes itself via
         a helper), so removal is polled for up to 30 s.

    Along the way it checks the Privacy page's answers under InstallDefaults,
    the key the app applies once at its next start
    (src/source/store/InstallDefaults.cpp): a first install writes all three
    as 1, a re-run with no privacy switch leaves the key alone, a re-run with
    /CONTROLLERUPDATES=no writes that one answer under a new stamp, and the
    uninstall removes the key.

    Everything happens under a scratch directory and HKCU; no machine state,
    no elevation, no prompts.

.PARAMETER Setup
    Path to the compiled dist\dish-setup.exe under test.

.PARAMETER WorkRoot
    Where the sandbox lives. Defaults to RUNNER_TEMP (CI) or TEMP.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Setup,
    [string]$WorkRoot = ''
)

$ErrorActionPreference = 'Stop'

# Inno derives the per-user ARP key from the AppId in installer.iss.
$appId = '{0B4F3D3C-9526-4953-9CCA-BAAD2AF4A5A1}'
$arpKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\${appId}_is1"
# The Privacy page's answers (installer.iss [Registry]), one DWORD per switch.
$defaultsKey = 'HKCU:\Software\TinkerNorth\Dish\InstallDefaults'
$privacyValues = @('UpdateCheck', 'CrashReports', 'ControllerUpdates')

$Setup = (Resolve-Path $Setup).Path
if (-not $WorkRoot) { $WorkRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP } }
# The space is on purpose: quoting bugs in the handoff or the uninstaller
# show up here or in the field.
$sandbox = Join-Path $WorkRoot ("dish roundtrip " + [IO.Path]::GetRandomFileName().Split('.')[0])
$installDir = Join-Path $sandbox 'Dish App'
New-Item -ItemType Directory -Force -Path $sandbox | Out-Null

$failures = [System.Collections.Generic.List[string]]::new()
function Assert([bool]$Condition, [string]$What) {
    if ($Condition) { "  ok: $What" } else { $failures.Add($What); Write-Warning "FAIL: $What" }
}

function Invoke-Setup([string[]]$Arguments) {
    $p = Start-Process -FilePath $Setup -ArgumentList $Arguments -Wait -PassThru
    return $p.ExitCode
}

try {
    # --- 1. Fresh silent install --------------------------------------------
    'step 1: fresh silent install'
    $log1 = Join-Path $sandbox 'install-1.log'
    $code = Invoke-Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CURRENTUSER', "/DIR=`"$installDir`"", "/LOG=`"$log1`"")
    Assert ($code -eq 0) "fresh install exits 0 (got $code)"
    Assert (Test-Path (Join-Path $installDir 'dish.exe')) 'dish.exe installed'
    Assert (Test-Path (Join-Path $installDir 'Qt6Core.dll')) 'Qt runtime beside the exe'
    Assert (Test-Path (Join-Path $installDir 'vcruntime140.dll')) 'app-local CRT staged'
    Assert (Test-Path (Join-Path $installDir 'licenses\LICENSE.LGPL-3.0.txt')) 'licence texts staged'
    $unins = Get-ChildItem -Path $installDir -Filter 'unins*.exe' -ErrorAction SilentlyContinue
    Assert ($null -ne $unins -and $unins.Count -ge 1) 'uninstaller present (the installed-vs-portable probe)'

    # A first silent install answers every Privacy choice with its default, on.
    $defaults = Get-ItemProperty $defaultsKey -ErrorAction SilentlyContinue
    Assert ($null -ne $defaults) 'InstallDefaults key written'
    $firstStamp = if ($null -ne $defaults) { $defaults.Stamp } else { $null }
    Assert (-not [string]::IsNullOrWhiteSpace($firstStamp)) 'InstallDefaults stamp recorded'
    foreach ($name in $privacyValues) {
        $value = if ($null -ne $defaults) { $defaults.$name } else { $null }
        Assert ($value -eq 1) "InstallDefaults $name is 1 (got '$value')"
    }

    # --- 2. ARP entry --------------------------------------------------------
    'step 2: Add/Remove Programs entry'
    Assert (Test-Path $arpKey) 'per-user ARP key exists'
    if (Test-Path $arpKey) {
        $arp = Get-ItemProperty $arpKey
        # Inno 6.7.3 appends " (Current user)" to a per-user install's ARP name
        # by itself; UninstallDisplayName has no say. Either spelling is the
        # same correct install, so the contract accepts both.
        $displayOk = $arp.DisplayName -eq 'Dish' -or $arp.DisplayName -eq 'Dish (Current user)'
        Assert $displayOk "DisplayName is 'Dish' or 'Dish (Current user)' (got '$($arp.DisplayName)')"
        Assert ($arp.InstallLocation.TrimEnd('\') -eq $installDir) 'InstallLocation points at the sandbox install'
        Assert (-not [string]::IsNullOrWhiteSpace($arp.DisplayVersion)) 'DisplayVersion recorded'
    }

    # --- 2b. The installed app starts and speaks HTTPS ------------------------
    # The smoke test over the installed directory, in place, with the uninstaller
    # beside it: the app this installer leaves behind must start and finish its
    # controller database check over TLS.
    'step 2b: the installed app starts and checks the controller list over HTTPS'
    try {
        & (Join-Path $PSScriptRoot 'test-portable-bundle.ps1') -Bundle $installDir -InPlace -RequireOnlineCheck
        Assert $true 'the installed dish.exe starts and completes an HTTPS check'
    } catch {
        Assert $false "the installed dish.exe starts and completes an HTTPS check ($_)"
    }

    # --- 3. Silent re-run over the same version ------------------------------
    # The repair path, and the exact shape of an OTA apply minus /OTA.
    'step 3: silent re-run (repair / upgrade shape)'
    $log2 = Join-Path $sandbox 'install-2.log'
    $code = Invoke-Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log2`"")
    Assert ($code -eq 0) "re-run exits 0 (got $code)"
    Assert (Test-Path (Join-Path $installDir 'dish.exe')) 'dish.exe still present after re-run'
    Assert (Test-Path $arpKey) 'ARP key survives the re-run'
    $defaults = Get-ItemProperty $defaultsKey -ErrorAction SilentlyContinue
    $stamp = if ($null -ne $defaults) { $defaults.Stamp } else { $null }
    Assert ($stamp -eq $firstStamp) "a re-run with no privacy switch leaves InstallDefaults alone (stamp '$stamp')"

    # --- 3b. A re-run that answers one Privacy choice ------------------------
    # Only the answered choice is written, under a new stamp, so the app's next
    # start applies that one and keeps whatever Settings holds for the others.
    'step 3b: silent re-run with /CONTROLLERUPDATES=no'
    $log3 = Join-Path $sandbox 'install-3.log'
    $code = Invoke-Setup @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/CONTROLLERUPDATES=no', "/LOG=`"$log3`"")
    Assert ($code -eq 0) "re-run with /CONTROLLERUPDATES=no exits 0 (got $code)"
    $defaults = Get-ItemProperty $defaultsKey -ErrorAction SilentlyContinue
    $stamp = if ($null -ne $defaults) { $defaults.Stamp } else { $null }
    Assert ((-not [string]::IsNullOrWhiteSpace($stamp)) -and ($stamp -ne $firstStamp)) "the answered re-run records a new stamp (got '$stamp')"
    $controllerUpdates = if ($null -ne $defaults) { $defaults.ControllerUpdates } else { $null }
    Assert ($controllerUpdates -eq 0) "InstallDefaults ControllerUpdates is 0 (got '$controllerUpdates')"
    $updateCheck = if ($null -ne $defaults) { $defaults.UpdateCheck } else { $null }
    $crashReports = if ($null -ne $defaults) { $defaults.CrashReports } else { $null }
    Assert (($null -eq $updateCheck) -and ($null -eq $crashReports)) "the choices the re-run did not answer are not written (UpdateCheck '$updateCheck', CrashReports '$crashReports')"

    # --- 4. Silent uninstall -------------------------------------------------
    'step 4: silent uninstall'
    $uninsExe = (Get-ChildItem -Path $installDir -Filter 'unins*.exe' | Select-Object -First 1).FullName
    $p = Start-Process -FilePath $uninsExe -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
    Assert ($p.ExitCode -eq 0) "uninstall exits 0 (got $($p.ExitCode))"
    # The uninstaller deletes itself and the directory through a helper after
    # exiting; poll rather than assert immediately.
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline -and ((Test-Path $installDir) -or (Test-Path $arpKey) -or (Test-Path $defaultsKey))) {
        Start-Sleep -Milliseconds 500
    }
    Assert (-not (Test-Path (Join-Path $installDir 'dish.exe'))) 'dish.exe removed'
    Assert (-not (Test-Path $installDir)) 'install directory removed'
    Assert (-not (Test-Path $arpKey)) 'ARP key removed'
    Assert (-not (Test-Path $defaultsKey)) 'InstallDefaults key removed'
}
finally {
    if (Test-Path $installDir) {
        # A failed run must not strand an ARP entry pointing into the sandbox.
        $uninsExe = (Get-ChildItem -Path $installDir -Filter 'unins*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($uninsExe) {
            Start-Process -FilePath $uninsExe.FullName -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait | Out-Null
            Start-Sleep -Seconds 3
        }
    }
    Remove-Item -Recurse -Force $sandbox -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
    throw "Installer round-trip failed: $($failures.Count) assertion(s): $($failures -join '; ')"
}
'Installer round-trip: all assertions passed.'
