[CmdletBinding()]
param(
    [switch]$Fix,
    [string]$ClangFormat = 'clang-format'
)

$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
Push-Location $taskRoot
try {
    # Include new files while respecting .gitignore; exclude third-party code.
    $taskFiles = @(& git ls-files --cached --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed.' }
    $taskSources = @($taskFiles | Where-Object {
        $_ -match '\.(cpp|cxx|cc|hpp|hxx|h)$' -and
        $_ -notmatch '^(ThirdParty|build|generated|tmp)/' -and
        (Test-Path -LiteralPath $_ -PathType Leaf)
    } | Sort-Object -Unique)

    if ($taskSources.Count -eq 0) {
        Write-Host 'No C++ source files to format yet.'
        return
    }

    $taskFormatter = Get-Command $ClangFormat -ErrorAction Stop
    $taskArguments = if ($Fix) { @('-i', '--style=file') } else {
        @('--dry-run', '--Werror', '--style=file')
    }
    foreach ($taskSource in $taskSources) {
        & $taskFormatter.Source @taskArguments -- $taskSource
        if ($LASTEXITCODE -ne 0) { throw "clang-format failed: $taskSource" }
    }
}
finally {
    Pop-Location
}
