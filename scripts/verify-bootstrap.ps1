# SPDX-License-Identifier: MIT OR Apache-2.0
# Human-run Bootstrap acceptance. Compatible with Windows PowerShell 5.1.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Compiler,
    [Parameter(Mandatory = $true)][string]$WorkDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$runRoot = $null
$utf8 = New-Object System.Text.UTF8Encoding($false)

# Windows CRT argument quoting, including quotes and trailing backslashes.
# ProcessStartInfo starts the executable directly; no shell is involved.
function Quote-Argument([string]$Value) {
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = New-Object System.Text.StringBuilder
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $slashes++
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * (2 * $slashes + 1)))
        } else {
            [void]$builder.Append(('\' * $slashes))
        }
        $slashes = 0
        [void]$builder.Append($character)
    }
    [void]$builder.Append(('\' * (2 * $slashes)))
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-CapturedProcess {
    param([string]$Executable, [string[]]$Arguments, [string]$LogName)
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = (($Arguments | ForEach-Object { Quote-Argument $_ }) -join ' ')
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = $utf8
    $info.StandardErrorEncoding = $utf8
    [System.IO.File]::WriteAllText((Join-Path $runRoot "$LogName.command.txt"),
        ((Quote-Argument $Executable) + ' ' + $info.Arguments), $utf8)
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw "Cannot start $Executable" }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit(180000)
        if ($timedOut) {
            $process.Kill()
            $process.WaitForExit()
        }
        $status = $process.ExitCode
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        [System.IO.File]::WriteAllText((Join-Path $runRoot "$LogName.stdout.txt"), $stdout, $utf8)
        [System.IO.File]::WriteAllText((Join-Path $runRoot "$LogName.stderr.txt"), $stderr, $utf8)
        [System.IO.File]::WriteAllText((Join-Path $runRoot "$LogName.status.txt"), [string]$status, $utf8)
        if ($timedOut) { throw "Process timed out: $Executable; logs: $LogName" }
        return [pscustomobject]@{ ExitCode = $status; Stdout = $stdout; Stderr = $stderr }
    } finally {
        $process.Dispose()
    }
}

function Assert-Exit($Result, [int]$Expected, [string]$Name) {
    if ($Result.ExitCode -ne $Expected) {
        throw "$Name expected exit $Expected, got $($Result.ExitCode).`n$($Result.Stderr)"
    }
}

function Write-Source([string]$Name, [string]$Text, [bool]$BOM = $false) {
    $path = Join-Path $runRoot "$Name.cns"
    $encoding = New-Object System.Text.UTF8Encoding($BOM)
    [System.IO.File]::WriteAllText($path, $Text, $encoding)
    return $path
}

function Invoke-Compiler([string[]]$Arguments, [string]$Name) {
    return Invoke-CapturedProcess -Executable $compilerPath -Arguments $Arguments -LogName "$Name.compile"
}

function Compile-Case {
    param([string]$Name, [string]$Text, [int]$Expected = 0,
          [Nullable[int]]$ProgramExit = $null, [bool]$BOM = $false)
    $sourcePath = Write-Source $Name $Text $BOM
    $outputPath = Join-Path $runRoot "$Name.exe"
    $result = Invoke-Compiler -Arguments @($sourcePath, '-o', $outputPath) -Name $Name
    Assert-Exit $result $Expected $Name
    if ($Expected -ne 0) {
        if (Test-Path -LiteralPath $outputPath) { throw "$Name published an output on failure" }
        if ([string]::IsNullOrWhiteSpace($result.Stderr)) { throw "$Name did not explain its failure" }
    } else {
        if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) { throw "$Name produced no executable" }
        if ((Get-Item -LiteralPath $outputPath).Length -eq 0) { throw "$Name produced an empty executable" }
        if ($null -ne $ProgramExit) {
            $execution = Invoke-CapturedProcess -Executable $outputPath -Arguments @() -LogName "$Name.run"
            Assert-Exit $execution $ProgramExit $Name
        }
    }
    Write-Host "PASS $Name"
    return [pscustomobject]@{ Source = $sourcePath; Output = $outputPath; Result = $result }
}

