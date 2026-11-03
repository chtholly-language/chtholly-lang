# SPDX-License-Identifier: MIT OR Apache-2.0
param([Parameter(Mandatory=$true)][string]$Preset,
      [ValidateSet('tests','style','analysis','product')][string]$Mode = 'tests')
$ErrorActionPreference = 'Stop'
$report = Join-Path 'out/reports' $Preset
[void][IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($report))
$sourceCommit = git rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source' }
$sourceStatus = git status --porcelain --untracked-files=all
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source status' }
$context = @{ commit=$sourceCommit; dirty=[bool]$sourceStatus; configuration=$Preset; started_utc=[DateTime]::UtcNow.ToString('o'); record_kind='pre-build source identity' }
$context | ConvertTo-Json | Out-File -LiteralPath (Join-Path $report 'run-context.json') -Encoding utf8
"Image=$env:ImageOS $env:ImageVersion`nMSVC=$env:VCToolsVersion`nSDK=$env:WindowsSDKVersion" |
    Out-File -LiteralPath (Join-Path $report 'environment.txt') -Encoding utf8
cmake --version | Out-File -LiteralPath (Join-Path $report 'tools.txt') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'Cannot query CMake' }
ninja --version | Out-File -LiteralPath (Join-Path $report 'tools.txt') -Append -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'Cannot query Ninja' }
$build = "out/build/$Preset"
if (Test-Path -LiteralPath $build) { throw 'CI requires a new project build directory' }
$query = Join-Path $build '.cmake/api/v1/query'
[void][IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($query))
foreach ($name in @('codemodel-v2','cache-v2')) {
    [IO.File]::WriteAllText([IO.Path]::GetFullPath((Join-Path $query $name)), '')
}
cmake --preset $Preset 2>&1 | Tee-Object -FilePath (Join-Path $report 'configure.log')
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build --preset $Preset --parallel 3 2>&1 | Tee-Object -FilePath (Join-Path $report 'build.log')
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
if ($Mode -eq 'product') {
    $names = @(Get-ChildItem -LiteralPath "$build/architecture" -Filter '*.txt' | Select-Object -ExpandProperty BaseName)
    if ($names.Count -ne 9 -or @($names | Where-Object { $_ -match 'test|fixture|gtest' }).Count -ne 0) {
        throw 'Product-only dependency isolation failed'
    }
    if (Select-String -LiteralPath "$build/CMakeCache.txt" -Pattern '^Python3_.*EXECUTABLE.*:' -Quiet) {
        throw 'Product configuration discovered Python'
    }
    if (-not (Select-String -LiteralPath "$build/CMakeCache.txt" -Pattern '^BUILD_TESTING:BOOL=OFF$' -Quiet)) {
        throw 'Product configuration enabled testing'
    }
    $reply = Join-Path $build '.cmake/api/v1/reply'
    $indexFile = Get-ChildItem -LiteralPath $reply -Filter 'index-*.json' | Sort-Object Name | Select-Object -Last 1
    if ($null -eq $indexFile) { throw 'Product File API evidence missing' }
    $index = Get-Content -LiteralPath $indexFile.FullName -Raw | ConvertFrom-Json
    $modelFile = @($index.objects | Where-Object { $_.kind -eq 'codemodel' })
    if ($modelFile.Count -ne 1) { throw 'Product codemodel missing' }
    $model = Get-Content -LiteralPath (Join-Path $reply $modelFile[0].jsonFile) -Raw | ConvertFrom-Json
    $actual = @($model.configurations[0].targets.name)
    if (@($actual | Where-Object { $_ -match 'gtest|test|fixture|header_check' }).Count -ne 0) {
        throw 'Product File API contains testing dependencies'
    }
    foreach ($name in $names) {
        $properties = Join-Path "$build/architecture" "$name.txt"
        $interface = Select-String -LiteralPath $properties -Pattern '^type=INTERFACE_LIBRARY$' -Quiet
        if (-not $interface -and $name -notin $actual) { throw "Product File API target missing: $name" }
    }
    $actual | ConvertTo-Json | Out-File -LiteralPath (Join-Path $report 'product-targets.json') -Encoding utf8
    exit 0
}
if ($Mode -eq 'style') {
    python scripts/check-format.py --root . --tool "$env:CHTHOLLY_LLVM_BIN/clang-format.exe" 2>&1 |
        Tee-Object -FilePath (Join-Path $report 'format.log')
    if ($LASTEXITCODE -ne 0) { throw 'Format failed' }
}
python scripts/check-architecture.py --root . --build-dir $build 2>&1 |
    Tee-Object -FilePath (Join-Path $report 'architecture.log')
if ($LASTEXITCODE -ne 0) { throw 'Architecture failed' }
if ($Mode -eq 'style') { exit 0 }
if ($Mode -eq 'analysis') {
    python scripts/run-tidy.py --root . --build-dir $build --tool "$env:CHTHOLLY_LLVM_BIN/clang-tidy.exe" 2>&1 |
        Tee-Object -FilePath (Join-Path $report 'tidy.log')
    if ($LASTEXITCODE -ne 0) { throw 'Static analysis failed' }
} else {
    python scripts/run-ctest.py --build-dir $build --report-dir out/reports --configuration $Preset 2>&1 |
        Tee-Object -FilePath (Join-Path $report 'runner.log')
    if ($LASTEXITCODE -ne 0) { throw 'CTest acceptance failed' }
}
