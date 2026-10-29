[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskManifest = Get-Content -LiteralPath (Join-Path $taskRoot 'vcpkg.json') -Raw | ConvertFrom-Json
$taskBaseline = $taskManifest.'builtin-baseline'
if ($taskBaseline -notmatch '^[0-9a-f]{40}$') { throw 'A pinned vcpkg baseline is required.' }
$taskVcpkg = Join-Path $taskRoot '.tools/vcpkg'
if (-not (Test-Path -LiteralPath (Join-Path $taskVcpkg '.git'))) {
    New-Item -ItemType Directory -Force -Path $taskVcpkg | Out-Null
    git -C $taskVcpkg init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize the local dependency checkout.' }
    git -C $taskVcpkg remote add origin https://github.com/microsoft/vcpkg.git
    if ($LASTEXITCODE -ne 0) { throw 'Cannot configure the vcpkg registry.' }
}
$taskRemote = git -C $taskVcpkg remote get-url origin
if ($taskRemote -ne 'https://github.com/microsoft/vcpkg.git') { throw 'Unexpected vcpkg remote.' }
git -C $taskVcpkg fetch --depth 1 origin $taskBaseline
if ($LASTEXITCODE -ne 0) { throw 'Cannot fetch the pinned vcpkg revision.' }
git -C $taskVcpkg checkout --detach $taskBaseline
if ($LASTEXITCODE -ne 0) { throw 'Cannot select the pinned vcpkg revision.' }
& (Join-Path $taskVcpkg 'bootstrap-vcpkg.bat') -disableMetrics
if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed.' }
Write-Host 'Pinned vcpkg is ready; CMake will install the manifest dependencies.'
