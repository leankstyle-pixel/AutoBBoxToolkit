param(
    [string]$Root = '',
    [string]$CreoRoot = 'D:\Program Files\PTC\Creo 10.0.8.0'
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

$finder = Join-Path $Root 'scripts\find_creo_install.py'
$indexDir = Join-Path $Root '.autobbox\index\creo_install'

if (!(Test-Path $finder)) {
    throw "Python finder not found: $finder"
}
if (!(Test-Path (Join-Path $indexDir 'search_index.jsonl'))) {
    throw "Creo install index not found. Run scripts/update_creo_install_index.ps1 -Full"
}

$qParam = [string]::Concat([char]0x53c2, [char]0x6570)
$qDialog = [string]::Concat([char]0x5bf9, [char]0x8bdd, [char]0x6846)
$qView = [string]::Concat([char]0x5efa, [char]0x89c6, [char]0x56fe)
$qNoTitlePopup = [string]::Concat([char]0x65e0, [char]0x6807, [char]0x9898, [char]0x5f39, [char]0x7a97)

$cases = @(
    @{ Query = 'ProToolkit.h'; Kind = 'all'; ExpectPath = 'Common Files\protoolkit\includes\ProToolkit.h' },
    @{ Query = 'ProUIDialog'; Kind = 'api'; ExpectAny = @('Common Files\protoolkit\includes\ProUIDialog.h', 'Common Files\protoolkit\protkdoc\api\ProUIDialog.html') },
    @{ Query = 'ProParameter'; Kind = 'api'; ExpectSymbol = 'ProParameter' },
    @{ Query = 'ProDrawingView'; Kind = 'api'; ExpectPath = 'Common Files\protoolkit\includes\ProDrawingView.h' },
    @{ Query = 'geardesigndlg.res'; Kind = 'ui'; ExpectPath = 'Common Files\protoolkit\protk_appls\pt_geardesign\text\usascii\resource\geardesigndlg.res' },
    @{ Query = 'CreoTkExamples_ribbon.rbn'; Kind = 'ui'; ExpectPath = 'Common Files\protoolkit\protk_appls\creotk_examples\text\ribbon\CreoTkExamples_ribbon.rbn' },
    @{ Query = 'protk.dat'; Kind = 'path'; ExpectPath = 'Common Files\protoolkit\protk.dat' },
    @{ Query = $qParam; Kind = 'all'; ExpectSymbol = 'ProParameter' },
    @{ Query = $qDialog; Kind = 'all'; ExpectAny = @('ProUIDialog', '.res') },
    @{ Query = $qView; Kind = 'all'; ExpectAny = @('ProDrawingView', 'ProView') },
    @{ Query = 'TitleBar False'; Kind = 'ui'; ExpectAny = @('Common Files\proe\uitools\text\resource\MiniToolbarDlg.res', 'Common Files\proe\uitools\text\resource\fdb_floatbox.res') },
    @{ Query = $qNoTitlePopup; Kind = 'ui'; ExpectAny = @('Common Files\proe\uitools\text\resource\MiniToolbarDlg.res', 'Common Files\proe\uitools\text\resource\fdb_floatbox.res') },
    @{ Query = 'pt_geardesign'; Kind = 'sample'; ExpectPath = 'Common Files\protoolkit\protk_appls\pt_geardesign' }
)

function Assert-Condition {
    param([bool]$Condition, [string]$Message)
    if (!$Condition) {
        throw $Message
    }
}

foreach ($case in $cases) {
    Write-Host ("Testing: {0} [{1}]" -f $case.Query, $case.Kind)
    $raw = & python $finder $case.Query --kind $case.Kind --top 8 --repo-root $Root --creo-root $CreoRoot --index-dir $indexDir --json
    if ($LASTEXITCODE -ne 0) {
        throw "finder failed for query: $($case.Query)"
    }
    $result = $raw | ConvertFrom-Json
    Assert-Condition ($result.results.Count -gt 0) "No results for query: $($case.Query)"
    foreach ($item in $result.results) {
        Assert-Condition ($item.absolute_path -like "$CreoRoot*") "Result outside Creo root: $($item.absolute_path)"
        Assert-Condition (Test-Path $item.absolute_path) "Result path missing: $($item.absolute_path)"
        Assert-Condition (![string]::IsNullOrWhiteSpace($item.record_type)) "Missing record_type for query: $($case.Query)"
        Assert-Condition (![string]::IsNullOrWhiteSpace($item.relative_path)) "Missing relative_path for query: $($case.Query)"
    }

    $joinedPaths = (($result.results | ForEach-Object { $_.relative_path }) -join "`n")
    $joinedSymbols = (($result.results | ForEach-Object { "$($_.symbol) $($_.title) $($_.relative_path)" }) -join "`n")
    if ($case.ContainsKey('ExpectPath')) {
        Assert-Condition ($joinedPaths -like "*$($case.ExpectPath)*") "Expected path fragment not found for $($case.Query): $($case.ExpectPath)"
    }
    if ($case.ContainsKey('ExpectSymbol')) {
        Assert-Condition ($joinedSymbols -like "*$($case.ExpectSymbol)*") "Expected symbol not found for $($case.Query): $($case.ExpectSymbol)"
    }
    if ($case.ContainsKey('ExpectAny')) {
        $matched = $false
        foreach ($needle in $case.ExpectAny) {
            if ($joinedSymbols -like "*$needle*" -or $joinedPaths -like "*$needle*") {
                $matched = $true
                break
            }
        }
        Assert-Condition $matched "None of expected fragments found for $($case.Query): $($case.ExpectAny -join ', ')"
    }
}

$bundleRaw = & python $finder 'ProUIDialog' --kind api --top 5 --repo-root $Root --creo-root $CreoRoot --index-dir $indexDir --bundle --json
$bundle = $bundleRaw | ConvertFrom-Json
Assert-Condition ($bundle.bundle.primary -ne $null) "Bundle primary missing."
Assert-Condition (($bundle.bundle.headers.Count + $bundle.bundle.docs.Count) -gt 0) "Bundle lacks header/doc evidence."

$apiRaw = & python $finder 'ProUIDialogCreate' --kind api --top 3 --repo-root $Root --creo-root $CreoRoot --index-dir $indexDir --json
$api = $apiRaw | ConvertFrom-Json
$apiDetailsJoined = (($api.results | ForEach-Object { $_.details.signature }) -join "`n")
Assert-Condition ($apiDetailsJoined -like '*ProUIDialogCreate*') "Header signature extraction failed for ProUIDialogCreate."

$docDetailsJoined = (($api.results | ForEach-Object { ($_.details.return_values -join ' ') }) -join "`n")
Assert-Condition ($docDetailsJoined -like '*PRO_TK_NO_ERROR*') "HTML return value extraction failed for ProUIDialogCreate."

$resRaw = & python $finder 'geardesigndlg.res' --kind ui --top 1 --repo-root $Root --creo-root $CreoRoot --index-dir $indexDir --json
$res = $resRaw | ConvertFrom-Json
Assert-Condition (($res.results[0].details.dialog_names -join ' ') -like '*geardesigndlg*') "RES dialog parsing failed for geardesigndlg.res."
Assert-Condition (($res.results[0].details.widget_names -join ' ') -like '*ComponentsTable*') "RES widget parsing failed for geardesigndlg.res."

$titlebarRaw = & python $finder 'MiniToolbarDlg.res' --kind ui --top 1 --repo-root $Root --creo-root $CreoRoot --index-dir $indexDir --json
$titlebar = $titlebarRaw | ConvertFrom-Json
$titlebarValues = $titlebar.results[0].details.dialog_properties.PSObject.Properties['.TitleBar'].Value
Assert-Condition (($titlebarValues -join ' ') -like '*False*') "RES dialog property parsing failed for .TitleBar False."

$sampleRaw = & python $finder 'GearDesignUI.c' --kind sample --top 1 --repo-root $Root --creo-root $CreoRoot --index-dir $indexDir --json
$sample = $sampleRaw | ConvertFrom-Json
Assert-Condition (($sample.results[0].details.api_refs -join ' ') -like '*ProUIDialogCreate*') "Sample API reference parsing failed for GearDesignUI.c."
Assert-Condition (($sample.results[0].details.resource_refs -join ' ') -like '*geardesigndlg*') "Sample resource reference parsing failed for GearDesignUI.c."

Write-Host "Creo install index tests passed: $($cases.Count) golden queries plus bundle/detail checks."
