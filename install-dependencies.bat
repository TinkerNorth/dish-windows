@echo off
REM ============================================================================
REM  Install Build Dependencies (Dish Windows)
REM
REM  Installs everything needed to build dish.exe and run the test suite:
REM
REM    [1] Visual Studio 2022 Build Tools (Desktop C++ workload + Win SDK)
REM    [2] CMake + Ninja
REM    [3] LLVM (clang-format, clang-tidy)
REM    [4] Python 3 + aqtinstall + Qt 6.7.3 to C:\Qt
REM    [5] vcpkg + libsodium + SDL2 to %USERPROFILE%\vcpkg
REM
REM  It also sets VCPKG_ROOT and CMAKE_PREFIX_PATH as user environment
REM  variables at the end, so later shells find Qt and vcpkg.
REM
REM  Total: ~12 GB of downloads, ~30-60 min wall-clock on a fresh box.
REM  Re-running is safe: every step short-circuits on an existing install.
REM
REM  Requires winget (ships with Windows 11; otherwise install "App Installer"
REM  from the Microsoft Store). Expect UAC prompts, VS Build Tools especially.
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
REM Only the VC C++ workload + Win11 SDK, not the full Visual Studio IDE.
REM --override is how winget passes component IDs to the VS installer;
REM --norestart suppresses a reboot prompt none of these components need.
REM ----------------------------------------------------------------------------
echo === [1/5] Visual Studio 2022 Build Tools ===

REM vswhere finds any VS flavour (Community / Pro / Enterprise / BuildTools)
REM wherever it was installed, so an existing VS is reused rather than a second
REM toolchain installed alongside it. -products * is required to see BuildTools.
set "VS_INSTALL="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "delims=" %%I in ('"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do set "VS_INSTALL=%%I"
)

if defined VS_INSTALL (
    echo [OK]  Visual Studio with VC++ tools found at: !VS_INSTALL!
) else (
    echo [INFO] Installing Microsoft.VisualStudio.2022.BuildTools via winget
    winget install --id Microsoft.VisualStudio.2022.BuildTools --silent ^
        --override "--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --quiet --wait --norestart" ^
        --accept-source-agreements --accept-package-agreements
    REM winget returns non-zero for benign outcomes such as "already installed
    REM at latest version" (0x8A150085), so re-probe instead of trusting it.
    if exist "%VSWHERE%" (
        for /f "delims=" %%I in ('"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do set "VS_INSTALL=%%I"
    )
    if not defined VS_INSTALL (
        echo [FAIL] Visual Studio with VC++ tools not detected after install attempt.
        echo        If a UAC prompt was canceled, approve it and re-run.
        echo        Otherwise launch the Visual Studio Installer and add the
        echo        "Desktop development with C++" workload to your VS instance.
        exit /b 1
    )
    echo [OK]  Visual Studio with VC++ tools found at: !VS_INSTALL!
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
REM Qt's own installer needs an account and a UI; aqtinstall fetches the same
REM artifacts headlessly into the same C:\Qt\<version>\<arch> layout.
REM %QT_VERSION% is pinned so a re-run can't quietly move you to a Qt that CI
REM has never built against.
REM ----------------------------------------------------------------------------
echo === [4/5] Python 3 + Qt %QT_VERSION% ===

REM On Windows 11 `where python` finds a WindowsApps alias stub that opens the
REM Microsoft Store instead of running Python, which fools ERRORLEVEL into
REM reporting Python present. The `py.exe` launcher is not aliased, so probe
REM with that. The explicit paths below exist because a freshly installed
REM Python's PATH entry does not reach this already-running batch process.
set "PY_EXE="
py -3 --version >nul 2>&1
if %ERRORLEVEL% equ 0 set "PY_EXE=py"

if not defined PY_EXE (
    echo [INFO] Installing Python 3.12 via winget ^(the `python` on PATH is the WindowsApps stub^)
    winget install --id Python.Python.3.12 --silent --accept-source-agreements --accept-package-agreements
    py -3 --version >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        set "PY_EXE=py"
    ) else if exist "%LOCALAPPDATA%\Programs\Python\Launcher\py.exe" (
        set "PY_EXE=%LOCALAPPDATA%\Programs\Python\Launcher\py.exe"
    ) else if exist "%ProgramFiles%\Python Launcher\py.exe" (
        set "PY_EXE=%ProgramFiles%\Python Launcher\py.exe"
    ) else if exist "%LOCALAPPDATA%\Programs\Python\Python312\python.exe" (
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
    REM Quoted: PY_EXE can expand to a path containing spaces.
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
REM Manifest mode: nothing is built here. The first cmake configure reads
REM vcpkg.json through the toolchain file and builds the deps then, which is
REM why this step only clones and bootstraps.
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
REM Persist VCPKG_ROOT + CMAKE_PREFIX_PATH so later shells pick them up.
REM User scope, so no admin rights are needed and nothing machine-wide changes.
REM
REM HEADS UP: both are REPLACED, not appended to. If you already had a
REM CMAKE_PREFIX_PATH pointing at another Qt or SDK, save it before running
REM this — it will be overwritten with the Qt installed above.
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
