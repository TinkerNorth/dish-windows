<#
.SYNOPSIS
    Version-consistency gate: every fact a document or mirror repeats from a
    source of truth still equals it.

.DESCRIPTION
    version-consistency.yml runs this on every pull request that touches a
    version-carrying file, and scripts/ci-local.ps1 runs it first. Each rule
    names its source and its copy:

      * project(Dish VERSION ...) in CMakeLists.txt -> packaging/dish.rc (two
        tuples, two strings), vcpkg.json version-string, and a CHANGELOG.md
        heading "## [X.Y.Z] - YYYY-MM-DD".
      * vcpkg.json builtin-baseline -> THIRD_PARTY.md, and no workflow pins a
        commit of its own (scripts/ci-env.ps1 reads it from vcpkg.json).
      * What that baseline resolves for each port vcpkg.json declares (read
        from microsoft/vcpkg's ports/<name>/vcpkg.json at that commit) ->
        THIRD_PARTY.md's summary row and assets/licenses/licenses.json.
        Crashpad comes with sentry-native, so its rows name the same version.
      * ENet's and Catch2's FetchContent tags -> THIRD_PARTY.md and
        licenses.json.
      * scripts/tools.ps1's Qt, clang-format and Inno Setup pins ->
        THIRD_PARTY.md, licenses.json, CONTRIBUTING.md and the PR template;
        no workflow pins a Qt version of its own.
      * kProtocolVersion in src/core/model/Protocol.h -> CONTRIBUTING.md
        ("Touching the wire protocol") and docs/contract.md.

    Every failure is printed before the script exits non-zero, so one run
    lists all the drift. It needs network access for the vcpkg port files.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
. (Join-Path $PSScriptRoot 'tools.ps1')

$script:failures = [System.Collections.Generic.List[string]]::new()
function Fail([string]$Message) {
    $script:failures.Add($Message)
    # The information stream, not the pipeline: Fail is called from functions
    # whose pipeline output is their return value.
    Write-Information "::error::$Message" -InformationAction Continue
}
function Read-Text([string]$Path) {
    $full = Join-Path $repoRoot $Path
    if (-not (Test-Path $full)) { Fail "$Path does not exist"; return '' }
    return [IO.File]::ReadAllText($full)
}
function Test-DocumentText([string]$Path, [string]$Needle, [string]$What) {
    if (-not (Read-Text $Path).Contains($Needle)) { Fail "$Path does not say $What ('$Needle')" }
}

$thirdParty = Read-Text 'THIRD_PARTY.md'
$licenses = (Read-Text 'assets/licenses/licenses.json' | ConvertFrom-Json).libraries

# The summary table row whose first cell links the component's name.
function Get-SummaryRow([string]$Component) {
    $pattern = '(?m)^\| \[' + [regex]::Escape($Component) + '\]\([^)]*\) \|.*$'
    $match = [regex]::Match($thirdParty, $pattern)
    if (-not $match.Success) { Fail "THIRD_PARTY.md has no summary row for $Component"; return '' }
    return $match.Value
}
function Get-LicenseVersion([string]$Artifact) {
    $entry = @($licenses | Where-Object { $_.artifact -eq $Artifact })
    if ($entry.Count -ne 1) { Fail "assets/licenses/licenses.json has $($entry.Count) entries for $Artifact, not one"; return '' }
    return [string]$entry[0].version
}
function Test-LicenseVersion([string]$Artifact, [string]$Expected) {
    $actual = Get-LicenseVersion $Artifact
    if ($actual -and $actual -ne $Expected) { Fail "assets/licenses/licenses.json lists $Artifact $actual; the source says $Expected" }
}
function Test-SummaryRow([string]$Component, [string]$Expected) {
    $row = Get-SummaryRow $Component
    if ($row -and -not $row.Contains($Expected)) { Fail "THIRD_PARTY.md's $Component row does not name $Expected" }
}

# --- The app version and its mirrors -----------------------------------------
$cmake = Read-Text 'CMakeLists.txt'
if ($cmake -notmatch 'project\(\s*Dish\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') { throw 'no project(Dish VERSION X.Y.Z) in CMakeLists.txt' }
$major, $minor, $patch = $Matches[1], $Matches[2], $Matches[3]
$version = "$major.$minor.$patch"
Write-Output "CMakeLists.txt declares $version"

$rc = Read-Text 'packaging/dish.rc'
foreach ($tuple in 'FILEVERSION', 'PRODUCTVERSION') {
    if ($rc -notmatch "(?m)^\s*$tuple\s+$major,$minor,$patch,0\s*$") { Fail "packaging/dish.rc $tuple is not $major,$minor,$patch,0" }
}
foreach ($field in 'FileVersion', 'ProductVersion') {
    if ($rc -notmatch ('"' + $field + '",\s*"' + [regex]::Escape($version) + '"')) { Fail "packaging/dish.rc StringFileInfo $field is not `"$version`"" }
}

$manifest = Read-Text 'vcpkg.json' | ConvertFrom-Json
if ($manifest.'version-string' -ne $version) { Fail "vcpkg.json version-string is $($manifest.'version-string'), not $version" }

$changelog = Read-Text 'CHANGELOG.md'
if ($changelog -notmatch ('(?m)^## \[' + [regex]::Escape($version) + '\] - \d{4}-\d{2}-\d{2}\s*$')) {
    Fail "CHANGELOG.md has no '## [$version] - YYYY-MM-DD' heading"
}

# --- The vcpkg baseline and what it resolves ---------------------------------
$baseline = Get-VcpkgBaseline -RepoRoot $repoRoot
$short = $baseline.Substring(0, 8)
Write-Output "vcpkg.json builtin-baseline is $baseline"
if (-not $thirdParty.Contains($baseline)) { Fail "THIRD_PARTY.md does not name the vcpkg baseline $baseline" }

foreach ($workflow in Get-ChildItem .github/workflows -Filter *.yml) {
    $text = [IO.File]::ReadAllText($workflow.FullName)
    if ($text -match '(?m)^\s*VCPKG_COMMITID:') { Fail "$($workflow.Name) pins VCPKG_COMMITID itself; scripts/ci-env.ps1 reads it from vcpkg.json" }
    if ($text -match '(?m)^\s*(LINGUIST_)?QT_VERSION:') { Fail "$($workflow.Name) pins a Qt version itself; scripts/tools.ps1 holds it" }
}

# Port name in vcpkg.json -> (THIRD_PARTY.md summary row, licenses.json artifact).
$ports = [ordered]@{
    'libsodium'     = @('libsodium', 'libsodium')
    'openssl'       = @('OpenSSL', 'openssl')
    'opus'          = @('Opus', 'opus')
    'sdl2'          = @('SDL2', 'SDL2')
    'sentry-native' = @('sentry-native', 'sentry-native')
}
foreach ($dependency in $manifest.dependencies) {
    $name = if ($dependency -is [string]) { $dependency } else { $dependency.name }
    if (-not $ports.Contains($name)) { Fail "vcpkg.json declares $name, which this gate does not map to THIRD_PARTY.md and licenses.json" }
}
foreach ($port in $ports.Keys) {
    $url = "https://raw.githubusercontent.com/microsoft/vcpkg/$baseline/ports/$port/vcpkg.json"
    $recipe = Invoke-RestMethod -Uri $url -UseBasicParsing
    $upstream = @('version', 'version-semver', 'version-date', 'version-string' |
        Where-Object { $recipe.PSObject.Properties.Name -contains $_ } |
        ForEach-Object { [string]$recipe.$_ })[0]
    $portVersion = if ($recipe.PSObject.Properties.Name -contains 'port-version') { [int]$recipe.'port-version' } else { 0 }
    $resolved = if ($portVersion -gt 0) { "$upstream#$portVersion" } else { $upstream }
    Write-Output "baseline resolves $port $resolved"
    $row, $artifact = $ports[$port]
    Test-SummaryRow $row "$resolved (vcpkg baseline ``$short``)"
    Test-LicenseVersion $artifact $upstream
    if ($port -eq 'sentry-native') {
        Test-SummaryRow 'Crashpad' "as vendored by sentry-native $upstream"
        Test-LicenseVersion 'crashpad' "as vendored by sentry-native $upstream"
    }
}

# --- FetchContent pins --------------------------------------------------------
if ($cmake -notmatch 'GIT_REPOSITORY\s+https://github.com/cgutman/enet\.git\s+GIT_TAG\s+([0-9a-f]{40})') { Fail 'no ENet GIT_TAG in CMakeLists.txt' }
else {
    $enet = $Matches[1]
    if (-not $thirdParty.Contains($enet)) { Fail "THIRD_PARTY.md does not name ENet commit $enet" }
    Test-SummaryRow 'ENet (cgutman fork)' "``$($enet.Substring(0, 7))``"
    Test-LicenseVersion 'enet' $enet.Substring(0, 7)
}
if ((Read-Text 'tests/CMakeLists.txt') -notmatch 'GIT_TAG\s+v(\d+\.\d+\.\d+)') { Fail 'no Catch2 GIT_TAG in tests/CMakeLists.txt' }
else {
    $catch2 = $Matches[1]
    Test-SummaryRow 'Catch2' $catch2
    Test-LicenseVersion 'Catch2' $catch2
}

# --- Tool pins (scripts/tools.ps1) ---------------------------------------------
Test-SummaryRow 'Qt 6' "$DishQtVersion (CI and release)"
Test-LicenseVersion 'qtbase' $DishQtVersion
$qtMinor = ($DishQtVersion -split '\.')[0..1] -join '.'
if ($cmake -notmatch ('find_package\(Qt6 ' + [regex]::Escape($qtMinor) + ' REQUIRED')) { Fail "CMakeLists.txt's find_package(Qt6 ...) floor is not $qtMinor" }
Test-SummaryRow 'Qt 6' "CMake requires >= $qtMinor"
Test-DocumentText 'CONTRIBUTING.md' "Qt $DishQtVersion" 'the pinned Qt'
Test-DocumentText 'CONTRIBUTING.md' "C:\Qt\$DishQtVersion\$DishQtArchDir" 'the Qt prefix install-deps.ps1 creates'
Test-DocumentText 'CONTRIBUTING.md' "clang-format $DishClangFormatVersion" 'the pinned clang-format'
Test-DocumentText 'CONTRIBUTING.md' "Inno Setup $DishInnoSetupVersion" 'the pinned Inno Setup'
Test-DocumentText '.github/pull_request_template.md' "clang-format==$DishClangFormatVersion" 'the pinned clang-format'

# --- The wire protocol ----------------------------------------------------------
if ((Read-Text 'src/core/model/Protocol.h') -notmatch 'kProtocolVersion\s*=\s*(\d+);') { throw 'no kProtocolVersion in src/core/model/Protocol.h' }
$protocol = $Matches[1]
Write-Output "Protocol.h speaks protocol $protocol"
Test-DocumentText 'CONTRIBUTING.md' "speak protocol $protocol" 'the protocol version'
Test-DocumentText 'docs/contract.md' "protocol $protocol" 'the protocol version'

if ($script:failures.Count -gt 0) {
    Write-Output ''
    Write-Output "$($script:failures.Count) version fact(s) drifted from their source. The header of scripts/check-versions.ps1 lists each source and its copies."
    exit 1
}
Write-Output 'All version facts agree with their sources.'
