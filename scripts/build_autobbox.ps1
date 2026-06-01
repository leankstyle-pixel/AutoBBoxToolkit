$ErrorActionPreference = 'Stop'

$Root = 'F:\\claude\\003'
$BuildDir = Join-Path $Root 'build\autobbox'
$Cmake = 'C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$VcVars = 'C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat'

if (!(Test-Path $Cmake)) {
    throw "cmake not found: $Cmake"
}
if (!(Test-Path $VcVars)) {
    throw "vcvars64 not found: $VcVars"
}

New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null

$cmd = "call `"$VcVars`" && `"$Cmake`" -S `"$Root`" -B `"$BuildDir`" -G `"NMake Makefiles`" -DCMAKE_BUILD_TYPE=Release && `"$Cmake`" --build `"$BuildDir`" --config Release"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) {
    throw "build failed with exit code $LASTEXITCODE"
}

$resourceSrc = Join-Path $Root 'resource'
$msgSrc = Join-Path $Root 'autobbox_msg.txt'
$deployRoot = Join-Path $Root 'deploy\AutoBBoxToolkit'
$runtimeRoot = Join-Path $Root 'runtime\AutoBBoxToolkit'
$resourceTargets = @(
    (Join-Path $deployRoot 'text\resource'),
    (Join-Path $runtimeRoot 'text\resource'),
    (Join-Path $runtimeRoot 'resource')
)

foreach ($target in $resourceTargets) {
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Copy-Item (Join-Path $resourceSrc '*.res') $target -Force
}

$iconSrc = Join-Path $Root 'text\resource'
foreach ($target in $resourceTargets) {
    Copy-Item (Join-Path $iconSrc '*.png') $target -Force
}

$msgTargets = @(
    (Join-Path $deployRoot 'text\autobbox_msg.txt'),
    (Join-Path $runtimeRoot 'text\autobbox_msg.txt'),
    (Join-Path $runtimeRoot 'autobbox_msg.txt')
)

foreach ($target in $msgTargets) {
    New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
    Copy-Item $msgSrc $target -Force
}

$deployDll = Join-Path $deployRoot 'autobbox_toolkit.dll'
$releaseDll = Join-Path $deployRoot 'Release\autobbox_toolkit.dll'

# The NMake single-config build links directly to $deployDll.  Some recovered
# workspaces also contain an older $releaseDll from a previous generator/layout;
# do not let that stale file overwrite the freshly linked DLL.
$builtDll = $deployDll
if (!(Test-Path $builtDll) -and (Test-Path $releaseDll)) {
    $builtDll = $releaseDll
}
if (Test-Path $builtDll) {
    if ([System.IO.Path]::GetFullPath($builtDll) -ne [System.IO.Path]::GetFullPath($deployDll)) {
        Copy-Item $builtDll $deployDll -Force
    }
    if ([System.IO.Path]::GetFullPath($builtDll) -ne [System.IO.Path]::GetFullPath($releaseDll)) {
        New-Item -ItemType Directory -Path (Split-Path $releaseDll -Parent) -Force | Out-Null
        Copy-Item $builtDll $releaseDll -Force
    }
    Copy-Item $builtDll (Join-Path $runtimeRoot 'autobbox_toolkit.dll') -Force
}

Write-Host "Build completed."
Write-Host "DLL: $Root\deploy\AutoBBoxToolkit\autobbox_toolkit.dll"
