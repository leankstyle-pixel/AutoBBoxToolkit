param(
    [string]$AfxResourceRoot = 'D:\Program Files\buw\AFX 10.0.8.0\text\resource'
)

$ErrorActionPreference = 'Stop'

$resPath = Join-Path $AfxResourceRoot 'ProTKDialogLibrarySelection.res'
$txtPath = Join-Path $AfxResourceRoot 'chinese_cn\ProTKDialogLibrarySelection.txt'
$backupRes = "$resPath.autobbox-search-backup"
$backupTxt = "$txtPath.autobbox-search-backup"

if (!(Test-Path $backupRes)) {
    throw "Backup not found: $backupRes"
}
if (!(Test-Path $backupTxt)) {
    throw "Backup not found: $backupTxt"
}

Copy-Item $backupRes $resPath -Force
Copy-Item $backupTxt $txtPath -Force

Write-Host "Restored: $resPath"
Write-Host "Restored: $txtPath"
