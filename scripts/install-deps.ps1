<#
.SYNOPSIS
    Install the Windows build toolchain for Dish: scripts/install-deps.ps1

.DESCRIPTION
    Installs what the Windows workflows install, at the versions
    scripts/tools.ps1 pins, in six idempotent steps:

      [1] Visual Studio 2022 Build Tools (Desktop C++ workload + Win11 SDK)
      [2] CMake + Ninja
      [3] LLVM (clang-tidy) + the pinned clang-format via pip (the PyPI
          wheel CI checks with; LLVM's own clang-format floats)
      [4] Python 3.12 + aqtinstall + the pinned Qt to C:\Qt (headless; Qt's
          own installer needs an account and a UI). The same Qt carries the
          Linguist tools the translation gate runs.
      [5] vcpkg to %USERPROFILE%\vcpkg, checked out at vcpkg.json's
          builtin-baseline, the commit CI checks out (manifest mode: the
          first cmake configure builds libsodium/openssl/opus/SDL2/
          sentry-native from vcpkg.json then)
      [6] The pinned Inno Setup, per user (scripts/install-inno-setup.ps1)

    It persists VCPKG_ROOT and CMAKE_PREFIX_PATH as user environment
    variables at the end, so later shells find Qt and vcpkg. HEADS UP: both
    are REPLACED, not appended to.

    Not pinned, here or in CI: Visual Studio, CMake, Ninja and LLVM come from
    the runner image and from winget's current packages.

    Total: roughly 12 GB of downloads, 30-60 min wall-clock on a fresh box.
    Re-running is safe: every step short-circuits on an existing install.

    Requires winget (ships with Windows 11; otherwise install "App Installer"
    from the Microsoft Store). Expect UAC prompts, VS Build Tools especially.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'tools.ps1')
$qtRoot = 'C:\Qt'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vcpkgRootDefault = Join-Path $env:USERPROFILE 'vcpkg'

function Step([string]$Text) { Write-Output ''; Write-Output "=== $Text ===" }

function Invoke-Winget([string]$Id, [string[]]$ExtraArgs = @()) {
    & winget install --id $Id --silent --accept-source-agreements --accept-package-agreements @ExtraArgs
    # winget returns non-zero for benign outcomes such as "already installed
    # at latest version" (0x8A150085); callers re-probe instead of trusting it.
    if ($LASTEXITCODE -ne 0) {
        Write-Output "[NOTE] winget install $Id exited $LASTEXITCODE (usually: already installed)."
    }
}

function Find-VsInstall {
    # vswhere finds any VS flavour wherever it was installed, so an existing
    # VS is reused rather than a second toolchain installed alongside it.
    # -products * is required to see BuildTools.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    return & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
}

Step 'Checking winget'
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Error 'winget not found on PATH. Install "App Installer" from the Microsoft Store, then re-run.'
}
Write-Output '[OK]  winget available'

Step '[1/6] Visual Studio 2022 Build Tools'
$vsInstall = Find-VsInstall
if ($vsInstall) {
    Write-Output "[OK]  Visual Studio with VC++ tools found at: $vsInstall"
} else {
    Write-Output '[INFO] Installing Microsoft.VisualStudio.2022.BuildTools via winget'
    # --override is how winget passes component IDs to the VS installer;
    # --norestart suppresses a reboot prompt none of these components need.
    Invoke-Winget 'Microsoft.VisualStudio.2022.BuildTools' @(
        '--override',
        '--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --quiet --wait --norestart'
    )
    $vsInstall = Find-VsInstall
    if (-not $vsInstall) {
        Write-Error ("Visual Studio with VC++ tools not detected after install attempt.`n" +
            "If a UAC prompt was canceled, approve it and re-run. Otherwise launch the`n" +
            'Visual Studio Installer and add the "Desktop development with C++" workload.')
    }
    Write-Output "[OK]  Visual Studio with VC++ tools found at: $vsInstall"
}

Step '[2/6] CMake + Ninja'
Invoke-Winget 'Kitware.CMake'
Invoke-Winget 'Ninja-build.Ninja'

Step "[3/6] LLVM (clang-tidy) + clang-format $DishClangFormatVersion"
Invoke-Winget 'LLVM.LLVM'

Step "[4/6] Python 3 + Qt $DishQtVersion (aqtinstall)"
# On Windows 11 `python` can resolve to the WindowsApps alias stub that opens
# the Microsoft Store; the py.exe launcher is not aliased, so probe with that.
$py = $null
& py -3 --version *> $null
if ($LASTEXITCODE -eq 0) { $py = 'py' }
if (-not $py) {
    Write-Output '[INFO] Installing Python 3.12 via winget'
    Invoke-Winget 'Python.Python.3.12'
    & py -3 --version *> $null
    if ($LASTEXITCODE -eq 0) {
        $py = 'py'
    } else {
        # A freshly installed Python's PATH entry does not reach this
        # already-running process; probe the standard locations directly.
        $candidates = @(
            (Join-Path $env:LOCALAPPDATA 'Programs\Python\Launcher\py.exe'),
            (Join-Path $env:ProgramFiles 'Python Launcher\py.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe')
        )
        $py = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    }
}
if (-not $py) {
    Write-Error 'Python installed but neither py.exe nor python.exe found. Open a new terminal and re-run this script.'
}
Write-Output "[OK]  Python launcher: $py"

