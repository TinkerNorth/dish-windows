<#
.SYNOPSIS
    Fails a build whose translation catalogues no longer match the source.

.DESCRIPTION
    Nothing was watching this before. Between the last catalogue refresh and the
    v3 redesign the app grew 618 user-facing strings that `translations/*.ts`
    had never heard of, while 208 dead widget-era entries sat in the files
    pretending to be work. Every one of those strings shipped in English to a
    German, Spanish, French, Bosnian or Brazilian user, and the build was green
    the whole time, because a stale .ts is not a compile error.

    This is the thing that was missing. It re-runs lupdate with the same flags
    CMake's `update_translations` target uses, then asks git whether anything
    changed. A clean tree means the catalogues describe the code. A dirty one
    means a string was added, edited or deleted without refreshing them, and the
    diff it prints is exactly the refresh the author owes.

    The fix is always the same one line, which the failure message states:

        cmake --build <build-dir> --target update_translations

    Coverage is reported but never enforced. Translating a new string is a
    separate act from extracting it, often by a different person on a different
    day, and a gate that blocks the code until the words arrive just teaches
    people to skip the gate.

.PARAMETER LinguistBin
    Directory holding lupdate. Defaults to QT_ROOT_DIR/bin (what
    jurplel/install-qt-action sets in CI), then to whatever is on PATH.

.EXAMPLE
    ./scripts/check-translations.ps1
#>
[CmdletBinding()]
param(
    [string]$LinguistBin = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot

# Windows PowerShell promotes a native command's stderr into ErrorRecords, and
# under $ErrorActionPreference = 'Stop' that terminates the script. git writes
# routine advisories there ("LF will be replaced by CRLF"), so every git call
# below goes through this. core.safecrlf=false silences that particular note at
# the source; relaxing the preference covers whatever else git decides to say.
function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & git -c core.safecrlf=false @Arguments 2>$null
    } finally {
        $ErrorActionPreference = $previous
    }
}

Push-Location $repoRoot
try {
    # Keep these identical to LUPDATE_OPTIONS in CMakeLists.txt. If they drift,
    # the gate fails on formatting rather than on content and everyone starts
    # ignoring it.
    #   -locations none : the catalogues are tracked, so they must change only
    #                     when a STRING changes, not when a line moves.
    #   -no-obsolete    : drop messages whose source is gone instead of hoarding.
    $lupdateArgs = @('-locations', 'none', '-no-obsolete')

    $lupdate = $null
    $candidates = @()
    if ($LinguistBin) { $candidates += (Join-Path $LinguistBin 'lupdate.exe') }
    if ($env:QT_ROOT_DIR) { $candidates += (Join-Path $env:QT_ROOT_DIR 'bin/lupdate.exe') }
    if ($env:CMAKE_PREFIX_PATH) {
        foreach ($prefix in ($env:CMAKE_PREFIX_PATH -split ';')) {
            if ($prefix) { $candidates += (Join-Path $prefix 'bin/lupdate.exe') }
        }
    }
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { $lupdate = $candidate; break }
    }
    if (-not $lupdate) {
        $onPath = Get-Command lupdate -ErrorAction SilentlyContinue
        if ($onPath) { $lupdate = $onPath.Source }
    }
    # Same last-resort scan scripts/build.ps1 uses, so running this by hand on a
    # dev box needs no environment setup that building does not already need.
    if (-not $lupdate -and (Test-Path 'C:\Qt')) {
        $lupdate = Get-ChildItem 'C:\Qt' -Filter 'lupdate.exe' -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $lupdate) {
        Write-Error 'lupdate not found. Pass -LinguistBin <qt>/bin, or set QT_ROOT_DIR.'
    }
    Write-Host "Using $lupdate"

    $catalogues = @(Invoke-Git ls-files 'translations/*.ts')
    if ($catalogues.Count -eq 0) {
        Write-Error 'No tracked catalogues under translations/.'
    }

    # A dirty tree BEFORE the refresh would make the diff below unreadable —
    # the author's own edits and lupdate's would be indistinguishable.
    $preexisting = @(Invoke-Git diff --name-only -- 'translations')
    if ($preexisting.Count -gt 0) {
        Write-Error ("translations/ has uncommitted changes before the check ran: " +
                     ($preexisting -join ', '))
    }

    foreach ($ts in $catalogues) {
        & $lupdate @lupdateArgs 'src' '-ts' $ts | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Error "lupdate failed on $ts" }
    }

    $stale = @(Invoke-Git diff --name-only -- 'translations')
    if ($stale.Count -gt 0) {
        Invoke-Git --no-pager diff --stat -- 'translations'
        Write-Host ''
        Write-Host 'Translation catalogues are stale.' -ForegroundColor Red
        Write-Host ''
        Write-Host '  A user-facing string was added, changed or removed without'
        Write-Host '  refreshing translations/. Until the catalogues are updated, the'
        Write-Host '  new wording ships in English to every non-English user.'
        Write-Host ''
        Write-Host '  Fix:  cmake --build <build-dir> --target update_translations'
        Write-Host '        git add translations/'
        Write-Host ''
        # ASCII only in this block: Windows PowerShell reads a BOM-less UTF-8
        # script as the system codepage, and this is the one message a person
        # reads at the moment the gate stops them.
        Write-Host '  Then translate the new entries, or leave them for a translator.'
        Write-Host '  An untranslated entry falls back to English, which is what'
        Write-Host '  happens today anyway - only now it is visible.'
        exit 1
    }

    # Informational only. Reported so a reviewer can see a language sliding
    # without the gate deciding when that becomes someone's problem.
    Write-Host ''
    Write-Host 'Catalogues are in sync. Coverage:'
    # XPath rather than dotted property access: under Set-StrictMode the dotted
    # form throws on any catalogue shaped even slightly differently, and a
    # coverage READOUT must never be the thing that fails the gate.
    foreach ($ts in ($catalogues | Sort-Object)) {
        $doc = New-Object System.Xml.XmlDocument
        $doc.PreserveWhitespace = $true
        $doc.Load((Resolve-Path -LiteralPath $ts).Path)

        $messages = $doc.SelectNodes('//message')
        $total = $messages.Count
        $translated = 0
        foreach ($message in $messages) {
            $node = $message.SelectSingleNode('translation')
            if ($null -eq $node) { continue }
            $forms = $node.SelectNodes('numerusform')
            if ($forms.Count -gt 0) {
                # A plural counts as translated once any form carries text; the
                # all-or-nothing rule is a unit test's job, not this readout's.
                foreach ($form in $forms) {
                    if (-not [string]::IsNullOrWhiteSpace($form.InnerText)) { $translated++; break }
                }
            } elseif (-not [string]::IsNullOrWhiteSpace($node.InnerText)) {
                $translated++
            }
        }
        $percent = if ($total -gt 0) { [math]::Floor(100 * $translated / $total) } else { 0 }
        $name = Split-Path -Leaf $ts
        Write-Host ("  {0,-18} {1,4}/{2,-4} {3,3}%" -f $name, $translated, $total, $percent)
    }
}
finally {
    Pop-Location
}
