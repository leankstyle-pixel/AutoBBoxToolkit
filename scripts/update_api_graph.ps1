param(
    [string]$Root = '',
    [switch]$Full
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$builderPath = Join-Path $Root 'scripts\build_api_graph.py'
if (!(Test-Path $builderPath)) {
    throw "API graph builder not found: $builderPath"
}

& python $builderPath --repo-root $Root
if ($LASTEXITCODE -ne 0) { throw "API graph build failed with exit code $LASTEXITCODE" }

Write-Host "API graph updated successfully."
