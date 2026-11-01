<#
.SYNOPSIS
Check or apply the shared C++, Python and Protobuf source style.
.DESCRIPTION
Run from any directory with the repository's Python validation environment active.
Documentation is checked but never synthesized. Omit Component and Path to check all owned sources.
.EXAMPLE
./scripts/check_format.ps1 -Fix -Component jobs
.EXAMPLE
./scripts/check_format.ps1 -Path libs/diagnostics/src/metrics.cpp
#>
[CmdletBinding()]
param(
    [switch]$Fix,
    [string]$Component,
    [string[]]$Path,
    [string]$Python = 'python',
    [string]$ClangFormat
)

$ErrorActionPreference = 'Stop'
$task_root = Split-Path -Parent $PSScriptRoot
$task_paths = @($Path | Where-Object { $_ } | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
Push-Location $task_root
try {
    $task_arguments = @('-m', 'scripts.ci.format_sources')
    if ($Fix) { $task_arguments += '--fix' }
    if ($Component) { $task_arguments += @('--component', $Component) }
    if ($ClangFormat) { $task_arguments += @('--clang-format', $ClangFormat) }
    foreach ($task_path in $task_paths) { $task_arguments += @('--path', $task_path) }
    & $Python @task_arguments
    if ($LASTEXITCODE -ne 0) { throw 'Source formatting or documentation validation failed.' }
}
finally {
    Pop-Location
}
