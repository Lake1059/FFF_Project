' RTX 两行判词的进程内探针 —— 全枚举、判词与配色、行装配
'
' 背景：Tab 图层的 超分辨率 / 自动HDR 两行由内核逐帧判定驱动，宿主不参与判定，
' 只在 播放器会话 侧把结构体翻成"判词 + 一句人话"，再由呈现器按判词前缀着色。
' 三段都可能悄悄坏：原因码新增一个值而宿主漏分支（内核明确报了值，界面却说
' "内核未报告原因"）、配色把常规态涂成告警（或反过来）、提供器读不到时伪造一行。
'
' 本探针的期望值**一律由结构体字段算**（生效 / 请求模式 / 原因），不从被判文本反推，
' 否则就是拿实现验证实现：
'   1) 判词三态与字段一致：生效≠0 ⇒ 已激活/已启用；生效=0 且 请求模式=0 ⇒ 未开启；
'      其余 ⇒ 未生效 开头。这条正面钉死"跳过态不许涂成常规态"。
'   2) 配色 = 该三态对应常量（反射读 Private Const，要求三色互不相同）。
'   3) 人话逐原因互不相同：原因 2..7 的 状态文本 不得等于 Case Else 的
'      "内核未报告原因"。新增原因码漏分支时，第 1、2 条照样绿，只有这一条会红
'      ——本 PR 自己补的 Case 7 正是这类缺陷。
'   4) NotRequested 与请求态分开：请求=Auto 而原因停在初值（自动 HDR 占用通道的
'      冷启动形态）必须判成"未生效"，不得判成"未开启"。
'   5) 闩兜底：越界原因 + 驱动拒绝闩=1 ⇒ "驱动拒绝过"。
'   6) 行装配走真呈现器：两行按标签落位、内容等于组好的文本；读不到时整行不出现。
'
' 本探针**不覆盖**的：配色落地到图层命令的像素（读取调试文本行 丢色）、
' 真机内核时序（那属 --…-live 一类）、以及上述人话的措辞好坏。
'
' 用法：FFF.Player.Tests --rtx-overlay-rows
' 退出码：0 全绿 / 1 有失败条 / 2 前置不满足（配色常量读不到＝探针坏了，按判红接线）

Imports System
Imports System.Collections.Generic
Imports System.Drawing
Imports System.Reflection
Imports FFF.Player

