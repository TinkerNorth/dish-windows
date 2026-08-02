<#
.SYNOPSIS
    Fails a build that hard-codes a design value in a QML page.

.DESCRIPTION
    The design system has exactly one job: one name per colour, one name per
    metric. A page that writes `#4FE3FF`, `radius: 8` or `font.pixelSize: 11`
    has forked the system silently — it still renders, it just stops following
    the palette and the scale. Nothing caught that before this script.

    Scope:
      * `src/qml/kit/**` is SKIPPED. The kit is where literals are legal: it is
        the layer that TURNS tokens into pixels, and a token defined in terms of
        itself is not a token.
      * `src/qml/wizard/**` and `src/qml/shared/**` are ERRORS in -Mode error.
        They are greenfield, written against the finished token surface, and
        must be clean on day one.
      * Everything else outside the kit WARNS. Those files predate the token
        surface; flipping them to error is the recorded follow-up.

    Only `-Mode error` violations set the exit code, so the CI step can run in
    error mode from day one and the warnings stay informational.

.PARAMETER Mode
    `error` (default) or `warn`. In `warn` mode nothing fails the build; every
    finding is reported as a warning, including the greenfield directories.

.EXAMPLE
    ./scripts/qml-lint-literals.ps1 -Mode error
#>
[CmdletBinding()]
param(
    [ValidateSet('error', 'warn')]
    [string]$Mode = 'error'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    # git ls-files is the authority on which paths are tracked AND on their
    # casing — a generated or build-tree copy of a page must not be scanned.
    $files = @(git ls-files 'src/qml/*.qml')
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed' }

    # Each rule is (name, regex). The regexes are deliberately narrow: they match
    # a literal being ASSIGNED to a design property, not any appearance of a
    # number, so `width: parent.width - 8` and `Qt.point(0, 0)` stay quiet.
    $rules = @(
        @{ Name = 'raw colour literal';        Pattern = '#[0-9A-Fa-f]{3,8}\b' },
        @{ Name = 'Qt.rgba() colour';          Pattern = 'Qt\.rgba\(' },
        @{ Name = 'hard-coded font.pixelSize'; Pattern = 'font\.pixelSize\s*:\s*\d' },
        @{ Name = 'hard-coded radius';         Pattern = 'radius\s*:\s*\d' },
        @{ Name = 'hard-coded spacing metric'; Pattern = '(?<![A-Za-z0-9_.])(spacing|padding|leftPadding|rightPadding|topPadding|bottomPadding|margins|leftMargin|rightMargin|topMargin|bottomMargin)\s*:\s*\d' },
        @{ Name = 'hard-coded font.family';    Pattern = 'font\.family\s*:\s*"' },
        @{ Name = 'hand-rolled disabled opacity'; Pattern = 'opacity\s*:\s*0\.4' }
    )

    # The greenfield directories, which must be clean on day one.
    $strictPrefixes = @('src/qml/wizard/', 'src/qml/shared/')

    $errorCount = 0
    $warnCount = 0

    foreach ($file in $files) {
        $normalized = $file -replace '\\', '/'
        # The kit is the allow-listed layer: it defines the pixels the tokens
        # name, so it is exempt by design, not by omission.
        if ($normalized -like 'src/qml/kit/*') { continue }
        # A tracked path can be absent from the worktree (a deletion that is not
        # staged yet, a half-applied rebase). Scanning is not the place to fail.
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }

        $strict = $false
        foreach ($prefix in $strictPrefixes) {
            if ($normalized.StartsWith($prefix)) { $strict = $true; break }
        }
        # In warn mode nothing is fatal, including the greenfield dirs.
        if ($Mode -eq 'warn') { $strict = $false }

        $lineNumber = 0
        foreach ($line in (Get-Content -LiteralPath $file -Encoding UTF8)) {
            $lineNumber++
            # A whole-line comment is prose, not code — a token table written in
            # a header comment must not fail the build.
            if ($line -match '^\s*(//|\*|/\*)') { continue }

            foreach ($rule in $rules) {
                if ($line -notmatch $rule.Pattern) { continue }
                $message = "{0}:{1}: {2} -- use a Theme/Tokens name" -f $normalized, $lineNumber, $rule.Name
                if ($strict) {
                    Write-Host "ERROR $message"
                    $errorCount++
                }
                else {
                    Write-Host "warn  $message"
                    $warnCount++
                }
            }
        }
    }

    Write-Host ""
    Write-Host ("qml-lint-literals: {0} error(s), {1} warning(s) over {2} tracked QML file(s)." -f $errorCount, $warnCount, $files.Count)
    if ($errorCount -gt 0) {
        Write-Host "The wizard and shared page-model directories may not hard-code a design value."
        exit 1
    }
    exit 0
}
finally {
    Pop-Location
}
