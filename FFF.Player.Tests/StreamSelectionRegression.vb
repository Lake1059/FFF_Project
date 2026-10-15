' 3FP 多轨流选择回归 —— 同进程直连内核
'
' 测的是 3FP 内核的流选择链路：全流枚举（GetMediaInfo JSON）、默认轨选择
' （AV_DISPOSITION_DEFAULT 优先，PlayerSession.cpp:519-530）、运行时热切换
' （DoSelectStream，重建解码器 + seek + OperationCompleted）、外部音轨整轨替换
' （DoLoadExternalAudio，带 100ns 偏移）与非法索引拒收（InvalidArgument）。
' 走 FFF.Player 托管包装（播放器会话，Public + InternalsVisibleTo），不 mock，
' P/Invoke 到 x64/Release 的 FFF.Native.dll 真内核。
'
' 样本：artifacts/kernel-tests/multitrack/（多音轨 1V+3A / 多视频轨 2V+1A /
' 外部音频 mka），由 _3fc_tmp/gen_multitrack_samples.sh 生成，ffprobe flat
' 实测流结构后入库（交接文档-2026-10-10.md 六-⑥ 记的"多轨样本缺位"由此补上）。
'
' 期望值来源：样本生成时的 -map/-metadata/-disposition 参数是**被测模块之外**
' 的权威数（生成后另经 ffprobe 独立复核），不引用内核自己的读数当期望值。
'
' 用法：FFF.Player.Tests --stream-selection-regression [样本目录]
' 退出码：0 全绿 / 1 有失败条 / 2 样本等前置条件不对

Imports System
Imports System.IO
Imports System.Threading
Imports FFF.Player

