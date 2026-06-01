param(
    [Parameter(Position = 0, Mandatory = $true)]
    [string]$Query,

    [switch]$Json,

    [int]$Top = 5,

    [string]$Root = ''
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Get-StringArray {
    param([object]$Value)

    if ($null -eq $Value) {
        return @()
    }
    if ($Value -is [string]) {
        return @($Value)
    }
    return @($Value)
}

function Resolve-RepoPath {
    param(
        [string]$RepoRoot,
        [string]$RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return $null
    }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot (($RelativePath -replace '/', '\').Trim())))
}

function Normalize-Text {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return ''
    }
    $lower = $Text.ToLowerInvariant()
    $normalized = [regex]::Replace($lower, '[^0-9a-zA-Z\u4e00-\u9fff]+', ' ')
    $normalized = [regex]::Replace($normalized, '\s+', ' ').Trim()
    return $normalized
}

function Normalize-Loose {
    param([string]$Text)

    $normalized = Normalize-Text $Text
    return ($normalized -replace '\s+', '')
}

function Get-Tokens {
    param([string]$Text)

    $normalized = Normalize-Text $Text
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        return @()
    }
    return @($normalized.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) | Sort-Object -Unique)
}

function Get-LatestWriteTimeUtc {
    param(
        [string]$RepoRoot,
        [string[]]$RelativeInputs
    )

    $latest = [datetime]::SpecifyKind([datetime]'2000-01-01T00:00:00', [System.DateTimeKind]::Utc)
    foreach ($relativePath in $RelativeInputs) {
        $fullPath = Resolve-RepoPath $RepoRoot $relativePath
        if (!$fullPath -or !(Test-Path $fullPath)) {
            continue
        }

        $items = if ((Get-Item $fullPath) -is [IO.DirectoryInfo]) {
            Get-ChildItem -Path $fullPath -Recurse -File -ErrorAction SilentlyContinue
        } else {
            @(Get-Item $fullPath)
        }

        foreach ($item in $items) {
            if ($item.LastWriteTimeUtc -gt $latest) {
                $latest = $item.LastWriteTimeUtc
            }
        }
    }
    return $latest
}

function Add-Reason {
    param(
        [System.Collections.Generic.List[string]]$Reasons,
        [string]$Text
    )

    if (![string]::IsNullOrWhiteSpace($Text) -and !$Reasons.Contains($Text)) {
        $Reasons.Add($Text)
    }
}

$indexPath = Join-Path $Root '.autobbox\index\feature_index.json'
if (!(Test-Path $indexPath)) {
    throw "feature index not found: $indexPath`nRun: powershell -ExecutionPolicy Bypass -File scripts/update_feature_index.ps1"
}

$index = Get-Content -Path $indexPath -Raw -Encoding UTF8 | ConvertFrom-Json
$trackedInputs = Get-StringArray $index.source_snapshot.tracked_inputs
$latestInputWriteTimeUtc = Get-LatestWriteTimeUtc -RepoRoot $Root -RelativeInputs $trackedInputs
$generatedAtUtc = [datetime]::Parse($index.generated_at_utc).ToUniversalTime()
$isStale = $latestInputWriteTimeUtc -gt $generatedAtUtc

$queryNormalized = Normalize-Text $Query
$queryLoose = Normalize-Loose $Query
$queryTokens = Get-Tokens $Query

