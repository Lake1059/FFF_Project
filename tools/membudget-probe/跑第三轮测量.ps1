# 第三轮实机测量：预取命中率（图片目录）/ 音频拒绝压力 / 限额抖动压测
#
# 为什么有它：这三组指标只有真正窗口化播放才会出现。脚本把 membudget-probe 在
# 不同压力臂（FFF_AUDIO_CAP_KB / FFF3FP_MEMARB_CAP_MB 梯度）下各跑一遍，从 stdout
# 与内核 trace 里取指标，按阈值判定，写三份 md 报告。
#
# 用法：
#   pwsh -File 跑第三轮测量.ps1                        # 默认三臂全跑
#   pwsh -File 跑第三轮测量.ps1 -WhatIf                # 只打印将要执行的命令，不跑
#   pwsh -File 跑第三轮测量.ps1 -Tag smoke -AudioSeconds 5 -VideoSeconds 8 -ImagesMax 4
#
# 判定口径（改阈值改这里）：
#   (a) 图片命中：hitrate = Δhits/(Δhits+Δmisses)，目标 > 0.5；lru1/trim1 记录是否非 0
#       （预算够时应为 0；非 0 说明 still lane 真的顶到了预算——是好事，证明压测有压力）。
#   (b) 音频压力：audioRejected 应随 cap 收紧而上升（拒绝被触发），但全程不得出现
#       state=Failed；underruns 在 0 KiB（不设 cap）臂应为 0。
#   (c) 抖动压测：重算频率 recompute <= floor(秒数/2)（< 1 次/2 s）视为抑制有效；
#       队列 queuedMax-queuedMin <= 4 帧；trace 里出现"预算数字升一档后又降"的方向
#       反转即记为 flap 候选（trace 行无时间戳，按日志顺序近似 4 s 窗口）。
param(
    [string]$Media = 'C:\PLAN\3FCompare\3fp\build\_audio_6ch_192k.mkv',
    [string]$ImagesDir = 'C:\PLAN\3FCompare\3fp\build\_prefetch_imgs',
    [int]$ImagesMax = 40,
    [int]$AudioSeconds = 20,
    [int]$VideoSeconds = 60,
    [int[]]$AudioCapGradients = @(0, 8, 16, 32),
    [int[]]$CapMBGradients = @(0, 96, 160, 256),
    [string]$Tag = 'r3',
    [switch]$WhatIf,
    [string]$OutDir = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$probeDir = $PSScriptRoot
$exe = Join-Path $probeDir 'membudget-probe.exe'
$kernel = Split-Path (Split-Path $probeDir -Parent) -Parent
$buildDir = Join-Path (Split-Path $kernel -Parent) 'build'   # 依赖 DLL 所在目录

if (-not (Test-Path -LiteralPath $exe)) { throw "探针未编译：$exe" }
if (-not (Test-Path -LiteralPath $buildDir)) { throw "build 目录不存在：$buildDir" }

function Reset-ArmEnv {
    # env 会跨进程残留：每臂开始前把两个压力开关都清掉，再由各臂自己设。
    Remove-Item Env:FFF3FP_MEMARB_CAP_MB -ErrorAction SilentlyContinue
    Remove-Item Env:FFF_AUDIO_CAP_KB -ErrorAction SilentlyContinue
    $env:FFF3FP_MEMARB = '1'
    $env:FFF_MEMARB_TRACE = '1'
    $env:FFF_LADDER_TRACE = '1'
}

# 跑一臂（WhatIf 时只打印命令）。返回 [pscustomobject]@{ Name; Log; Exit; Cmd }。
function Invoke-ProbeArm {
    param([string]$Name, [string[]]$ProbeArgs, [string]$LogName)
    $envDesc = @(
        "FFF3FP_MEMARB=$env:FFF3FP_MEMARB"
        if ($env:FFF3FP_MEMARB_CAP_MB) { "FFF3FP_MEMARB_CAP_MB=$env:FFF3FP_MEMARB_CAP_MB" }
        if ($env:FFF_AUDIO_CAP_KB) { "FFF_AUDIO_CAP_KB=$env:FFF_AUDIO_CAP_KB" }
    ) -join ' '
    $log = Join-Path $OutDir $LogName
    $cmdText = "cd $buildDir && $envDesc && & $exe $($ProbeArgs -join ' ') 2>&1 | Tee-Object $log"
    Write-Host "[$Name] $cmdText"
    if ($WhatIf) {
        return [pscustomobject]@{ Name = $Name; Log = $log; Exit = -1; Cmd = $cmdText; WhatIf = $true }
    }
    Push-Location $buildDir
    & $exe @ProbeArgs 2>&1 | Tee-Object -FilePath $log | Out-Null
    $code = $LASTEXITCODE
    Pop-Location
    return [pscustomobject]@{ Name = $Name; Log = $log; Exit = $code; Cmd = $cmdText; WhatIf = $false }
}

# ── 解析单个日志 ─────────────────────────────────────────────────────────────
function Parse-ProbeLog {
    param([string]$Path)
    $text = Get-Content -LiteralPath $Path
    $envLine = ($text | Select-String -Pattern '^\[probe\] env ' | Select-Object -First 1).Line
    $summary = ($text | Select-String -Pattern '^SUMMARY ' | Select-Object -First 1).Line
    $audioSum = ($text | Select-String -Pattern '^summary-audio' | Select-Object -First 1).Line
    $prefetch = ($text | Select-String -Pattern '^\[probe\] prefetch ' | Select-Object -First 1).Line
    $imgSum = ($text | Select-String -Pattern '^\[probe\] images-summary ' | Select-Object -First 1).Line

    # tick 时间序列（抖动臂取 queued 区间）
    $queued = @()
    foreach ($line in $text) {
        if ($line -match '^\[probe\] tick t=[0-9.]+ state=\S+ queued=([0-9]+) audioRejected=([0-9]+)') {
            $queued += [int]$Matches[1]
        }
    }
    # 旧格式 t= 行兜底（视频模式一直打）
    if ($queued.Count -eq 0) {
        foreach ($line in $text) {
            if ($line -match '^t=[0-9.]+ state=\S+ queued=([0-9]+) ') { $queued += [int]$Matches[1] }
        }
    }
    $qMin = if ($queued.Count) { ($queued | Measure-Object -Minimum).Minimum } else { 0 }
    $qMax = if ($queued.Count) { ($queued | Measure-Object -Maximum).Maximum } else { 0 }

    # 重算/抑制/flap：内核在 [arbiter] budget 行里自带累计计数器
    # （格式：[arbiter] budget total=Nmib granted=Nmib recompute=N suppress=N flap=N），
    # 直接取日志里见过的最大值，不再自己从预算序列推方向。
    $recompute = 0; $suppress = 0; $flap = 0
    $rx = [regex]'\[arbiter\] budget total=\d+mib granted=\d+mib recompute=(\d+) suppress=(\d+) flap=(\d+)'
    foreach ($line in $text) {
        $mm = $rx.Match($line)
        if (-not $mm.Success) { continue }
        $r = [int]$mm.Groups[1].Value; $s = [int]$mm.Groups[2].Value; $f = [int]$mm.Groups[3].Value
        if ($r -gt $recompute) { $recompute = $r }
        if ($s -gt $suppress) { $suppress = $s }
        if ($f -gt $flap) { $flap = $f }
    }
    $failed = @($text | Select-String -Pattern 'state=Failed|FAIL open-refused').Count
    $arbiLines = @($text | Select-String -Pattern '\[arbiter\]').Count
    return [pscustomobject]@{
        Path = $Path; Env = $envLine; Summary = $summary; AudioSum = $audioSum
        Prefetch = $prefetch; ImgSummary = $imgSum
        QueuedMin = $qMin; QueuedMax = $qMax; Recompute = $recompute
        Suppress = $suppress; Flaps = $flap; FailedMarks = $failed
        ArbiterLines = $arbiLines
    }
}

# ── 跑三臂 ───────────────────────────────────────────────────────────────────
$results = @()

# (a) 图片目录命中
Reset-ArmEnv
$results += Invoke-ProbeArm -Name 'images' -LogName "probe3-$Tag-images.log" `
    -ProbeArgs @('--images', $ImagesDir, "$ImagesMax")

# (b) 音频拒绝压力：cap 梯度
foreach ($cap in $AudioCapGradients) {
    Reset-ArmEnv
    $env:FFF_AUDIO_CAP_KB = "$cap"
    $results += Invoke-ProbeArm -Name "audio-$cap" -LogName "probe3-$Tag-audio-$cap.log" `
        -ProbeArgs @($Media, "$AudioSeconds", '2')
}

