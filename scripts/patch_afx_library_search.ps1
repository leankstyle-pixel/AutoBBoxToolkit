param(
    [string]$AfxResourceRoot = 'D:\Program Files\buw\AFX 10.0.8.0\text\resource'
)

$ErrorActionPreference = 'Stop'

$resPath = Join-Path $AfxResourceRoot 'ProTKDialogLibrarySelection.res'
$txtPath = Join-Path $AfxResourceRoot 'chinese_cn\ProTKDialogLibrarySelection.txt'
$backupRes = "$resPath.autobbox-search-backup"
$backupTxt = "$txtPath.autobbox-search-backup"

if (!(Test-Path $resPath)) {
    throw "AFX resource file not found: $resPath"
}
if (!(Test-Path $txtPath)) {
    throw "AFX Chinese text file not found: $txtPath"
}

if (!(Test-Path $backupRes)) {
    Copy-Item $resPath $backupRes -Force
    Write-Host "Backup created: $backupRes"
}
if (!(Test-Path $backupTxt)) {
    Copy-Item $txtPath $backupTxt -Force
    Write-Host "Backup created: $backupTxt"
}

$res = Get-Content $resPath -Raw

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string]$Value
    )
    $encoding = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Find-BalancedBlock {
    param(
        [Parameter(Mandatory=$true)][string]$Text,
        [Parameter(Mandatory=$true)][string]$Needle
    )

    $start = $Text.IndexOf($Needle, [System.StringComparison]::Ordinal)
    if ($start -lt 0) {
        return $null
    }

    $depth = 0
    $inString = $false
    for ($i = $start; $i -lt $Text.Length; $i++) {
        $ch = $Text[$i]
        if ($ch -eq '"') {
            $inString = -not $inString
            continue
        }
        if ($inString) {
            continue
        }
        if ($ch -eq '(') {
            $depth++
        } elseif ($ch -eq ')') {
            $depth--
            if ($depth -eq 0) {
                return @{ Start = $start; End = $i + 1 }
            }
        }
    }
    return $null
}

$newBottom = @'
(Layout BottomLayout
    (Components
        (Label                          LabelLibrarySearch)
        (InputPanel                     InputPanelLibrarySearch)
        (Label                          LabelLibrarySearchStatus)
        (PushButton                     Pushbutton_SearchNext)
        (PushButton                     Pushbutton_CANCEL)
    )
    (Resources
        (LabelLibrarySearch.Label       "Search:")
        (LabelLibrarySearch.TopOffset   0)
        (LabelLibrarySearch.BottomOffset 0)
        (LabelLibrarySearch.LeftOffset  0)
        (LabelLibrarySearch.RightOffset 4)
        (InputPanelLibrarySearch.Columns 24)
        (InputPanelLibrarySearch.Value  "")
        (InputPanelLibrarySearch.HelpText "Search library item")
        (InputPanelLibrarySearch.ModalOverride 2)
        (InputPanelLibrarySearch.AutoHighlight True)
        (InputPanelLibrarySearch.AttachLeft True)
        (InputPanelLibrarySearch.TopOffset 0)
        (InputPanelLibrarySearch.BottomOffset 0)
        (InputPanelLibrarySearch.LeftOffset 0)
        (InputPanelLibrarySearch.RightOffset 8)
        (LabelLibrarySearchStatus.Label "")
        (LabelLibrarySearchStatus.TopOffset 0)
        (LabelLibrarySearchStatus.BottomOffset 0)
        (LabelLibrarySearchStatus.LeftOffset 0)
        (LabelLibrarySearchStatus.RightOffset 8)
        (Pushbutton_SearchNext.Label "Next")
        (Pushbutton_SearchNext.HelpText "Find next search result")
        (Pushbutton_SearchNext.ButtonStyle 2)
        (Pushbutton_SearchNext.ModalOverride 2)
        (Pushbutton_SearchNext.KeyboardInput 1)
        (Pushbutton_SearchNext.TopOffset 0)
        (Pushbutton_SearchNext.BottomOffset 0)
        (Pushbutton_SearchNext.LeftOffset 0)
        (Pushbutton_SearchNext.RightOffset 4)
        (Pushbutton_CANCEL.Label "Cancel")
        (Pushbutton_CANCEL.HelpText "Cancel definition")
        (Pushbutton_CANCEL.ButtonStyle 2)
        (Pushbutton_CANCEL.ModalOverride 2)
        (Pushbutton_CANCEL.AttachRight True)
        (Pushbutton_CANCEL.KeyboardInput 1)
        (Pushbutton_CANCEL.TopOffset 0)
        (Pushbutton_CANCEL.BottomOffset 0)
        (Pushbutton_CANCEL.RightOffset 0)
        (Pushbutton_CANCEL.Accelerator "Esc")
        (Pushbutton_CANCEL.LeftOffset 0)
        (.AttachRight True)
        (.TopOffset 0)
        (.BottomOffset 10)
        (.AttachLeft True)
        (.AttachTop True)
        (.AttachBottom True)
        (.LeftOffset 10)
        (.RightOffset 10)
        (.CanReduceWidth True)
        (.Layout
            (Grid
                (Rows 1)
                (Cols 0 0 0 0 1)
                LabelLibrarySearch InputPanelLibrarySearch LabelLibrarySearchStatus Pushbutton_SearchNext Pushbutton_CANCEL
            )
        )
    )
)
'@

