param(
    [ValidateSet("Debug", "Release")][string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Logs = Join-Path $Root "artifacts\kernel-tests\logs"
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
. (Join-Path $PSScriptRoot "Resolve-Toolchain.ps1")
$MsBuild = Get-MSBuildTool
Push-Location $Root
try {
    if (-not $SkipBuild) {
        & (Join-Path $PSScriptRoot "构建3FP.ps1") -Configuration $Configuration -SkipDependencyPreparation
        if ($LASTEXITCODE -ne 0) { throw "Player build failed." }
    }
    & $MsBuild (Join-Path $Root "FFF.Player.Tests\Native\TimedTextAtlas.Tests.vcxproj") "/p:Configuration=$Configuration" /p:Platform=x64 /m /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "GPU pixel test build failed." }
    & (Join-Path $Root "FFF.Player.Tests\Native\bin\$Configuration\TimedTextAtlas.Tests.exe") *> (Join-Path $Logs "danmaku-pixels-$Configuration.log")
    $PixelExitCode = $LASTEXITCODE
    Get-Content (Join-Path $Logs "danmaku-pixels-$Configuration.log")
    if ($PixelExitCode -ne 0) { throw "GPU pixel regression failed." }
    $ManagedTest = Join-Path $Root "FFF.Player.Tests\bin\$Configuration\net10.0-windows10.0.26100.0\FFF.Player.Tests.exe"
    foreach ($Suite in @("timed-text", "overlay-color")) {
        & $ManagedTest "--$Suite-regression" *> (Join-Path $Logs "danmaku-$Suite-$Configuration.log")
        $TestExitCode = $LASTEXITCODE
        Get-Content (Join-Path $Logs "danmaku-$Suite-$Configuration.log")
        if ($TestExitCode -ne 0) { throw "$Suite regression failed." }
    }
} finally {
    Pop-Location
}