# The pinned clang-format CI checks with. Installed via the Python that step
# 4 guarantees; LLVM's floating clang-format stays available for clang-tidy.
& $py -3 -m pip install --user "clang-format==$DishClangFormatVersion"
if ($LASTEXITCODE -ne 0) { Write-Error "pip install clang-format==$DishClangFormatVersion failed" }

$qtPrefix = Join-Path $qtRoot (Join-Path $DishQtVersion $DishQtArchDir)
if (Test-Path (Join-Path $qtPrefix 'bin\Qt6Core.dll')) {
    Write-Output "[OK]  Qt $DishQtVersion already installed at $qtPrefix"
} else {
    # The aqtinstall jurplel/install-qt-action uses in CI: the source
    # scripts/tools.ps1 names, or that action's default PyPI line.
    $aqt = if ($DishAqtSource) { $DishAqtSource } else { 'aqtinstall==3.3.*' }
    Write-Output "[INFO] Installing $aqt (pip --user)"
    & $py -3 -m pip install --user $aqt
    if ($LASTEXITCODE -ne 0) { Write-Error 'pip install aqtinstall failed' }
    Write-Output "[INFO] Downloading Qt $DishQtVersion $DishQtArch to $qtRoot (~3 GB)"
    & $py -3 -m aqt install-qt windows desktop $DishQtVersion $DishQtArch --outputdir $qtRoot
    if ($LASTEXITCODE -ne 0) { Write-Error 'aqt install-qt failed' }
}

Step '[5/6] vcpkg at the builtin-baseline of vcpkg.json'
$baseline = Get-VcpkgBaseline -RepoRoot $repoRoot
$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { $vcpkgRootDefault }
if (-not (Test-Path (Join-Path $vcpkgRoot '.git'))) {
    Write-Output "[INFO] Cloning microsoft/vcpkg to $vcpkgRoot"
    & git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
    if ($LASTEXITCODE -ne 0) { Write-Error 'git clone vcpkg failed' }
}
$head = (& git -C $vcpkgRoot rev-parse HEAD).Trim()
if ($head -ne $baseline) {
    Write-Output "[INFO] Checking out vcpkg $baseline (CI's commit)"
    & git -C $vcpkgRoot fetch --quiet origin
    if ($LASTEXITCODE -ne 0) { Write-Error 'git fetch vcpkg failed' }
    & git -C $vcpkgRoot checkout --quiet --detach $baseline
    if ($LASTEXITCODE -ne 0) { Write-Error "git checkout $baseline failed in $vcpkgRoot" }
    Remove-Item (Join-Path $vcpkgRoot 'vcpkg.exe') -ErrorAction SilentlyContinue
}
if (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe')) {
    Write-Output "[OK]  vcpkg $baseline bootstrapped at $vcpkgRoot"
} else {
    Write-Output '[INFO] Bootstrapping vcpkg'
    & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { Write-Error 'bootstrap-vcpkg failed' }
}

Step "[6/6] Inno Setup $DishInnoSetupVersion"
& (Join-Path $PSScriptRoot 'install-inno-setup.ps1')

Step 'Persisting env vars (user scope)'
$curVcpkg = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
if ($curVcpkg -ne $vcpkgRoot) {
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $vcpkgRoot, 'User')
    Write-Output "[OK]  VCPKG_ROOT -> $vcpkgRoot"
} else {
    Write-Output '[OK]  VCPKG_ROOT already set'
}
$curPrefix = [Environment]::GetEnvironmentVariable('CMAKE_PREFIX_PATH', 'User')
if ($curPrefix -ne $qtPrefix) {
    [Environment]::SetEnvironmentVariable('CMAKE_PREFIX_PATH', $qtPrefix, 'User')
    Write-Output "[OK]  CMAKE_PREFIX_PATH -> $qtPrefix"
} else {
    Write-Output '[OK]  CMAKE_PREFIX_PATH already set'
}

Write-Output ''
Write-Output '=== Done ==='
Write-Output ''
Write-Output 'Next steps:'
Write-Output '  1. Open a NEW terminal so the env changes take effect (any PowerShell'
Write-Output '     window works: scripts\build.ps1 imports the MSVC environment itself).'
Write-Output '  2. Build:   scripts\build.ps1 release'
Write-Output '  3. Run:     .\build-release\dish.exe'
Write-Output '  4. Test:    scripts\build.ps1 debug test'
Write-Output '  5. CI parity before pushing:  scripts\ci-local.ps1'
