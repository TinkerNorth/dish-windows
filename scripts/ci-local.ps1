<#
.SYNOPSIS
    Run the gates windows-ci.yml and version-consistency.yml run, in the same
    order, against the local tree, through the same scripts.

.DESCRIPTION
    A green run here means a green run there. Every gate is a script under
    scripts/ (or a CMake preset) that the workflow calls too, and every pin
    comes from scripts/tools.ps1:

      check-versions.ps1        version mirrors, pins and documented facts
      action-pin lint           every uses: is a 40-character SHA
      check-format.ps1          clang-format, at the pinned version
      preset debug              configure, build, ctest
      measurements report tests
      check-qml.ps1             qmllint
      qml-lint-literals.ps1     design-token literals
      check-translations.ps1    catalogues in sync and complete
      check-tidy.ps1            clang-tidy
      preset release            Dish + dish_setup_image
      stage-bundle.ps1          the portable bundle, then its smoke test and
                                the same test over the install image
      build-installer.ps1 and test-installer-roundtrip.ps1 (-WithInstaller)

        scripts/ci-local.ps1
        scripts/ci-local.ps1 -Quick            # up to the translation gate: no tidy, no release
        scripts/ci-local.ps1 -WithInstaller    # also iscc + the install/uninstall round-trip
        scripts/ci-local.ps1 -AllowMissing     # missing tool -> notice, not failure

    Without -AllowMissing a gate whose tool is absent FAILS rather than
    printing a notice and continuing: a "green" run that silently skipped a
    gate is worse than no run at all.

    CI-only, and why:
      * CodeQL (codeql.yml): needs the CodeQL bundle and uploads to code
        scanning.
      * OSV-Scanner, gitleaks and the allowlist expiry check (security.yml):
        pinned Linux binaries in _security.yml; CONTRIBUTING.md, "Running the
        security checks locally", has the commands.
      * The input-path bench and lock laws (measurements.yml): they measure a
        quiet runner; tools/bench/README.md has the local recipe.

.PARAMETER AllowMissing
    Downgrade a missing gated tool (clang-format, clang-tidy, cmake, python,
    qmllint, lupdate, windeployqt, MSVC) to a notice instead of failing.

.PARAMETER Quick
    Stop after the translation gate: skips clang-tidy, the release build, the
    bundle and image smoke tests and the installer.

.PARAMETER WithInstaller
    Also install the pinned Inno Setup (per user), compile installer.iss
    (scripts/build-installer.ps1) and run scripts/test-installer-roundtrip.ps1
    against it, like CI. Opt-in because the round-trip really installs and
    uninstalls Dish (disk + HKCU) on this machine.
#>
[CmdletBinding()]
param(
    [switch]$AllowMissing,
    [switch]$Quick,
    [switch]$WithInstaller
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
. (Join-Path $PSScriptRoot 'tools.ps1')

function Step([string]$Text) { Write-Output ''; Write-Output "=== $Text ===" }

# The gates run in the host this script runs in (pwsh in CI, either locally).
$shell = (Get-Process -Id $PID).Path

# Returns $true when the caller should run the gate, $false when it was
# skipped by permission, and throws when a tool CI gates on is missing.
# The information stream for the notice: this function's pipeline output IS its return
# value.
function Have([string]$Tool) {
    if (Get-Command $Tool -ErrorAction SilentlyContinue) { return $true }
    if ($AllowMissing) {
        Write-Information "::notice:: $Tool is not installed; CI gates this. Skipping (-AllowMissing)." -InformationAction Continue
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
        Write-Information "::notice:: $Exe not found in the Qt prefix; CI gates this. Skipping (-AllowMissing)." -InformationAction Continue
        return $null
    }
    throw "$Exe not found (QT_ROOT_DIR / CMAKE_PREFIX_PATH unset or Qt missing). Run scripts/install-deps.ps1, or re-run with -AllowMissing."
}

Step 'Versions, pins and documented facts'
& $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-versions.ps1')
if ($LASTEXITCODE -ne 0) { throw 'version consistency failed' }

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

Step 'clang-format (check only)'
if (Have 'clang-format') {
    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-format.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'clang-format check failed' }
}

# Toolchain env, the way scripts/build.ps1 and scripts/ci-env.ps1 find it.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    try {
        Import-MsvcEnvironment
    } catch {
        if (-not $AllowMissing) { throw "$_ Run scripts/install-deps.ps1, or re-run with -AllowMissing." }
        Write-Output "::notice:: $_ Skipping every build-dependent gate (-AllowMissing)."
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

    Step 'Measurements report tests'
    if (Have 'python') {
        & python -m unittest discover -s tests -p 'test_measurements_report.py'
        if ($LASTEXITCODE -ne 0) { throw 'measurements report tests failed' }
    }

    Step 'qmllint (QML static analysis)'
    if (Resolve-QtTool 'qmllint.exe') {
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-qml.ps1') -BuildDir build
        if ($LASTEXITCODE -ne 0) { throw 'qmllint failed' }
    }

    Step 'QML literal scanner'
    & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'qml-lint-literals.ps1') -Mode error
    if ($LASTEXITCODE -ne 0) { throw 'QML literal scanner failed' }

    Step 'Translation catalogues in sync'
    # The build Qt's own Linguist, passed the way windows-ci.yml passes it.
    $lupdate = Resolve-QtTool 'lupdate.exe'
    if ($lupdate) {
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-translations.ps1') -LinguistBin (Split-Path -Parent $lupdate)
        if ($LASTEXITCODE -ne 0) { throw 'translation check failed' }
    }

    if ($Quick) {
        Write-Output ''
        Write-Output 'Quick run: clang-tidy, the release build, the bundle smoke tests and the installer were skipped.'
        Write-Output 'Quick local CI gates passed.'
        return
    }

    Step 'clang-tidy'
    if (Have 'clang-tidy') {
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'check-tidy.ps1') -BuildDir build
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
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'stage-bundle.ps1') -BuildDir build-release -OutDir artifact
        if ($LASTEXITCODE -ne 0) { throw 'stage-bundle failed' }

        Step 'Portable bundle smoke test (it must start)'
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'test-portable-bundle.ps1') -Bundle artifact
        if ($LASTEXITCODE -ne 0) { throw 'portable bundle smoke test failed' }

        Step 'Install image smoke test (it must start)'
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'test-portable-bundle.ps1') -Bundle build-release/setup-image
        if ($LASTEXITCODE -ne 0) { throw 'install image smoke test failed' }
    }

    if ($WithInstaller) {
        Step 'Install Inno Setup (pinned)'
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'install-inno-setup.ps1')
        if ($LASTEXITCODE -ne 0) { throw 'Inno Setup install failed' }

        Step 'Compile installer (Inno Setup)'
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-installer.ps1') -BuildDir build-release
        if ($LASTEXITCODE -ne 0) { throw 'installer compile failed' }

        Step 'Installer silent round-trip (no UI)'
        & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'test-installer-roundtrip.ps1') -Setup dist/dish-setup.exe
        if ($LASTEXITCODE -ne 0) { throw 'installer round-trip failed' }
    } else {
        Write-Output ''
        Write-Output '(installer compile + round-trip skipped; pass -WithInstaller to run them like CI)'
    }
}

Write-Output ''
Write-Output 'All local CI gates passed.'
