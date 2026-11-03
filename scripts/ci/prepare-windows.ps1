# SPDX-License-Identifier: MIT OR Apache-2.0
[CmdletBinding()]
param([switch]$ProductOnly)
$ErrorActionPreference = 'Stop'
$lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../../cmake/dependencies.json') -Raw | ConvertFrom-Json
$dependencyRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../out/dependencies'))
[void][IO.Directory]::CreateDirectory($dependencyRoot)

function Get-Archive([string]$Name, [string]$Url, [string]$Hash) {
    $archive = Join-Path $dependencyRoot $Name
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $Url -OutFile $archive }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hash) {
        throw "Dependency hash mismatch: $archive"
    }
    return $archive
}

$cmakeName = "cmake-$($lock.cmake.version)-windows-x86_64"
$cmakeZip = Get-Archive "$cmakeName.zip" "https://github.com/Kitware/CMake/releases/download/v$($lock.cmake.version)/$cmakeName.zip" $lock.cmake.windows_sha256
# Re-extract checked archives, so a cache is never trusted as extracted code.
$extraction = Join-Path $dependencyRoot ('extract-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($extraction)
Expand-Archive -LiteralPath $cmakeZip -DestinationPath $extraction
$cmakeBin = Join-Path $extraction "$cmakeName/bin"
$ninjaZip = Get-Archive 'ninja-win.zip' "https://github.com/ninja-build/ninja/releases/download/v$($lock.ninja.version)/ninja-win.zip" $lock.ninja.windows_sha256
$ninjaBin = Join-Path $extraction 'ninja'
Expand-Archive -LiteralPath $ninjaZip -DestinationPath $ninjaBin
$llvmArchive = Get-Archive 'llvm-windows.tar.xz' $lock.llvm_windows.url $lock.llvm_windows.sha256
$llvmExtract = Join-Path $extraction 'llvm'
[void][IO.Directory]::CreateDirectory($llvmExtract)
tar -xf $llvmArchive -C $llvmExtract
if ($LASTEXITCODE -ne 0) { throw 'LLVM extraction failed' }
$packages = @(Get-ChildItem -LiteralPath $llvmExtract -Recurse -Filter LLVMConfig.cmake)
if ($packages.Count -ne 1) { throw 'LLVM archive must contain one development package' }
$llvmPackage = $packages[0].Directory.FullName
$llvmBin = [IO.Path]::GetFullPath((Join-Path $llvmPackage '../../../bin'))
foreach ($tool in @('clang-cl.exe','lld-link.exe','clang-format.exe','clang-tidy.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $llvmBin $tool) -PathType Leaf)) { throw "Missing SDK tool: $tool" }
}
foreach ($path in @($llvmBin, $ninjaBin, $cmakeBin)) { $path | Out-File -LiteralPath $env:GITHUB_PATH -Append -Encoding utf8 }
"CHTHOLLY_LLVM_DIR=$llvmPackage" | Out-File -LiteralPath $env:GITHUB_ENV -Append -Encoding utf8
"CHTHOLLY_LLVM_BIN=$llvmBin" | Out-File -LiteralPath $env:GITHUB_ENV -Append -Encoding utf8
if (-not $ProductOnly) {
    $gtest = Join-Path $extraction 'googletest'
    git clone --no-checkout $lock.googletest.repository $gtest
    if ($LASTEXITCODE -ne 0) { throw 'Google Test clone failed' }
    git -C $gtest checkout --detach $lock.googletest.commit
    if ($LASTEXITCODE -ne 0) { throw 'Google Test checkout failed' }
    $actual = git -C $gtest rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $actual -ne $lock.googletest.commit) { throw 'Google Test identity mismatch' }
    "CHTHOLLY_GTEST_SOURCE_DIR=$gtest" | Out-File -LiteralPath $env:GITHUB_ENV -Append -Encoding utf8
}