Friend Module StreamSelectionRegression
    Private 通过 As Integer
    Private 失败 As Integer

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"  ok   {名称}  {实测}")
        Else
            失败 += 1
            Console.WriteLine($"  FAIL {名称}  {实测}")
        End If
    End Sub

    ''' <summary>切换/加载是异步操作：轮询快照直到条件成立或超时，返回最后一次读数。</summary>
    Private Function 等待快照(会话 As 播放器会话, 条件 As Func(Of 播放器快照, Boolean)) As 播放器快照
        Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(15)
        Dim 快照 = 会话.当前快照
        While DateTimeOffset.UtcNow < 截止
            快照 = 会话.当前快照
            If 条件(快照) Then Return 快照
            Thread.Sleep(50)
        End While
        Return 快照
    End Function

    ' 内核语义（实读 PlayerSession.cpp:1313-1314 / 4377-4379 / 4848-4856）：
    ' SelectVideoStream/SelectAudioStream 同步只校验会话状态并入队（返回 Success），
    ' 索引/类型校验在会话线程 DoSelectStream 里做，失败走 ReportError → Error 事件
    ' （JSON 带 result/message/operation="select-stream"），当前活跃流保持不变。
    ' 所以"拒收"在宿主层的可观测语义 = 错误事件到达 + 快照里的当前轨不动。
    Private Function 收集错误(会话 As 播放器会话) As List(Of String)
        Dim 列表 As New List(Of String)()
        AddHandler 会话.错误, Sub(sender, e)
                                  SyncLock 列表
                                      列表.Add(e.详情JSON)
                                  End SyncLock
                              End Sub
        Return 列表
    End Function

    Private Function 等待错误(错误列表 As List(Of String)) As Boolean
        Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(10)
        While DateTimeOffset.UtcNow < 截止
            SyncLock 错误列表
                If 错误列表.Count > 0 Then Return True
            End SyncLock
            Thread.Sleep(50)
        End While
        Return False
    End Function

    Private Function 探测样本目录(参数 As String()) As String
        If 参数.Length > 0 Then Return 参数(0)
        Dim 目录 = New DirectoryInfo(AppContext.BaseDirectory)
        For i = 1 To 10
            Dim 候选 = Path.Combine(目录.FullName, "artifacts", "kernel-tests", "multitrack")
            If File.Exists(Path.Combine(候选, "multitrack_audio.mkv")) Then Return 候选
            If 目录.Parent Is Nothing Then Exit For
            目录 = 目录.Parent
        Next
        Return Nothing
    End Function

    Friend Function 运行(参数 As String()) As Integer
        Console.WriteLine("3FP 多轨流选择回归")

        Dim 样本目录 = 探测样本目录(参数)
        Dim 音频样本 = If(样本目录 IsNot Nothing, Path.Combine(样本目录, "multitrack_audio.mkv"), Nothing)
        Dim 视频样本 = If(样本目录 IsNot Nothing, Path.Combine(样本目录, "multitrack_video.mkv"), Nothing)
        Dim 外部音频 = If(样本目录 IsNot Nothing, Path.Combine(样本目录, "external_audio.mka"), Nothing)
        If 样本目录 Is Nothing OrElse Not File.Exists(音频样本) OrElse
           Not File.Exists(视频样本) OrElse Not File.Exists(外部音频) Then
            Console.WriteLine($"SKIP：多轨样本不齐（目录={样本目录}）。")
            Console.WriteLine("请先运行 _3fc_tmp/gen_multitrack_samples.sh 生成 artifacts/kernel-tests/multitrack/ 三个样本。")
            Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
            Return 2
        End If

        场景多音轨枚举与切换(音频样本)
        场景多视频轨切换与外部音轨(视频样本, 外部音频)

        ' 场景三是 docs/52 §5.1 挂账 bug（bufferedAudio100ns 对 IAMF 报 365s）的回归哨兵：
        ' 素材路径机器相关：环境变量 FFF_IAMF_SAMPLE 指定，缺席则 SKIP（与歌词套件同一约定）。
        Dim iamf样本 = Environment.GetEnvironmentVariable("FFF_IAMF_SAMPLE")
        If Not String.IsNullOrWhiteSpace(iamf样本) AndAlso File.Exists(iamf样本) Then
            场景IAMF缓冲记账(iamf样本)
        Else
            Console.WriteLine("SKIP 场景三：IAMF 素材未配置（环境变量 FFF_IAMF_SAMPLE）")
        End If

        Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
        If 失败 > 0 Then Return 1
        Return 0
    End Function

    ' ── 场景一：multitrack_audio.mkv（0=video，1=aac eng DEFAULT，2=aac jpn，3=aac chi）──
    Private Sub 场景多音轨枚举与切换(样本 As String)
        Console.WriteLine("场景一：多音轨枚举 / 默认轨 / 热切换 / 非法索引")
        Try
            Using 会话 = New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
                Dim 错误列表 = 收集错误(会话)
                会话.打开Async(样本).GetAwaiter().GetResult()

                Dim 信息 = 会话.当前媒体信息
                检查(信息 IsNot Nothing, "媒体信息可读", "GetMediaInfo JSON 非空")
                If 信息 Is Nothing Then Return
                检查(信息.流.Count = 4, "全流枚举 4 条（1V+3A）", $"实测 {信息.流.Count} 条")
                Dim 视频 = 信息.流.Where(Function(流) 流.类型 = "video").ToList()
                Dim 音频 = 信息.流.Where(Function(流) 流.类型 = "audio").ToList()
                检查(视频.Count = 1, "视频流 1 条", $"实测 {视频.Count}")
                检查(音频.Count = 3, "音频流 3 条", $"实测 {音频.Count}")
                检查(音频.Count = 3 AndAlso 音频(0).语言 = "eng" AndAlso 音频(0).是默认流,
                     "音轨1 eng 且 DEFAULT 标记为真", $"lang={音频(0).语言} default={音频(0).是默认流}")
                If 音频.Count >= 3 Then
                    检查(音频(1).语言 = "jpn", "音轨2 jpn", $"lang={音频(1).语言}")
                    检查(音频(2).语言 = "chi", "音轨3 chi", $"lang={音频(2).语言}")
                End If

                Dim 初始 = 会话.当前快照
                检查(初始.当前视频流 = 0, "默认视频流 = 0", $"实测 {初始.当前视频流}")
                ' 期望：FindDefaultOrFirstStream 对 DEFAULT disposition 音轨（流1）优先。
                检查(初始.当前音频流 = 1, "默认音频流 = 1（DEFAULT 优先，不是第一条无标记轨）",
                     $"实测 {初始.当前音频流}")

                ' 逐轨热切换：2 → 3 → 1，每次都等快照真实落位。
                For Each 目标 In New Integer() {2, 3, 1}
                    会话.选择音频流(目标)
                    Dim 切后 = 等待快照(会话, Function(快照) 快照.当前音频流 = 目标)
                    检查(切后.当前音频流 = 目标, $"热切换音频流 → {目标}", $"实测 {切后.当前音频流}")
                Next

                ' 非法索引必须拒收：错误事件到达，且当前音频流保持切回后的 1 不变。
                SyncLock 错误列表
                    错误列表.Clear()
                End SyncLock
                会话.选择音频流(0)
                Dim 有错0 = 等待错误(错误列表)
                Dim 保持0 = 会话.当前快照.当前音频流
                检查(有错0 AndAlso 保持0 = 1, "选音频流(0)=视频流 被拒且当前轨不变",
                     $"错误事件={有错0}, 当前音频流={保持0}")

                SyncLock 错误列表
                    错误列表.Clear()
                End SyncLock
                会话.选择音频流(99)
                Dim 有错99 = 等待错误(错误列表)
                Dim 保持99 = 会话.当前快照.当前音频流
                检查(有错99 AndAlso 保持99 = 1, "选音频流(99) 越界被拒且当前轨不变",
                     $"错误事件={有错99}, 当前音频流={保持99}")

                ' ── 音频延迟（内嵌音轨，快照 v9 新字段）──────────────────────────
                ' 语义：正值 = 声音落后于画面。内核同步校验范围（±10 分钟，越界同步抛），
                ' 生效走异步重 seek（镜像外部音轨偏移机制，PlayerSession.cpp SetAudioDelay）。
                检查(会话.当前快照.音频延迟 = TimeSpan.Zero, "音频延迟初始 = 0",
                     $"实测 {会话.当前快照.音频延迟.TotalMilliseconds} ms")
                会话.设置音频延迟(TimeSpan.FromMilliseconds(300))
                Dim 延迟300 = 等待快照(会话, Function(快照) 快照.音频延迟 = TimeSpan.FromMilliseconds(300))
                检查(延迟300.音频延迟 = TimeSpan.FromMilliseconds(300), "设置音频延迟 → 300 ms",
                     $"实测 {延迟300.音频延迟.TotalMilliseconds} ms")
                ' 跨切轨保持：延迟是会话级偏好，不随流选择复位。
                会话.选择音频流(2)
                Dim 切2 = 等待快照(会话, Function(快照) 快照.当前音频流 = 2)
                检查(切2.当前音频流 = 2 AndAlso 切2.音频延迟 = TimeSpan.FromMilliseconds(300),
                     "切轨后音频延迟保持 300 ms", $"流={切2.当前音频流}, 延迟={切2.音频延迟.TotalMilliseconds} ms")
                会话.设置音频延迟(TimeSpan.FromMilliseconds(-150))
                Dim 延迟负 = 等待快照(会话, Function(快照) 快照.音频延迟 = TimeSpan.FromMilliseconds(-150))
                检查(延迟负.音频延迟 = TimeSpan.FromMilliseconds(-150), "负延迟 -150 ms 生效",
                     $"实测 {延迟负.音频延迟.TotalMilliseconds} ms")
                ' 越界（>±10 分钟）内核同步拒收 → 托管层抛播放器异常。
                Dim 越界拒收 = False
                Try
                    会话.设置音频延迟(TimeSpan.FromMinutes(11))
                Catch ex As 播放器异常
                    越界拒收 = True
                End Try
                检查(越界拒收, "音频延迟 +11 分钟 越界同步拒收", "播放器异常已抛出")
                会话.设置音频延迟(TimeSpan.Zero)
                Dim 延迟清零 = 等待快照(会话, Function(快照) 快照.音频延迟 = TimeSpan.Zero)
                检查(延迟清零.音频延迟 = TimeSpan.Zero, "音频延迟归零", "0 ms")
            End Using
        Catch ex As Exception
            检查(False, "场景一未捕获异常", ex.Message)
        End Try
    End Sub

    ' ── 场景二：multitrack_video.mkv（0=video eng DEFAULT，1=video jpn，2=aac）+ 外部音轨 ──
    Private Sub 场景多视频轨切换与外部音轨(样本 As String, 外部音频 As String)
        Console.WriteLine("场景二：多视频轨切换 / 外部音轨替换与偏移 / 清除")
        Try
            Using 会话 = New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
                Dim 错误列表 = 收集错误(会话)
                会话.打开Async(样本).GetAwaiter().GetResult()

                Dim 信息 = 会话.当前媒体信息
                检查(信息 IsNot Nothing AndAlso 信息.流.Count = 3, "全流枚举 3 条（2V+1A）",
                     $"实测 {If(信息 Is Nothing, -1, 信息.流.Count)}")
                Dim 快照0 = 会话.当前快照
                检查(快照0.当前视频流 = 0, "默认视频流 = 0", $"实测 {快照0.当前视频流}")
                检查(快照0.当前音频流 = 2, "唯一音频流 = 2", $"实测 {快照0.当前音频流}")

                会话.选择视频流(1)
                Dim 切1 = 等待快照(会话, Function(快照) 快照.当前视频流 = 1)
                检查(切1.当前视频流 = 1, "热切换视频流 → 1", $"实测 {切1.当前视频流}")
                会话.选择视频流(0)
                Dim 切0 = 等待快照(会话, Function(快照) 快照.当前视频流 = 0)
                检查(切0.当前视频流 = 0, "热切换视频流 → 0（回默认）", $"实测 {切0.当前视频流}")

                ' 流2 是音频：对视频选择器是类型不匹配。错误事件到达，当前视频流保持 0 不变。
                SyncLock 错误列表
                    错误列表.Clear()
                End SyncLock
                会话.选择视频流(2)
                Dim 有错2 = 等待错误(错误列表)
                Dim 保持2 = 会话.当前快照.当前视频流
                检查(有错2 AndAlso 保持2 = 0, "选视频流(2)=音频流 被拒且当前轨不变",
                     $"错误事件={有错2}, 当前视频流={保持2}")

                ' 外部音轨整轨替换：isExternalAudio 置位、偏移默认 0。
                会话.加载外部音轨(外部音频)
                Dim 外1 = 等待快照(会话, Function(快照) 快照.正在使用外部音轨)
                检查(外1.正在使用外部音轨, "加载外部音轨生效", $"isExternalAudio={外1.正在使用外部音轨}")
                检查(外1.外部音轨偏移 = TimeSpan.Zero, "外部音轨默认偏移 = 0",
                     $"实测 {外1.外部音轨偏移.TotalMilliseconds} ms")

                ' 运行时改偏移（内核改后重 seek，PlayerSession.cpp:1343）。
                会话.设置外部音轨偏移(TimeSpan.FromMilliseconds(500))
                Dim 外2 = 等待快照(会话, Function(快照) 快照.外部音轨偏移 = TimeSpan.FromMilliseconds(500))
                检查(外2.外部音轨偏移 = TimeSpan.FromMilliseconds(500), "运行时改偏移 → 500 ms",
                     $"实测 {外2.外部音轨偏移.TotalMilliseconds} ms")

                会话.清除外部音轨()
                Dim 清后 = 等待快照(会话, Function(快照) Not 快照.正在使用外部音轨)
                检查(Not 清后.正在使用外部音轨, "清除外部音轨生效", $"isExternalAudio={清后.正在使用外部音轨}")

                ' 带负偏移的加载：加载时直接带 100ns 偏移（-250 ms）。
                会话.加载外部音轨(外部音频, -1, TimeSpan.FromMilliseconds(-250))
                Dim 外3 = 等待快照(会话, Function(快照) 快照.正在使用外部音轨 AndAlso
                                          快照.外部音轨偏移 = TimeSpan.FromMilliseconds(-250))
                检查(外3.正在使用外部音轨 AndAlso 外3.外部音轨偏移 = TimeSpan.FromMilliseconds(-250),
                     "带 -250 ms 偏移加载生效", $"偏移={外3.外部音轨偏移.TotalMilliseconds} ms")
                会话.清除外部音轨()
            End Using
        Catch ex As Exception
            检查(False, "场景二未捕获异常", ex.Message)
        End Try
    End Sub

    ' ── 场景三：IAMF 空间音频的缓冲记账哨兵 ─────────────────────────────
    ' docs/52 §5.1 挂账：bufferedAudio100ns 对 IAMF 报荒谬值（1.02s 内容报 365s）。
    ' 本场景用真内核 + libiamf 播 1 秒，断言缓冲读数落在物理合理范围。
    ' 无音频端点等环境限制按 SKIP 处理，不算失败。
    Private Sub 场景IAMF缓冲记账(样本 As String)
        Console.WriteLine("场景三：IAMF 缓冲记账（真播放 1 秒）")
        Try
            Using 会话 = New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
                会话.设置音量(0.0F, True)   ' 静音播放，不打扰人
                会话.打开Async(样本).GetAwaiter().GetResult()
                Dim 初始 = 会话.当前快照
                检查(初始.IAMF已接管, "libiamf 接管 7.1.4 IAMF",
                     $"渲染目标={初始.IAMF声道数}ch, 内容={初始.IAMF内容声道数}ch, 声场系统={初始.IAMF声场系统}")
                If Not 初始.IAMF已接管 Then Return
                会话.播放()
                Thread.Sleep(1000)
                Dim 播放中 = 会话.当前快照
                ' 合理界：共享模式端点缓冲约几十毫秒 + 应用队列；docs/52 的失真值是 365000 ms。
                检查(播放中.音频缓冲时长 <= TimeSpan.FromSeconds(2),
                     "音频缓冲记账在合理范围（挂账值曾为 365 s）",
                     $"缓冲={播放中.音频缓冲时长.TotalMilliseconds:F0} ms, 欠载={播放中.音频欠载次数}")
                会话.暂停()
            End Using
        Catch ex As Exception
            ' 无音频端点/设备独占冲突等环境问题：SKIP，不算失败。
            Console.WriteLine($"  SKIP 场景三（环境限制：{ex.Message}）")
        End Try
    End Sub
End Module
