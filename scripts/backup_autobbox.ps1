param(
    [string]$Root,
    [string]$BackupDir = '',
    [string]$Timestamp
)

$ErrorActionPreference = 'Stop'

function New-BackupArchive {
    param(
        [string[]]$SourcePaths,
        [string]$DestinationPath
    )

    if (Test-Path -LiteralPath $DestinationPath) {
        Remove-Item -LiteralPath $DestinationPath -Force
    }

    if (-not $SourcePaths -or $SourcePaths.Count -eq 0) {
        throw "No source paths selected for archive: $DestinationPath"
    }

    Compress-Archive -Path $SourcePaths -DestinationPath $DestinationPath -CompressionLevel Optimal
}

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = Split-Path -Parent $PSScriptRoot
}
if ([string]::IsNullOrWhiteSpace($Timestamp)) {
    $Timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
}

$Root = (Resolve-Path -LiteralPath $Root).Path
if ([string]::IsNullOrWhiteSpace($BackupDir)) {
    $BackupDir = Join-Path $Root 'backup'
}

$PluginRoot = Join-Path $Root 'deploy\AutoBBoxToolkit'
if (!(Test-Path -LiteralPath $PluginRoot)) {
    throw "Plugin runtime folder not found: $PluginRoot"
}

New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null

$pluginZip = Join-Path $BackupDir ("AutoBBoxToolkit_plugin_{0}.zip" -f $Timestamp)
$sourceZip = Join-Path $BackupDir ("autobbox_source_{0}.zip" -f $Timestamp)

$pluginSources = @(Join-Path $PluginRoot '*')
New-BackupArchive -SourcePaths $pluginSources -DestinationPath $pluginZip

$excludeDirs = @('.git', 'archive', 'backup', 'build', 'deploy', 'package', 'runtime')
$excludeFiles = @('1567f1a2-8705-4b68-a600-4951d45b3e6d.png')
$sourceItems = Get-ChildItem -LiteralPath $Root -Force | Where-Object {
    if ($_.PSIsContainer) {
        return $_.Name -notin $excludeDirs
    }
    return $_.Name -notin $excludeFiles
} | Select-Object -ExpandProperty FullName

New-BackupArchive -SourcePaths $sourceItems -DestinationPath $sourceZip

Write-Host "Backup completed."
Write-Host "Plugin zip: $pluginZip"
Write-Host "Source zip: $sourceZip"
