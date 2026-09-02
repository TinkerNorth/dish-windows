<#
.SYNOPSIS
    qmllint gate: the exact file set and flags windows-ci.yml runs.

.DESCRIPTION
    Must run after a debug build: qt_add_qml_module generates the Dish.Chrome
    qmldir/qmltypes into the build tree, which importing pages resolve against.
    Every category gates except `unqualified`, downgraded to info because `App`
    is a runtime context property the linter cannot see (docs/QML_CONTRACT.md).

.PARAMETER BuildDir
    The configured build tree whose generated QML module output to import.
    Defaults to build (the debug preset's binaryDir, same as CI).

.PARAMETER QtBin
    Directory holding qmllint.exe. Defaults to QT_ROOT_DIR/bin (what
    jurplel/install-qt-action sets in CI), then CMAKE_PREFIX_PATH/bin, then
    whatever is on PATH.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$QtBin = ''
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

if (-not $QtBin) {
    if ($env:QT_ROOT_DIR -and (Test-Path (Join-Path $env:QT_ROOT_DIR 'bin\qmllint.exe'))) {
        $QtBin = Join-Path $env:QT_ROOT_DIR 'bin'
    } elseif ($env:CMAKE_PREFIX_PATH -and (Test-Path (Join-Path $env:CMAKE_PREFIX_PATH 'bin\qmllint.exe'))) {
        $QtBin = Join-Path $env:CMAKE_PREFIX_PATH 'bin'
    } else {
        $onPath = Get-Command qmllint.exe -ErrorAction SilentlyContinue
        if ($onPath) { $QtBin = Split-Path -Parent $onPath.Source }
    }
}
if (-not $QtBin) {
    throw 'qmllint.exe not found. Set QT_ROOT_DIR or CMAKE_PREFIX_PATH to the Qt prefix, or pass -QtBin.'
}
# The Qt import root sits beside bin/ in the same prefix.
$qtQml = Join-Path (Split-Path -Parent $QtBin) 'qml'

# git's * crosses directory levels; 'src/qml/**/*.qml' would miss the
# top-level Main/AppShell/WindowTitleBar.
$files = git ls-files 'src/qml/*.qml'
if (-not $files) { throw 'git ls-files found no QML files' }
& (Join-Path $QtBin 'qmllint.exe') `
    -I $BuildDir `
    -I $qtQml `
    --unqualified info `
    $files
if ($LASTEXITCODE -ne 0) { throw 'qmllint failed' }
Write-Output "qmllint: OK ($(@($files).Count) files)"
