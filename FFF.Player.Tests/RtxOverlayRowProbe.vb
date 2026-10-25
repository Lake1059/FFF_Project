' RTX 两行判词的进程内探针 —— 全枚举、配色、行装配
'
' 背景：Tab 图层的 超分辨率 / 自动HDR 两行由内核逐帧判定驱动，宿主不参与判定，
' 只在 播放器会话 侧把结构体翻成"判词 + 一句人话"，再由呈现器按判词前缀着色。
' 三段都可能悄悄坏：原因码新增一个值而宿主漏分支（内核明确报了值，界面却说
' "内核未报告原因"）、配色把常规态涂成告警（或反过来把跳过态涂成常规）、
' 提供器读不到时伪造一行。本探针按这三段收口，全部用真类型真函数，不复制逻辑。
'
' 判据：
'   1) 生效×原因 全枚举（0..7 各 8 值 × 生效 0/1）都产出非空文本 —— 空枚举必拦，
'      新增原因码若没分支，这里会以"文本为空"或"落到未报告原因"暴露。
'   2) 三态前缀：生效=1 走"已激活/已启用"，原因=1 走"未开启"，其余走"未生效 · "，
'      且 生效=1 时不得出现"未生效"字样（勾选态不参与判定，只有 生效 参与）。
'   3) reason=7（YieldedToAutoHdr）必须说清"让位给自动 HDR"，不得报"内核未报告原因"。
'   4) 配色取真常量比对（反射读 Private Const），要求三态各得其所且互不相同。
'   5) 行装配用真呈现器：两行按标签出现、内容等于组好的文本；提供器返回 Nothing
'      时整行不出现（不伪造"未生效"）。
'
' 用法：FFF.Player.Tests --rtx-overlay-rows
' 退出码：0 全绿 / 1 有失败条 / 2 前置不满足

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

    Private Function 常量颜色(名称 As String) As UInteger
        Dim 字段 = GetType(播放器信息图层呈现器).GetField(名称,
                       BindingFlags.NonPublic Or BindingFlags.Static Or BindingFlags.Public)
        If 字段 Is Nothing Then Return UInteger.MaxValue
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

    Private Function 造超分状态(生效 As UInteger, 原因 As UInteger) As 原生视频超分辨率状态
        Return New 原生视频超分辨率状态 With {
            .生效 = 生效, .原因 = 原因, .源宽 = 1280UI, .源高 = 720UI, .目标宽 = 2560UI, .目标高 = 1440UI}
    End Function

    Private Function 造自动HDR状态(生效 As UInteger, 原因 As UInteger) As 原生视频自动HDR状态
        Return New 原生视频自动HDR状态 With {
            .生效 = 生效, .原因 = 原因, .源宽 = 1280UI, .源高 = 720UI, .输出宽 = 2560UI, .输出高 = 1440UI}
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
        Console.WriteLine("3FP RTX 图层两行探针（--rtx-overlay-rows：全枚举 + 配色 + 行装配）")
        Dim 绿 = 常量颜色("绿色"), 青 = 常量颜色("青色"), 警 = 常量颜色("告警色")
        If 绿 = UInteger.MaxValue OrElse 青 = UInteger.MaxValue OrElse 警 = UInteger.MaxValue Then
            Console.WriteLine("  SKIP 读不到配色常量（呈现器改名？）")
            Return 2
        End If
        检查(Not (绿 = 青 OrElse 青 = 警 OrElse 绿 = 警), "三个配色常量互不相同",
               $"绿={绿:X} 青={青:X} 告警={警:X}")

        For Each 特性 In New String() {"超分辨率", "自动HDR"}
            Dim 前缀生效 = If(特性 = "超分辨率", "已激活", "已启用")
            For 生效 = 0UI To 1UI
                For 原因 = 0UI To 7UI
                    Dim 文本 = If(特性 = "超分辨率",
                                  播放器会话.视频超分辨率图层文本(造超分状态(生效, 原因)),
                                  播放器会话.视频自动HDR图层文本(造自动HDR状态(生效, 原因)))
                    Dim 判词 = 播放器信息图层呈现器.判词颜色(文本)
                    Dim 期望颜色 = If(文本.StartsWith("未生效", StringComparison.Ordinal), 警,
                                   If(文本.StartsWith("未开启", StringComparison.Ordinal), 青, 绿))
                    Dim 态 = $"生效={生效} 原因={原因}"
                    检查(Not String.IsNullOrWhiteSpace(文本), $"{特性} {态} 有文本", 文本)
                    检查(If(生效 = 1UI,
                           文本.StartsWith(前缀生效, StringComparison.Ordinal),
                           Not 文本.StartsWith(前缀生效, StringComparison.Ordinal)),
                           $"{特性} {态} 生效判词与 生效 字段一致", 文本)
                    检查(If(生效 = 0UI AndAlso 原因 = 1UI,
                           文本 = "未开启", Not 文本.StartsWith("未开启", StringComparison.Ordinal)),
                           $"{特性} {态} 未开启只属于 原因=1", 文本)
                    检查(判词 = 期望颜色, $"{特性} {态} 配色随判词", $"颜色={判词:X}")
                    ' 只适用于超分：FFF3FPVideoAutoHdrReason 的 7 是 DriverRejected，
                    ' 与 FFF3FPVideoSuperResolutionReason 的 7（YieldedToAutoHdr）不同名同数。
                    If 生效 = 0UI AndAlso 原因 = 7UI AndAlso 特性 = "超分辨率" Then
                        检查(文本.Contains("自动 HDR") AndAlso Not 文本.Contains("内核未报告原因"),
                             $"{特性} {态} 说清是让位而不是未报告", 文本)
                    End If
                Next
            Next
        Next

        ' 行装配：真呈现器，两行按标签出现且内容等于组好的文本。
        Dim 超分文本 = 播放器会话.视频超分辨率图层文本(造超分状态(0UI, 3UI))
        Dim HDR文本 = 播放器会话.视频自动HDR图层文本(造自动HDR状态(1UI, 0UI))
        Dim 呈现器 = 造呈现器(Function() 超分文本, Function() HDR文本)
        Dim 超分行 = 取行(呈现器, "超分辨率：")
        Dim HDR行 = 取行(呈现器, "自动HDR：")
        检查(超分行 = "超分辨率：" & 超分文本, "行装配 超分行按标签落位", 超分行)
        检查(HDR行 = "自动HDR：" & HDR文本, "行装配 自动HDR行按标签落位", HDR行)

        ' 负对照：内核读不到（提供器答 Nothing）时整行不出现，不伪造一个"未生效"。
        Dim 空呈现器 = 造呈现器(Nothing, Nothing)
        检查(取行(空呈现器, "超分辨率：") Is Nothing AndAlso
             取行(空呈现器, "自动HDR：") Is Nothing,
             "负对照 读数缺失时两行都不出现",
             $"超分={If(取行(空呈现器, "超分辨率："), "<无>")} HDR={If(取行(空呈现器, "自动HDR："), "<无>")}")

        Console.WriteLine($"RTX 图层两行探针：通过 {通过}，失败 {失败}")
        Return If(失败 = 0, 0, 1)
    End Function
End Module
