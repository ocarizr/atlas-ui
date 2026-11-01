<#
.SYNOPSIS
Install the pinned Windows Doxygen archive after checking its SHA-256 digest.
.DESCRIPTION
Keeps the verified tool under ignored build tooling and prints the executable path.
Requires the repository Python environment for reading the shared YAML tool pin.
#>
[CmdletBinding()]
param([string]$Python = 'python')

$ErrorActionPreference = 'Stop'
$task_root = Split-Path -Parent $PSScriptRoot
$task_pin = & $Python -c 'import json,sys,yaml; print(json.dumps(yaml.safe_load(open(sys.argv[1], encoding="utf-8"))))' (Join-Path $task_root '.ci/doxygen.yaml')
if ($LASTEXITCODE -ne 0) { throw 'Cannot read Doxygen pin; install scripts/ci/requirements.txt.' }
$task_pin = $task_pin | ConvertFrom-Json
$task_directory = Join-Path $task_root ".tools/doxygen/$($task_pin.version)"
$task_archive = Join-Path $task_root ".tools/doxygen/doxygen-$($task_pin.version).zip"
New-Item -ItemType Directory -Force (Split-Path -Parent $task_archive) | Out-Null
if (-not (Test-Path -LiteralPath $task_archive)) {
    Invoke-WebRequest -Uri $task_pin.windows_url -OutFile $task_archive
}
if ((Get-FileHash -LiteralPath $task_archive -Algorithm SHA256).Hash -ne $task_pin.windows_sha256) {
    throw 'Doxygen archive checksum mismatch; discard the cached archive and retry.'
}
Expand-Archive -LiteralPath $task_archive -DestinationPath $task_directory -Force
$task_executable = @(Get-ChildItem -LiteralPath $task_directory -Filter doxygen.exe -Recurse -File)
if ($task_executable.Count -ne 1) { throw 'Expected exactly one Doxygen executable.' }
$task_version = & $task_executable[0].FullName --version
if ($LASTEXITCODE -ne 0 -or ($task_version.Trim() -split '\s+')[0] -ne $task_pin.version) { throw 'Unexpected Doxygen version.' }
$task_executable[0].FullName
