# SPDX-License-Identifier: MIT OR Apache-2.0
$ErrorActionPreference = 'Stop'
$locator = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $locator -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'MSVC x64 installation not found' }
Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installation -Arch amd64 -SkipAutomaticLocation
# Export only development-shell variables required by subsequent CI steps.
foreach ($name in @('PATH','INCLUDE','LIB','LIBPATH','VCToolsInstallDir','VCToolsVersion',
                     'VSINSTALLDIR','VCINSTALLDIR','WindowsSdkDir','WindowsSDKVersion','UniversalCRTSdkDir','UCRTVersion')) {
    $value = [Environment]::GetEnvironmentVariable($name, 'Process')
    if ($null -ne $value) { "$name=$value" | Out-File -LiteralPath $env:GITHUB_ENV -Append -Encoding utf8 }
}
Write-Host "Runner image: $env:ImageOS $env:ImageVersion; MSVC $env:VCToolsVersion; SDK $env:WindowsSDKVersion"
