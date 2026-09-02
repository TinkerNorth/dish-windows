<#
.SYNOPSIS
    Install the Windows build toolchain for Dish: scripts/install-deps.ps1

.DESCRIPTION
    Installs everything needed to build dish.exe and run the test suite, in
    five idempotent steps (the same set install-dependencies.bat installed;
    that file now forwards here):

      [1] Visual Studio 2022 Build Tools (Desktop C++ workload + Win11 SDK)
      [2] CMake + Ninja
      [3] LLVM (clang-tidy) + clang-format 22.1.4 via pip, the exact pin CI
          checks with (LLVM's own clang-format floats and 18 vs 22 disagree
          on braced-init lists)
      [4] Python 3 + aqtinstall + Qt 6.7.3 to C:\Qt (headless; Qt's own
          installer needs an account and a UI)
      [5] vcpkg to %USERPROFILE%\vcpkg (manifest mode: the first cmake
          configure reads vcpkg.json through the toolchain file and builds
          libsodium/openssl/opus/SDL2 then)

    It persists VCPKG_ROOT and CMAKE_PREFIX_PATH as user environment
    variables at the end, so later shells find Qt and vcpkg. HEADS UP: both
    are REPLACED, not appended to.

    Total: roughly 12 GB of downloads, 30-60 min wall-clock on a fresh box.
    Re-running is safe: every step short-circuits on an existing install.

    Requires winget (ships with Windows 11; otherwise install "App Installer"
    from the Microsoft Store). Expect UAC prompts, VS Build Tools especially.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$qtVersion = '6.7.3'
$qtArch = 'win64_msvc2019_64'
$qtRoot = 'C:\Qt'
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
    return & $vswhere -latest -prerelease -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
}

Step 'Checking winget'
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    Write-Error 'winget not found on PATH. Install "App Installer" from the Microsoft Store, then re-run.'
}
Write-Output '[OK]  winget available'

Step '[1/5] Visual Studio 2022 Build Tools'
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

Step '[2/5] CMake + Ninja'
Invoke-Winget 'Kitware.CMake'
Invoke-Winget 'Ninja-build.Ninja'

Step '[3/5] LLVM (clang-tidy) + pinned clang-format 22.1.4'
Invoke-Winget 'LLVM.LLVM'

Step '[4/5] Python 3 + Qt 6.7.3 (aqtinstall)'
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
& $py -3 -m pip install --user --upgrade 'clang-format==22.1.4'
if ($LASTEXITCODE -ne 0) { Write-Error 'pip install clang-format==22.1.4 failed' }

if (Test-Path (Join-Path $qtRoot "$qtVersion\msvc2019_64\bin\Qt6Core.dll")) {
    Write-Output "[OK]  Qt $qtVersion already installed at $qtRoot"
} else {
    Write-Output '[INFO] Installing aqtinstall (pip --user)'
    & $py -3 -m pip install --user --upgrade aqtinstall
    if ($LASTEXITCODE -ne 0) { Write-Error 'pip install aqtinstall failed' }
    Write-Output "[INFO] Downloading Qt $qtVersion $qtArch to $qtRoot (~3 GB)"
    # Pinned so a re-run cannot quietly move you to a Qt CI has never built
    # against.
    & $py -3 -m aqt install-qt windows desktop $qtVersion $qtArch --outputdir $qtRoot
    if ($LASTEXITCODE -ne 0) { Write-Error 'aqt install-qt failed' }
}

Step '[5/5] vcpkg (libsodium/openssl/opus/SDL2 build on first cmake configure)'
$vcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { $vcpkgRootDefault }
if (Test-Path (Join-Path $vcpkgRoot 'vcpkg.exe')) {
    Write-Output "[OK]  vcpkg already bootstrapped at $vcpkgRoot"
} else {
    if (-not (Test-Path (Join-Path $vcpkgRoot '.git'))) {
        Write-Output "[INFO] Cloning microsoft/vcpkg to $vcpkgRoot"
        & git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
        if ($LASTEXITCODE -ne 0) { Write-Error 'git clone vcpkg failed' }
    }
    Write-Output '[INFO] Bootstrapping vcpkg'
    & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { Write-Error 'bootstrap-vcpkg failed' }
}

Step 'Persisting env vars (user scope)'
$curVcpkg = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
if ($curVcpkg -ne $vcpkgRoot) {
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $vcpkgRoot, 'User')
    Write-Output "[OK]  VCPKG_ROOT -> $vcpkgRoot"
} else {
    Write-Output '[OK]  VCPKG_ROOT already set'
}
$qtPrefix = Join-Path $qtRoot "$qtVersion\msvc2019_64"
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