try {
    $compilerPath = (Resolve-Path -LiteralPath $Compiler).ProviderPath
    if (-not (Test-Path -LiteralPath $compilerPath -PathType Leaf)) { throw 'Compiler must be a file' }
    $parent = [System.IO.Path]::GetFullPath($WorkDir)
    [void][System.IO.Directory]::CreateDirectory($parent)
    $runRoot = Join-Path $parent ('run-' + [guid]::NewGuid().ToString('N'))
    [void][System.IO.Directory]::CreateDirectory($runRoot)
    Write-Host "Bootstrap acceptance artifacts: $runRoot"

    $help = Invoke-Compiler -Arguments @('--help') -Name 'help'
    Assert-Exit $help 0 'help'
    if ($help.Stdout -notmatch 'Usage:') { throw 'Help omitted usage' }
    $version = Invoke-Compiler -Arguments @('--version') -Name 'version'
    Assert-Exit $version 0 'version'
    if ($version.Stdout -notmatch 'bootstrap-b0' -or $version.Stdout -notmatch '18\.1\.8' -or
        $version.Stdout -notmatch 'x86_64-pc-windows-msvc') { throw 'Incomplete version information' }

    foreach ($code in @(0, 1, 42)) {
        $null = Compile-Case -Name "return-$code" -Text "fn main() -> i32 { return $code; }" -ProgramExit $code
    }
    $null = Compile-Case -Name 'leading-zero' -Text 'fn main() -> i32 { return 00042; }' -ProgramExit 42
    $spacedName = 'space path ' + [char]0x6D4B + [char]0x8BD5
    $null = Compile-Case -Name $spacedName -Text "// comment`r`n  fn main() -> i32 {`r`n`treturn 42; // end`r`n}`r`n" -ProgramExit 42
    $null = Compile-Case -Name 'bom' -Text 'fn main() -> i32 { return 1; }' -ProgramExit 1 -BOM $true
    # Compile the maximum without relying on the shell's large exit-code representation.
    $null = Compile-Case -Name 'i32-max' -Text 'fn main() -> i32 { return 2147483647; }'
    $overflow = Compile-Case -Name 'overflow' -Text 'fn main() -> i32 { return 2147483648; }' -Expected 1
    if ($overflow.Result.Stderr -notmatch 'outside the i32 range') { throw 'Missing integer range diagnostic' }
    $missing = Compile-Case -Name 'missing-semicolon' -Text "fn main() -> i32 {`n    return 0`n}" -Expected 1
    if ($missing.Result.Stderr -notmatch ':3:1: error:') { throw 'Incorrect diagnostic location' }
    $null = Compile-Case -Name 'trailing' -Text 'fn main() -> i32 { return 0; } extra' -Expected 1
    $null = Compile-Case -Name 'entry-name' -Text 'fn other() -> i32 { return 0; }' -Expected 1
    $null = Compile-Case -Name 'return-type' -Text 'fn main() -> u32 { return 0; }' -Expected 1
    $null = Compile-Case -Name 'expression' -Text 'fn main() -> i32 { return 1 + 1; }' -Expected 1
    $null = Compile-Case -Name 'negative' -Text 'fn main() -> i32 { return -1; }' -Expected 1
    $null = Compile-Case -Name 'suffix' -Text 'fn main() -> i32 { return 1u32; }' -Expected 1
    $null = Compile-Case -Name 'parameters' -Text 'fn main(x) -> i32 { return 0; }' -Expected 1
    $null = Compile-Case -Name 'block-comment' -Text '/* comment */ fn main() -> i32 { return 0; }' -Expected 1

    $invalidPath = Join-Path $runRoot 'invalid-utf8.cns'
    [byte[]]$invalidBytes = $utf8.GetBytes('fn main() -> i32 { return 0; } // ') + [byte[]]@(0xFF)
    [System.IO.File]::WriteAllBytes($invalidPath, $invalidBytes)
    $invalid = Invoke-Compiler -Arguments @($invalidPath, '-o', (Join-Path $runRoot 'invalid.exe')) -Name 'invalid-utf8'
    Assert-Exit $invalid 1 'invalid-utf8'
    if ($invalid.Stderr -notmatch 'invalid UTF-8') { throw 'Missing UTF-8 diagnostic' }

    $validSource = Write-Source 'tool-input' 'fn main() -> i32 { return 0; }'
    $missingInput = Invoke-Compiler -Arguments @((Join-Path $runRoot 'absent.cns'), '-o', (Join-Path $runRoot 'absent.exe')) -Name 'missing-input'
    Assert-Exit $missingInput 1 'missing-input'

    $sameBefore = (Get-FileHash -LiteralPath $validSource -Algorithm SHA256).Hash
    $same = Invoke-Compiler -Arguments @($validSource, '-o', $validSource) -Name 'same-file'
    Assert-Exit $same 1 'same-file'
    if ((Get-FileHash -LiteralPath $validSource -Algorithm SHA256).Hash -ne $sameBefore) { throw 'Input was overwritten' }

    $originalTools = [Environment]::GetEnvironmentVariable('CHTHOLLY_LLVM_BIN', 'Process')
    try {
        $emptyTools = Join-Path $runRoot 'empty tools'
        [void][System.IO.Directory]::CreateDirectory($emptyTools)
        [Environment]::SetEnvironmentVariable('CHTHOLLY_LLVM_BIN', $emptyTools, 'Process')
        $missingTool = Invoke-Compiler -Arguments @($validSource, '-o', (Join-Path $runRoot 'no-clang.exe')) -Name 'missing-clang'
        Assert-Exit $missingTool 1 'missing-clang'
        if ($missingTool.Stderr -notmatch 'missing tool.*clang-cl') { throw 'Missing clang-cl diagnostic' }
        # Both paths must be checked before executing either tool. This marker
        # is deliberately not executable; the missing lld-link must be reported.
        [System.IO.File]::WriteAllText((Join-Path $emptyTools 'clang-cl.exe'), 'not an executable', $utf8)
        $missingLinker = Invoke-Compiler -Arguments @($validSource, '-o', (Join-Path $runRoot 'no-lld.exe')) -Name 'missing-lld'
        Assert-Exit $missingLinker 1 'missing-lld'
        if ($missingLinker.Stderr -notmatch 'missing tool.*lld-link') { throw 'Missing lld-link diagnostic' }
    } finally {
        [Environment]::SetEnvironmentVariable('CHTHOLLY_LLVM_BIN', $originalTools, 'Process')
    }

    $preserved = Compile-Case -Name 'preserved' -Text 'fn main() -> i32 { return 42; }' -ProgramExit 42
    $before = (Get-FileHash -LiteralPath $preserved.Output -Algorithm SHA256).Hash
    [System.IO.File]::WriteAllText($preserved.Source, 'fn main() -> i32 { return 0 }', $utf8)
    $rejected = Invoke-Compiler -Arguments @($preserved.Source, '-o', $preserved.Output) -Name 'preserve-invalid'
    Assert-Exit $rejected 1 'preserve-invalid'
    if ((Get-FileHash -LiteralPath $preserved.Output -Algorithm SHA256).Hash -ne $before) { throw 'Failed compilation changed old output' }
    # Do not execute the previous file as if it were this rejected invocation's output.

    $lockedPath = Join-Path $runRoot 'locked.exe'
    [System.IO.File]::Copy($preserved.Output, $lockedPath)
    $lockedBefore = (Get-FileHash -LiteralPath $lockedPath -Algorithm SHA256).Hash
    # Metadata can be inspected, but the existing file cannot be replaced
    # while this handle remains open without delete sharing.
    $lock = [System.IO.File]::Open($lockedPath, [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    try {
        $unwritable = Invoke-Compiler -Arguments @($validSource, '-o', $lockedPath) -Name 'unwritable-output'
        Assert-Exit $unwritable 1 'unwritable-output'
    } finally {
        $lock.Dispose()
    }
    if ((Get-FileHash -LiteralPath $lockedPath -Algorithm SHA256).Hash -ne $lockedBefore) { throw 'Publication failure changed old output' }

    $usageCases = @(
        @{ Name = 'no-arguments'; Args = @() },
        @{ Name = 'unknown-option'; Args = @('--unknown') },
        @{ Name = 'missing-output'; Args = @($validSource) },
        @{ Name = 'missing-o-value'; Args = @($validSource, '-o') },
        @{ Name = 'duplicate-output'; Args = @($validSource, '-o', 'a.exe', '-o', 'b.exe') }
    )
    foreach ($case in $usageCases) {
        $result = Invoke-Compiler -Arguments $case.Args -Name $case.Name
        Assert-Exit $result 2 $case.Name
        if ($result.Stderr -notmatch 'Usage:') { throw 'Usage failure omitted help' }
    }

    Write-Host "PASS Bootstrap acceptance. Inputs, outputs and logs retained at $runRoot"
    exit 0
} catch {
    [Console]::Error.WriteLine("Bootstrap acceptance failed: $($_.Exception.Message)")
    if ($null -ne $runRoot) { [Console]::Error.WriteLine("Artifacts retained at $runRoot") }
    exit 1
}
