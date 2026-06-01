param(
    [string]$Root = '',
    [string]$CreoRoot = 'D:\Program Files\PTC\Creo 10.0.8.0',
    [switch]$SkipRefreshTest
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$updateScript = Join-Path $Root 'scripts\update_creo_evidence_cache.ps1'
$finderScript = Join-Path $Root 'scripts\find_creo_context.ps1'
$installTestScript = Join-Path $Root 'scripts\test_creo_install_index.ps1'
$cacheDir = Join-Path $Root '.autobbox\index\creo_evidence_cache'
$QueryTitlelessDialog = -join ([char]0x65e0, [char]0x6807, [char]0x9898, [char]0x5f39, [char]0x7a97)
$QueryQuickRename = -join ([char]0x5feb, [char]0x901f, [char]0x91cd, [char]0x547d, [char]0x540d)

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (!$Condition) {
        throw "ASSERTION FAILED: $Message"
    }
}

function ConvertTo-StringList {
    param([object]$Value)
    $result = New-Object System.Collections.Generic.List[string]
    if ($null -eq $Value) {
        return @()
    }
    if ($Value -is [string]) {
        if (![string]::IsNullOrWhiteSpace($Value)) { $result.Add($Value) }
        return @($result)
    }
    if ($Value -is [System.Collections.IEnumerable]) {
        foreach ($item in $Value) {
            foreach ($nested in (ConvertTo-StringList $item)) { $result.Add($nested) }
        }
        return @($result)
    }
    if ($Value.PSObject -and $Value.PSObject.Properties.Count -gt 0) {
        foreach ($prop in $Value.PSObject.Properties) {
            foreach ($nested in (ConvertTo-StringList $prop.Value)) { $result.Add($nested) }
        }
        return @($result)
    }
    $text = [string]$Value
    if (![string]::IsNullOrWhiteSpace($text)) { $result.Add($text) }
    return @($result)
}

