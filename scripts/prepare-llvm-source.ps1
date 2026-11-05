# SPDX-License-Identifier: MIT OR Apache-2.0
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$lock = (Get-Content -LiteralPath (Join-Path $PSScriptRoot '../cmake/dependencies.json') -Raw | ConvertFrom-Json).llvm_source
if (-not (Test-Path -LiteralPath $Destination)) {
    git clone --filter=blob:none --sparse --depth 1 --branch $lock.tag $lock.repository $Destination
    if ($LASTEXITCODE -ne 0) { throw 'LLVM source clone failed' }
    git -C $Destination sparse-checkout set compiler-rt llvm cmake third-party
    if ($LASTEXITCODE -ne 0) { throw 'LLVM sparse checkout failed' }
}
$identity = git -C $Destination rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $identity -ne $lock.commit) { throw 'LLVM source identity mismatch' }
$dirty = git -C $Destination status --porcelain --untracked-files=all
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'LLVM source checkout must be clean' }
Write-Output ([IO.Path]::GetFullPath($Destination))
