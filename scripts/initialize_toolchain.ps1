[CmdletBinding()]
param([switch]$ExportForGitHub)

$ErrorActionPreference = 'Stop'
$task_vs_where = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $task_vs_where)) { throw 'Visual Studio Installer is required.' }
$task_vs_root = & $task_vs_where -latest -version '[17.0,18.0)' -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $task_vs_root) { throw 'Visual Studio 2022 C++ tools were not found.' }

$task_dev_command = Join-Path $task_vs_root 'Common7/tools/VsDevCmd.bat'
$task_environment = & $env:ComSpec /d /c "call `"$task_dev_command`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Developer environment initialization failed.' }

$env:VCPKG_VISUAL_STUDIO_PATH = $task_vs_root
$task_export_names = @('INCLUDE', 'LIB', 'LIBPATH', 'VCToolsInstallDir', 'WindowsSdkDir', 'WindowsSDKVersion')
foreach ($task_line in $task_environment) {
    if ($task_line -match '^([^=]+)=(.*)$') {
        $task_name, $task_value = $Matches[1], $Matches[2]
        if ($task_name -ieq 'PATH' -or $task_name -in $task_export_names) {
            Set-Item -LiteralPath "Env:$task_name" -Value $task_value
            if ($ExportForGitHub -and $task_name -in $task_export_names) {
                "$task_name=$task_value" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
            }
        }
    }
}

$task_tool_directories = @(
    (Join-Path $task_vs_root 'VC/tools/Llvm/x64/bin'),
    (Join-Path $task_vs_root 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'),
    (Join-Path $task_vs_root 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja')
)
$env:Path = ($task_tool_directories -join ';') + ';' + $env:Path
$env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path (Split-Path -Parent $PSScriptRoot) '.tools/vcpkg-cache'
New-Item -ItemType Directory -Force -Path $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
if ($ExportForGitHub) {
    # Every archive required by another runner must be in the persisted tree.
    $env:VCPKG_BINARY_SOURCES = "clear;files,$env:VCPKG_DEFAULT_BINARY_CACHE,readwrite"
} elseif (-not $env:VCPKG_BINARY_SOURCES) {
    $task_existing_cache = Join-Path $env:LOCALAPPDATA 'vcpkg/archives'
    $env:VCPKG_BINARY_SOURCES = "clear;files,$task_existing_cache,read;files,$env:VCPKG_DEFAULT_BINARY_CACHE,readwrite"
}
if ($ExportForGitHub) {
    if (-not $env:GITHUB_PATH -or -not $env:GITHUB_ENV) { throw 'GitHub environment files are required.' }
    $env:Path.Split(';') | Where-Object { $_ } | Select-Object -Unique |
        Out-File -FilePath $env:GITHUB_PATH -Encoding utf8 -Append
    "VCPKG_VISUAL_STUDIO_PATH=$task_vs_root" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    "VCPKG_DEFAULT_BINARY_CACHE=$env:VCPKG_DEFAULT_BINARY_CACHE" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    "VCPKG_BINARY_SOURCES=$env:VCPKG_BINARY_SOURCES" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
}
Write-Host 'Initialized Visual Studio 2022 x64, CMake, Ninja and LLVM.'
