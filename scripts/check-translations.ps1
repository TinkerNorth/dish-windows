<#
.SYNOPSIS
    Fails a build whose translation catalogues no longer match the source.

.DESCRIPTION
    A stale .ts is not a compile error, so without this check a build stays
    green while every new string ships in English to non-English users.

    Re-runs lupdate with the same flags CMake's `update_translations` target
    uses, then asks git whether anything changed. A dirty tree means a string
    was added, edited or deleted without refreshing the catalogues, and the
    fix is the one line the failure message prints.

    NOTE: this REWRITES translations/*.ts in your working tree. It refuses to
    run if they are already dirty, so nothing of yours is lost, but expect
    modified files afterwards.

    Coverage is ENFORCED, not reported. An untranslated string does not fail
    a build, it just silently ships in English to someone who does not read
    it, and nothing else in the suite can see that. Every catalogue must be
    complete.

    The source catalogue (dish_en.ts) is seeded from its own source strings
    first - see scripts/seed-source-language.py for why that is not busywork -
    so the count means the same thing in all six and the gate is one rule, not
    five languages plus an exception.

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

# Windows PowerShell turns a native command's stderr into ErrorRecords, which
# under 'Stop' kills the script over routine git advisories ("LF will be
# replaced by CRLF"). core.safecrlf=false silences that one at the source;
# relaxing the preference covers whatever else git decides to say.
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
    # Keep identical to LUPDATE_OPTIONS in CMakeLists.txt. On drift the gate
    # fails on formatting rather than content, and people learn to ignore it.
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
    # Same last-resort scan build.ps1 does, so running this by hand needs no
    # setup beyond what building already needs.
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

    # With a dirty tree the diff below can't distinguish the author's edits
    # from lupdate's, and this script would overwrite unsaved work.
    $preexisting = @(Invoke-Git diff --name-only -- 'translations')
    if ($preexisting.Count -gt 0) {
        Write-Error ("translations/ has uncommitted changes before the check ran: " +
                     ($preexisting -join ', '))
    }

    foreach ($ts in $catalogues) {
        & $lupdate @lupdateArgs 'src' '-ts' $ts | Out-Null
        if ($LASTEXITCODE -ne 0) { Write-Error "lupdate failed on $ts" }
    }

    # The source catalogue answers for itself; every other language needs words.
    # Same discovery shape as lupdate above: CI's setup-python puts `python` on
    # PATH, a developer box more often has `py`.
    $python = $null
    foreach ($candidate in @('python', 'python3', 'py')) {
        $found = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($found) { $python = $found.Source; break }
    }
    if (-not $python) {
        Write-Error 'python not found; scripts/seed-source-language.py needs it.'
    }
    & $python scripts/seed-source-language.py translations/dish_en.ts
    if ($LASTEXITCODE -ne 0) { Write-Error 'seed-source-language.py failed' }

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
        # ASCII only below: Windows PowerShell reads a BOM-less UTF-8 script as
        # the system codepage, and this is the message that has to be legible.
        Write-Host '  Then translate the new entries, or leave them for a translator.'
        Write-Host '  An untranslated entry falls back to English, which is what'
        Write-Host '  happens today anyway - only now it is visible.'
        exit 1
    }

    Write-Host ''
    Write-Host 'Catalogues are in sync. Coverage:'
    $incomplete = @()
    # XPath rather than dotted property access: under Set-StrictMode the dotted
    # form throws on any slightly differently shaped catalogue, and a coverage
    # readout must never be the thing that fails the gate.
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
                # Any one form counts; enforcing all-or-nothing is a unit
                # test's job, not this readout's.
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
        if ($translated -lt $total) {
            $incomplete += ("{0} ({1} missing)" -f $name, ($total - $translated))
        }
    }

    if ($incomplete.Count -gt 0) {
        Write-Host ''
        Write-Host 'Untranslated strings remain:' -ForegroundColor Red
        foreach ($entry in $incomplete) { Write-Host ("  " + $entry) }
        Write-Host ''
        # ASCII only below, same reason as the message above.
        Write-Host '  Every catalogue must be complete before merge. A missing'
        Write-Host '  translation is not a blank - it is English shown to someone'
        Write-Host '  who does not read English.'
        exit 1
    }
}
finally {
    Pop-Location
}
