<#
.SYNOPSIS
    Launches a staged portable bundle and asserts that dish.exe actually
    starts, from a directory that is not the build tree.

.DESCRIPTION
    The only gate that runs the DEPLOYED app. ctest, qmllint, clang-tidy and
    the installer round-trip all exercise a dish.exe sitting in its build
    directory, and the build directory is exactly what hides a missing
    deployment file: Qt resolves QML modules from applicationDirPath first, so
    generated files that never made it into the bundle are still found next to
    the exe. That is how the portable zip came to ship a dish.exe that died on
    launch with `Module "Dish.Chrome" contains no type named "Main"` while
    every other gate stayed green.

    The bundle is COPIED into a scratch directory (a path with a space in it,
    on purpose) before launching, so nothing can silently resolve back into the
    build tree, and so the run's crash logs and settings writes do not land in
    the artifact that is about to be published.

    dish.exe is a GUI-subsystem binary, so it writes nothing to a console of
    its own; QT_ASSUME_STDERR_HAS_CONSOLE makes Qt's message handler use the
    redirected stderr, which is where the QML engine reports a module it cannot
    load. Startup failures are fatal AND silent otherwise: the process just
    exits.

.PARAMETER Bundle
    The staged bundle directory to test (release.yml: dish-windows,
    windows-ci.yml: artifact). Must contain dish.exe.

.PARAMETER WorkRoot
    Where the scratch copy lives. Defaults to RUNNER_TEMP (CI) or TEMP.

.PARAMETER SettleSeconds
    How long the app must stay up. Six seconds is well past the QML engine
    load, the first frame and the update check; a startup failure exits in
    well under one.

.PARAMETER KeepWorkDir
    Leave the scratch copy behind for inspection.

.EXAMPLE
    ./scripts/test-portable-bundle.ps1 -Bundle dish-windows
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Bundle,
    [string]$WorkRoot = '',
    [int]$SettleSeconds = 6,
    [switch]$KeepWorkDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Checks = 0

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

# --- 1. The bundle is complete enough to be worth launching -----------------
# Checked before the launch so a missing piece is named, instead of showing up
# as a process that exited for no stated reason.

if (-not (Test-Path -LiteralPath $Bundle)) { Fail "no bundle directory at $Bundle" }
$bundleRoot = (Resolve-Path -LiteralPath $Bundle).Path
Write-Host "== Portable bundle: $bundleRoot" -ForegroundColor Cyan

Assert-True (Test-Path -LiteralPath (Join-Path $bundleRoot 'dish.exe')) 'dish.exe is in the bundle'

