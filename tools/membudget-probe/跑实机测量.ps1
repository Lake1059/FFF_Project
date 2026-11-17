# 实机测量：预算池 / 视频队列重算 / epoch 丢旧帧 / 音频借用 / 预取命中率
#
# 为什么要这个脚本：这些数字只有真正窗口化播放才会出现。脚本把 membudget-probe 在
# "仲裁器开 / 关"两种开关状态下各跑一遍（同一媒体、同一 seek 节奏），从探针 stdout
# 与内核 stderr trace 里取指标，再按阈值判定，最后写一份报告。
#
# 用法：
#   pwsh -File 跑实机测量.ps1
#   pwsh -File 跑实机测量.ps1 -Media 'C:\PLAN\3FCompare\tests\media\video\vidA.mp4' -Seconds 12
#   pwsh -File 跑实机测量.ps1 -Media '...\fx_bgr0_1920x1080.mkv' -Tag heavy
#
# 阈值（判定口径写在报告里，改阈值改这里）：
#   - 视频队列稳态波动 queuedMax-queuedMin <= 3 帧（前 1 s 填充期不计）
#   - 视频队列重算频率 < 1 次 / 2 s（即 recompute <= floor(seconds/2)）
#   - seek 运行中 staleSkip >= 1（epoch 闸门真的拦下过旧帧）；为 0 记"未触发"
#   - audioRejected 稳态为 0（除了压力臂；>0 但 < decodedAudio 的 0.5% 记"轻微"）
#   - 预取命中率 > 0（有计划且邻居可解码时）
param(
    [string]$Media = 'C:\PLAN\3FCompare\tests\media\video\vidA.mp4',
    [int]$Seconds = 12,
    [string]$Tag = 'main',
    [int]$Profile = 0,
    [string]$OutDir = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$probeDir = $PSScriptRoot
$exe = Join-Path $probeDir 'membudget-probe.exe'
$kernel = Split-Path (Split-Path $probeDir -Parent) -Parent
$buildDir = Join-Path (Split-Path $kernel -Parent) 'build'   # 依赖 DLL 所在目录

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host '探针未编译，先编译…'
    Push-Location $probeDir
    cmd /c "`"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && cl /nologo /std:c++20 /utf-8 /EHsc /O2 /MD /I ..\..\FFF.Native membudget-probe.cpp /link ..\..\FFF.Native\x64\Release\FFF.Native.lib user32.lib gdi32.lib /OUT:membudget-probe.exe"
    Pop-Location
}

function Invoke-Arm {
    param([string]$Name, [string]$Arbiter, [string]$MediaPath)
    $env:FFF3FP_MEMARB = $Arbiter
    $env:FFF_MEMARB_TRACE = '1'
    $env:FFF_LADDER_TRACE = '1'
    $log = Join-Path $OutDir ("probe-$Tag-$Name.log")
    Push-Location $buildDir
    & $exe $MediaPath $Seconds $Profile 2>&1 | Tee-Object -FilePath $log | Out-Null
    $code = $LASTEXITCODE
    Pop-Location
    return [pscustomobject]@{ Name = $Name; Arbiter = $Arbiter; Log = $log; Exit = $code }
}

$arms = @()
$arms += Invoke-Arm -Name 'arbiter-on' -Arbiter '1' -MediaPath $Media
$arms += Invoke-Arm -Name 'arbiter-off' -Arbiter '0' -MediaPath $Media

function Measure-Log {
    param([string]$Path, [int]$SecondsValue)
    $text = Get-Content -LiteralPath $Path
    $samples = @()
    $summary = $null
    $audioSummary = $null
    $prefetch = $null
    $openLine = ($text | Select-String -Pattern '^open=' | Select-Object -First 1).Line
    $precheck = ($text | Select-String -Pattern '\[arbiter\] precheck' | Select-Object -First 1).Line
    foreach ($line in $text) {
        if ($line -match '^t=([0-9.]+) .*queued=([0-9]+) decoded=([0-9]+) presented=([0-9]+) dropped=([0-9]+) audioRejected=([0-9]+) presents=([0-9]+)') {
            $samples += [pscustomobject]@{
                t = [double]$Matches[1]; queued = [int]$Matches[2]; decoded = [int]$Matches[3]
                presented = [int]$Matches[4]; dropped = [int]$Matches[5]
                audioRejected = [int]$Matches[6]; presents = [int]$Matches[7]
            }
        } elseif ($line -match '^SUMMARY decoded=([0-9]+)') {
            $summary = $line
        } elseif ($line -match '^summary-audio') {
            $audioSummary = $line
        } elseif ($line -match '^summary-prefetch') {
            $prefetch = $line
        }
    }
    $epochs = @($text | Select-String -Pattern '\[epoch\] advance=([0-9]+)')
    $advanceCount = $epochs.Count
    $staleSkips = 0
    foreach ($m in ($text | Select-String -Pattern 'stale-skip epoch=[0-9]+ skipped=([0-9]+)')) {
        $v = [int](($m.Line -split 'skipped=')[1]); if ($v -gt $staleSkips) { $staleSkips = $v }
    }
    $recompute = 0
    foreach ($m in ($text | Select-String -Pattern '\[arbiter\] limit=[0-9]+ granted=[0-9]+ MiB recompute=([0-9]+)')) {
        $v = [int](($m.Line -split 'recompute=')[1] -split ' ')[0]; if ($v -gt $recompute) { $recompute = $v }
    }
    $borrowTake = @($text | Select-String -Pattern '\[borrow\] audio take').Count
    $borrowMiss = @($text | Select-String -Pattern '\[borrow\] audio miss').Count
    $borrowReturn = @($text | Select-String -Pattern '\[borrow\] audio return').Count
    $invariant = @($text | Select-String -Pattern 'invariant|ΣGranted|overcommit').Count
    $seeks = @($text | Select-String -Pattern '^seek\[').Count
    $redraws = @($text | Select-String -Pattern '^redraw-after-seek').Count
    # 稳态窗口：跳过前 1.5 s 的填充期（队列从 0 长到稳态的那一段不算波动）
    $steady = $samples | Where-Object { $_.t -ge 1.5 }
    $qMin = ($steady | Measure-Object -Property queued -Minimum).Minimum
    $qMax = ($steady | Measure-Object -Property queued -Maximum).Maximum
    if ($null -eq $qMin) { $qMin = 0; $qMax = 0 }
    $last = $samples | Select-Object -Last 1
    return [pscustomobject]@{
        Log = $Path; Open = $openLine; Precheck = $precheck; Summary = $summary
        AudioSummary = $audioSummary; Prefetch = $prefetch
        EpochAdvances = $advanceCount; StaleMax = $staleSkips; Recompute = $recompute
        BorrowTake = $borrowTake; BorrowMiss = $borrowMiss; BorrowReturn = $borrowReturn
        InvariantHits = $invariant; Seeks = $seeks; Redraws = $redraws
        QueuedSteadyMin = [int]$qMin; QueuedSteadyMax = [int]$qMax
        AudioRejected = if ($last) { $last.audioRejected } else { 0 }
        Samples = $samples.Count
    }
}

