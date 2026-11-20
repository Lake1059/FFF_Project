param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Debug",

    # 可覆盖项目默认的 .NET 11 Windows 目标框架。
    [string]$NetTFM = "",

    [switch]$SkipDependencyPreparation,

    [switch]$SkipIamf,

    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    Split-Path -Parent $MyInvocation.MyCommand.Path
} else { $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")
. (Join-Path $ScriptDirectory "Resolve-NativeDependencies.ps1")

$MSBuild = Get-MSBuildTool
$DotNet = Get-DotNetTool

$LakeUIProject = Join-Path (Split-Path -Parent $ProjectRoot) "LakeUI\LakeUI\LakeUI.vbproj"
if (-not (Test-Path -LiteralPath $LakeUIProject -PathType Leaf)) {
    throw "LakeUI project is missing at $LakeUIProject. Check out the .NET 11 LakeUI source next to 3F_Project."
}

Initialize-NativeDependencies -ProjectRoot $ProjectRoot -Configuration $Configuration `
    -SkipPreparation:$SkipDependencyPreparation -SkipIamf:$SkipIamf

$TfmArgs = @()
if (-not [string]::IsNullOrWhiteSpace($NetTFM)) { $TfmArgs += "-p:NetTFM=$NetTFM" }

$NativeArgs = @(if ($SkipIamf) { '/p:HaveLibiamf=false' } else { '/p:FFF_FORCE_LIBIAMF=true' })
& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=x64 @NativeArgs /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

& $DotNet build (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
    -c $Configuration --nologo @TfmArgs
if ($LASTEXITCODE -ne 0) { throw "FFF.Player x64 $Configuration build failed." }

if (-not $SkipTests) {
    & $DotNet build (Join-Path $ProjectRoot "FFF.Player.Tests\FFF.Player.Tests.vbproj") `
        -c $Configuration --nologo @TfmArgs
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player.Tests x64 $Configuration build failed." }

}

Write-Host "3FP $Configuration build completed with MSBuild: $MSBuild"
