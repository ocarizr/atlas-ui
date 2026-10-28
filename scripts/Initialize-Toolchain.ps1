[CmdletBinding()]
param([switch]$ExportForGitHub)

$ErrorActionPreference = 'Stop'
$taskVsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVsWhere)) { throw 'Visual Studio Installer is required.' }
$taskVsRoot = & $taskVsWhere -latest -version '[17.0,18.0)' -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $taskVsRoot) { throw 'Visual Studio 2022 C++ tools were not found.' }

$taskDevCommand = Join-Path $taskVsRoot 'Common7/Tools/VsDevCmd.bat'
$taskEnvironment = & $env:ComSpec /d /c "call `"$taskDevCommand`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Developer environment initialization failed.' }

$taskExportNames = @('INCLUDE', 'LIB', 'LIBPATH', 'VCToolsInstallDir', 'WindowsSdkDir', 'WindowsSDKVersion')
foreach ($taskLine in $taskEnvironment) {
    if ($taskLine -match '^([^=]+)=(.*)$') {
        $taskName, $taskValue = $Matches[1], $Matches[2]
        if ($taskName -ieq 'PATH' -or $taskName -in $taskExportNames) {
            Set-Item -LiteralPath "Env:$taskName" -Value $taskValue
            if ($ExportForGitHub -and $taskName -in $taskExportNames) {
                "$taskName=$taskValue" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
            }
        }
    }
}

$taskToolDirectories = @(
    (Join-Path $taskVsRoot 'VC/Tools/Llvm/x64/bin'),
    (Join-Path $taskVsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'),
    (Join-Path $taskVsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja')
)
$env:Path = ($taskToolDirectories -join ';') + ';' + $env:Path
if ($ExportForGitHub) {
    if (-not $env:GITHUB_PATH -or -not $env:GITHUB_ENV) { throw 'GitHub environment files are required.' }
    $env:Path.Split(';') | Where-Object { $_ } | Select-Object -Unique |
        Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
}
Write-Host 'Initialized Visual Studio 2022 x64, CMake, Ninja and LLVM.'
