param(
    [switch]$Full,
    [string]$Root = '',
    [string]$CreoRoot = 'D:\Program Files\PTC\Creo 10.0.8.0'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$cacheDir = Join-Path $Root '.autobbox\index\creo_evidence_cache'
$metadataPath = Join-Path $cacheDir 'metadata.json'
$builderPath = Join-Path $Root 'scripts\build_creo_evidence_cache.py'
$installIndexDir = Join-Path $Root '.autobbox\index\creo_install'
$installMetadataPath = Join-Path $installIndexDir 'metadata.json'

if (!(Test-Path $builderPath)) {
    throw "evidence cache builder not found: $builderPath"
}
if (!(Test-Path (Join-Path $installIndexDir 'search_index.jsonl'))) {
    throw "Creo install index not found: $installIndexDir`nRun: powershell -ExecutionPolicy Bypass -File $Root\scripts\update_creo_install_index.ps1 -Full"
}

$trackedInputs = @(
    'src',
    'include',
    'resource',
    'ribbon',
    'text',
    'deploy',
    'runtime',
    'autobbox_msg.txt',
    'protk.dat',
    '.autobbox/index/feature_index.json'
)

$scanExtensions = @{
    '.c' = $true; '.cc' = $true; '.cpp' = $true; '.cxx' = $true; '.h' = $true; '.hpp' = $true
    '.res' = $true; '.rbn' = $true; '.mnu' = $true; '.txt' = $true; '.dat' = $true
    '.json' = $true
}

function Resolve-RepoPath {
    param([string]$RepoRoot, [string]$RelativePath)
    if ([string]::IsNullOrWhiteSpace($RelativePath)) { return $null }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot (($RelativePath -replace '/', '\').Trim())))
}

function Get-LatestInputWriteTimeUtc {
    param([string]$RepoRoot, [string[]]$Inputs)
    $latest = [datetime]::SpecifyKind([datetime]'2000-01-01T00:00:00', [System.DateTimeKind]::Utc)
    foreach ($relative in $Inputs) {
        $full = Resolve-RepoPath $RepoRoot $relative
        if (!$full -or !(Test-Path $full)) { continue }
        $item = Get-Item $full
        $files = if ($item -is [IO.DirectoryInfo]) {
            Get-ChildItem -Path $full -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object { $scanExtensions.ContainsKey($_.Extension.ToLowerInvariant()) -or $_.Name -ieq 'protk.dat' }
        } else {
            @($item)
        }
        foreach ($file in $files) {
            if ($file.LastWriteTimeUtc -gt $latest) { $latest = $file.LastWriteTimeUtc }
        }
    }
    return $latest
}

$latestProjectUtc = Get-LatestInputWriteTimeUtc -RepoRoot $Root -Inputs $trackedInputs
$shouldBuild = $Full -or !(Test-Path $metadataPath)

if (!$shouldBuild) {
    try {
        $metadata = Get-Content -Path $metadataPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $cachedLatest = [datetime]::Parse($metadata.project_snapshot.latest_input_write_time_utc).ToUniversalTime()
        if ($latestProjectUtc -gt $cachedLatest.AddSeconds(1)) { $shouldBuild = $true }

        if (Test-Path $installMetadataPath) {
            $installMeta = Get-Content -Path $installMetadataPath -Raw -Encoding UTF8 | ConvertFrom-Json
            $currentInstallGenerated = [string]$installMeta.generated_at_utc
            $cachedInstallGenerated = [string]$metadata.creo_install_index.generated_at_utc
            if ($currentInstallGenerated -ne $cachedInstallGenerated) { $shouldBuild = $true }
        }
    } catch {
        $shouldBuild = $true
    }
}

if (!$shouldBuild) {
    Write-Host "Creo evidence cache is up to date: $cacheDir"
    Write-Host ("Latest project input UTC: {0}" -f $latestProjectUtc.ToString('u'))
    exit 0
}

New-Item -ItemType Directory -Path $cacheDir -Force | Out-Null
python $builderPath --repo-root $Root --creo-root $CreoRoot --cache-dir $cacheDir --install-index-dir $installIndexDir
if ($LASTEXITCODE -ne 0) { throw "Evidence cache build failed with exit code $LASTEXITCODE" }

# Also rebuild the API co-occurrence graph
$graphBuilder = Join-Path $Root 'scripts\build_api_graph.py'
if (Test-Path $graphBuilder) {
    Write-Host "Rebuilding API co-occurrence graph..."
    & python $graphBuilder --repo-root $Root
    if ($LASTEXITCODE -ne 0) { Write-Warning "API graph build failed (non-fatal)" }
}

exit 0
