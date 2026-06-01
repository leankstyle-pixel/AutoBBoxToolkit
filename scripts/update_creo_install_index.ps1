param(
    [switch]$Full,
    [string]$Root = '',
    [string]$CreoRoot = 'D:\Program Files\PTC\Creo 10.0.8.0'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$indexDir = Join-Path $Root '.autobbox\index\creo_install'
$metadataPath = Join-Path $indexDir 'metadata.json'
$builderPath = Join-Path $Root 'scripts\build_creo_install_index.py'

if (!(Test-Path $builderPath)) {
    throw "index builder not found: $builderPath"
}
if (!(Test-Path $CreoRoot)) {
    throw "Creo root not found: $CreoRoot"
}

$scanInputs = @(
    'Common Files\protoolkit\includes',
    'Common Files\protoolkit\protkdoc',
    'Common Files\protoolkit\protk_appls',
    'Common Files\protoolkit\protk.dat',
    'Common Files\protoolkit\x86e_win64',
    'Common Files\otk\otk_cpp\include',
    'Common Files\otk\otk_cpp\otk_examples',
    'Common Files\otk\otk_cpp\x86e_win64',
    'Common Files\otk_cpp_doc\objecttoolkit_Creo\api',
    'Common Files\afx\text\resource',
    'Common Files\afx\text\ribbon',
    'Parametric\text',
    'Common Files\protoolkit\Creo_Toolkit_GSG.pdf',
    'Common Files\protoolkit\Creo_Toolkit_RelNotes.pdf',
    'Common Files\protoolkit\tkuse.pdf'
)

$manifestExtensions = @{
    '.h' = $true
    '.hpp' = $true
    '.c' = $true
    '.cc' = $true
    '.cpp' = $true
    '.cxx' = $true
    '.txt' = $true
    '.res' = $true
    '.rbn' = $true
    '.mnu' = $true
    '.dat' = $true
    '.xml' = $true
    '.html' = $true
    '.htm' = $true
    '.pdf' = $true
    '.png' = $true
    '.gif' = $true
    '.jpg' = $true
    '.jpeg' = $true
    '.ico' = $true
    '.lib' = $true
    '.dll' = $true
    '.exe' = $true
    '.bat' = $true
}

function Get-LatestInputWriteTimeUtc {
    param([string]$BaseRoot, [string[]]$Inputs)

    $latest = [datetime]::SpecifyKind([datetime]'2000-01-01T00:00:00', [System.DateTimeKind]::Utc)
    foreach ($relative in $Inputs) {
        $full = Join-Path $BaseRoot $relative
        if (!(Test-Path $full)) {
            continue
        }
        $item = Get-Item $full
        $files = if ($item -is [IO.DirectoryInfo]) {
            Get-ChildItem -Path $full -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object { $manifestExtensions.ContainsKey($_.Extension.ToLowerInvariant()) -or $_.Name -ieq 'protk.dat' }
        } else {
            @($item) | Where-Object { $manifestExtensions.ContainsKey($_.Extension.ToLowerInvariant()) -or $_.Name -ieq 'protk.dat' }
        }
        foreach ($file in $files) {
            if ($file.LastWriteTimeUtc -gt $latest) {
                $latest = $file.LastWriteTimeUtc
            }
        }
    }
    return $latest
}

$latestInputUtc = Get-LatestInputWriteTimeUtc -BaseRoot $CreoRoot -Inputs $scanInputs
$shouldBuild = $Full -or !(Test-Path $metadataPath)

if (!$shouldBuild) {
    try {
        $metadata = Get-Content -Path $metadataPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $indexedLatest = [datetime]::Parse($metadata.source_snapshot.latest_input_write_time_utc).ToUniversalTime()
        # PowerShell and Python can round file timestamps slightly differently.
        # Use a one-second tolerance so an unchanged install does not rebuild forever.
        $shouldBuild = $latestInputUtc -gt $indexedLatest.AddSeconds(1)
    } catch {
        $shouldBuild = $true
    }
}

if (!$shouldBuild) {
    Write-Host "Creo install index is up to date: $indexDir"
    Write-Host ("Latest input UTC: {0}" -f $latestInputUtc.ToString('u'))
    exit 0
}

New-Item -ItemType Directory -Path $indexDir -Force | Out-Null
python $builderPath --repo-root $Root --creo-root $CreoRoot --index-dir $indexDir
