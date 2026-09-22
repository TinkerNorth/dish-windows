<#
.SYNOPSIS
    clang-tidy gate: the exact file set and flags windows-ci.yml runs.

.DESCRIPTION
    Must run after a debug build: it lints against that tree's
    compile_commands.json and needs the generated mocs to exist.

    .cpp only: headers are covered transitively via HeaderFilterRegex, and
    passing a .h directly trips clang-diagnostic-pragma-once-outside-header.

    The sweep reads a copy of compile_commands.json with /Zc:preprocessor
    removed. clang-cl's preprocessor conforms already, so the flag means
    nothing to it and its driver reports the argument as unused, which the
    gate below would count as a finding. Every other MSVC flag in the
    database (/permissive-, /utf-8, /external:I, /Zc:__cplusplus) clang-cl
    understands, so nothing is silenced: clang sees exactly the flags it can
    act on.

    Findings fail the gate: --warnings-as-errors='*' on the command line, the
    same way linux-ci.yml gates its sweep, so the fleet-canonical .clang-tidy
    (WarningsAsErrors: '') stays shared and unforked.

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

.PARAMETER Files
    Sources to sweep instead of CI's whole set, repo-relative. The pre-commit
    hook passes the staged ones; the flags and the verdict stay CI's.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [int]$Throttle = 4,
    [string[]]$Files = @()
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$database = Join-Path $BuildDir 'compile_commands.json'
if (-not (Test-Path $database)) {
    throw "$BuildDir/compile_commands.json not found; configure and build the debug preset first (scripts/build.ps1 debug)."
}

# The clang view of the database (see the description): the same entries,
# directories and files, minus the one flag clang-cl has no use for. Written
# without a BOM, which the database reader does not expect. -InputObject @(),
# not the pipeline: Windows PowerShell serialises a piped collection of one
# as a bare object and a bound collection as {value, Count}, and clang-tidy
# would silently fall back to the unfiltered database in the parent directory.
$tidyDir = Join-Path $BuildDir 'clang-tidy'
New-Item -ItemType Directory -Force $tidyDir | Out-Null
$entries = Get-Content $database -Raw | ConvertFrom-Json
foreach ($entry in $entries) {
    $entry.command = ($entry.command -split ' ' | Where-Object { $_ -ne '/Zc:preprocessor' }) -join ' '
}
$json = ConvertTo-Json -InputObject @($entries) -Depth 4
if (-not $json.TrimStart().StartsWith('[')) { throw 'the filtered compile database is not a JSON array' }
[System.IO.File]::WriteAllText(
    (Join-Path (Resolve-Path $tidyDir) 'compile_commands.json'),
    $json,
    [System.Text.UTF8Encoding]::new($false))

if ($Files.Count) {
    # Split as well as bound: under `powershell -File` an argument is a plain
    # string, so a comma-joined list (the hook's form) arrives as one element.
    $files = @($Files | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
} else {
    $files = @(git ls-files 'src/*.cpp' | Where-Object { $_ -notlike 'src/UI/*' })
    if (-not $files) { throw 'git ls-files found no sources for clang-tidy' }
}

$failed = [System.Collections.Generic.List[string]]::new()

if ($PSVersionTable.PSVersion.Major -ge 7) {
    $bd = $tidyDir
    $files | ForEach-Object -Parallel {
        $ErrorActionPreference = 'Continue'
        try {
            $out = clang-tidy -p $using:bd --quiet --warnings-as-errors='*' $_ 2>&1
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
                    $out = clang-tidy -p $using:tidyDir --quiet --warnings-as-errors='*' $f 2>&1
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