Friend Module RtxOverlayRowProbe
    Private 通过 As Integer
    Private 失败 As Integer

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"  ok   {名称}  {实测}")
        Else
            失败 += 1
            Console.WriteLine($"  FAIL {名称}  实测=[{实测}]")
        End If
    End Sub

    ''' <summary>反射读 Private Const。用 found 标志而不是哨兵值：合法 ARGB 可以是
    ''' &amp;HFFFFFFFF，拿 UInteger.MaxValue 当"没读到"会与白色撞车。</summary>
    Private Function 常量颜色(名称 As String, ByRef 找到 As Boolean) As UInteger
        找到 = False
        Dim 字段 = GetType(播放器信息图层呈现器).GetField(名称,
                       BindingFlags.NonPublic Or BindingFlags.Static Or BindingFlags.Public)
        If 字段 Is Nothing OrElse Not 字段.IsLiteral Then Return 0UI
        找到 = True
        Return Convert.ToUInt32(字段.GetRawConstantValue())
    End Function

    Private Function 媒体信息合成() As 媒体信息
        Dim 流 = New 媒体流信息 With {
            .索引 = 0, .类型 = "video", .编码 = "hevc", .宽度 = 1280, .高度 = 720}
        Return New 媒体信息 With {.流 = New List(Of 媒体流信息) From {流}}
    End Function

    Private Function 造快照() As 播放器快照
        Dim 原生 As New 原生播放器快照 With {
            .大小 = CUInt(Runtime.InteropServices.Marshal.SizeOf(Of 原生播放器快照)()),
            .状态 = CUInt(播放状态.正在播放),
            .解码器 = CUInt(解码模式.GPU),
            .请求色彩模式 = CUInt(色彩输出模式.映射到SDR),
            .实际色彩模式 = CUInt(色彩输出模式.映射到SDR),
            .当前视频流 = 0,
            .视频宽度 = 1280UI, .视频高度 = 720UI,
            .视频队列帧数 = 8UI,
            .显示器峰值尼特 = 1000UI}
        Return New 播放器快照(原生)
    End Function

    Private Function 造超分状态(生效 As UInteger, 请求 As UInteger, 原因 As UInteger,
                                 Optional 闩 As UInteger = 0UI) As 原生视频超分辨率状态
        Return New 原生视频超分辨率状态 With {
            .生效 = 生效, .请求模式 = 请求, .原因 = 原因, .驱动拒绝闩 = 闩,
            .源宽 = 1280UI, .源高 = 720UI, .目标宽 = 2560UI, .目标高 = 1440UI}
    End Function

    Private Function 造自动HDR状态(生效 As UInteger, 请求 As UInteger, 原因 As UInteger,
                                   Optional 闩 As UInteger = 0UI) As 原生视频自动HDR状态
        Return New 原生视频自动HDR状态 With {
            .生效 = 生效, .请求模式 = 请求, .原因 = 原因, .驱动拒绝闩 = 闩,
            .源宽 = 1280UI, .源高 = 720UI, .输出宽 = 2560UI, .输出高 = 1440UI}
    End Function

    Private Function 取行(呈现器 As 播放器信息图层呈现器, 标签 As String) As String
        For Each 文本 In 呈现器.读取调试文本行(媒体信息合成(), 造快照(), "C:\probe\rtx.mp4")
            If 文本.StartsWith(标签, StringComparison.Ordinal) Then Return 文本
        Next
        Return Nothing
    End Function

    Private Function 造呈现器(超分 As Func(Of String), 自动HDR As Func(Of String)) As 播放器信息图层呈现器
        Dim 画面 As New 播放器画面控件()
        Dim 快照 = 造快照()
        Return New 播放器信息图层呈现器(
            画面,
            Function() 快照,
            AddressOf 媒体信息合成,
            Function() "C:\probe\rtx.mp4",
            Function() Nothing, Function() Nothing, Function() Nothing, Function() Nothing,
            Function() WASAPI共享模式.共享,
            Function() Nothing,
            Sub(大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令), 序号 As ULong, 值 As Single)
            End Sub,
            超分, 自动HDR)
    End Function

    Friend Function 运行() As Integer
        Console.WriteLine("3FP RTX 图层两行探针（--rtx-overlay-rows：字段定判词 + 配色 + 人话互异 + 行装配）")
        Dim 绿找到 As Boolean, 青找到 As Boolean, 警找到 As Boolean
        Dim 绿 = 常量颜色("绿色", 绿找到), 青 = 常量颜色("青色", 青找到), 警 = 常量颜色("告警色", 警找到)
        If Not (绿找到 AndAlso 青找到 AndAlso 警找到) Then
            Console.WriteLine("  SKIP 读不到配色常量（呈现器改名或常量不再是 Const）")
            Return 2
        End If
        检查(Not (绿 = 青 OrElse 青 = 警 OrElse 绿 = 警), "三个配色常量互不相同",
               $"绿={绿:X} 青={青:X} 告警={警:X}")

        ' 人话互异：先取 Case Else 的兜底句，再要求 2..7 每个原因都不落回它。
        Dim 超分兜底 = 播放器会话.视频超分辨率状态文本(造超分状态(0UI, 1UI, 9UI))
        Dim HDR兜底 = 播放器会话.视频自动HDR状态文本(造自动HDR状态(0UI, 1UI, 9UI))
        For 原因 = 2UI To 7UI
            检查(播放器会话.视频超分辨率状态文本(造超分状态(0UI, 1UI, 原因)) <> 超分兜底,
                   $"超分辨率 原因={原因} 有专属人话（不落回内核未报告原因）",
                   播放器会话.视频超分辨率状态文本(造超分状态(0UI, 1UI, 原因)))
            检查(播放器会话.视频自动HDR状态文本(造自动HDR状态(0UI, 1UI, 原因)) <> HDR兜底,
                   $"自动HDR 原因={原因} 有专属人话（不落回内核未报告原因）",
                   播放器会话.视频自动HDR状态文本(造自动HDR状态(0UI, 1UI, 原因)))
        Next
        检查(播放器会话.视频超分辨率状态文本(造超分状态(0UI, 1UI, 9UI, 闩:=1UI)) = "驱动拒绝过",
               "超分辨率 越界原因 + 闩=1 走驱动拒绝兜底",
              播放器会话.视频超分辨率状态文本(造超分状态(0UI, 1UI, 9UI, 闩:=1UI)))

        ' 全枚举：生效 × 请求 × 原因(0..8)。期望三态只由字段决定。
        For Each 特性 In New String() {"超分辨率", "自动HDR"}
            Dim 前缀生效 = If(特性 = "超分辨率", "已激活", "已启用")
            For 生效 = 0UI To 1UI
                For 请求 = 0UI To 1UI
                    For 原因 = 0UI To 8UI
                        Dim 文本 = If(特性 = "超分辨率",
                                      播放器会话.视频超分辨率图层文本(造超分状态(生效, 请求, 原因)),
                                      播放器会话.视频自动HDR图层文本(造自动HDR状态(生效, 请求, 原因)))
                        Dim 类别 = If(生效 <> 0UI, "active", If(请求 = 0UI, "off", "skipped"))
                        Dim 期望颜色 = If(类别 = "active", 绿, If(类别 = "off", 青, 警))
                        Dim 判词 = 播放器信息图层呈现器.判词颜色(文本)
                        Dim 态 = $"生效={生效} 请求={请求} 原因={原因}"
                        检查(If(类别 = "active", 文本.StartsWith(前缀生效, StringComparison.Ordinal),
                                 Not 文本.StartsWith(前缀生效, StringComparison.Ordinal)),
                               $"{特性} {态} 生效态与 生效 字段一致", 文本)
                        检查(If(类别 = "off", 文本 = "未开启",
                                 Not 文本.StartsWith("未开启", StringComparison.Ordinal)),
                               $"{特性} {态} 未开启只属于 请求=0", 文本)
                        检查(If(类别 = "skipped", 文本.StartsWith("未生效", StringComparison.Ordinal), True),
                               $"{特性} {态} 已请求未生效必须有未生效判词", 文本)
                        检查(判词 = 期望颜色, $"{特性} {态} 配色随字段判定的三态", $"颜色={判词:X}")
                        ' 只在"已请求"时才能要求说清让位：请求=0 时"未开启"才是对的。
                        If 生效 = 0UI AndAlso 请求 = 1UI AndAlso 原因 = 7UI AndAlso 特性 = "超分辨率" Then
                            检查(文本.Contains("自动 HDR"),
                                   $"{特性} {态} 说清是让位给自动 HDR", 文本)
                        End If
                    Next
                Next
            Next
        Next

        ' 行装配：真呈现器，两行按标签出现且内容等于组好的文本。
        Dim 超分文本 = 播放器会话.视频超分辨率图层文本(造超分状态(0UI, 1UI, 3UI))
        Dim HDR文本 = 播放器会话.视频自动HDR图层文本(造自动HDR状态(1UI, 1UI, 0UI))
        Dim 呈现器 = 造呈现器(Function() 超分文本, Function() HDR文本)
        检查(取行(呈现器, "超分辨率：") = "超分辨率：" & 超分文本, "行装配 超分行按标签落位",
              取行(呈现器, "超分辨率："))
        检查(取行(呈现器, "自动HDR：") = "自动HDR：" & HDR文本, "行装配 自动HDR行按标签落位",
              取行(呈现器, "自动HDR："))

        ' 负对照：内核读不到（没有任何提供器）时整行不出现，不伪造一个"未生效"。
        Dim 空呈现器 = 造呈现器(Nothing, Nothing)
        检查(取行(空呈现器, "超分辨率：") Is Nothing AndAlso 取行(空呈现器, "自动HDR：") Is Nothing,
             "负对照 读数缺失时两行都不出现",
             $"{If(取行(空呈现器, "超分辨率："), "<无>")} / {If(取行(空呈现器, "自动HDR："), "<无>")}")

        Console.WriteLine($"RTX 图层两行探针：通过 {通过}，失败 {失败}")
        Return If(失败 = 0, 0, 1)
    End Function
End Module
