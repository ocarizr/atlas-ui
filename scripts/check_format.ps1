[CmdletBinding()]
param(
    [switch]$Fix,
    [string]$ClangFormat = 'clang-format'
)

$ErrorActionPreference = 'Stop'
$task_root = Split-Path -Parent $PSScriptRoot
Push-Location $task_root
try {
    # Include new files while respecting .gitignore; exclude third-party code.
    $task_files = @(& git ls-files --cached --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }
    $task_sources = @($task_files | Where-Object {
        $_ -match '\.(cpp|cxx|cc|hpp|hxx|h)$' -and
        $_ -notmatch '^(third_party|dependencies|build|generated|tmp)/' -and
        (Test-Path -LiteralPath $_ -PathType Leaf)
    } | Sort-Object -Unique)

    if ($task_sources.Count -eq 0) {
        Write-Host 'No C++ source files to format yet.'
        return
    }

    $task_formatter = Get-Command $ClangFormat -ErrorAction Stop
    $task_arguments = if ($Fix) { @('-i', '--style=file') } else {
        @('--dry-run', '--Werror', '--style=file')
    }
    foreach ($task_source in $task_sources) {
        & $task_formatter.Source @task_arguments -- $task_source
        if ($LASTEXITCODE -ne 0) { throw "clang-format failed: $task_source" }
    }
}
finally {
    Pop-Location
}