$block = Find-BalancedBlock -Text $res -Needle '(Layout BottomLayout'
if ($null -eq $block) {
    throw "Failed to locate BottomLayout in $resPath"
}
$patched = $res.Substring(0, $block.Start) + $newBottom + $res.Substring($block.End)

$dialogBlock = Find-BalancedBlock -Text $patched -Needle '(Dialog ProTKDialogLibrarySelection'
if ($null -eq $dialogBlock) {
    throw "Failed to locate ProTKDialogLibrarySelection dialog in $resPath"
}
$dialogText = $patched.Substring($dialogBlock.Start, $dialogBlock.End - $dialogBlock.Start)
if ($dialogText -match '\(\.DefaultButton\s+"[^"]*"\)') {
    $dialogText = [regex]::Replace($dialogText, '\(\.DefaultButton\s+"[^"]*"\)', '(.DefaultButton "Pushbutton_SearchNext")', 1)
} else {
    $labelNeedle = '        (.Label "Select From Library")'
    $insert = "        (.DefaultButton `"Pushbutton_SearchNext`")"
    $idx = $dialogText.IndexOf($labelNeedle, [System.StringComparison]::Ordinal)
    if ($idx -ge 0) {
        $afterLabel = $idx + $labelNeedle.Length
        $dialogText = $dialogText.Substring(0, $afterLabel) + "`r`n" + $insert + $dialogText.Substring($afterLabel)
    } else {
        $resourcesNeedle = "    (Resources`r`n"
        $idx = $dialogText.IndexOf($resourcesNeedle, [System.StringComparison]::Ordinal)
        if ($idx -lt 0) {
            $resourcesNeedle = "    (Resources`n"
            $idx = $dialogText.IndexOf($resourcesNeedle, [System.StringComparison]::Ordinal)
        }
        if ($idx -lt 0) {
            throw "Failed to locate Dialog Resources in $resPath"
        }
        $afterResources = $idx + $resourcesNeedle.Length
        $dialogText = $dialogText.Substring(0, $afterResources) + $insert + "`r`n" + $dialogText.Substring($afterResources)
    }
}
$patched = $patched.Substring(0, $dialogBlock.Start) + $dialogText + $patched.Substring($dialogBlock.End)
Write-Utf8NoBom -Path $resPath -Value $patched
Write-Host "Patched: $resPath"

$txt = Get-Content $txtPath -Raw

# Keep this script ASCII-only so Windows PowerShell 5.1 can parse it correctly
# even when the file is saved as UTF-8 without BOM.
$cnSearchLabel = -join ([char[]](0x641c, 0x7d22, 0x003a))
$cnSearchHelp = -join ([char[]](0x641c, 0x7d22, 0x5e93, 0x9879, 0x76ee))
$cnNextLabel = -join ([char[]](0x4e0b, 0x4e00, 0x4e2a))
$cnNextHelp = -join ([char[]](0x67e5, 0x627e, 0x4e0b, 0x4e00, 0x4e2a, 0x641c, 0x7d22, 0x7ed3, 0x679c))
$append = @"

# AutoBBox AFX library search additions
# LabelLibrarySearch.Label
Search:
$cnSearchLabel

# InputPanelLibrarySearch.HelpText
Search library item
$cnSearchHelp

# LabelLibrarySearchStatus.Label


# Pushbutton_SearchNext.Label
Next
$cnNextLabel

# Pushbutton_SearchNext.HelpText
Find next search result
$cnNextHelp

"@

if ($txt -match '# AutoBBox AFX library search additions') {
    $txt = [regex]::Replace(
        $txt,
        '(?s)\r?\n# AutoBBox AFX library search additions.*$',
        $append)
    Set-Content -Path $txtPath -Value $txt -Encoding UTF8
    Write-Host "Repaired: $txtPath"
} else {
    Add-Content -Path $txtPath -Value $append -Encoding UTF8
    Write-Host "Patched: $txtPath"
}

Write-Host "AFX library search resource patch complete."
