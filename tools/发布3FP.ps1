param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = ""
)

# 传统散装发布：产出「FFF.Player.exe + 全部依赖 DLL 同目录」的目录形态。
# 与 发布3FP单文件.ps1 的区别：
#   - 不做 PublishSingleFile ⇒ exe 只有 ~0.3 MB，其余是同目录的 DLL（可增量替换、便于排查）
#   - 不内置 .NET 运行时（--self-contained false，需本机装 .NET 10 桌面运行时）
#   - **自带 FFmpeg**：非单文件发布时 CopyProductNativeToPublish 会连 DevelopmentRuntime 一起拷
#     （单文件模式不拷，见 FFF.Player.vbproj 里的 Condition）
#   - R2R 经两次独立评估后按"体积优先"关闭（vbproj 里是注释态），勿被旧注释误导。

$ErrorActionPreference = "Stop"
$ScriptDirectory = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) { Split-Path -Parent $MyInvocation.MyCommand.Path } else { $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDirectory ".."))
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot "publish\win-x64-散装"
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $ProjectRoot $OutputDirectory
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath (Join-Path $OutputDirectory "FFF.DolbyVision.Test.dll")) {
    throw "发布目录含旧商标私有 DLL（FFF.DolbyVision.Test.dll），请使用干净的发布目录；中性模块请使用 FFF.HDRdp.Test.dll。"
}
. (Join-Path $ScriptDirectory "Resolve-Toolchain.ps1")
$MSBuild = Get-MSBuildTool
$DotNet = Get-DotNetTool
$LibassReadyMarker = Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\share\3f-project\libass-ready.txt"
if (-not (Test-Path -LiteralPath $LibassReadyMarker -PathType Leaf)) {
    throw "缺少 libass，请先运行 tools\准备Libass.ps1。"
}
if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\lib\bluray.lib") -PathType Leaf)) {
    throw "缺少光盘库，请先运行 tools\准备光盘库.ps1。"
}

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration 构建失败。" }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-publish-loose-" + [Guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null
    & $DotNet publish (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:DebugType=None -p:DebugSymbols=false
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player 散装发布失败。" }

    # 确认运行依赖齐全后，再替换发布目录。
    # 为什么要校验：散装形态必须自带这些，否则宿主会走「核心文件缺失」早退路径。
    $Required = @(
        'FFF.Player.exe', 'FFF.Native.dll',
        'avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll',
        'avfilter-12.dll', 'swresample-7.dll', 'swscale-10.dll',
        'ass-9.dll', 'bluray-3.dll', 'udfread-3.dll',
        'libxml2.dll', 'iconv-2.dll', 'charset-1.dll'
    )
    $Missing = @($Required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $StagingDirectory $_) -PathType Leaf) })
    if ($Missing) { throw "发布产物缺少文件：$($Missing -join ', ')" }

    # 侵权护栏（tripwire）：旧商标私有 DLL 不得随包发布。2026-10-14 恢复——
    # 中性模块 FFF.HDRdp.Test.dll 自此随包发布，"旧名不在树里=>护栏无用"的判断随之反转。
    # 散装发布是 dotnet publish + 本脚本 Copy 两步，vbproj 的
    # RejectPrivateColorExtensionPublish 只覆盖 MSBuild 自己解析到的文件，
    # 手工误放入 staging 的件必须在脚本层拦死。
    $ForbiddenLegacy = @(Get-ChildItem -LiteralPath $StagingDirectory -Filter "FFF.DolbyVision.Test.dll" -File -ErrorAction SilentlyContinue)
    if ($ForbiddenLegacy.Count -gt 0) {
        throw "侵权护栏：staging 目录出现旧商标私有 DLL FFF.DolbyVision.Test.dll（$($ForbiddenLegacy.FullName -join ', ')）——必须移除；中性模块请使用 FFF.HDRdp.Test.dll。"
    }

    # ── 许可随包步骤（与主仓 pack.ps1 的 F4b/F10/F4a 同一口径）──
    # 分发第三方件却不附清单与许可全文 = 义务没落地。这一步把"有没有"变成有牙的门：
    # 缺哪件、为什么缺，逐件点名，不许静默少收；放行必须显式设环境变量。
    $NoticesCandidates = @(
        (Join-Path $ProjectRoot "THIRD-PARTY-NOTICES.md"),
        (Join-Path ([IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))) "THIRD-PARTY-NOTICES.md"))
    $NoticesSrc = @($NoticesCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($NoticesSrc.Count -eq 0) {
        throw ("没有 THIRD-PARTY-NOTICES.md（找过：$($NoticesCandidates -join '；')）—— " +
               "本包自带 FFmpeg/libass 一族/LakeUI 等第三方件，清单缺席即不发。")
    }
    Copy-Item -LiteralPath $NoticesSrc[0] -Destination $StagingDirectory -Force
    $NoticesText = [IO.File]::ReadAllText($NoticesSrc[0])
    if ($NoticesText.Length -lt 400 -or -not $NoticesText.Contains('--enable-nonfree')) {
        throw "包内清单不像实测文件（$($NoticesText.Length) 字符 / 未登记 nonfree 构建串）：$($NoticesSrc[0])"
    }
    Write-Host "   ✅ 第三方清单已入包：$($NoticesSrc[0])" -ForegroundColor Green

    $LicPorts = @('libass','libbluray','libudfread','libiconv','fribidi','freetype',
                  'harfbuzz','brotli','bzip2','libpng','zlib','libxml2')
    $vcpkgShare = Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\share"
    $LicStage = Join-Path $StagingDirectory "licenses"
    New-Item -ItemType Directory -Force -Path $LicStage | Out-Null
    $licCopied = @(); $licMissing = @()
    foreach ($p in $LicPorts) {
        $s = Join-Path $vcpkgShare "$p\copyright"
        if (Test-Path -LiteralPath $s -PathType Leaf) {
            Copy-Item -LiteralPath $s -Destination (Join-Path $LicStage "vcpkg-$p.txt") -Force
            $licCopied += "vcpkg-$p"
        } else { $licMissing += "vcpkg-$p" }
    }
    # 每条许可文本给多个候选位置（本仓是独立 git 树，LakeUI 与 ffmpeg 既可能挂在本仓
    # third_party 下、也可能只在主仓那一侧），并把"找过哪些路径"写进失败消息 ——
    # 缺件要能说清是"没有"还是"我找错地方"。
    $MainRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))
    $LicExtra = @(
        [pscustomobject]@{ Name = 'ffmpeg-COPYING.LGPLv2.1';
                           Cands = @((Join-Path $ProjectRoot "third_party\ffmpeg\COPYING.LGPLv2.1"),
                                     (Join-Path $MainRoot "third_party\ffmpeg\COPYING.LGPLv2.1")) },
        [pscustomobject]@{ Name = 'ffmpeg-COPYING.LGPLv3';
                           Cands = @((Join-Path $ProjectRoot "third_party\ffmpeg\COPYING.LGPLv3"),
                                     (Join-Path $MainRoot "third_party\ffmpeg\COPYING.LGPLv3")) },
        [pscustomobject]@{ Name = 'LakeUI-GPL-3.0';
                           Cands = @((Join-Path $ProjectRoot "third_party\LakeUI\LICENSE.txt"),
                                     (Join-Path $MainRoot "third_party\LakeUI\LICENSE.txt"),
                                     (Join-Path $ProjectRoot "..\LakeUI\LICENSE.txt")) })
    foreach ($e in $LicExtra) {
        $hit = @($e.Cands | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
        if ($hit.Count -gt 0) {
            Copy-Item -LiteralPath $hit[0] -Destination (Join-Path $LicStage "$($e.Name).txt") -Force
            $licCopied += $e.Name
        } else { $licMissing += "$($e.Name)（找过：$($e.Cands -join '；')）" }
    }
    $licWant = $LicPorts.Count + 3
    if ($licMissing.Count -gt 0) {
        $msg = "许可文本缺 $($licMissing.Count)/$licWant 件：$($licMissing -join '；')"
        if ($env:FFF_ALLOW_MISSING_LICENSES -eq '1') {
            Write-Host "   ⚠ 已放行：$msg" -ForegroundColor Yellow
        } else { throw "$msg —— 分发第三方件却不附许可全文；确认可接受后再设 FFF_ALLOW_MISSING_LICENSES=1" }
    }
    $licEmpty = @(Get-ChildItem -LiteralPath $LicStage -Filter *.txt -File | Where-Object { $_.Length -eq 0 })
    if ($licEmpty.Count -gt 0) { throw "licenses\ 里有 $($licEmpty.Count) 个 0 字节文件：$(($licEmpty | ForEach-Object { $_.Name }) -join ', ')" }
    if ($licMissing.Count -eq 0) {
        Write-Host "   ✅ 许可文本已入包 licenses/（$($licCopied.Count)/$licWant 件）" -ForegroundColor Green
    } else {
        Write-Host "   ⚠ 许可文本只入包 $($licCopied.Count)/$licWant 件（放行缺 $($licMissing.Count) 件）" -ForegroundColor Yellow
    }

    # FFmpeg 变体判定：件数对≠变体对。判据脚本住在主仓 tools\ 下（本仓独立使用时可能没有），
    # 拿不到判据就明说"未执行"，绝不把"没跑"写成"通过"。判红同主仓：默认中止，显式放行才继续。
    $variantCandidates = @(
        (Join-Path ([IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))) "tools\check_ffmpeg_variant.py"),
        (Join-Path $ScriptDirectory "check_ffmpeg_variant.py"))
    $variantPy = @($variantCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    $pyExe = $null
    foreach ($cand in @('py', 'python')) {
        $g = Get-Command $cand -CommandType Application -ErrorAction SilentlyContinue
        if ($g -and $g.Source -and ($g.Source -notmatch '(?i)[\\/]WindowsApps[\\/]')) { $pyExe = $g.Source; break }
    }
    if ($variantPy.Count -eq 0) {
        Write-Host "   ⚠ FFmpeg 变体判定**未执行**：找不到 tools\check_ffmpeg_variant.py（找过：$($variantCandidates -join '；')）——别把“发布完成”当“分发条件已核”" -ForegroundColor Yellow
    } elseif (-not $pyExe) {
        Write-Host "   ⚠ FFmpeg 变体判定**未执行**：本机没有可用的 python（py/python 都不在 PATH 或只有应用执行别名占位桩）" -ForegroundColor Yellow
    } else {
        $prevEAP = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $global:LASTEXITCODE = $null
        & $pyExe $variantPy[0] '--self-test' | Out-Null
        $stRc = if ($null -eq $LASTEXITCODE) { -1 } else { $LASTEXITCODE }
        $ErrorActionPreference = $prevEAP
        if ($stRc -ne 0) { throw "FFmpeg 变体自检失败（rc=$stRc）：探针扫不出合成件里的 nonfree ⇒ 判据本身不可信" }
        $prevEAP = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        $global:LASTEXITCODE = $null
        $varOut = @(& $pyExe $variantPy[0] '--dir' $StagingDirectory '--quiet' 2>&1 | ForEach-Object { $_.ToString() })
        $varRc = if ($null -eq $LASTEXITCODE) { -1 } else { $LASTEXITCODE }
        $ErrorActionPreference = $prevEAP
        $tail = ($varOut | Select-Object -Last 3) -join ' / '
        if ($varRc -eq 0) { Write-Host "   ✅ FFmpeg 变体已核：无 --enable-nonfree。$tail" -ForegroundColor Green }
        elseif ($varRc -eq 2) { Write-Host "   ⚠ FFmpeg 变体**未判定**：进包件里一个构建串都没读到 —— 判定未执行，不是通过。$tail" -ForegroundColor Yellow }
        elseif ($varRc -eq 1) {
            if ($env:FFF_ALLOW_NONFREE -eq '1') { Write-Host "   ⚠ 已放行 nonfree 构建（FFF_ALLOW_NONFREE=1）：分发条件最严的一档，清单 §1 的 GPL 文本/源码要约必须按此补齐。$tail" -ForegroundColor Yellow }
            else { throw "FFmpeg 变体判红：进包的 DLL 含 --enable-nonfree。换无 nonfree 的构建，或明确承担后果后设 FFF_ALLOW_NONFREE=1 再发布。$tail" }
        } else { throw "FFmpeg 变体门禁退出码异常（rc=$varRc）⇒ 守卫不成立，中止：$tail" }
    }

    if (Test-Path -LiteralPath $OutputDirectory) {
        Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    Get-ChildItem -LiteralPath $StagingDirectory -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $OutputDirectory -Recurse -Force
    }

    $Files = @(Get-ChildItem -LiteralPath $OutputDirectory -File)
    $Bytes = ($Files | Measure-Object -Property Length -Sum).Sum
    Write-Host ("散装发布完成：{0}（{1} 个文件，{2:N1} MB）" -f $OutputDirectory, $Files.Count, ($Bytes / 1MB))
}
finally {
    if (Test-Path -LiteralPath $StagingDirectory) { Remove-Item -LiteralPath $StagingDirectory -Recurse -Force }
}