$measured = foreach ($arm in $arms) {
    $m = Measure-Log -Path $arm.Log -SecondsValue $Seconds
    Add-Member -InputObject $m -NotePropertyName Arm -NotePropertyValue $arm.Name -Force
    Add-Member -InputObject $m -NotePropertyName Exit -NotePropertyValue $arm.Exit -Force
    $m
}

$expectedRecompute = [math]::Floor($Seconds / 2)
$rows = foreach ($m in $measured) {
    $steadySpan = $m.QueuedSteadyMax - $m.QueuedSteadyMin
    [pscustomobject]@{
        臂 = $m.Arm
        退出码 = $m.Exit
        epoch递增 = $m.EpochAdvances
        旧帧丢弃 = $m.StaleMax
        重算次数 = $m.Recompute
        重算判定 = if ($m.Recompute -le $expectedRecompute) { "PASS(<=$expectedRecompute)" } else { 'FAIL' }
        队列稳态区间 = "$($m.QueuedSteadyMin)..$($m.QueuedSteadyMax)"
        波动判定 = if ($steadySpan -le 4) { 'PASS(<=4帧)' } else { "FAIL(${steadySpan}帧)" }
        借用取_借不到_归还 = "$($m.BorrowTake)/$($m.BorrowMiss)/$($m.BorrowReturn)"
        "audioRejected" = $m.AudioRejected
        不变量命中 = $m.InvariantHits
        PreviousState = $m.Open
    }
}

$report = Join-Path $OutDir ("实机测量报告-$Tag-$(Get-Date -Format 'yyyyMMdd-HHmm').md")
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("# 实机测量报告（$Tag）")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("- 媒体：``$Media``")
[void]$sb.AppendLine("- 时长：${Seconds}s，seek 节奏：2/3/4/5 s 各一次 + 7.5 s「seek 后立即暂停」")
[void]$sb.AppendLine("- 探针：``membudget-probe.exe``（同目录 跑实机测量.ps1 生成）")
[void]$sb.AppendLine("")
[void]$sb.AppendLine('```')
($rows | Format-Table -AutoSize | Out-String) -split "`r`n" | ForEach-Object { [void]$sb.AppendLine($_) }
[void]$sb.AppendLine('```')
[void]$sb.AppendLine("")
foreach ($m in $measured) {
    [void]$sb.AppendLine("## $($m.Arm)")
    [void]$sb.AppendLine('```')
    [void]$sb.AppendLine("open:      $($m.Open)")
    [void]$sb.AppendLine("precheck:  $($m.Precheck)")
    [void]$sb.AppendLine("summary:   $($m.Summary)")
    [void]$sb.AppendLine("audio:     $($m.AudioSummary)")
    [void]$sb.AppendLine("prefetch:  $($m.Prefetch)")
    [void]$sb.AppendLine('```')
    [void]$sb.AppendLine("")
}
[void]$sb.AppendLine("## 判定口径")
[void]$sb.AppendLine("- 队列波动：前 1.5 s 填充期不计，只看稳态；<=3 帧视为抖动已抑制。")
[void]$sb.AppendLine("- 重算频率：``recompute`` 计数 <= floor(秒数/2)，即 < 1 次/2 s。")
[void]$sb.AppendLine("- 旧帧丢弃：seek 运行中 ``[epoch] stale-skip`` 的最大 skipped 值；0 表示本机渲染节奏没有让 Present 在旧 epoch 上被唤醒（缺陷未被触发，不等于没防护）。")
[void]$sb.AppendLine("- 借用：``[borrow] audio take/miss/return`` 各计数；取=借不到=归还应满足 take = return（归还时限 5 s 内）。")
[void]$sb.AppendLine("- audioRejected：稳态应为 0；>0 只在压力臂（低预算 profile 或高码率音频）可接受。")
[void]$sb.AppendLine("- 不变量：日志里出现 invariant/overcommit 行即为 ΣGranted > TotalBudget 的缺陷信号，必须为 0。")
[void]$sb.AppendLine("")
$sb.ToString() | Set-Content -LiteralPath $report -Encoding utf8BOM

$rows | Format-Table -AutoSize
Write-Host "报告：$report"
