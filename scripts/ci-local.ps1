<#
.SYNOPSIS
    Run the gates windows-ci.yml runs, in the same order, against the local
    tree.

.DESCRIPTION
    A green run here means a green run there: clang-format (pinned 22.1.4)
    over CI's exact file set, the action-pin lint from _security.yml, the
    debug preset's configure + build + ctest, qmllint with CI's flags, the QML
    literal scanner, the translation gate, clang-tidy four wide over CI's file
    set, the release preset build (Dish + dish_setup_image), the staged-bundle
    portable smoke test, and (opt-in) the installer compile + silent
    round-trip.

        scripts/ci-local.ps1
        scripts/ci-local.ps1 -AllowMissing     # missing tool -> notice, not failure
        scripts/ci-local.ps1 -WithInstaller    # also iscc + the install/uninstall round-trip

    Without -AllowMissing a gate whose tool is absent FAILS rather than
    printing a notice and continuing: a "green" run that silently skipped a
    gate is worse than no run at all.

    The installer round-trip is opt-in because it really installs and
    uninstalls Dish (disk + HKCU) on this machine, exactly like CI does on a
    throwaway runner.

.PARAMETER AllowMissing
    Downgrade a missing gated tool (clang-format, clang-tidy, cmake, qmllint,
    lupdate, windeployqt) to a notice instead of failing.

.PARAMETER WithInstaller
    Also compile installer.iss (scripts/build-installer.ps1) and run
    scripts/test-installer-roundtrip.ps1 against it, like CI.
#>
[CmdletBinding()]
param(
    [switch]$AllowMissing,
    [switch]$WithInstaller
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

function Step([string]$Text) { Write-Output ''; Write-Output "=== $Text ===" }

# Returns $true when the caller should run the gate, $false when it was
# skipped by permission, and throws when a tool CI gates on is missing.
# Write-Host for the notice: this function's pipeline output IS its return
# value.
function Have([string]$Tool) {
    if (Get-Command $Tool -ErrorAction SilentlyContinue) { return $true }
    if ($AllowMissing) {
        Write-Host "::notice:: $Tool is not installed; CI gates this. Skipping (-AllowMissing)."
        return $false
    }
    throw "$Tool is not installed and CI gates it. Install it (scripts/install-deps.ps1), or re-run with -AllowMissing."
}

# Qt tools live in the Qt prefix, not necessarily on PATH.
function Resolve-QtTool([string]$Exe) {
    foreach ($prefix in @($env:QT_ROOT_DIR, $env:CMAKE_PREFIX_PATH)) {
        if ($prefix -and (Test-Path (Join-Path $prefix "bin\$Exe"))) { return (Join-Path $prefix "bin\$Exe") }
    }
    $onPath = Get-Command $Exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    if ($AllowMissing) {
        Write-Host "::notice:: $Exe not found in the Qt prefix; CI gates this. Skipping (-AllowMissing)."
        return $null
    }
    throw "$Exe not found (QT_ROOT_DIR / CMAKE_PREFIX_PATH unset or Qt missing). Run scripts/install-deps.ps1, or re-run with -AllowMissing."
}

Step 'clang-format (check only)'
if (Have 'clang-format') {
    # CI pins 22.1.4; other versions disagree on braced-init lists, which is
    # why the pin exists.
    $want = '22.1.4'
    $got = ((& clang-format --version) | Select-String -Pattern '\d+\.\d+\.\d+').Matches[0].Value
    if ($got -ne $want) {
        Write-Output "::notice:: clang-format $got, CI pins $want; disagreements may be the version, not the code."
    }
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-format.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'clang-format check failed' }
}

Step 'Action pin lint (40-char SHA required)'
# PowerShell port of the awk in _security.yml's action-pin-lint job.
$pinFail = $false
Get-ChildItem -Path .github\workflows -Recurse -File -Include *.yml, *.yaml | ForEach-Object {
    $file = $_
    foreach ($raw in (Get-Content $file.FullName)) {
        if ($raw -match '^\s*#') { continue }
        $line = $raw -replace '\s+#.*$', ''
        if ($line -notmatch '^\s*-?\s*uses:\s+(\S.*)$') { continue }
        $ref = ($line -replace '^\s*-?\s*uses:\s+', '').TrimEnd()
        if ($ref -match '^\./') { continue }
        if ($ref -match '^docker://[^@]+@sha256:[0-9a-f]{64}$') { continue }
        if ($ref -notmatch '@[0-9a-f]{40}(\s|$)') {
            Write-Output "$($file.FullName): $ref"
            $script:pinFail = $true
        } elseif ($ref -match '@0{40}(\s|$)') {
            Write-Output "$($file.FullName): $ref (forbidden all-zero placeholder pin)"
            $script:pinFail = $true
        }
    }
}
if ($pinFail) { throw 'unpinned action reference' }
Write-Output 'action pins: OK'

