[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$task_root = Split-Path -Parent $PSScriptRoot
$task_manifest = Get-Content -LiteralPath (Join-Path $task_root 'vcpkg.json') -Raw | ConvertFrom-Json
$task_baseline = $task_manifest.'builtin-baseline'
if ($task_baseline -notmatch '^[0-9a-f]{40}$') { throw 'A pinned vcpkg baseline is required.' }
$task_vcpkg = Join-Path $task_root '.tools/vcpkg'
if (-not (Test-Path -LiteralPath (Join-Path $task_vcpkg '.git'))) {
    New-Item -ItemType Directory -Force -Path $task_vcpkg | Out-Null
    git -C $task_vcpkg init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the local dependency checkout.' }
    git -C $task_vcpkg remote add origin https://github.com/microsoft/vcpkg.git
    if ($LASTEXITCODE -ne 0) { throw 'Cannot configure the vcpkg registry.' }
}
$task_remote = git -C $task_vcpkg remote get-url origin
if ($task_remote -ne 'https://github.com/microsoft/vcpkg.git') { throw 'Unexpected vcpkg remote.' }
git -C $task_vcpkg fetch --depth 1 origin $task_baseline
if ($LASTEXITCODE -ne 0) { throw 'Cannot fetch the pinned vcpkg revision.' }
git -C $task_vcpkg checkout --detach $task_baseline
if ($LASTEXITCODE -ne 0) { throw 'Cannot select the pinned vcpkg revision.' }
& (Join-Path $task_vcpkg 'bootstrap-vcpkg.bat') -disableMetrics
if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
Write-Host 'Pinned vcpkg is ready; CMake will install the manifest dependencies.'
