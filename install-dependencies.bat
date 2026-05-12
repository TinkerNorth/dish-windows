@echo off
REM ============================================================================
REM  Install Build Dependencies (Dish Windows)
REM
REM  Installs everything needed to build dish.exe and run the unit-test suite.
REM
REM    [1] Visual Studio 2022 Build Tools (Desktop C++ workload + Win SDK)
REM    [2] CMake + Ninja
REM    [3] LLVM (clang-format, clang-tidy)
REM    [4] Python 3 + aqtinstall + Qt 6.7.3 to C:\Qt
REM    [5] vcpkg + libsodium + SDL2 to %USERPROFILE%\vcpkg
REM
REM  Each step is idempotent — winget skips already-installed packages, and
REM  the Qt + vcpkg paths short-circuit on existing installs.
REM
REM  Total: ~12 GB of downloads, ~30-60 min wall-clock on a fresh box.
REM
REM  Requires: winget (ships with Windows 11; in the Microsoft Store as
REM  "App Installer" otherwise). Some installers (VS Build Tools in
REM  particular) will trigger a UAC prompt — approve them when asked.
REM ============================================================================

setlocal EnableDelayedExpansion

set QT_VERSION=6.7.3
set QT_ARCH=win64_msvc2019_64
set QT_ROOT=C:\Qt
set VCPKG_ROOT_DEFAULT=%USERPROFILE%\vcpkg

echo === Checking winget ===
where winget >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [FAIL] winget not found on PATH.
    echo        Install "App Installer" from the Microsoft Store, then re-run.
    exit /b 1
)
echo [OK]  winget available
echo.

REM ----------------------------------------------------------------------------
REM [1/5] Visual Studio 2022 Build Tools
REM
REM Installs only the VC C++ workload + Win11 SDK so we get cl.exe + the
REM Windows headers without the full Visual Studio IDE. The --override clause
REM is the documented way to pass workload/component IDs through winget into
REM the VS installer; --quiet keeps the VS UI hidden, --norestart suppresses
REM the reboot prompt (none of the components installed require one, but the
REM installer asks anyway).
REM ----------------------------------------------------------------------------
echo === [1/5] Visual Studio 2022 Build Tools ===
where cl.exe >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [OK]  cl.exe already on PATH — skipping VS Build Tools install
) else (
    if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC" (
        echo [OK]  Build Tools already installed at %ProgramFiles%\Microsoft Visual Studio\2022\BuildTools
    ) else (
        winget install --id Microsoft.VisualStudio.2022.BuildTools --silent ^
            --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --quiet --wait --norestart" ^
            --accept-source-agreements --accept-package-agreements
        if !ERRORLEVEL! neq 0 (
            echo [FAIL] VS Build Tools install failed. Re-run after handling any UAC prompt.
            exit /b 1
        )
    )
)
echo.

REM ----------------------------------------------------------------------------
REM [2/5] CMake + Ninja
REM ----------------------------------------------------------------------------
echo === [2/5] CMake + Ninja ===
winget install --id Kitware.CMake --silent --accept-source-agreements --accept-package-agreements
winget install --id Ninja-build.Ninja --silent --accept-source-agreements --accept-package-agreements
echo.

REM ----------------------------------------------------------------------------
REM [3/5] LLVM (clang-format, clang-tidy)
REM ----------------------------------------------------------------------------
echo === [3/5] LLVM ^(clang-format, clang-tidy^) ===
winget install --id LLVM.LLVM --silent --accept-source-agreements --accept-package-agreements
echo.

REM ----------------------------------------------------------------------------
REM [4/5] Python + Qt 6 via aqtinstall
REM
REM Qt's official installer at qt.io requires a free account and an interactive
REM UI. aqtinstall is a Python package that downloads the same artifacts
REM headlessly into C:\Qt\<version>\<arch>, which is the layout the official
REM installer produces. We pin %QT_VERSION% so subsequent runs of this script
REM don't quietly upgrade to a newer Qt that the CI workflow hasn't seen.
REM ----------------------------------------------------------------------------
echo === [4/5] Python 3 + Qt %QT_VERSION% ===

REM On Windows 11 `where python` resolves to a WindowsApps "App Execution
REM Alias" stub at %LOCALAPPDATA%\Microsoft\WindowsApps\python.exe that
REM does NOT run Python — it opens the Microsoft Store. The stub fools
REM ERRORLEVEL into thinking Python is installed when only the alias is.
REM
REM Workaround: use the Python launcher (`py.exe`). The launcher is
REM installed by the official Python installer (which is what winget
REM Python.Python.3.12 uses) and is NOT subject to the App Execution
REM Alias mechanism. If `py -3` doesn't work, we know Python is genuinely
REM missing, install it, then look in the documented launcher install
REM locations so we don't depend on the new PATH propagating into this
REM running script.
set "PY_EXE="
py -3 --version >nul 2>&1
if %ERRORLEVEL% equ 0 set "PY_EXE=py"

