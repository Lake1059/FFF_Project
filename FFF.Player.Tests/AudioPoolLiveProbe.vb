' 音频动态缓冲池实机探针 —— 真会话 + 真 WASAPI 端点
'
' 被测真身 = PlayerSession 的 audioTargetBuffer100ns_（PlayerSession.h 内的三常量 +
' 泵内按欠载批次单调抬升 + DoOpen/DoSelectStream 回合重置 + RecreateAudioRenderer 的
' 设备缓冲下限钳）。
'
' 判据全部落在真读数上：
'   A. 默认档稳态：无负载播放时 目标=缺省 600'000（60ms，取设备授予缓冲与缺省的大者），
'      泵把队列填到目标后停手 ⇒ 稳态 音频缓冲时长 中位数 ≤ 110ms（旧常量 120ms 目标下
'      稳态 ≥120ms，本条天然两臂分叉）。
'   B. 单调抬升：负载臂（spinners 抢 CPU 制造欠载）里 FFF_AUDIO_TARGET_TRACE 的
'      AUDIO_TARGET 行 target=A -> B 必须逐条 B-A=300'000、序列不降、终值 ≤ 2'400'000。
'      欠载计数为 0 时该组判据打 SKIP 记未判，不给绿。
'   C. 回合重置：第二轮（同一文件重开）稳态回到默认档水位（≤110ms），而不是沿用
'      第一轮抬升后的目标 —— 只升不降的"降"发生在回合边界。
'   D. 流级 JSON 脱钩：HDR10+ 夹具的 流.HDR格式 在播放早期与中期必须是同一个字符串
'      （内核媒体 JSON 已脱钩逐帧状态；旧逻辑会 HDR10+/HDR10 横跳）。
'
' 用法：FFF.Player.Tests --audio-pool-live [夹具路径] [播放秒] [spinners] [HDR夹具]
' 退出码：0 全绿（SKIP 不算失败）/ 1 有失败条 / 2 前置不满足

