<#
.SYNOPSIS
    clang-tidy gate: the exact file set and flags windows-ci.yml runs.

.DESCRIPTION
    Must run after a debug build: it lints against that tree's
    compile_commands.json and needs the generated mocs to exist.

    .cpp only: headers are covered transitively via HeaderFilterRegex, and
    passing a .h directly trips clang-diagnostic-pragma-once-outside-header.
    -Wno-unused-command-line-argument because compile_commands.json carries
    MSVC-style flags (/Zc:preprocessor, /utf-8) the GNU-style clang driver
    would otherwise reject.

    Findings are advisory (.clang-tidy sets WarningsAsErrors: ''); only a
    non-zero clang-tidy exit (hard error, unparseable file) fails the gate.

    Under PowerShell 7+ this is CI's recipe verbatim: four workers via
    ForEach-Object -Parallel (the runner's vCPU count; a serial sweep took 17
    minutes), each folding BOTH its streams into data (2>&1 under an explicit
    EAP Continue, try/catch for the rest) because native stderr crossing the
    runspace boundary lands on the step's error stream, where a strict
    $ErrorActionPreference turns the first routine "N warnings generated."
    summary into a kill. Windows PowerShell 5.1 has no -Parallel; there the
    same file set runs as four chunked background jobs, same verdict rules.

.PARAMETER BuildDir
    The configured Debug tree carrying compile_commands.json. Defaults to
    build (the debug preset's binaryDir, same as CI).

.PARAMETER Throttle
    Concurrent clang-tidy processes. Default 4, the CI runner's vCPU count.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [int]$Throttle = 4
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

if (-not (Test-Path (Join-Path $BuildDir 'compile_commands.json'))) {
    throw "$BuildDir/compile_commands.json not found; configure and build the debug preset first (scripts/build.ps1 debug)."
}

$files = @(git ls-files 'src/*.cpp' | Where-Object { $_ -notlike 'src/UI/*' })
if (-not $files) { throw 'git ls-files found no sources for clang-tidy' }

$failed = [System.Collections.Generic.List[string]]::new()

if ($PSVersionTable.PSVersion.Major -ge 7) {
    $bd = $BuildDir
    $files | ForEach-Object -Parallel {
        $ErrorActionPreference = 'Continue'
        try {
            $out = clang-tidy -p $using:bd --quiet `
                --extra-arg-before=-Wno-unused-command-line-argument `
                $_ 2>&1
            [pscustomobject]@{ File = $_; Code = $LASTEXITCODE; Out = $out }
        } catch {
            [pscustomobject]@{ File = $_; Code = 1; Out = @("$_") }
        }
    } -ThrottleLimit $Throttle | ForEach-Object {
        foreach ($line in $_.Out) { "$line" }
        if ($_.Code -ne 0) { $failed.Add($_.File) }
    }
} else {
    # 5.1 fallback: the same files split into $Throttle chunks, one background
    # job per chunk running its share serially. Same flags, same verdict.
    # $using:, not -ArgumentList: ArgumentList flattens an array argument into
    # separate parameters and the chunk would arrive as its first file only.
    $chunks = @{}
    for ($i = 0; $i -lt $files.Count; $i++) { $chunks[$i % $Throttle] += @($files[$i]) }
    $root = (Get-Location).Path
    $jobs = foreach ($key in @($chunks.Keys)) {
        $chunk = $chunks[$key]
        Start-Job -ScriptBlock {
            Set-Location $using:root
            $ErrorActionPreference = 'Continue'
            foreach ($f in $using:chunk) {
                try {
                    $out = clang-tidy -p $using:BuildDir --quiet `
                        --extra-arg-before=-Wno-unused-command-line-argument `
                        $f 2>&1
                    [pscustomobject]@{ File = $f; Code = $LASTEXITCODE; Out = ($out | ForEach-Object { "$_" }) }
                } catch {
                    [pscustomobject]@{ File = $f; Code = 1; Out = @("$_") }
                }
            }
        }
    }
    $jobs | Wait-Job | Receive-Job | ForEach-Object {
        foreach ($line in $_.Out) { "$line" }
        if ($_.Code -ne 0) { $failed.Add($_.File) }
    }
    $jobs | Remove-Job -Force
}

if ($failed.Count) { throw "clang-tidy failed on: $($failed -join ', ')" }
Write-Output "clang-tidy: OK ($(@($files).Count) files)"