function Invoke-CreoContextJson {
    param(
        [string]$Query,
        [string]$Kind = 'all',
        [int]$Top = 8,
        [switch]$RefreshIfStale
    )
    $argsList = @($finderScript, $Query, '-Kind', $Kind, '-Top', ([string]$Top), '-Root', $Root, '-CreoRoot', $CreoRoot, '-Json')
    if ($RefreshIfStale) { $argsList += '-RefreshIfStale' }
    $output = & powershell -ExecutionPolicy Bypass -File @argsList
    if ($LASTEXITCODE -ne 0) {
        throw "find_creo_context.ps1 failed for '$Query': $($output -join "`n")"
    }
    return (($output -join "`n") | ConvertFrom-Json)
}

Write-Host "== Build evidence cache =="
& powershell -ExecutionPolicy Bypass -File $updateScript -Full -Root $Root -CreoRoot $CreoRoot
Assert-True ($LASTEXITCODE -eq 0) 'update_creo_evidence_cache.ps1 -Full failed'

$requiredFiles = @('project_usage.jsonl', 'evidence_cache.jsonl', 'metadata.json')
foreach ($name in $requiredFiles) {
    Assert-True (Test-Path (Join-Path $cacheDir $name)) "missing cache artifact: $name"
}

$metadata = Get-Content -Path (Join-Path $cacheDir 'metadata.json') -Raw -Encoding UTF8 | ConvertFrom-Json
Assert-True ([int]$metadata.counts.project_usage -gt 0) 'metadata counts.project_usage should be > 0'
Assert-True ([int]$metadata.counts.evidence_cache -gt 0) 'metadata counts.evidence_cache should be > 0'
Assert-True (![string]::IsNullOrWhiteSpace([string]$metadata.generated_at_utc)) 'metadata generated_at_utc missing'

Write-Host "== Query ProUIDialogCreate =="
$api = Invoke-CreoContextJson -Query 'ProUIDialogCreate' -Kind 'api' -Top 8
$apiEvidence = @($api.evidence_results)
Assert-True ($apiEvidence.Count -gt 0) 'ProUIDialogCreate should hit evidence_cache'
Assert-True ((@($apiEvidence | Where-Object { $_.usage_key -eq 'api:ProUIDialogCreate' })).Count -gt 0) 'missing api:ProUIDialogCreate usage'
$apiPaths = ConvertTo-StringList ($apiEvidence | ForEach-Object { $_.project_paths })
Assert-True ((@($apiPaths | Where-Object { $_ -like '*src\ui\quick_rename_dialog.cpp' })).Count -gt 0) 'missing quick_rename_dialog.cpp project usage for ProUIDialogCreate'
$apiOfficial = ConvertTo-StringList ($apiEvidence | ForEach-Object { $_.official_evidence | ForEach-Object { $_.install_relative_path } })
Assert-True ((@($apiOfficial | Where-Object { $_ -like '*Common Files\protoolkit\includes\ProUIDialog.h' })).Count -gt 0) 'missing official ProUIDialog.h evidence'

Write-Host "== Query titleless dialog =="
$titleless = Invoke-CreoContextJson -Query $QueryTitlelessDialog -Kind 'ui' -Top 12
Assert-True ((@(@($titleless.feature_results) | Where-Object { $_.feature_id -eq 'quick_rename' })).Count -gt 0) 'titleless dialog should map to quick_rename feature'
$titlelessEvidence = @($titleless.evidence_results)
Assert-True ((@($titlelessEvidence | Where-Object { $_.usage_key -eq 'ui_property:.TitleBar False' })).Count -gt 0) 'missing .TitleBar False cached usage'
$titlelessPaths = ConvertTo-StringList ($titlelessEvidence | ForEach-Object { $_.project_paths })
Assert-True ((@($titlelessPaths | Where-Object { $_ -like '*resource\autobbox_quick_rename.res' })).Count -gt 0) 'missing autobbox_quick_rename.res project path'
Assert-True ((@($titlelessPaths | Where-Object { $_ -like '*src\ui\quick_rename_dialog.cpp' })).Count -gt 0) 'missing quick_rename_dialog.cpp related project path'
$titlelessOfficial = ConvertTo-StringList ($titlelessEvidence | ForEach-Object { $_.official_evidence | ForEach-Object { $_.install_relative_path } })
Assert-True ((@($titlelessOfficial | Where-Object { $_ -like '*MiniToolbarDlg.res' -or $_ -like '*fdb_floatbox.res' -or $_ -like '*popup_preview.res' })).Count -gt 0) 'missing official titleless/floating dialog resource evidence'

Write-Host "== Query quick rename =="
$quickRename = Invoke-CreoContextJson -Query $QueryQuickRename -Kind 'all' -Top 10
Assert-True ((@(@($quickRename.feature_results) | Where-Object { $_.feature_id -eq 'quick_rename' })).Count -gt 0) 'quick rename should hit feature_index'
Assert-True ((@(@($quickRename.evidence_results) | Where-Object { (ConvertTo-StringList $_.feature_ids) -contains 'quick_rename' })).Count -gt 0) 'quick rename should include quick_rename evidence'

Write-Host "== Query unused official API fallback =="
$fallback = Invoke-CreoContextJson -Query 'ProSelectionarrayToReferences' -Kind 'api' -Top 5
Assert-True ((@($fallback.evidence_results)).Count -eq 0) 'unused official API should not hit evidence cache as a used API'
Assert-True ((@(@($fallback.install_results) | Where-Object { $_.source -eq 'creo_install' -and $_.symbol -eq 'ProSelectionarrayToReferences' })).Count -gt 0) 'unused official API should fallback to creo_install'

Write-Host "== Text output smoke test =="
& powershell -ExecutionPolicy Bypass -File $finderScript $QueryQuickRename -Kind all -Top 5 -Root $Root -CreoRoot $CreoRoot | Out-Host
Assert-True ($LASTEXITCODE -eq 0) 'text output smoke test failed'

if (!$SkipRefreshTest) {
    Write-Host "== Stale and RefreshIfStale test =="
    $touchPath = Join-Path $Root 'resource\autobbox_quick_rename.res'
    if (Test-Path $touchPath) {
        Start-Sleep -Milliseconds 1500
        (Get-Item $touchPath).LastWriteTimeUtc = [DateTime]::UtcNow
        $stale = Invoke-CreoContextJson -Query $QueryTitlelessDialog -Kind 'ui' -Top 5
        Assert-True ([bool]$stale.stale) 'touched project input should mark evidence cache stale'
        $refreshed = Invoke-CreoContextJson -Query $QueryTitlelessDialog -Kind 'ui' -Top 5 -RefreshIfStale
        Assert-True (![bool]$refreshed.stale) '-RefreshIfStale should refresh stale evidence cache'
    } else {
        Write-Warning "Skipping stale test because $touchPath was not found."
    }
}

Write-Host "== Existing install index regression =="
if (Test-Path $installTestScript) {
    & powershell -ExecutionPolicy Bypass -File $installTestScript -Root $Root -CreoRoot $CreoRoot
    Assert-True ($LASTEXITCODE -eq 0) 'test_creo_install_index.ps1 failed'
}

Write-Host "Creo evidence cache tests passed."