if not defined PY_EXE (
    echo [INFO] Installing Python 3.12 via winget ^(the `python` on PATH is the WindowsApps stub^)
    winget install --id Python.Python.3.12 --silent --accept-source-agreements --accept-package-agreements
    REM Re-probe via py.exe after install. winget Python adds it to PATH
    REM but the change won't propagate into this batch process; try
    REM standard install locations directly.
    py -3 --version >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        set "PY_EXE=py"
    ) else if exist "%LOCALAPPDATA%\Programs\Python\Launcher\py.exe" (
        set "PY_EXE=%LOCALAPPDATA%\Programs\Python\Launcher\py.exe"
    ) else if exist "%ProgramFiles%\Python Launcher\py.exe" (
        set "PY_EXE=%ProgramFiles%\Python Launcher\py.exe"
    ) else if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" (
        REM Last-ditch: skip the launcher and point at python.exe directly.
        set "PY_EXE=%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
    )
)

if not defined PY_EXE (
    echo [WARN] Python installed but neither py.exe nor python.exe found at
    echo        the standard launcher / per-user install locations. Close
    echo        this terminal, open a new one, and re-run this script.
    exit /b 1
)
echo [OK]  Python launcher: !PY_EXE!

if exist "%QT_ROOT%\%QT_VERSION%\msvc2019_64\bin\Qt6Core.dll" (
    echo [OK]  Qt %QT_VERSION% already installed at %QT_ROOT%
) else (
    echo [INFO] Installing aqtinstall ^(pip --user^)
    REM Quote PY_EXE in case it expanded to a path containing spaces
    REM (e.g. "C:\Program Files\Python Launcher\py.exe").
    "!PY_EXE!" -3 -m pip install --user --upgrade aqtinstall
    if !ERRORLEVEL! neq 0 (
        echo [FAIL] pip install aqtinstall
        exit /b 1
    )
    echo [INFO] Downloading Qt %QT_VERSION% %QT_ARCH% to %QT_ROOT% ^(~3 GB^)
    "!PY_EXE!" -3 -m aqt install-qt windows desktop %QT_VERSION% %QT_ARCH% --outputdir "%QT_ROOT%"
    if !ERRORLEVEL! neq 0 (
        echo [FAIL] aqt install-qt
        exit /b 1
    )
)
echo.

REM ----------------------------------------------------------------------------
REM [5/5] vcpkg + libsodium + SDL2
REM
REM Manifest mode: CMakeLists.txt is configured with the vcpkg toolchain
REM file, and the first cmake configure reads vcpkg.json and builds the
REM listed deps on-demand. Cloning to %USERPROFILE%\vcpkg matches the
REM Microsoft-recommended location and is what the README documents.
REM ----------------------------------------------------------------------------
echo === [5/5] vcpkg ^(libsodium + SDL2 will build on first cmake configure^) ===
if not defined VCPKG_ROOT set VCPKG_ROOT=%VCPKG_ROOT_DEFAULT%

if exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [OK]  vcpkg already bootstrapped at %VCPKG_ROOT%
) else (
    if not exist "%VCPKG_ROOT%\.git" (
        echo [INFO] Cloning microsoft/vcpkg to %VCPKG_ROOT%
        git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
        if !ERRORLEVEL! neq 0 (
            echo [FAIL] git clone vcpkg
            exit /b 1
        )
    )
    echo [INFO] Bootstrapping vcpkg
    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if !ERRORLEVEL! neq 0 (
        echo [FAIL] bootstrap-vcpkg
        exit /b 1
    )
)
echo.

REM ----------------------------------------------------------------------------
REM Persist VCPKG_ROOT + CMAKE_PREFIX_PATH so subsequent shells pick them up
REM without having to think. User-scope so we don't require admin.
REM ----------------------------------------------------------------------------
echo === Persisting env vars ^(user scope^) ===
powershell -NoProfile -Command "$root = '%VCPKG_ROOT%'; $cur = [Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User'); if ($cur -ne $root) { [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $root, 'User'); Write-Host ('[OK]  VCPKG_ROOT -> ' + $root) } else { Write-Host '[OK]  VCPKG_ROOT already set' }"
powershell -NoProfile -Command "$qt = '%QT_ROOT%\%QT_VERSION%\msvc2019_64'; $cur = [Environment]::GetEnvironmentVariable('CMAKE_PREFIX_PATH', 'User'); if ($cur -ne $qt) { [Environment]::SetEnvironmentVariable('CMAKE_PREFIX_PATH', $qt, 'User'); Write-Host ('[OK]  CMAKE_PREFIX_PATH -> ' + $qt) } else { Write-Host '[OK]  CMAKE_PREFIX_PATH already set' }"
echo.

echo === Done ===
echo.
echo Next steps:
echo   1. Close this window and open a NEW Developer Command Prompt for VS 2022
echo      (Start menu ^> "Developer Command Prompt for VS 2022") so cl.exe is
echo      on PATH and VCPKG_ROOT / CMAKE_PREFIX_PATH are picked up.
echo   2. cd /d %~dp0
echo   3. powershell -ExecutionPolicy Bypass -File scripts\build.ps1 release
echo   4. .\build-release\dish.exe
echo.
echo If `cl.exe` still isn't found in the Developer Command Prompt, run:
echo   "%%ProgramFiles%%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
echo manually first.

endlocal
