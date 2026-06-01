param(
    [Parameter(Position = 0, Mandatory = $true)]
    [string]$Query,

    [ValidateSet('api', 'ui', 'sample', 'doc', 'path', 'all')]
    [string]$Kind = 'all',

    [int]$Top = 10,
    [switch]$Json,
    [switch]$Bundle,
    [switch]$Exact,
    [switch]$Explain,
    [switch]$NoRelated,
    [switch]$Open,
    [string]$Root = '',
    [string]$CreoRoot = 'D:\Program Files\PTC\Creo 10.0.8.0'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$finderPath = Join-Path $Root 'scripts\find_creo_install.py'
$indexDir = Join-Path $Root '.autobbox\index\creo_install'
$searchPath = Join-Path $indexDir 'search_index.jsonl'

if (!(Test-Path $finderPath)) {
    throw "Python finder not found: $finderPath"
}
if (!(Test-Path $searchPath)) {
    throw "Creo install search index not found: $searchPath`nRun: powershell -ExecutionPolicy Bypass -File $Root\scripts\update_creo_install_index.ps1 -Full"
}

$argsList = @(
    $finderPath,
    $Query,
    '--kind', $Kind,
    '--top', ([string]$Top),
    '--repo-root', $Root,
    '--creo-root', $CreoRoot,
    '--index-dir', $indexDir
)

if ($Json) { $argsList += '--json' }
if ($Bundle) { $argsList += '--bundle' }
if ($Exact) { $argsList += '--exact' }
if ($Explain) { $argsList += '--explain' }
if ($NoRelated) { $argsList += '--no-related' }

if ($Open) {
    $jsonArgs = @(
        $finderPath,
        $Query,
        '--kind', $Kind,
        '--top', '1',
        '--repo-root', $Root,
        '--creo-root', $CreoRoot,
        '--index-dir', $indexDir,
        '--json'
    )
    if ($Exact) { $jsonArgs += '--exact' }
    $raw = & python @jsonArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $parsed = $raw | ConvertFrom-Json
    if ($parsed.results.Count -le 0) {
        Write-Host "No Creo install index matches for: $Query"
        exit 0
    }
    $path = [string]$parsed.results[0].absolute_path
    if (!(Test-Path $path)) {
        throw "Matched path does not exist: $path"
    }
    Invoke-Item -LiteralPath $path
    Write-Host "Opened: $path"
    exit 0
}

& python @argsList
exit $LASTEXITCODE
