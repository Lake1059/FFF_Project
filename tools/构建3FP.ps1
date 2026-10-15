param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Debug",

    [switch]$SkipDependencyPreparation,

    [switch]$SkipTests,

    # 跑"真控件 + 真计时器 + 真消息"那一档体感回归。它要开一个窗口、约 25 秒，
    # 所以默认不跑；纯函数档每次都跑（秒级、无桌面依赖）。
    [switch]$StrictZoomGate
)

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    Split-Path -Parent $MyInvocation.MyCommand.Path
} else { $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")

$MSBuild = Get-MSBuildTool
$DotNet = Get-DotNetTool

if (-not $SkipDependencyPreparation) {
    $PrepareLibassScript = Get-ChildItem -LiteralPath $ScriptDirectory -Filter "*Libass.ps1" -File |
        Select-Object -First 1
    if ($null -eq $PrepareLibassScript) { throw "The libass preparation script was not found." }
    & $PrepareLibassScript.FullName
}

$RequiredFfmpegFiles = @(
    (Join-Path $ProjectRoot "third_party\ffmpeg\include\libavcodec\avcodec.h"),
    (Join-Path $ProjectRoot "third_party\ffmpeg\lib\x64\avcodec-63.lib"),
    (Join-Path $ProjectRoot "runtime\avcodec-63.dll")
)
$MissingFfmpegFiles = @($RequiredFfmpegFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($MissingFfmpegFiles.Count -ne 0) {
    $PrepareFfmpegScript = Get-ChildItem -LiteralPath $ScriptDirectory -Filter "*FFmpeg.ps1" -File |
        Select-Object -First 1
    if ($null -eq $PrepareFfmpegScript) { throw "The FFmpeg preparation script was not found." }
    & $PrepareFfmpegScript.FullName
    $MissingFfmpegFiles = @($RequiredFfmpegFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
    if ($MissingFfmpegFiles.Count -ne 0) {
        throw "FFmpeg preparation completed without all required development files."
    }
}

# FFF.Player references the UI library by hint path
# (..\..\LakeUI\LakeUI\bin\Debug\...\LakeUI.dll). LakeUI is a sibling repository
# that is not part of this one and ships no binaries, so on a fresh machine the host
# build dies with hundreds of "undefined type LakeUI.*" errors that never name the
# real cause. Say what to do instead of letting the compiler shout.
$LakeUiRoot = Join-Path (Split-Path -Parent $ProjectRoot) "LakeUI\LakeUI\bin\Debug"
$LakeUiFound = 0
if (Test-Path -LiteralPath $LakeUiRoot) {
    $LakeUiFound = @(Get-ChildItem -LiteralPath $LakeUiRoot -Recurse -Filter "LakeUI.dll" -File).Count
}
if ($LakeUiFound -eq 0) {
    throw "LakeUI has not been built. FFF.Player hints a DLL under ..\..\LakeUI\LakeUI\bin\Debug, and LakeUI is a sibling repository that is not vendored here. Build it first: dotnet build <this repo>\..\LakeUI\LakeUI\LakeUI.vbproj -c Debug"
}

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") `
    /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration build failed." }

& $DotNet build (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
    -c $Configuration --nologo
if ($LASTEXITCODE -ne 0) { throw "FFF.Player x64 $Configuration build failed." }

if (-not $SkipTests) {
    & $DotNet build (Join-Path $ProjectRoot "FFF.Player.Tests\FFF.Player.Tests.vbproj") `
        -c $Configuration --nologo
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player.Tests x64 $Configuration build failed." }

    # ── 缩放体感门禁（docs/79 P0 / docs/81 第四节第 5 条）──────────────────────
    # 为什么挂在这里：这批判据此前只有人手跑，"写好没人调"本身就是最大的假绿源。
    # 纯函数档无桌面依赖、秒级 ⇒ 红了就 throw。
    $testsProj = Join-Path $ProjectRoot "FFF.Player.Tests\FFF.Player.Tests.vbproj"
    & $DotNet run --project $testsProj --no-build -- --image-zoom-regression
    if ($LASTEXITCODE -ne 0) {
        throw "图片缩放几何回归失败（exit $LASTEXITCODE）。判据见 3fp/docs/81 第四节。"
    }

    if ($StrictZoomGate) {
        # 真控件档在无桌面会话的环境下会自报"环境不具备"并退出 2 —— 那不算产品失败，
        # 按"能不拒就不拒"的口径只警告并说清代价，别把它变成构建链上的假红。
        & $DotNet run --project $testsProj --no-build -- --image-zoom-live 0
        switch ($LASTEXITCODE) {
            0 { Write-Host "缩放体感回归通过。" }
            1 { throw "缩放体感回归判红（exit 1）——产品缺陷，不是环境问题。" }
            2 { Write-Host "警告：体感档判定环境不具备（exit 2，多半是无桌面会话）。本轮未判。" -ForegroundColor Yellow }
            # default 以前只打警告，然后照样走到下面那句 "build completed" ——
            # 那就是假绿：exit 3 的含义是 Program.vb 自报"这个入口本树没实现"，
            # 也就是这道门**根本没跑**；负数（如 -1073741819 = 0xC0000005）是测件
            # 自己崩了。两者都不能当成"跑过了、只是没判"。白名单只留 0/1/2，
            # 其余一律失败并要求点名解释。
            default {
                throw "体感档退出码 $LASTEXITCODE 不在白名单 (0/1/2) 内 —— 3 表示入口未实现（门没跑），负数表示测件崩溃（0xC0000005 型）。这两种都不是「本轮未判」，而是这道门失效，必须查。"
            }
        }
    } else {
        Write-Host "提示：未跑真控件体感档（-StrictZoomGate）。它才覆盖计时器/停表/真消息分流/拖动跟手。" -ForegroundColor DarkGray
    }
}

Write-Host "3FP $Configuration build completed with MSBuild: $MSBuild"