# (c) 限额抖动压测：总预算 cap 梯度，长跑
foreach ($mb in $CapMBGradients) {
    Reset-ArmEnv
    if ($mb -gt 0) { $env:FFF3FP_MEMARB_CAP_MB = "$mb" }
    $results += Invoke-ProbeArm -Name "jitter-$mb" -LogName "probe3-$Tag-jitter-$mb.log" `
        -ProbeArgs @($Media, "$VideoSeconds", '2')
}

if ($WhatIf) {
    Write-Host ''
    Write-Host '（-WhatIf 模式：未实际运行，未写报告。）'
    return
}

# ── 解析 + 判定 + 写报告 ─────────────────────────────────────────────────────
$parsed = foreach ($r in $results) {
    $p = Parse-ProbeLog -Path $r.Log
    Add-Member -InputObject $p -NotePropertyName Arm -NotePropertyValue $r.Name -Force
    Add-Member -InputObject $p -NotePropertyName Exit -NotePropertyValue $r.Exit -Force
    $p
}

$expectedRecompute = [math]::Floor($VideoSeconds / 2)
$report = Join-Path $OutDir ("第三轮测量报告-$Tag-$(Get-Date -Format 'yyyyMMdd-HHmm').md")
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# 第三轮实机测量报告（$Tag）")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("- 视频媒体：``$Media``")
[void]$sb.AppendLine("- 图片目录：``$ImagesDir``（最多 $ImagesMax 张）")
[void]$sb.AppendLine("- 音频臂时长 ${AudioSeconds}s，抖动臂时长 ${VideoSeconds}s，videoProfile=2")
[void]$sb.AppendLine("- 探针：``membudget-probe.exe``，工作目录 ``$buildDir``")
[void]$sb.AppendLine("")

# (a) 图片命中
[void]$sb.AppendLine("## (a) 图片目录预取命中")
$pa = $parsed | Where-Object { $_.Arm -eq 'images' } | Select-Object -First 1
[void]$sb.AppendLine('```')
[void]$sb.AppendLine("env:     $($pa.Env)")
[void]$sb.AppendLine("summary: $($pa.ImgSummary)")
[void]$sb.AppendLine("prefetch:$($pa.Prefetch)")
[void]$sb.AppendLine('```')
if ($pa.ImgSummary -match 'hitrate=([0-9.]+)') {
    $hr = [double]$Matches[1]
    [void]$sb.AppendLine("- 命中率判定：hitrate=$hr → $(if ($hr -gt 0.5) { 'PASS(>0.5)' } else { 'WARN(<=0.5，可能 worker 预热不足或预算过小)' })")
}
if ($pa.ImgSummary -match 'lru1=(\d+) trim1=(\d+)') {
    [void]$sb.AppendLine("- LRU 驱逐 / 预算裁剪：lru=$($Matches[1]) trim=$($Matches[2])（非 0 = still lane 顶到预算）")
}
[void]$sb.AppendLine("")

# (b) 音频压力
[void]$sb.AppendLine("## (b) 音频拒绝压力（FFF_AUDIO_CAP_KB 梯度）")
[void]$sb.AppendLine('```')
[void]$sb.AppendLine("{0,-12} {1,-8} {2,-10} {3,-12} {4}" -f 'cap(KiB)', '退出码', 'audioRejected', 'underruns', 'Failed标记')
foreach ($p in ($parsed | Where-Object { $_.Arm -like 'audio-*' })) {
    $rej = if ($p.Summary -match 'audioRejected=(\d+)') { $Matches[1] } else { '?' }
    $und = if ($p.AudioSum -match 'underruns=(\d+)') { $Matches[1] } else { '?' }
    $capVal = $p.Arm -replace 'audio-', ''
    [void]$sb.AppendLine("{0,-12} {1,-8} {2,-10} {3,-12} {4}" -f $capVal, $p.Exit, $rej, $und, $p.FailedMarks)
}
[void]$sb.AppendLine('```')
[void]$sb.AppendLine("- 判定：audioRejected 应随 cap 收紧上升；全程 Failed 标记必须为 0。")
[void]$sb.AppendLine("")

# (c) 抖动压测
[void]$sb.AppendLine("## (c) 限额抖动压测（FFF3FP_MEMARB_CAP_MB 梯度）")
[void]$sb.AppendLine('```')
[void]$sb.AppendLine("{0,-12} {1,-6} {2,-9} {3,-9} {4,-9} {5,-9} {6,-7} {7}" -f 'cap(MiB)', '退出', 'recompute', 'suppress', 'queuedMin', 'queuedMax', 'flap', '判定')
foreach ($p in ($parsed | Where-Object { $_.Arm -like 'jitter-*' })) {
    $capVal = $p.Arm -replace 'jitter-', ''
    $judge = @()
    if ($p.Recompute -le $expectedRecompute) { $judge += '重算PASS' } else { $judge += '重算FAIL' }
    if (($p.QueuedMax - $p.QueuedMin) -le 4) { $judge += '队列PASS' } else { $judge += '队列FAIL' }
    if ($p.Flaps -eq 0) { $judge += '无flap' } else { $judge += "flap=$($p.Flaps)" }
    [void]$sb.AppendLine("{0,-12} {1,-6} {2,-9} {3,-9} {4,-9} {5,-9} {6,-7} {7}" -f `
        $capVal, $p.Exit, $p.Recompute, $p.Suppress, $p.QueuedMin, $p.QueuedMax, $p.Flaps, ($judge -join '/'))
}
[void]$sb.AppendLine('```')
[void]$sb.AppendLine("- 判定口径：recompute <= floor(${VideoSeconds}/2)=$expectedRecompute（<1 次/2 s）视为死区抑制有效；")
[void]$sb.AppendLine("  suppress 为内核自记的被死区压住的重算次数；flap 为内核自记的「升一档后 4 s 内又降」次数，必须为 0；")
[void]$sb.AppendLine("  队列稳态区间 <=4 帧。")
[void]$sb.AppendLine("")

[void]$sb.AppendLine("## 日志清单")
foreach ($p in $parsed) { [void]$sb.AppendLine("- $($p.Arm): ``$($p.Path)``") }

$sb.ToString() | Set-Content -LiteralPath $report -Encoding utf8BOM
Write-Host "报告：$report"
$parsed | Format-Table Arm, Exit, QueuedMin, QueuedMax, Recompute, Flaps, FailedMarks -AutoSize
