param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDirectory = "",
    # 目标框架。留空 = 用播放器默认值（当前 net10.0-windows）。
    # .NET 11 正式版发布后改为：发布3FP.ps1 -NetTFM net11.0-windows10.0.26100.0
    [string]$NetTFM = "",
    # 外部商业兄弟仓 LakeUI 的目标框架，可与宿主不同（混血状态会出警告但可工作）。
    [string]$LakeUITfm = "",
    # 跨产品框架配对：默认拒绝不同 .NET 运行时版本，见 Resolve-Toolchain.ps1。
    [switch]$AllowFrameworkMismatch,
    # IAMF 空间音频：默认强制携带（缺料即报错，而非静默编译成无 IAMF 内核）。
    # 体积敏感/无准备环境用 -SkipIamf 显式放弃该能力（docs/105 §十三/§十四）。
    [switch]$SkipIamf,
    [switch]$SkipDependencyPreparation,
    # 默认生成本体包：不检查、不复制 FFmpeg 外部 runtime。
    # 需要把外部 runtime 一起打包时显式使用 -IncludeExternalRuntime；
    # 该模式属于“完整运行时包”，必须单独按 FFmpeg 自身许可合规。
    [switch]$IncludeExternalRuntime
)

# 传统散装发布：产出「FFF.Player.exe + 全部依赖 DLL 同目录」的目录形态。
# 与 发布3FP单文件.ps1 的区别：
#   - 不做 PublishSingleFile ⇒ exe 只有 ~0.3 MB，其余是同目录的 DLL（可增量替换、便于排查）
#   - 不内置 .NET 运行时（--self-contained false，需本机装 .NET 10 桌面运行时）
#   - **默认不生成本体外的 FFmpeg runtime**：BodyOnly=true 时 CopyProductNativeToPublish
#     不复制 DevelopmentRuntime；仅 -IncludeExternalRuntime 才生成完整运行时包
#   - R2R 经两次独立评估后按"体积优先"关闭（vbproj 里是注释态，勿被旧注释误导）。

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
. (Join-Path $ScriptDirectory "Resolve-NativeDependencies.ps1")
Initialize-NativeDependencies -ProjectRoot $ProjectRoot -Configuration $Configuration `
    -SkipPreparation:$SkipDependencyPreparation -SkipIamf:$SkipIamf

$NativeArgs = @()
if ($SkipIamf) { $NativeArgs += "/p:HaveLibiamf=false" } else { $NativeArgs += "/p:FFF_FORCE_LIBIAMF=true" }

& $MSBuild (Join-Path $ProjectRoot "FFF.Native\FFF.Native.vcxproj") /p:Configuration=$Configuration /p:Platform=x64 @NativeArgs /m /v:minimal
if ($LASTEXITCODE -ne 0) { throw "FFF.Native x64 $Configuration 构建失败。" }

# 前后预加载小助手是独立 C++ 进程，必须与内核一起构建：否则 vbproj 的
# Exists 条件会跳过拷贝，下面 $Required 的散装校验就会自相矛盾地判红。
New-Item -ItemType Directory -Force -Path (Join-Path $ProjectRoot "..\build") | Out-Null
$global:LASTEXITCODE = $null
& $MSBuild (Join-Path $ProjectRoot "FFF.PrefetchHost\FFF.PrefetchHost.vcxproj") /p:Configuration=$Configuration /p:Platform=x64 /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw "FFF.PrefetchHost x64 $Configuration 构建失败。" }

$StagingDirectory = Join-Path ([IO.Path]::GetTempPath()) ("3FP-publish-loose-" + [Guid]::NewGuid().ToString("N"))
try {
    New-Item -ItemType Directory -Path $StagingDirectory | Out-Null
    # TFM 参数包：留空时展开为 0 个参数，命令行与改动前逐字符一致。
    $TfmArgs = @()
    if (-not [string]::IsNullOrWhiteSpace($NetTFM)) { $TfmArgs += "-p:NetTFM=$NetTFM" }
    if (-not [string]::IsNullOrWhiteSpace($LakeUITfm)) { $TfmArgs += "-p:LakeUITfm=$LakeUITfm" }
    $BodyOnlyArgs = if ($IncludeExternalRuntime) { "-p:BodyOnly=false" } else { "-p:BodyOnly=true" }
    & $DotNet publish (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -c $Configuration -r win-x64 --self-contained false -o $StagingDirectory `
        -p:DebugType=None -p:DebugSymbols=false $BodyOnlyArgs @TfmArgs
    if ($LASTEXITCODE -ne 0) { throw "FFF.Player 散装发布失败。" }

    # 确认运行依赖齐全后，再替换发布目录。
    # 本体包不包含外部 FFmpeg runtime；完整包才要求 av*.dll。
    $Required = @(
        'FFF.Player.exe', 'FFF.Native.dll',
        'ass-9.dll', 'bluray-3.dll', 'udfread-3.dll',
        'libxml2.dll', 'iconv-2.dll', 'charset-1.dll',
        'FFF.PrefetchHost.exe'
    )
    if ($IncludeExternalRuntime) {
        $Required += @('avcodec-63.dll', 'avformat-63.dll', 'avutil-61.dll',
                       'avfilter-12.dll', 'swresample-7.dll', 'swscale-10.dll')
    }
    $Missing = @($Required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $StagingDirectory $_) -PathType Leaf) })
    if ($Missing) { throw "发布产物缺少文件：$($Missing -join ', ')" }

    if (-not $IncludeExternalRuntime) {
        $UnexpectedFfmpeg = @(Get-ChildItem -LiteralPath $StagingDirectory -File -Filter 'av*.dll' -ErrorAction SilentlyContinue)
        if ($UnexpectedFfmpeg.Count -gt 0) {
            throw "本体包出现外部 FFmpeg DLL：$($UnexpectedFfmpeg.Name -join ', ')。请勿把开发输出或完整运行时包当成本体包；如确需完整包，请使用 -IncludeExternalRuntime。"
        }
    }
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
    $LicenseCandidates = @(
        (Join-Path $ProjectRoot "LICENSE.txt"),
        (Join-Path ([IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))) "LICENSE.txt"))
    $LicenseSrc = @($LicenseCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($LicenseSrc.Count -eq 0) { throw ("没有 LICENSE.txt（找过：$($LicenseCandidates -join '；')）—— 第一方 MIT 全文缺席即不发。") }
    Copy-Item -LiteralPath $LicenseSrc[0] -Destination (Join-Path $StagingDirectory "LICENSE.txt") -Force
    Write-Host "   ✅ 第一方 MIT 许可已入包：$($LicenseSrc[0])" -ForegroundColor Green
    $NoticesCandidates = @(
        (Join-Path $ProjectRoot "THIRD-PARTY-NOTICES.md"),
        (Join-Path ([IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))) "THIRD-PARTY-NOTICES.md"))
    $NoticesSrc = @($NoticesCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($NoticesSrc.Count -eq 0) {
        throw ("没有 THIRD-PARTY-NOTICES.md（找过：$($NoticesCandidates -join '；')）—— " +
               "本包自带 FFmpeg/libass 一族/LakeUI 等第三方件，清单缺席即不发。")
    }
    Copy-Item -LiteralPath $NoticesSrc[0] -Destination $StagingDirectory -Force
    $NoticeCandidates = @(
        (Join-Path $ProjectRoot "NOTICE"),
        (Join-Path ([IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))) "NOTICE"))
    $NoticeSrc = @($NoticeCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($NoticeSrc.Count -eq 0) {
        throw ("没有 NOTICE（找过：$($NoticeCandidates -join '；')）—— 版权归属声明缺席即不发。")
    }
    Copy-Item -LiteralPath $NoticeSrc[0] -Destination (Join-Path $StagingDirectory "NOTICE") -Force
    $NoticeText = [IO.File]::ReadAllText($NoticeSrc[0])
    if ($NoticeText.Length -lt 200) { throw "NOTICE 不像有效归属声明（$($NoticeText.Length) 字符）：$($NoticeSrc[0])" }
    if (-not (Test-Path -LiteralPath (Join-Path $StagingDirectory "THIRD-PARTY-NOTICES.md") -PathType Leaf)) {
        throw "THIRD-PARTY-NOTICES.md 未进入 staging，版权/第三方清单不完整。"
    }
    Write-Host "   ✅ 版权归属 NOTICE 已入包：$($NoticeSrc[0])" -ForegroundColor Green
    $LicensingCandidates = @(
        (Join-Path $ProjectRoot "LICENSING.md"),
        (Join-Path ([IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))) "LICENSING.md"))
    $LicensingSrc = @($LicensingCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($LicensingSrc.Count -eq 0) { throw ("没有 LICENSING.md（找过：$($LicensingCandidates -join '；')）—— 归属说明缺席即不发。") }
    Copy-Item -LiteralPath $LicensingSrc[0] -Destination (Join-Path $StagingDirectory "LICENSING.md") -Force
    Write-Host "   ✅ 许可/贡献说明已入包：$($LicensingSrc[0])" -ForegroundColor Green
    $NoticesText = [IO.File]::ReadAllText($NoticesSrc[0])
    if ($NoticesText.Length -lt 400 -or -not $NoticesText.Contains('--enable-nonfree')) {
        throw "包内清单不像实测文件（$($NoticesText.Length) 字符 / 未登记 nonfree 构建串）：$($NoticesSrc[0])"
    }
    Write-Host "   ✅ 第三方清单已入包：$($NoticesSrc[0])" -ForegroundColor Green

    $AssetsCandidates = @(
        (Join-Path $ProjectRoot "ASSETS.md"),
        (Join-Path ([IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))) "ASSETS.md"))
    $AssetsSrc = @($AssetsCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($AssetsSrc.Count -eq 0) { throw ("没有 ASSETS.md（找过：$($AssetsCandidates -join '；')）—— 美术资产来源清单缺席即不发。") }
    Copy-Item -LiteralPath $AssetsSrc[0] -Destination (Join-Path $StagingDirectory "ASSETS.md") -Force
    Write-Host "   ✅ 美术资产来源清单已入包：$($AssetsSrc[0])" -ForegroundColor Green

    $LicPorts = @('libass','libbluray','libudfread','libiconv','fribidi','freetype',
                  'harfbuzz','brotli','bzip2','libpng','zlib','libxml2')
    $vcpkgShare = Join-Path $ProjectRoot "third_party\vcpkg_installed\x64-windows\share"
    $LicStage = Join-Path $StagingDirectory "licenses"
    $LocalLicDir = Join-Path $ProjectRoot "licenses"
    New-Item -ItemType Directory -Force -Path $LicStage | Out-Null
    $licCopied = @(); $licMissing = @()
    $LicFallbacks = @{
        'libass' = 'vcpkg-libass.txt'
        'libbluray' = 'vcpkg-libbluray.txt'
        'libudfread' = 'vcpkg-libudfread.txt'
        'libiconv' = 'vcpkg-libiconv.txt'
        'libxml2' = 'vcpkg-libxml2.txt'
    }
    foreach ($p in $LicPorts) {
        $s = Join-Path $vcpkgShare "$p\copyright"
        $dst = Join-Path $LicStage "vcpkg-$p.txt"
        if ((Test-Path -LiteralPath $s -PathType Leaf) -and ((Get-Item -LiteralPath $s).Length -gt 0)) {
            Copy-Item -LiteralPath $s -Destination $dst -Force
            $licCopied += "vcpkg-$p.txt"
            continue
        }
        $fallbackName = $LicFallbacks[$p]
        $fallback = if ($fallbackName) { Join-Path $LocalLicDir $fallbackName } else { $null }
        if ($fallback -and (Test-Path -LiteralPath $fallback -PathType Leaf) -and ((Get-Item -LiteralPath $fallback).Length -gt 0)) {
            Copy-Item -LiteralPath $fallback -Destination $dst -Force
            $licCopied += "vcpkg-$p.txt"
        } else {
            $licMissing += "vcpkg-$p（vcpkg copyright 为空或缺失，且无回退文本）"
        }
    }
    # 每条许可文本给多个候选位置（本仓是独立 git 树，LakeUI 与 ffmpeg 既可能挂在本仓
    # third_party 下、也可能只在主仓那一侧），并把"找过哪些路径"写进失败消息 ——
    # 缺件要能说清是"没有"还是"我找错地方"。
    $MainRoot = [IO.Path]::GetFullPath((Join-Path $ProjectRoot "..\.."))
    $LicExtra = @(
        [pscustomobject]@{ Name = 'LakeUI-GPL-3.0';
                           Cands = @((Join-Path $ProjectRoot "third_party\LakeUI\LICENSE.txt"),
                                     (Join-Path $MainRoot "third_party\LakeUI\LICENSE.txt"),
                                     (Join-Path $ProjectRoot "..\LakeUI\LICENSE.txt")) })
    if ($IncludeExternalRuntime) {
        $LicExtra += @(
            [pscustomobject]@{ Name = 'ffmpeg-COPYING.LGPLv2.1';
                               Cands = @((Join-Path $ProjectRoot "third_party\ffmpeg\COPYING.LGPLv2.1"),
                                         (Join-Path $MainRoot "third_party\ffmpeg\COPYING.LGPLv2.1")) },
            [pscustomobject]@{ Name = 'ffmpeg-COPYING.LGPLv3';
                               Cands = @((Join-Path $ProjectRoot "third_party\ffmpeg\COPYING.LGPLv3"),
                                         (Join-Path $MainRoot "third_party\ffmpeg\COPYING.LGPLv3")) })
    }
    foreach ($e in $LicExtra) {
        $hit = @($e.Cands | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
        if ($hit.Count -gt 0) {
            Copy-Item -LiteralPath $hit[0] -Destination (Join-Path $LicStage "$($e.Name).txt") -Force
            $licCopied += "$($e.Name).txt"
        } else { $licMissing += "$($e.Name)（找过：$($e.Cands -join '；')）" }
    }
    # 本仓 licenses/ 下的静态库/专利/说明文本；AI 子目录单独递归，随完整包保留，
    # 便于用户在同一包内看到 AI 扩展包的许可原文。
    $LocalLicFiles = @(Get-ChildItem -LiteralPath $LocalLicDir -File -ErrorAction SilentlyContinue)
    foreach ($f in $LocalLicFiles) {
        Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $LicStage $f.Name) -Force
        $licCopied += $f.Name
    }
    $AiLicDir = Join-Path $LocalLicDir "ai"
    $AiLicFiles = @(Get-ChildItem -LiteralPath $AiLicDir -File -ErrorAction SilentlyContinue)
    if ($AiLicFiles.Count -gt 0) {
        $AiStage = Join-Path $LicStage "ai"
        New-Item -ItemType Directory -Force -Path $AiStage | Out-Null
        foreach ($f in $AiLicFiles) {
            Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $AiStage $f.Name) -Force
            $licCopied += "ai/$($f.Name)"
        }
    }
    # 以目标相对路径去重计数：本地回退件与 vcpkg copyright 可能写同一个 vcpkg-*.txt，
    # 不能按“复制次数”算成全量，否则报告会出现 45/45 但目录只有 40 件的假通过。
    $licExpected = @()
    $licExpected += @($LicPorts | ForEach-Object { "vcpkg-$_.txt" })
    $licExpected += @($LicExtra | ForEach-Object { "$($_.Name).txt" })
    $licExpected += @($LocalLicFiles | ForEach-Object { $_.Name })
    $licExpected += @($AiLicFiles | ForEach-Object { "ai/$($_.Name)" })
    $licWant = @($licExpected | Sort-Object -Unique).Count
    $licCopiedCount = @($licCopied | Sort-Object -Unique).Count
    if ($licMissing.Count -gt 0) {
        $msg = "许可文本缺 $($licMissing.Count)/$licWant 件：$($licMissing -join '；')"
        if ($env:FFF_ALLOW_MISSING_LICENSES -eq '1') {
            Write-Host "   ⚠ 已放行：$msg" -ForegroundColor Yellow
        } else { throw "$msg —— 分发第三方件却不附许可全文；确认可接受后再设 FFF_ALLOW_MISSING_LICENSES=1" }
    }
    $licEmpty = @(Get-ChildItem -LiteralPath $LicStage -File -Recurse | Where-Object { $_.Length -eq 0 })
    if ($licEmpty.Count -gt 0) { throw "licenses\ 里有 $($licEmpty.Count) 个 0 字节文件：$(($licEmpty | ForEach-Object { $_.Name }) -join ', ')" }
    if ($licMissing.Count -eq 0) {
        Write-Host "   ✅ 许可文本已入包 licenses/（$licCopiedCount/$licWant 件）" -ForegroundColor Green
    } else {
        Write-Host "   ⚠ 许可文本只入包 $licCopiedCount/$licWant 件（放行缺 $($licMissing.Count) 件）" -ForegroundColor Yellow
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

    $PlayerFramework = Get-ProductTargetFramework -ProjectPath (Join-Path $ProjectRoot "FFF.Player\FFF.Player.vbproj") `
        -NetTFM $NetTFM -DotNet $DotNet
    # 配对护栏必须先于删目录跑：账本（.product-frameworks.txt）就躺在输出目录里，
    # 先删后查永远读到空账，跨产品框架配对在这条发布路径上就形同虚设。
    # 先在对家旧账还在时判一次（能拦住），重建目录后再记一次新账（账已随目录清空，不会自冲突）。
    Assert-ProductFrameworkPair -OutputDirectory $OutputDirectory -Product "FFF.Player" `
        -Framework $PlayerFramework -AllowMismatch:$AllowFrameworkMismatch
    if (Test-Path -LiteralPath $OutputDirectory) {
        Remove-Item -LiteralPath $OutputDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    Assert-ProductFrameworkPair -OutputDirectory $OutputDirectory -Product "FFF.Player" `
        -Framework $PlayerFramework -AllowMismatch:$AllowFrameworkMismatch

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
