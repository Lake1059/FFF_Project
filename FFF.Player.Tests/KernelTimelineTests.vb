Imports System.Diagnostics
Imports System.Drawing
Imports System.IO
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module 内核时间轴测试
    Public Sub 运行(目录 As String)
        For Each 模式 In {解码模式.CPU, 解码模式.GPU}
            For Each 样本 In {("short.mkv", 30UL), ("silent.mkv", 30UL),
                              ("tiny.mkv", 2UL), ("early-audio.mkv", 30UL),
                              ("late-video.mkv", 15UL), ("audio.flac", 0UL)}
                测试完整时间轴(Path.Combine(目录, 样本.Item1), 模式, 样本.Item2)
            Next
            测试完整时间轴(Path.Combine(目录, "short.mkv"), 模式, 30UL, True)
            For Each 偏移 In {0.25, -0.25}
                测试完整时间轴(Path.Combine(目录, "silent.mkv"), 模式, 30UL,
                              False, Path.Combine(目录, "audio.flac"), 偏移)
            Next
            测试时间轴操作(Path.Combine(目录, "long.mkv"), 模式)
            测试延迟窗口(Path.Combine(目录, "long.mkv"), 模式)
        Next
        测试HDR像素回读(Path.Combine(目录, "hdr-white.mkv"))
        Console.WriteLine("内核时间轴、完整排空、外部音轨和生命周期回归通过。")
    End Sub

    Public Sub 测试HDR像素回读(路径 As String)
        Using 窗口 = 创建窗口("scRGB FP16 pixel readback")
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU, .输出窗口句柄 = 窗口.Handle,
                .色彩模式 = 色彩输出模式.峰值映射HDR, .强制HDR输出 = True})
                会话.打开Async(路径).GetAwaiter().GetResult()
                会话.播放()
                等待(会话, Function(快照) 快照.状态 = 播放状态.播放结束, "HDR 排空")
                Dim 像素 = 会话.读取视频输出原始像素(320, 180)
                确保(像素.输出位深度 = 16 AndAlso 像素.色彩模式 = 色彩输出模式.峰值映射HDR, "未覆盖 FP16 后缓冲。")
                确保(Single.IsFinite(像素.红) AndAlso Single.IsFinite(像素.绿) AndAlso Single.IsFinite(像素.蓝) AndAlso
                    像素.红 > 1 AndAlso Math.Abs(像素.Alpha - 1.0F) < 0.001F,
                    $"FP16 回读错误：{像素.红}/{像素.绿}/{像素.蓝}/{像素.Alpha}")
                Console.WriteLine($"HDR-PIXEL: {像素.红:F3}/{像素.绿:F3}/{像素.蓝:F3}/{像素.Alpha:F3}")
            End Using
        End Using
    End Sub

    Private Function 创建窗口(名称 As String) As Form
        Dim 窗口 As New Form With {.ClientSize = New Size(640, 360),
            .Text = 名称, .ShowInTaskbar = False,
            .StartPosition = FormStartPosition.Manual, .Location = New Point(50, 50)}
        窗口.Show()
        Application.DoEvents()
        Return 窗口
    End Function

    Private Sub 确保(条件 As Boolean, 消息 As String)
        If Not 条件 Then Throw New InvalidOperationException(消息)
    End Sub

    Private Function 等待(会话 As 播放器会话, 条件 As Func(Of 播放器快照, Boolean),
                          阶段 As String) As 播放器快照
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            确保(快照.状态 <> 播放状态.失败, $"{阶段}：{会话.最后错误消息}")
            If 条件(快照) Then Return 快照
            确保(计时.Elapsed < TimeSpan.FromSeconds(15),
               $"{阶段}超时：{快照.状态}, position={快照.播放位置.TotalMilliseconds:F1}, " &
               $"pts={快照.原始帧PTS}, audio={快照.音频缓冲时长.TotalMilliseconds:F1}")
            Thread.Sleep(1)
        Loop
    End Function

    Private Sub 测试完整时间轴(路径 As String, 模式 As 解码模式, 期望帧数 As ULong,
                           Optional 独占 As Boolean = False,
                           Optional 外部音频 As String = Nothing, Optional 偏移 As Double = 0)
        Dim 名称 = $"{模式} {Path.GetFileName(路径)} exclusive={独占} external={外部音频 IsNot Nothing}/{偏移}"
        Using 窗口 = 创建窗口(名称)
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 模式, .输出窗口句柄 = 窗口.Handle,
                .色彩模式 = 色彩输出模式.映射到SDR})
                会话.设置音量(0.0F, True)
                会话.打开Async(路径).GetAwaiter().GetResult()
                If 独占 Then 会话.设置WASAPI独占模式(True)
                If 外部音频 IsNot Nothing Then 会话.加载外部音轨(外部音频, -1, TimeSpan.FromSeconds(偏移))
                会话.播放()
                If Path.GetFileName(路径) = "early-audio.mkv" Then
                    等待(会话, Function(快照) 快照.播放位置.TotalSeconds > 0.35, "视频尾段")
                    会话.暂停()
                    等待(会话, Function(快照) 快照.状态 = 播放状态.已暂停, "音频排空后的暂停")
                    会话.播放()
                End If
                Dim 前位置 = TimeSpan.Zero
                Dim 结果 = 等待(会话,
                    Function(快照)
                        确保(快照.播放位置 >= 前位置 - TimeSpan.FromMilliseconds(25),
                           $"{名称}时钟倒退：{前位置.TotalMilliseconds:F1} -> {快照.播放位置.TotalMilliseconds:F1}，" &
                           $"state={快照.状态}, pts={快照.原始帧PTS}, present={快照.已呈现视频帧数}, audio={快照.音频缓冲时长.TotalMilliseconds:F1}。")
                        前位置 = 快照.播放位置
                        Return 快照.状态 = 播放状态.播放结束
                    End Function, 名称)
                确保(结果.已呈现视频帧数 = 期望帧数, $"{名称}：实际呈现 {结果.已呈现视频帧数}/{期望帧数}。")
                确保(结果.已丢弃视频帧数 = 0 AndAlso 结果.已合并视频帧数 = 0, $"{名称}丢帧或合帧。")
                确保(结果.音频欠载次数 = 0, $"{名称}音频欠载 {结果.音频欠载次数} 次。")
                Console.WriteLine($"TIMELINE {名称}: frames={结果.已呈现视频帧数}, " &
                    $"end={结果.播放位置.TotalMilliseconds:F1}ms, underrun={结果.音频欠载次数}")
                会话.播放()
                等待(会话, Function(快照) 快照.状态 = 播放状态.正在播放, 名称 & " 重播")
                等待(会话, Function(快照) 快照.状态 = 播放状态.播放结束, 名称 & " 重播结束")
            End Using
        End Using
    End Sub

    Private Sub 测试时间轴操作(路径 As String, 模式 As 解码模式)
        Using 窗口 = 创建窗口($"{模式} seek/pause/resume/reopen")
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 模式, .输出窗口句柄 = 窗口.Handle,
                .色彩模式 = 色彩输出模式.映射到SDR})
                会话.设置音量(0.0F, True)
                会话.打开Async(路径).GetAwaiter().GetResult()
                会话.播放()
                等待(会话, Function(快照) 快照.播放位置.TotalSeconds > 0.3, "顺播")
                会话.暂停()
                Dim 暂停 = 等待(会话, Function(快照) 快照.状态 = 播放状态.已暂停, "暂停")
                Thread.Sleep(150)
                确保(会话.当前快照.播放位置 = 暂停.播放位置, "暂停时钟继续前进。")
                会话.跳转(TimeSpan.FromSeconds(1))
                等待(会话, Function(快照) 快照.时间轴代次 > 暂停.时间轴代次 AndAlso 快照.播放位置.TotalSeconds >= 1, "暂停跳转")
                会话.播放()
                Dim 播放 = 等待(会话, Function(快照) 快照.播放位置.TotalSeconds > 1.3, "恢复")
                会话.跳转(TimeSpan.FromSeconds(0.2))
                等待(会话, Function(快照) 快照.时间轴代次 > 播放.时间轴代次 AndAlso 快照.播放位置.TotalSeconds < 1, "播放中跳转")
                等待(会话, Function(快照) 快照.播放位置.TotalSeconds > 0.5, "跳转恢复")
                会话.停止()
                等待(会话, Function(快照) 快照.状态 = 播放状态.空闲, "停止")
                会话.打开Async(路径).GetAwaiter().GetResult()
                会话.播放()
                等待(会话, Function(快照) 快照.播放位置.TotalSeconds > 0.3, "重新打开")
                Console.WriteLine($"LIFECYCLE {模式}: pause/seek/resume/stop/reopen passed")
            End Using
        End Using
    End Sub

    Private Sub 测试延迟窗口(路径 As String, 模式 As 解码模式)
        Using 窗口 = 创建窗口($"{模式} late HWND")
            Using 会话 As New 播放器会话(New 播放器配置 With {.解码器 = 模式})
                会话.设置音量(0.0F, True)
                会话.打开Async(路径).GetAwaiter().GetResult()
                会话.播放()
                Dim 初始 = 等待(会话, Function(快照) 快照.播放位置.TotalSeconds > 0.2, "无窗口播放")
                会话.设置输出窗口(窗口.Handle)
                Dim 已绑定 = 等待(会话, Function(快照) 快照.交换链呈现次数 > 0 AndAlso
                    快照.播放位置 > 初始.播放位置 + TimeSpan.FromSeconds(0.3), "延迟绑定窗口")
                会话.设置输出窗口(IntPtr.Zero)
                Dim 无窗口 = 等待(会话, Function(快照) 快照.播放位置 >
                    已绑定.播放位置 + TimeSpan.FromSeconds(0.2), "解除窗口")
                会话.设置输出窗口(窗口.Handle)
                等待(会话, Function(快照) 快照.交换链呈现次数 > 已绑定.交换链呈现次数 AndAlso
                    快照.播放位置 > 无窗口.播放位置 + TimeSpan.FromSeconds(0.2), "重新绑定窗口")
                Console.WriteLine($"LATE-WINDOW {模式}: passed")
            End Using
        End Using
    End Sub
End Module
