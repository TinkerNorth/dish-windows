<#
.SYNOPSIS
    Turn a staged portable bundle into the release's dish-windows.zip.

.DESCRIPTION
    Writes the bundle's own SHA256SUMS (every file, paths relative to the
    bundle root, LF with no BOM so GNU `sha256sum -c` reads it off Windows
    too), then zips the bundle. release.yml packages the zip it publishes with
    this, and windows-ci.yml packages the zip its release-hardening dry run
    scans and signs, so the two are made the same way.

    Paths are relative rather than basenames: windeployqt lays plugins out in
    subdirectories (platforms/, imageformats/, styles/), and basenames alone
    both lose that and can collide. `Out-File -Encoding ascii` would emit CRLF,
    which `sha256sum -c` rejects as "no properly formatted checksum lines".

.PARAMETER Bundle
    The staged bundle directory (scripts/stage-bundle.ps1's -OutDir).

.PARAMETER Zip
    The zip to write.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Bundle,
    [Parameter(Mandatory)][string]$Zip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = (Resolve-Path $Bundle).Path
$sums = Join-Path $root 'SHA256SUMS'
Remove-Item $sums -ErrorAction SilentlyContinue
$lines = Get-ChildItem $root -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
    "$((Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower())  $relative"
}
[IO.File]::WriteAllText($sums, ($lines -join "`n") + "`n", (New-Object Text.UTF8Encoding $false))

Compress-Archive -Path (Join-Path $root '*') -DestinationPath $Zip -Force
$out = Get-Item $Zip
Write-Output "Packaged $($out.FullName): $(@($lines).Count) files, $([math]::Round($out.Length / 1MB, 1)) MB"
