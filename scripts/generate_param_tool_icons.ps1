$scriptPath = 'F:\\claude\\003\scripts\generate_plugin_icons.ps1'
if (!(Test-Path $scriptPath)) {
    throw "icon generator not found: $scriptPath"
}

& $scriptPath