# ── Toolchain env, the way scripts/build.ps1 finds it ─────────────────────
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $install = $null
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -prerelease -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if ($install) {
        $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
        Write-Output "==> Importing MSVC env via $vcvars"
        foreach ($line in (cmd.exe /c "`"$vcvars`" >nul && set")) {
            if ($line -match '^([^=]+)=(.*)$') { Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2] }
        }
    } elseif (-not $AllowMissing) {
        throw 'No MSVC toolchain found (vswhere). Run scripts/install-deps.ps1, or re-run with -AllowMissing.'
    } else {
        Write-Host '::notice:: no MSVC toolchain; skipping every build-dependent gate (-AllowMissing).'
    }
}
if (-not $env:VCPKG_ROOT) {
    $default = Join-Path $env:USERPROFILE 'vcpkg'
    if (Test-Path (Join-Path $default 'scripts\buildsystems\vcpkg.cmake')) { $env:VCPKG_ROOT = $default }
}

$builds = (Get-Command cl.exe -ErrorAction SilentlyContinue) -and (Have 'cmake')
if ($builds) {
    $jobs = $env:NUMBER_OF_PROCESSORS

    Step 'Configure (Debug, preset debug)'
    & cmake --preset debug
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure (debug) failed' }

    Step 'Build (Debug)'
    & cmake --build --preset debug --parallel $jobs
    if ($LASTEXITCODE -ne 0) { throw 'cmake build (debug) failed' }

    Step 'Run tests'
    & ctest --preset debug --parallel
    if ($LASTEXITCODE -ne 0) { throw 'ctest failed' }

    Step 'qmllint (QML static analysis)'
    if (Resolve-QtTool 'qmllint.exe') {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-qml.ps1') -BuildDir build
        if ($LASTEXITCODE -ne 0) { throw 'qmllint failed' }
    }

    Step 'QML literal scanner'
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'qml-lint-literals.ps1') -Mode error
    if ($LASTEXITCODE -ne 0) { throw 'QML literal scanner failed' }

    Step 'Translation catalogues in sync'
    if (Resolve-QtTool 'lupdate.exe') {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-translations.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'translation check failed' }
    }

    Step 'clang-tidy'
    if (Have 'clang-tidy') {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-tidy.ps1') -BuildDir build
        if ($LASTEXITCODE -ne 0) { throw 'clang-tidy failed' }
    }

    Step 'Configure (Release, preset release)'
    & cmake --preset release
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure (release) failed' }

    Step 'Build release (Dish + dish_setup_image)'
    & cmake --build --preset release --parallel $jobs
    if ($LASTEXITCODE -ne 0) { throw 'cmake build (release) failed' }

    Step 'Stage portable bundle'
    if (Resolve-QtTool 'windeployqt.exe') {
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'stage-bundle.ps1') -BuildDir build-release -OutDir artifact
        if ($LASTEXITCODE -ne 0) { throw 'stage-bundle failed' }

        Step 'Portable bundle smoke test (it must start)'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'test-portable-bundle.ps1') -Bundle artifact
        if ($LASTEXITCODE -ne 0) { throw 'portable bundle smoke test failed' }
    }

    if ($WithInstaller) {
        Step 'Compile installer (Inno Setup)'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-installer.ps1') -BuildDir build-release
        if ($LASTEXITCODE -ne 0) { throw 'installer compile failed' }

        Step 'Installer silent round-trip (no UI)'
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'test-installer-roundtrip.ps1') -Setup dist/dish-setup.exe
        if ($LASTEXITCODE -ne 0) { throw 'installer round-trip failed' }
    } else {
        Write-Output ''
        Write-Output '(installer compile + round-trip skipped; pass -WithInstaller to run them like CI)'
    }
}

Write-Output ''
Write-Output 'All local CI gates passed.'
