# SPDX-License-Identifier: MIT OR Apache-2.0
param([Parameter(Mandatory=$true)][string]$Report)
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$reportRoot = [IO.Path]::GetFullPath($Report)
[void][IO.Directory]::CreateDirectory($reportRoot)
$commit = git -C $sourceRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source' }
$status = git -C $sourceRoot status --porcelain --untracked-files=all
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source status' }
$listed = git -C $sourceRoot ls-files -z --cached --others --exclude-standard
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate source' }
$paths = ($listed -join "`n").Split([char]0, [StringSplitOptions]::RemoveEmptyEntries) | Sort-Object -Unique
$manifest = @()
foreach ($relative in $paths) {
    if ($relative -match '^(out|\.vs|\.git)(/|\\)') { continue }
    $path = Join-Path $sourceRoot $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $manifest += @{ path=$relative; deleted=$true }
        continue
    }
    $manifest += @{ path=$relative; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    if ($status) {
        $destination = Join-Path (Join-Path $reportRoot 'source') $relative
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        [IO.File]::Copy($path, $destination, $true)
    }
}
ConvertTo-Json -InputObject @($manifest) -Depth 4 |
    Out-File -LiteralPath (Join-Path $reportRoot 'source-manifest.json') -Encoding utf8
@{ commit=$commit; dirty=[bool]$status; manifest='source-manifest.json'; recorded_utc=[DateTime]::UtcNow.ToString('o') } |
    ConvertTo-Json | Out-File -LiteralPath (Join-Path $reportRoot 'source-identity.json') -Encoding utf8