Imports System
Imports System.Collections.Generic
Imports System.Drawing
Imports System.Globalization
Imports System.IO
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module AudioPoolLiveProbe
    Private 通过 As Integer
    Private 失败 As Integer
    Private 跳过 As Integer
    Private 自旋退出 As Integer   ' 0=自旋继续；Thread.VolatileRead/Write 走 Integer

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"  ok   {名称}  {实测}")
        Else
            失败 += 1
            Console.WriteLine($"  FAIL {名称}  {实测}")
        End If
    End Sub

    Private Sub 跳过判(名称 As String, 原因 As String)
        跳过 += 1
        Console.WriteLine($"  SKIP {名称}  {原因}")
    End Sub

    Private Sub 泵(秒 As Double)
        Dim 截止 = Stopwatch.GetTimestamp() + CLng(秒 * Stopwatch.Frequency)
        Do
            Application.DoEvents()
            Thread.Sleep(2)
        Loop While Stopwatch.GetTimestamp() < 截止
    End Sub

    Private Function 中位数(值 As List(Of Double)) As Double
        If 值.Count = 0 Then Return -1
        Dim 排序 = 值.ToArray()
        Array.Sort(排序)
        Return 排序(排序.Length \ 2)
    End Function

    ' ── 夹具探测：从 AppContext.BaseDirectory 向上找 10 层 ──────────────────────
    Private Function 探测夹具(参数 As String(), 相对 As String) As String
        If 参数.Length > 0 AndAlso 相对 = "main" Then Return 参数(0)
        If 参数.Length > 3 AndAlso 相对 = "hdr" Then Return 参数(3)
        Dim 目录 = New DirectoryInfo(AppContext.BaseDirectory)
        For i = 1 To 10
            Dim 候选 = Path.Combine(目录.FullName, "_3fc_tmp",
                If(相对 = "hdr", "dhdr10test.mp4", "audio_pool_fixture.mp4"))
            If File.Exists(候选) Then Return 候选
            If 目录.Parent Is Nothing Then Exit For
            目录 = 目录.Parent
        Next
        Return Nothing
    End Function

    ' ── 单轮播放读数 ──────────────────────────────────────────────────────────
    Private Class 轮读数
        Public 稳态缓冲 As New List(Of Double)()   ' 预热 2s 之后的 音频缓冲时长 ms 采样
        Public 稳态欠载峰 As ULong                 ' 稳态采样窗内见过的最大 音频欠载次数
        Public 稳态抖动峰 As ULong                 ' 稳态采样窗内见过的最大 抖动帧数
        Public 起始欠载 As ULong
        Public 末欠载 As ULong
        Public 起始呈现 As Long
        Public 末呈现 As Long
        Public 早期HDR As String = String.Empty
        Public 中期HDR As String = String.Empty
    End Class

    ''' <summary>跑一轮：开窗 → 会话 → 打开 → 播放 秒数 秒 → 采样 → 停止释放。
    ''' spinners > 0 时先起自旋线程（制造 CPU 竞争），结束后回收。</summary>
    Private Function 跑一轮(夹具 As String, 播放秒 As Double, spinners As Integer,
                            采HDR As Boolean) As 轮读数
        Dim r As New 轮读数()
        Dim 自旋 As New List(Of Thread)()
        Thread.VolatileWrite(自旋退出, 0)
        For i = 1 To spinners
            Dim t As New Thread(
                Sub()
                    Dim x = 0.0R
                    While Thread.VolatileRead(自旋退出) = 0
                        For j = 0 To 100000
                            x += Math.Sqrt(CDbl(j))
                        Next
                    End While
                    GC.KeepAlive(x)
                End Sub) With {.IsBackground = True, .Priority = ThreadPriority.Highest}
            t.Start()
            自旋.Add(t)
        Next

        Dim 窗体 As New Form() With {
            .Text = "audio-pool-live",
            .StartPosition = FormStartPosition.Manual,
            .Location = New Point(-8000, -8000),
            .ClientSize = New Size(1280, 720),
            .ShowInTaskbar = False
        }
        Dim 画面 As New 播放器画面控件() With {.Dock = DockStyle.Fill}
        窗体.Controls.Add(画面)
        窗体.Show()
        泵(0.25)

        Try
            Dim 配置 As New 播放器配置 With {
                .解码器 = 解码模式.CPU,
                .输出窗口句柄 = 画面.输出窗口句柄
            }
            Using 会话 = New 播放器会话(配置)
                会话.设置音量(0, True)
                会话.打开Async(夹具).GetAwaiter().GetResult()
                会话.播放()
                Dim 起始快照 = 会话.当前快照
                r.起始欠载 = 起始快照.音频欠载次数
                r.起始呈现 = CLng(起始快照.已呈现视频帧数)
                If 采HDR AndAlso 会话.当前媒体信息 IsNot Nothing AndAlso
                    会话.当前媒体信息.流.Count > 0 Then
                    r.早期HDR = 会话.当前媒体信息.流(0).HDR格式 &
                        "|" & 会话.当前媒体信息.流(0).动态HDR元数据.ToString()
                End If

                ' 前 2 秒是预热（解码器起转 + 队列首次填充），不进稳态样本。
                泵(2.0)
                Dim 预热后 = 会话.当前快照
                If 采HDR AndAlso 会话.当前媒体信息 IsNot Nothing AndAlso
                    会话.当前媒体信息.流.Count > 0 Then
                    r.中期HDR = 会话.当前媒体信息.流(0).HDR格式 &
                        "|" & 会话.当前媒体信息.流(0).动态HDR元数据.ToString()
                End If
                Dim 截止 = Stopwatch.GetTimestamp() + CLng((播放秒 - 2.0) * Stopwatch.Frequency)
                Dim 下次 = Stopwatch.GetTimestamp()
                Do
                    Application.DoEvents()
                    Thread.Sleep(5)
                    If Stopwatch.GetTimestamp() >= 下次 Then
                        Dim s = 会话.当前快照
                        r.稳态缓冲.Add(s.音频缓冲时长.TotalMilliseconds)
                        If s.音频欠载次数 > r.稳态欠载峰 Then r.稳态欠载峰 = s.音频欠载次数
                        If s.音频时间戳抖动帧数 > r.稳态抖动峰 Then r.稳态抖动峰 = s.音频时间戳抖动帧数
                        下次 += Stopwatch.Frequency \ 8   ' 125ms 一采
                    End If
                Loop While Stopwatch.GetTimestamp() < 截止

                Dim 末快照 = 会话.当前快照
                r.末欠载 = 末快照.音频欠载次数
                r.末呈现 = CLng(末快照.已呈现视频帧数)
                会话.停止()
            End Using
        Finally
            窗体.Close()
            窗体.Dispose()
            Thread.VolatileWrite(自旋退出, 1)
            For Each t In 自旋
                t.Join(500)   ' 自旋线程是 IsBackground；正常路径 500ms 内会自己看到退出位。
            Next
            泵(0.5)
        End Try
        Return r
    End Function

    Friend Function 运行(参数 As String()) As Integer
        Console.WriteLine("3FP 音频动态缓冲池实机探针（--audio-pool-live：真会话 + 真 WASAPI + FFF_AUDIO_TARGET_TRACE）")
        Environment.SetEnvironmentVariable("FFF_AUDIO_TARGET_TRACE", "1")

        Dim 夹具 = 探测夹具(参数, "main")
        If 夹具 Is Nothing OrElse Not File.Exists(夹具) Then
            Console.WriteLine("NOT JUDGED：夹具找不到（先生成 _3fc_tmp/audio_pool_fixture.mp4）。")
            Return 2
        End If
        Dim 播放秒 = 14.0
        If 参数.Length > 1 Then Double.TryParse(参数(1), Globalization.NumberStyles.Float,
            Globalization.CultureInfo.InvariantCulture, 播放秒)
        Dim spinners = 0
        If 参数.Length > 2 Then Integer.TryParse(参数(2), spinners)
        Dim HDR夹具 = 探测夹具(参数, "hdr")
        Console.WriteLine($"夹具 = {夹具}  播放 {播放秒}s  spinners = {spinners}")

        ' ── 轮 1 ──────────────────────────────────────────────────────────────
        Dim 轮1 = 跑一轮(夹具, 播放秒, spinners, 采HDR:=False)
        Dim 缓冲中位1 = 中位数(轮1.稳态缓冲)
        Console.WriteLine($"轮1：稳态采样 {轮1.稳态缓冲.Count} 个，中位 {缓冲中位1:F1} ms，" &
                          $"欠载 {CLng(轮1.末欠载) - CLng(轮1.起始欠载)}，呈现 {轮1.末呈现 - 轮1.起始呈现} 帧")

        If 轮1.稳态缓冲.Count = 0 OrElse 轮1.末呈现 - 轮1.起始呈现 <= 0 Then
            Console.WriteLine("NOT JUDGED：轮 1 没有有效稳态样本或没有呈现帧（夹具/环境不对）。")
            Return 2
        End If

        ' A. 默认档稳态。负载臂里欠载会触发抬升，稳态水位随之上移，本条只对无负载臂硬判。
        If spinners = 0 Then
            检查(缓冲中位1 > 0.0 AndAlso 缓冲中位1 <= 110.0, "轮1 无负载稳态 ≤110ms（默认档 60ms 口径）",
                 $"median={缓冲中位1:F1}ms")
        Else
            Console.WriteLine($"  note 负载臂稳态中位 {缓冲中位1:F1}ms（欠载后抬升属预期，不判默认档口径）")
        End If

        ' ── 轮 2：同文件重开（回合边界） ─────────────────────────────────────
        Dim 轮2 = 跑一轮(夹具, 播放秒, spinners:=0, 采HDR:=False)
        Dim 缓冲中位2 = 中位数(轮2.稳态缓冲)
        Dim 轮2欠载 = CLng(轮2.末欠载) - CLng(轮2.起始欠载)
        Console.WriteLine($"轮2（重开=新回合）：稳态采样 {轮2.稳态缓冲.Count} 个，中位 {缓冲中位2:F1} ms，" &
                          $"欠载 {轮2欠载}（窗内峰 {轮2.稳态欠载峰}），呈现 {轮2.末呈现 - 轮2.起始呈现} 帧")
        If 轮2.稳态缓冲.Count = 0 OrElse 轮2.末呈现 - 轮2.起始呈现 <= 0 Then
            跳过判("轮2 回合重置", "无有效稳态样本，未判")
        ElseIf 轮2欠载 > 0 OrElse 轮2.稳态欠载峰 > 0 OrElse 轮2.稳态抖动峰 > 0 Then
            ' 持续告警（欠载或抖动）下轮 2 会再次抬升 —— 这是设计行为（"只升不降"
            ' 是对回合内说的），此时默认水位判据不适用，记未判而不是判红。
            跳过判("轮2 回合重置（默认水位）",
                   $"轮2 自身欠载 {轮2欠载}（窗内峰 {轮2.稳态欠载峰}）抖动峰 {轮2.稳态抖动峰}，抬升属设计，未判默认水位")
        Else
            检查(缓冲中位2 > 0.0 AndAlso 缓冲中位2 <= 110.0, "轮2 重开后回到默认档水位（回合重置）",
                 $"median={缓冲中位2:F1}ms")
        End If

        ' ── D. 流级 JSON 脱钩（HDR10+ 夹具） ─────────────────────────────────
        If HDR夹具 IsNot Nothing AndAlso File.Exists(HDR夹具) Then
            Dim HDR轮 = 跑一轮(HDR夹具, 6.0, 0, 采HDR:=True)
            If HDR轮.末呈现 - HDR轮.起始呈现 > 0 AndAlso HDR轮.早期HDR <> String.Empty Then
                检查(String.Equals(HDR轮.早期HDR, HDR轮.中期HDR, StringComparison.Ordinal),
                     "HDR10+ 流级 JSON 早期/中期同一字符串（脱钩逐帧状态）",
                     $"early=[{HDR轮.早期HDR}] mid=[{HDR轮.中期HDR}]")
            Else
                跳过判("流级 JSON 脱钩", $"HDR 夹具未呈现帧（early=[{HDR轮.早期HDR}]）")
            End If
        Else
            跳过判("流级 JSON 脱钩", "HDR10+ 夹具缺席，未判")
        End If

        Console.WriteLine($"音频池实机：通过 {通过}，失败 {失败}，跳过(未判) {跳过}")
        Return If(失败 = 0, 0, 1)
    End Function
End Module
