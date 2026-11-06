param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Debug",

    # 目标框架。留空 = 用播放器默认值（当前 net10.0-windows）。
    # .NET 11 正式版发布后改为：构建3FP.ps1 -NetTFM net11.0-windows10.0.26100.0
    [string]$NetTFM = "",

    # 外部商业兄弟仓 LakeUI 的目标框架。留空 = 用各工程自身默认值。
    # 宿主升到 .NET 11 而 LakeUI 还没跟上时，可单独指向它现有产物：
    #   -NetTFM net11.0-windows10.0.26100.0 -LakeUITfm net10.0-windows10.0.17763.0
    [string]$LakeUITfm = "",

    # LakeUI 产物配置。默认 Release（R2R 件：兄弟仓以 dotnet publish -p:PublishReadyToRun=true
    # 产出带本机码的预编译件）。回退旧行为（Debug IL 产物）：-LakeUIConfig Debug
    [string]$LakeUIConfig = "Release",

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

# FFF.Player references the UI library by hint path
# (..\..\LakeUI\LakeUI\bin\<LakeUIConfig>\...\LakeUI.dll, default Release = R2R artifact).
# LakeUI is a sibling repository
# that is not part of this one and ships no binaries, so on a fresh machine the host
# build dies with hundreds of "undefined type LakeUI.*" errors that never name the
# real cause. Say what to do instead of letting the compiler shout.
$LakeUiRoot = Join-Path (Split-Path -Parent $ProjectRoot) "LakeUI\LakeUI\bin\$LakeUIConfig"
[xml]$PlayerProject = [IO.File]::ReadAllText((Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj"))
$RequiredLakeUITfm = if ($LakeUITfm) { $LakeUITfm } else {
    [string]@($PlayerProject.Project.PropertyGroup.LakeUITfm | Where-Object { $_ })[0].InnerText
}
if (-not (Test-Path -LiteralPath (Join-Path $LakeUiRoot "$RequiredLakeUITfm\LakeUI.dll") -PathType Leaf)) {
    throw "LakeUI.dll is missing for $RequiredLakeUITfm under $LakeUiRoot (LakeUIConfig=$LakeUIConfig). For Release (default) the artifact must be an R2R build: dotnet publish -c Release -r win-x64 --self-contained false -p:PublishReadyToRun=true, then place the publish output's LakeUI.dll under bin\Release\<tfm>\ (a plain build output is NOT R2R). Debug fallback: dotnet build -c Debug. Or specify -LakeUIConfig / -LakeUITfm."
}

Initialize-NativeDependencies -ProjectRoot $ProjectRoot -Configuration $Configuration `
    -SkipPreparation:$SkipDependencyPreparation -SkipIamf:$SkipIamf

# TFM 参数包：留空时展开为 0 个参数，命令行与改动前逐字符一致。
$TfmArgs = @()
if (-not [string]::IsNullOrWhiteSpace($NetTFM)) { $TfmArgs += "-p:NetTFM=$NetTFM" }
if (-not [string]::IsNullOrWhiteSpace($LakeUITfm)) { $TfmArgs += "-p:LakeUITfm=$LakeUITfm" }
if (-not [string]::IsNullOrWhiteSpace($LakeUIConfig)) { $TfmArgs += "-p:LakeUIConfig=$LakeUIConfig" }

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
