' 色彩档位同进程回归 —— HDR 源显式 SDR 档必须保持，SDR 源按 Auto 策略抬升
'
' 补的是 PR #33 在 EnsureSwapChain 里改的那条规则：HDR 源 + 调用方显式选
' 映射SDR / 原始HDR按SDR 时，档位不能被支持 HDR 的输出重新抬回峰值映射HDR；
' 而 >8bit 的 SDR 源仍按 sdrScRgbMode 的 Auto 策略进 scRGB 链。判据读两层：
' 快照的请求/实际档 + 同一呈现链的中心像素色彩模式与输出位深。
'
' 本档要求输出窗口所在主屏支持 HDR；不支持时返回 2（未判定），不把
' "显式 SDR 在 SDR 屏上当然还是 SDR" 洗成绿。
'
' 用法：FFF.Player.Tests --color-tier-regression [HDR10夹具] [8bit SDR夹具] [10bit SDR夹具]
' 退出码：0 全绿 / 1 有失败条 / 2 夹具或屏幕不具备
Imports System
Imports System.Collections.Generic
Imports System.Drawing
Imports System.IO
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module ColorTierRegression
    Private 通过 As Integer
    Private 失败 As Integer

    Private Const 默认Hdr10 As String =
        "C:\PLAN\3FCompare\testmedia\media\hdr10plus\real_hdr10plus_nocurve.mp4"
    Private Const 默认Sdr8 As String =
        "C:\PLAN\3FCompare\testmedia\media\small.mp4"
    Private Const 默认Sdr10 As String =
        "C:\PLAN\3FCompare\testmedia\media\real\real_4k_hevc10_60m.mp4"

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"  ok   {名称}  {实测}")
        Else
            失败 += 1
            Console.WriteLine($"  FAIL {名称}  {实测}")
        End If
    End Sub

    Private Function 档名(值 As 色彩输出模式) As String
        Select Case 值
            Case 色彩输出模式.映射到SDR : Return "映射SDR"
            Case 色彩输出模式.原始HDR按SDR呈现 : Return "原始HDR按SDR"
            Case 色彩输出模式.峰值映射HDR : Return "峰值映射HDR"
            Case Else : Return CInt(值).ToString()
        End Select
    End Function

    Private Class 读数
        Public 请求 As 色彩输出模式
        Public 实际 As 色彩输出模式
        Public 位深 As Integer
        Public 像素模式 As 色彩输出模式
        Public 像素位深 As Integer
        Public 帧 As Long
    End Class

    Private Function 采样(会话 As 播放器会话) As List(Of 读数)
        Dim 列表 As New List(Of 读数)
        For i = 1 To 5
            Application.DoEvents()
            Thread.Sleep(100)
            Dim s = 会话.当前快照
            Dim p = 会话.读取视频输出原始像素(320, 180)
            列表.Add(New 读数 With {
                .请求 = s.请求色彩模式, .实际 = s.实际色彩模式, .位深 = s.视频输出位深度,
                .像素模式 = p.色彩模式, .像素位深 = p.输出位深度, .帧 = s.帧序号})
        Next
        Return 列表
    End Function

    Private Function 全为(值 As List(Of 读数), 字段 As Func(Of 读数, Integer), 期望 As Integer) As Boolean
        If 值.Count = 0 Then Return False
        For Each x In 值
            If 字段(x) <> 期望 Then Return False
        Next
        Return True
    End Function

    Private Sub 判一档(标签 As String, 会话 As 播放器会话, 期望实际 As 色彩输出模式,
                        Optional 期望请求 As Integer = -1, Optional 期望位深 As Integer = -1)
        Dim 样本 = 采样(会话)
        Dim 请求期望 = If(期望请求 >= 0, 期望请求, CInt(期望实际))
        Dim 末 = 样本(样本.Count - 1)
        检查(全为(样本, Function(x) CInt(x.实际), CInt(期望实际)),
            $"{标签}·实际档稳定 = {档名(期望实际)}",
            $"实际={末.实际} 请求={末.请求} 位深={末.位深}")
        检查(全为(样本, Function(x) CInt(x.请求), 请求期望),
            $"{标签}·请求档 = {档名(CType(请求期望, 色彩输出模式))}", $"请求={末.请求}")
        检查(全为(样本, Function(x) CInt(x.像素模式), CInt(期望实际)),
            $"{标签}·像素回读色彩模式 = {档名(期望实际)}",
            $"像素模式={末.像素模式} 像素位深={末.像素位深}")
        检查(New HashSet(Of Long)(样本.ConvertAll(Function(x) x.帧)).Count > 1,
            $"{标签}·采样窗内有真帧推进", $"{末.帧} 帧序号")
        If 期望位深 > 0 Then
            检查(末.像素位深 = 期望位深, $"{标签}·像素位深 = {期望位深}",
                $"实测={末.像素位深} 快照={末.位深}")
        End If
    End Sub

    Private Function 等播放(会话 As 播放器会话, 超时秒 As Double) As Boolean
        Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(超时秒)
        While DateTimeOffset.UtcNow < 截止
            Dim s = 会话.当前快照
            If s.状态 = 播放状态.正在播放 AndAlso s.帧序号 > 5 Then Return True
            Application.DoEvents()
            Thread.Sleep(50)
        End While
        Return False
    End Function

    Private Function 开会话(窗口 As Form, 夹具 As String, 模式 As 色彩输出模式) As 播放器会话
        Dim 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU, .输出窗口句柄 = 窗口.Handle,
            .色彩模式 = 模式, .SDRscRGB模式 = 1UI})
        会话.设置音量(0, True)
        会话.打开Async(夹具).GetAwaiter().GetResult()
        会话.播放()
        Return 会话
    End Function

    Private Sub 切到(会话 As 播放器会话, 模式 As 色彩输出模式)
        会话.设置色彩模式(模式, 100.0F, 0.0F, 203.0F, False)
        Thread.Sleep(500)
        Application.DoEvents()
    End Sub

    Friend Function 运行(args As String()) As Integer
        Dim Hdr10 = If(args.Length > 0, args(0), 默认Hdr10)
        Dim Sdr8 = If(args.Length > 1, args(1), 默认Sdr8)
        Dim Sdr10 = If(args.Length > 2, args(2), 默认Sdr10)
        Console.WriteLine("色彩档位回归（真会话 + 真窗口；要求主屏支持 HDR）")
        For Each 路径 In {Hdr10, Sdr8, Sdr10}
            If Not File.Exists(路径) Then
                Console.WriteLine($"EXIT 2 夹具不存在：{路径} —— 未测")
                Return 2
            End If
        Next

        Dim 窗体 As New Form() With {
            .Text = "color-tier-regression", .StartPosition = FormStartPosition.Manual,
            .Location = New Point(100, 100), .ClientSize = New Size(640, 360),
            .ShowInTaskbar = False}
        Dim 画面 As New 播放器画面控件() With {.Dock = DockStyle.Fill}
        窗体.Controls.Add(画面)
        窗体.Show()
        Application.DoEvents()
        Thread.Sleep(300)
        Try
            Using 会话 = 开会话(窗体, Hdr10, 色彩输出模式.映射到SDR)
                If Not 等播放(会话, 30.0) Then
                    Console.WriteLine("EXIT 2 HDR10 夹具 30s 内没进播放状态 —— 未测")
                    Return 2
                End If
                判一档("HDR源·初始映射SDR", 会话, 色彩输出模式.映射到SDR, 期望位深:=10)
                切到(会话, 色彩输出模式.原始HDR按SDR呈现)
                判一档("HDR源·切原始HDR按SDR", 会话, 色彩输出模式.原始HDR按SDR呈现, 期望位深:=10)
                切到(会话, 色彩输出模式.峰值映射HDR)
                判一档("HDR源·切峰值映射HDR", 会话, 色彩输出模式.峰值映射HDR, 期望位深:=16)
                If 会话.当前快照.实际色彩模式 <> 色彩输出模式.峰值映射HDR Then
                    Console.WriteLine("EXIT 2 主屏没有走通 HDR 输出，显式 SDR 保持判据不成立 —— 未测")
                    Return 2
                End If
                切到(会话, 色彩输出模式.映射到SDR)
                判一档("HDR源·切回映射SDR（不会每帧弹回HDR）", 会话, 色彩输出模式.映射到SDR, 期望位深:=10)
            End Using

            Using 会话 = 开会话(窗体, Sdr8, 色彩输出模式.映射到SDR)
                If Not 等播放(会话, 20.0) Then
                    Console.WriteLine("EXIT 2 8bit SDR 夹具没进播放状态 —— 未测")
                    Return 2
                End If
                判一档("8bitSDR·初始映射SDR", 会话, 色彩输出模式.映射到SDR, 期望位深:=8)
                切到(会话, 色彩输出模式.峰值映射HDR)
                判一档("8bitSDR·请求HDR（源不需要，留SDR）", 会话, 色彩输出模式.映射到SDR,
                    期望请求:=CInt(色彩输出模式.峰值映射HDR), 期望位深:=8)
            End Using

            Using 会话 = 开会话(窗体, Sdr10, 色彩输出模式.映射到SDR)
                If Not 等播放(会话, 30.0) Then
                    Console.WriteLine("EXIT 2 10bit SDR 夹具没进播放状态 —— 未测")
                    Return 2
                End If
                判一档("10bitSDR·初始映射SDR（Auto 抬升）", 会话, 色彩输出模式.峰值映射HDR,
                    期望请求:=CInt(色彩输出模式.映射到SDR), 期望位深:=16)
                切到(会话, 色彩输出模式.峰值映射HDR)
                判一档("10bitSDR·请求HDR", 会话, 色彩输出模式.峰值映射HDR,
                    期望请求:=CInt(色彩输出模式.峰值映射HDR), 期望位深:=16)
            End Using
        Catch ex As Exception
            Console.WriteLine()
            Console.WriteLine("环境不具备，本轮不产出结论：" & ex.GetType().Name & " " & ex.Message)
            Return 2
        Finally
            窗体.Close()
            窗体.Dispose()
            Application.DoEvents()
            Thread.Sleep(300)
        End Try

        Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
        Return If(失败 > 0, 1, 0)
    End Function
End Module
