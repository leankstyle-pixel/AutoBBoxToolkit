param(
    [Parameter(Position = 0, Mandatory = $true)]
    [string]$Query,

    [ValidateSet('api', 'ui', 'feature', 'sample', 'doc', 'path', 'all')]
    [string]$Kind = 'all',

    [int]$Top = 8,
    [switch]$Json,
    [switch]$Explain,
    [switch]$RefreshIfStale,
    [string]$Root = '',
    [string]$CreoRoot = 'D:\Program Files\PTC\Creo 10.0.8.0'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$finderPath = Join-Path $Root 'scripts\find_creo_context.py'
$cacheDir = Join-Path $Root '.autobbox\index\creo_evidence_cache'
$installIndexDir = Join-Path $Root '.autobbox\index\creo_install'

if (!(Test-Path $finderPath)) {
    throw "Python context finder not found: $finderPath"
}
if (!(Test-Path (Join-Path $installIndexDir 'search_index.jsonl'))) {
    throw "Creo install index not found: $installIndexDir`nRun: powershell -ExecutionPolicy Bypass -File $Root\scripts\update_creo_install_index.ps1 -Full"
}
if (!(Test-Path (Join-Path $cacheDir 'project_usage.jsonl'))) {
    Write-Warning "Creo evidence cache not found. Building it now."
    & powershell -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\update_creo_evidence_cache.ps1') -Full -Root $Root -CreoRoot $CreoRoot
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$argsList = @(
    $finderPath,
    $Query,
    '--kind', $Kind,
    '--top', ([string]$Top),
    '--repo-root', $Root,
    '--creo-root', $CreoRoot,
    '--cache-dir', $cacheDir,
    '--install-index-dir', $installIndexDir
)

if ($Json) { $argsList += '--json' }
if ($Explain) { $argsList += '--explain' }
if ($RefreshIfStale) { $argsList += '--refresh-if-stale' }

& python @argsList
exit $LASTEXITCODE
