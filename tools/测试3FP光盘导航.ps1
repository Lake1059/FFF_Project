param(
    [Parameter(Mandatory = $true)][string]$DvdIso,
    [Parameter(Mandatory = $true)][string]$BlurayPath,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")
if (-not $SkipBuild) {
    $MSBuild = Get-MSBuildTool
    & $MSBuild (Join-Path $ProjectRoot "FFF.Player.Tests\Native\DiscNavigationProbe.vcxproj") /p:Configuration=Release /p:Platform=x64 /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Disc navigation probe build failed." }
}
$Probe = Join-Path $ProjectRoot "artifacts\disc-probe\DiscNavigationProbe.exe"
$OriginalPath = $env:PATH
try {
    $env:PATH = (Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\bin") + ";" + $env:PATH
    & $Probe dvd $DvdIso 2>&1 | Tee-Object -FilePath (Join-Path $ProjectRoot "artifacts\disc-probe\dvd.log")
    $DvdExit = $LASTEXITCODE
    & $Probe bluray $BlurayPath 2>&1 | Tee-Object -FilePath (Join-Path $ProjectRoot "artifacts\disc-probe\bluray.log")
    $BlurayExit = $LASTEXITCODE
    if ($DvdExit -ne 0 -or $BlurayExit -ne 0) {
        throw "Navigation verification failed: DVD=$DvdExit, Blu-ray=$BlurayExit. See artifacts/disc-probe."
    }
}
finally { $env:PATH = $OriginalPath }