# The app's own QML module entry. Qt searches <exedir>/Dish/Chrome/qmldir before
# the copy compiled into dish.exe, so the one in the bundle decides what the
# engine loads, and it has to declare the app's Main type. release.yml,
# windows-ci.yml and cmake/DishSetupImage.cmake section 3 all stage and check
# this one file.
$qmldir = Join-Path $bundleRoot 'Dish/Chrome/qmldir'
Assert-True (Test-Path -LiteralPath $qmldir) 'Dish/Chrome/qmldir is in the bundle'
Assert-True ((Get-Content -LiteralPath $qmldir -Raw) -match '(?m)^Main 1\.0 ') `
    'the staged qmldir declares the app Main type'

# The runtime DLLs windeployqt does not deploy, and which this test cannot
# otherwise catch: every Windows machine that builds or runs CI already has the
# VC++ redistributable in System32, so a bundle missing the app-local CRT starts
# here and fails only on a user's fresh install, silently, with 0xc0000135.
# Presence is therefore asserted rather than inferred from the launch below.
# libsodium and SDL2 are dish.exe's own imports (vcpkg stages them beside the
# exe at link time); the five CRT files are the set cmake/DishSetupImage.cmake
# section 6 puts in the install image, MSVCP140_1/_2 arriving via Qt6Core.
foreach ($dll in 'libsodium.dll', 'SDL2.dll', 'opus.dll',
                 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
                 'vcruntime140.dll', 'vcruntime140_1.dll') {
    Assert-True (Test-Path -LiteralPath (Join-Path $bundleRoot $dll)) "$dll is in the bundle"
}

# --- 2. Copy it out of the build tree ---------------------------------------

if (-not $WorkRoot) {
    $WorkRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
}
$workDir = Join-Path $WorkRoot ("dish portable smoke " + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$runDir = Join-Path $workDir 'bundle'
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
Copy-Item -Path (Join-Path $bundleRoot '*') -Destination $runDir -Recurse -Force
Ok "copied to a path outside the build tree, with a space in it: $runDir"

$exe = Join-Path $runDir 'dish.exe'

# --- 3. Launch, and require it to still be there ----------------------------

# System.Diagnostics.Process rather than Start-Process, for the exit code:
# Windows PowerShell 5.1's `Start-Process -PassThru` hands back a Process
# object whose ExitCode is always $null, and 0xc0000135 (a runtime DLL missing
# from the bundle) produces no output at all — the exit code is the whole
# diagnosis. Reading both streams with ReadToEndAsync also means a chatty
# startup cannot fill a pipe buffer and deadlock the app we are timing.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.WorkingDirectory = $runDir
$psi.UseShellExecute = $false
$psi.RedirectStandardError = $true
$psi.RedirectStandardOutput = $true
# The software adapter: CI runners have no GPU worth the name, and a smoke test
# must not fail because a D3D11 device could not be created. This asserts that
# the app STARTS, not how it renders.
$psi.EnvironmentVariables['QT_QUICK_BACKEND'] = 'software'
# Qt drops qWarning/qCritical on the floor for a GUI-subsystem process unless
# it believes stderr goes somewhere. Without this the QML engine's diagnosis of
# a failed load is lost and all that is left is the exit code.
$psi.EnvironmentVariables['QT_ASSUME_STDERR_HAS_CONSOLE'] = '1'

$process = [System.Diagnostics.Process]::Start($psi)
$stderrRead = $process.StandardError.ReadToEndAsync()
$stdoutRead = $process.StandardOutput.ReadToEndAsync()

$exitedEarly = $process.WaitForExit($SettleSeconds * 1000)
if (-not $exitedEarly) {
    $process.Kill()
    $process.WaitForExit(5000) | Out-Null
}

# Both tasks complete when the pipes close, which the exit above guarantees.
$stderrText = $stderrRead.Result
$stdoutRead.Result | Out-Null
if ($null -eq $stderrText) { $stderrText = '' }

if ($stderrText.Trim()) {
    Write-Host '   --- dish.exe stderr ---'
    $stderrText.TrimEnd() -split "`r?`n" | ForEach-Object { Write-Host "   | $_" }
    Write-Host '   -----------------------'
}

if ($exitedEarly) {
    # 0xc0000135 = STATUS_DLL_NOT_FOUND, i.e. a runtime DLL that never made it
    # into the bundle. Anything else with an empty stderr is worth a debugger.
    $code = '0x{0:x8}' -f $process.ExitCode
    Fail ("dish.exe exited after less than $SettleSeconds s, exit code $code. A deployed app that " +
          'cannot start is what this test exists to catch; the stderr above, if any, is the reason.')
}
Ok "dish.exe stayed up for $SettleSeconds s"

# Belt and braces: the engine reports a module it cannot resolve and then the
# app may keep a window open anyway (Qt only quits when the last window goes).
# Any of these means the UI is not the UI that was built.
foreach ($pattern in @('failed to load component', 'contains no type named', 'is not a type', 'module .* is not installed')) {
    if ($stderrText -match $pattern) { Fail "dish.exe reported a QML load failure matching '$pattern'" }
}
Ok 'no QML load failures on stderr'

if (-not $KeepWorkDir) {
    Remove-Item -LiteralPath $workDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "PORTABLE BUNDLE OK - $script:Checks checks passed" -ForegroundColor Green
