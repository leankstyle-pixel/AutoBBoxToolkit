param(
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

function Normalize-RelativePath {
    param([string]$PathText)

    if ([string]::IsNullOrWhiteSpace($PathText)) {
        return $null
    }
    return ($PathText -replace '/', '\').Trim()
}

function To-RepoRelativePath {
    param(
        [string]$AbsolutePath,
        [string]$RepoRoot
    )

    $rootPath = [IO.Path]::GetFullPath($RepoRoot).TrimEnd('\')
    $fullPath = [IO.Path]::GetFullPath($AbsolutePath)
    if ($fullPath.StartsWith($rootPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $fullPath.Substring($rootPath.Length).TrimStart('\').Replace('\', '/')
    }
    return $fullPath.Replace('\', '/')
}

function Resolve-RepoPath {
    param(
        [string]$RepoRoot,
        [string]$RelativePath
    )

    $normalized = Normalize-RelativePath $RelativePath
    if ([string]::IsNullOrWhiteSpace($normalized)) {
        return $null
    }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $normalized))
}

function Get-ExistingRelativePaths {
    param(
        [string]$RepoRoot,
        [object]$PathList
    )

    $result = @()
    foreach ($item in (Get-StringArray $PathList)) {
        $fullPath = Resolve-RepoPath $RepoRoot $item
        if ($fullPath -and (Test-Path $fullPath)) {
            $result += (To-RepoRelativePath $fullPath $RepoRoot)
        }
    }
    return @($result | Sort-Object -Unique)
}

function Get-IncludePathsFromSources {
    param(
        [string]$RepoRoot,
        [string[]]$SourcePaths
    )

    $includeRoot = Join-Path $RepoRoot 'include\autobbox'
    if (!(Test-Path $includeRoot)) {
        return @()
    }

    $result = @()
    foreach ($sourcePath in $SourcePaths) {
        $leaf = [IO.Path]::GetFileNameWithoutExtension($sourcePath)
        if ([string]::IsNullOrWhiteSpace($leaf)) {
            continue
        }
        $matches = Get-ChildItem -Path $includeRoot -Recurse -File -ErrorAction SilentlyContinue |
            Where-Object { $_.BaseName -eq $leaf }
        foreach ($match in $matches) {
            $result += (To-RepoRelativePath $match.FullName $RepoRoot)
        }
    }
    return @($result | Sort-Object -Unique)
}

function Get-RegisteredCommands {
    param([string]$CommandRegistryPath)

    $content = Get-Content -Path $CommandRegistryPath -Raw
    $matches = [regex]::Matches(
        $content,
        'Register(?:Action|ActionWithPriority|ActionAlias)\("([^"]+)"')
    $commands = foreach ($match in $matches) {
        $match.Groups[1].Value
    }
    return @($commands | Sort-Object -Unique)
}

function Get-MessageKeys {
    param([string]$MessagePath)

    $keys = New-Object System.Collections.Generic.List[string]
    $expectKey = $true
    foreach ($line in Get-Content -Path $MessagePath) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith('#')) {
            continue
        }
        if ($expectKey) {
            $keys.Add($trimmed)
            $expectKey = $false
        } else {
            $expectKey = $true
        }
    }
    return @($keys | Sort-Object -Unique)
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

$aliasPath = Join-Path $Root '.autobbox\index\feature_aliases.json'
$indexPath = Join-Path $Root '.autobbox\index\feature_index.json'
$commandRegistryPath = Join-Path $Root 'src\main\command_registry.cpp'
$messagePath = Join-Path $Root 'autobbox_msg.txt'

if (!(Test-Path $aliasPath)) {
    throw "feature aliases file not found: $aliasPath"
}
if (!(Test-Path $commandRegistryPath)) {
    throw "command registry not found: $commandRegistryPath"
}
if (!(Test-Path $messagePath)) {
    throw "message file not found: $messagePath"
}

$aliasData = Get-Content -Path $aliasPath -Raw -Encoding UTF8 | ConvertFrom-Json
$registeredCommands = Get-RegisteredCommands $commandRegistryPath
$registeredCommandSet = @{}
foreach ($command in $registeredCommands) {
    $registeredCommandSet[$command] = $true
}

$messageKeys = Get-MessageKeys $messagePath
$messageKeySet = @{}
foreach ($messageKey in $messageKeys) {
    $messageKeySet[$messageKey] = $true
}

$trackedInputs = @(
    '.autobbox/index/feature_aliases.json',
    'src/main',
    'src/application',
    'src/ui',
    'src/creo',
    'include/autobbox',
    'autobbox_msg.txt',
    'docs/REBUILD_SOURCE_STATUS.md'
)

$latestInputWriteTimeUtc = Get-LatestWriteTimeUtc -RepoRoot $Root -RelativeInputs $trackedInputs
$features = New-Object System.Collections.Generic.List[object]

foreach ($feature in (Get-StringArray $aliasData.features)) {
    $mainPaths = Get-ExistingRelativePaths -RepoRoot $Root -PathList $feature.path_hints.main
    $applicationPaths = Get-ExistingRelativePaths -RepoRoot $Root -PathList $feature.path_hints.application
    $uiPaths = Get-ExistingRelativePaths -RepoRoot $Root -PathList $feature.path_hints.ui
    $creoPaths = Get-ExistingRelativePaths -RepoRoot $Root -PathList $feature.path_hints.creo
    $includePaths = Get-IncludePathsFromSources -RepoRoot $Root -SourcePaths ($mainPaths + $applicationPaths + $uiPaths + $creoPaths)

    $validCommands = @()
    foreach ($command in (Get-StringArray $feature.commands)) {
        if ($registeredCommandSet.ContainsKey($command)) {
            $validCommands += $command
        } else {
            Write-Warning "feature '$($feature.feature_id)' references missing command '$command'"
        }
    }
    $validCommands = @($validCommands | Sort-Object -Unique)

    $validMessageKeys = @()
    foreach ($messageKey in (Get-StringArray $feature.message_keys)) {
        if ($messageKeySet.ContainsKey($messageKey)) {
            $validMessageKeys += $messageKey
        } else {
            Write-Warning "feature '$($feature.feature_id)' references missing message key '$messageKey'"
        }
    }
    $validMessageKeys = @($validMessageKeys | Sort-Object -Unique)

    $pathBasenames = @($mainPaths + $applicationPaths + $uiPaths + $creoPaths + $includePaths |
            ForEach-Object { [IO.Path]::GetFileName($_) } |
            Sort-Object -Unique)
    $searchTerms = @(
        $feature.title
    ) + (Get-StringArray $feature.aliases) +
        (Get-StringArray $feature.keywords) +
        $validCommands +
        $validMessageKeys +
        $pathBasenames

    $features.Add([ordered]@{
            feature_id = $feature.feature_id
            title = $feature.title
            aliases = @((Get-StringArray $feature.aliases) | Sort-Object -Unique)
            keywords = @((Get-StringArray $feature.keywords) | Sort-Object -Unique)
            commands = $validCommands
            message_keys = $validMessageKeys
            paths = [ordered]@{
                main = $mainPaths
                application = $applicationPaths
                ui = $uiPaths
                creo = $creoPaths
                include = $includePaths
            }
            notes = $feature.notes
            search_terms = @($searchTerms | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
        })
}

$indexData = [ordered]@{
    schema_version = 1
    generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
    root = $Root
    source_snapshot = [ordered]@{
        latest_input_write_time_utc = $latestInputWriteTimeUtc.ToString('o')
        tracked_inputs = @($trackedInputs | Sort-Object -Unique)
        generator = 'scripts/update_feature_index.ps1'
    }
    features = $features
}

New-Item -ItemType Directory -Path (Split-Path $indexPath -Parent) -Force | Out-Null
$json = $indexData | ConvertTo-Json -Depth 8
Set-Content -Path $indexPath -Value $json -Encoding UTF8

Write-Host "Feature index updated:"
Write-Host $indexPath
Write-Host ("Features: {0}" -f $features.Count)
Write-Host ("Latest input UTC: {0}" -f $latestInputWriteTimeUtc.ToString('u'))