$results = foreach ($feature in (Get-StringArray $index.features)) {
    $phraseHits = 0
    $keywordHits = 0
    $commandHits = 0
    $pathHits = 0
    $reasons = New-Object System.Collections.Generic.List[string]

    $phraseCandidates = @($feature.title) + (Get-StringArray $feature.aliases)
    foreach ($candidate in $phraseCandidates) {
        $candidateLoose = Normalize-Loose $candidate
        if ([string]::IsNullOrWhiteSpace($candidateLoose)) {
            continue
        }
        if ($queryLoose -eq $candidateLoose) {
            $phraseHits += 3
            Add-Reason $reasons ("exact phrase: {0}" -f $candidate)
            continue
        }
        if ($queryLoose.Contains($candidateLoose) -or $candidateLoose.Contains($queryLoose)) {
            $phraseHits += 1
            Add-Reason $reasons ("phrase: {0}" -f $candidate)
        }
    }

    $keywordPool = @((Get-StringArray $feature.aliases) + (Get-StringArray $feature.keywords) + @($feature.title) | Sort-Object -Unique)
    foreach ($token in $queryTokens) {
        foreach ($keyword in $keywordPool) {
            $keywordNorm = Normalize-Text $keyword
            if ([string]::IsNullOrWhiteSpace($keywordNorm)) {
                continue
            }
            if ($keywordNorm.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries) -contains $token) {
                $keywordHits += 1
                Add-Reason $reasons ("keyword: {0}" -f $token)
                break
            }
        }
    }

    foreach ($command in (Get-StringArray $feature.commands)) {
        $commandLoose = Normalize-Loose $command
        if ([string]::IsNullOrWhiteSpace($commandLoose)) {
            continue
        }
        if ($queryLoose.Contains($commandLoose) -or $commandLoose.Contains($queryLoose)) {
            $commandHits += 2
            Add-Reason $reasons ("command: {0}" -f $command)
            continue
        }
        foreach ($token in $queryTokens) {
            if ($command.ToLowerInvariant().Contains($token)) {
                $commandHits += 1
                Add-Reason $reasons ("command keyword: {0}" -f $token)
                break
            }
        }
    }

    $pathValues = @()
    foreach ($group in @('main', 'application', 'ui', 'creo', 'include')) {
        $pathValues += Get-StringArray $feature.paths.$group
    }
    foreach ($pathValue in $pathValues) {
        $leaf = [IO.Path]::GetFileName($pathValue)
        $leafLoose = Normalize-Loose $leaf
        if ([string]::IsNullOrWhiteSpace($leafLoose)) {
            continue
        }
        if ($queryLoose.Contains($leafLoose) -or $leafLoose.Contains($queryLoose)) {
            $pathHits += 2
            Add-Reason $reasons ("file: {0}" -f $leaf)
            continue
        }
        foreach ($token in $queryTokens) {
            if ($leaf.ToLowerInvariant().Contains($token)) {
                $pathHits += 1
                Add-Reason $reasons ("path keyword: {0}" -f $token)
                break
            }
        }
    }

    $score = ($phraseHits * 1000) + ($keywordHits * 100) + ($commandHits * 10) + $pathHits
    if ($score -le 0) {
        continue
    }

    [ordered]@{
        feature_id = $feature.feature_id
        title = $feature.title
        score = $score
        phrase_hits = $phraseHits
        keyword_hits = $keywordHits
        command_hits = $commandHits
        path_hits = $pathHits
        reasons = @($reasons)
        commands = @(Get-StringArray $feature.commands)
        message_keys = @(Get-StringArray $feature.message_keys)
        notes = $feature.notes
        paths = [ordered]@{
            main = @(Get-StringArray $feature.paths.main | ForEach-Object { Resolve-RepoPath $Root $_ })
            application = @(Get-StringArray $feature.paths.application | ForEach-Object { Resolve-RepoPath $Root $_ })
            ui = @(Get-StringArray $feature.paths.ui | ForEach-Object { Resolve-RepoPath $Root $_ })
            creo = @(Get-StringArray $feature.paths.creo | ForEach-Object { Resolve-RepoPath $Root $_ })
            include = @(Get-StringArray $feature.paths.include | ForEach-Object { Resolve-RepoPath $Root $_ })
        }
    }
}

$sortedResults = @(
    $results |
    Sort-Object -Property @{ Expression = 'score'; Descending = $true }, `
        @{ Expression = 'phrase_hits'; Descending = $true }, `
        @{ Expression = 'keyword_hits'; Descending = $true }, `
        @{ Expression = 'command_hits'; Descending = $true }, `
        @{ Expression = 'path_hits'; Descending = $true }, `
        @{ Expression = 'feature_id'; Descending = $false } |
    Select-Object -First $Top
)

if ($Json) {
    $payload = [ordered]@{
        query = $Query
        index_path = $indexPath
        generated_at_utc = $index.generated_at_utc
        latest_input_write_time_utc = $latestInputWriteTimeUtc.ToString("o")
        stale = $isStale
        results = $sortedResults
    }
    $payload | ConvertTo-Json -Depth 8
    return
}

Write-Host ("Query: {0}" -f $Query)
Write-Host ("Index: {0}" -f $indexPath)
if ($isStale) {
    Write-Warning ("Index may be stale. Latest input UTC: {0}; index generated UTC: {1}" -f $latestInputWriteTimeUtc.ToString("u"), $generatedAtUtc.ToString("u"))
}

if ($sortedResults.Count -eq 0) {
    Write-Host "No feature-index match. Fall back to full-repo search."
    return
}

$groupNames = @("main", "application", "ui", "creo")
$rank = 1
foreach ($result in $sortedResults) {
    Write-Host ("{0}. {1} [{2}]" -f $rank, $result.title, $result.feature_id)
    Write-Host ("   Hits: {0}" -f (($result.reasons | Select-Object -First 6) -join "; "))
    if ($result.commands.Count -gt 0) {
        Write-Host ("   Commands: {0}" -f ($result.commands -join ", "))
    }

    foreach ($group in $groupNames) {
        $rawPaths = $null
        if ($result.paths -is [System.Collections.IDictionary]) {
            $rawPaths = $result.paths[$group]
        } else {
            $pathsProperty = $result.paths.PSObject.Properties[$group]
            if ($null -ne $pathsProperty) {
                $rawPaths = $pathsProperty.Value
            }
        }
        $paths = Get-StringArray $rawPaths
        if ($paths.Count -gt 0) {
            Write-Host ("   {0}:" -f $group)
            foreach ($pathItem in $paths) {
                Write-Host ("     - {0}" -f $pathItem)
            }
        }
    }
    $rank += 1
}
