' 弹幕节拍实机探针 —— 真播放会话 + 真输出窗口，采样 呈现节律状态 与真呈现器泵
'
' 验证的事（对应 播放器弹幕图层呈现器.vb 的显示器节拍 + VideoRenderer
' FrameRateBoundHz 的图层目标抬升子句）：
'   · 泵间隔由窗口所在显示器决定，目标最高 120Hz；整数毫秒取整后允许相邻值交替。
'   · 内核 overlay 合成上限抬到图层目标，但绝不超过声明面板率。
'   · 片源快于面板时丢弃进显示速率桶，不重复计入管线丢帧。
'   · switch 臂：同一呈现器跨片源，解码计数归零后 1s 内回到 ≤9ms。
'
' manual 臂是探针自带的 16ms 泵，用于只验证内核 bound 公式；real/switch 臂由真
' 播放器弹幕图层呈现器 驱动，测的是宿主实际节拍。两臂对照在探针外做：用旧内核
' 或旧呈现器跑同一命令，读数分叉才算改动有牙。
'
' 用法：FFF.Player.Tests --danmaku-pacing-live [夹具路径] [播放秒数] [manual|real|switch] [offscreen|visible|60hz] [B夹具]
'   visible=主屏上屏；60hz=本机 60 Hz 显示器上屏（找不到记未判）；缺省=屏幕外。
'   switch=同一呈现器先播 A（缺省 24fps，播放秒数=前段秒数）再原位开 B（缺省同目录
'          clip_120fps.mp4），验证解码计数归零后泵间隔在 1s 内回到 ≤9ms；B 观察 4s。
' 退出码：0 结构判据绿 / 1 有失败条 / 2 夹具等前置条件不对

Imports System
Imports System.Collections.Generic
Imports System.Drawing
Imports System.IO
Imports System.Globalization
Imports System.Runtime.InteropServices
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module DanmakuPacingLive
    Private 通过 As Integer
    Private 失败 As Integer

    <StructLayout(LayoutKind.Sequential, CharSet:=CharSet.Unicode)>
    Private Structure DEVMODE
        <MarshalAs(UnmanagedType.ByValTStr, SizeConst:=32)> Public dmDeviceName As String
        Public dmSpecVersion As UShort
        Public dmDriverVersion As UShort
        Public dmSize As UShort
        Public dmDriverExtra As UShort
        Public dmFields As UInteger
        Public dmPositionX As Integer
        Public dmPositionY As Integer
        Public dmDisplayOrientation As UInteger
        Public dmDisplayFixedOutput As UInteger
        Public dmColor As Short
        Public dmDuplex As Short
        Public dmYResolution As Short
        Public dmTTOption As Short
        Public dmCollate As Short
        <MarshalAs(UnmanagedType.ByValTStr, SizeConst:=32)> Public dmFormName As String
        Public dmLogPixels As UShort
        Public dmBitsPerPel As UInteger
        Public dmPelsWidth As UInteger
        Public dmPelsHeight As UInteger
        Public dmDisplayFlags As UInteger
        Public dmDisplayFrequency As UInteger
        Public dmICMMethod As UInteger
        Public dmICMIntent As UInteger
        Public dmMediaType As UInteger
        Public dmDitherType As UInteger
        Public dmReserved1 As UInteger
        Public dmReserved2 As UInteger
        Public dmPanningWidth As UInteger
        Public dmPanningHeight As UInteger
    End Structure

    <DllImport("user32.dll", CharSet:=CharSet.Unicode)>
    Private Function EnumDisplaySettingsExW(设备名 As String, 模式 As Integer,
                                            ByRef 模式信息 As DEVMODE, 标志 As Integer) As Boolean
    End Function

    ''' <summary>从窗口所在显示器读系统当前刷新率；取不到返回 0（调用方记未判）。</summary>
    Private Function 取实际刷新率(控件 As Control) As Double
        Try
            Return 取设备刷新率(Screen.FromControl(控件).DeviceName)
        Catch
            Return 0.0
        End Try
    End Function

    Private Function 取设备刷新率(设备名 As String) As Double
        Try
            Dim 信息 As New DEVMODE With {.dmSize = CUShort(Marshal.SizeOf(GetType(DEVMODE)))}
            If Not EnumDisplaySettingsExW(设备名, -1, 信息, 0) Then Return 0.0
            Return CDbl(信息.dmDisplayFrequency)
        Catch
            Return 0.0
        End Try
    End Function

    ''' <summary>第 4 参：visible=主屏；60hz=本机 60 Hz 那块屏（找不到记未判）；缺省=屏幕外。</summary>
    Private Function 取目标屏(参数 As String()) As Screen
        If 参数.Length <= 3 Then Return Nothing
        If String.Equals(参数(3), "visible", StringComparison.OrdinalIgnoreCase) Then
            Return Screen.PrimaryScreen
        End If
        If String.Equals(参数(3), "60hz", StringComparison.OrdinalIgnoreCase) Then
            For Each 屏 In Screen.AllScreens
                Dim 刷新率 = 取设备刷新率(屏.DeviceName)
                If 刷新率 > 0.0 AndAlso Math.Abs(刷新率 - 60.0) <= 1.0 Then Return 屏
            Next
            Return Nothing
        End If
        Return Nothing
    End Function

    ''' <summary>真呈现器臂与换片臂共用：160 条 × 0.2s（覆盖 32s），保证播放窗内弹幕不空。</summary>
    Private Function 准备资料库(夹具 As String) As 弹幕资料库
        Dim 弹幕夹具 = Path.Combine(Path.GetDirectoryName(夹具), "danmaku_live_160.xml")
        If Not File.Exists(弹幕夹具) Then
            Dim sb As New Text.StringBuilder()
            sb.Append("<?xml version=""1.0"" encoding=""UTF-8""?><i><chatid>0</chatid><maxlimit>100</maxlimit>")
            For n = 1 To 160
                sb.Append("<d p=""").Append(((n - 1) * 0.2R).ToString("0.000", CultureInfo.InvariantCulture)).
                    Append(",1,25,16777215,0,0,10000").Append(n).Append(",").Append(n).
                    Append(""">实机节拍条目").Append(n).Append("</d>")
            Next
            sb.Append("</i>")
            File.WriteAllText(弹幕夹具, sb.ToString(), New Text.UTF8Encoding(False))
        End If
        Return B站弹幕解析器.解析文件(弹幕夹具)
    End Function

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"  ok   {名称}  {实测}")
        Else
            失败 += 1
            Console.WriteLine($"  FAIL {名称}  {实测}")
        End If
    End Sub

    Private Function N2(值 As Double) As String
        Return 值.ToString("0.00", CultureInfo.InvariantCulture)
    End Function

    Private Sub 泵(秒 As Double)
        Dim 截止 = Stopwatch.GetTimestamp() + CLng(秒 * Stopwatch.Frequency)
        Do
            Application.DoEvents()
            Thread.Sleep(2)
        Loop While Stopwatch.GetTimestamp() < 截止
    End Sub

    Private Function 探测夹具(参数 As String()) As String
        If 参数.Length > 0 Then Return 参数(0)
        Dim 目录 = New DirectoryInfo(AppContext.BaseDirectory)
        For i = 1 To 10
            Dim 候选 = Path.Combine(目录.FullName, "_3fc_tmp", "danmaku-live", "clip_24fps.mp4")
            If File.Exists(候选) Then Return 候选
            If 目录.Parent Is Nothing Then Exit For
            目录 = 目录.Parent
        Next
        Return Nothing
    End Function

    Friend Function 运行(参数 As String()) As Integer
        Console.WriteLine("3FP 弹幕节拍实机探针（--danmaku-pacing-live：真会话 + 真窗口 + 显示器节拍提交泵）")

        Dim 夹具 = 探测夹具(参数)
        If 夹具 Is Nothing OrElse Not File.Exists(夹具) Then
            Console.WriteLine("NOT JUDGED：24fps 夹具找不到（先跑 tools 里 ffmpeg 生成 clip_24fps.mp4，或用第 1 参指定）。")
            Console.WriteLine("用法：FFF.Player.Tests --danmaku-pacing-live [夹具路径] [播放秒数] [manual|real|switch] [offscreen|visible|60hz] [B夹具]")
            Return 2
        End If
        Dim 播放秒 As Double = 10.0
        If 参数.Length > 1 Then
            If Not Double.TryParse(参数(1), 播放秒) OrElse 播放秒 <= 0 OrElse 播放秒 > 60 Then
                Console.WriteLine($"NOT JUDGED：播放秒数「{参数(1)}」不是 (0,60] 内的数。")
                Return 2
            End If
        End If
        Dim 内核路径 = Path.Combine(AppContext.BaseDirectory, "FFF.Native.dll")
        Console.WriteLine($"夹具 = {夹具}")
        Console.WriteLine($"内核 = {内核路径}（{If(File.Exists(内核路径), New FileInfo(内核路径).Length & " 字节，" & File.GetLastWriteTime(内核路径).ToString("yyyy-MM-dd HH:mm"), "缺失！")}）")
        Dim 模式 = If(参数.Length > 2, 参数(2), "manual")
        Console.WriteLine($"模式 = {模式}（manual=探针自带 16ms 泵；real=真 播放器弹幕图层呈现器 驱动；switch=同一呈现器跨换片）")
        Dim 屏幕模式 = If(参数.Length > 3, 参数(3), "offscreen")
        Dim 目标屏 = 取目标屏(参数)
        If (String.Equals(屏幕模式, "visible", StringComparison.OrdinalIgnoreCase) OrElse
            String.Equals(屏幕模式, "60hz", StringComparison.OrdinalIgnoreCase)) AndAlso
            目标屏 Is Nothing Then
            Console.WriteLine($"NOT JUDGED：找不到屏幕「{屏幕模式}」（60hz=本机没有 60 Hz 显示器）。")
            Return 2
        End If
        Dim 可见 = 目标屏 IsNot Nothing
        Dim 窗位置 = If(可见,
            New Point(目标屏.Bounds.X + 100, 目标屏.Bounds.Y + 100),
            New Point(-8000, -8000))
        Console.WriteLine($"窗口位置 = {If(可见, $"屏幕上({窗位置.X},{窗位置.Y})", "屏幕外(-8000,-8000)")}" &
                          If(可见, $"；屏={目标屏.DeviceName} 系统刷新率={N2(取设备刷新率(目标屏.DeviceName))}Hz", ""))

        Dim 窗体 As New Form() With {
            .Text = "danmaku-pacing-live",
            .StartPosition = FormStartPosition.Manual,
            .Location = 窗位置,
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
                Dim 起始节律 = 会话.读取呈现节律状态()
                Dim 起始快照 = 会话.当前快照
                Console.WriteLine($"播放开始（素材 {起始快照.视频宽度}x{起始快照.视频高度}）")

                会话.播放()

                If String.Equals(模式, "real", StringComparison.OrdinalIgnoreCase) Then
                    ' ── 真呈现器臂：让 播放器弹幕图层呈现器 自己驱动提交，量它的泵间隔与提交率 ──
                    Dim 资料库 = 准备资料库(夹具)
                    If 资料库 Is Nothing OrElse 资料库.数量 = 0 Then
                        Console.WriteLine("NOT JUDGED：弹幕夹具解析出 0 条。")
                        会话.停止()
                        Return 2
                    End If
                    Dim 提交包装 As New Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single)(
                        Sub(画布 As Size, 命令 As IReadOnlyList(Of 定时文字命令), 序号 As ULong, 目标 As Single)
                            会话.设置弹幕图层(画布, 命令, 序号, 目标)
                        End Sub)
                    Using 呈现器 As New 播放器弹幕图层呈现器(
                        画面, Function() 会话.当前快照, 提交包装, Function() 资料库,
                        New 弹幕显示配置())
                        Dim 间隔样本 As New List(Of Integer)()
                        Dim 上限样本 As New List(Of Single)()
                        Dim 起始提交 = 呈现器.已提交次数
                        Dim 稳态丢弃起点 As Long = -1L
                        Dim 下次采样 = Stopwatch.GetTimestamp() + Stopwatch.Frequency \ 2
                        Dim 下次打印 = Stopwatch.GetTimestamp()
                        Dim 截止 = Stopwatch.GetTimestamp() + CLng(播放秒 * Stopwatch.Frequency)
                        Do
                            Application.DoEvents()
                            Dim 现在 = Stopwatch.GetTimestamp()
                            If 现在 >= 下次采样 Then
                                间隔样本.Add(呈现器.当前间隔毫秒)
                                上限样本.Add(会话.读取呈现节律状态().帧率上限)
                                下次采样 = 现在 + Stopwatch.Frequency \ 2
                            End If
                            If 现在 >= 下次打印 Then
                                Dim 已播 = 播放秒 - CDbl(截止 - 现在) / Stopwatch.Frequency
                                ' 启动期允许一次首拍丢弃；稳态判据从 t+2s 起算，单列启动数。
                                If 稳态丢弃起点 < 0L AndAlso 已播 >= 2.0 Then
                                    稳态丢弃起点 = 呈现器.已丢弃节拍数
                                End If
                                Console.WriteLine($"  t+{N2(已播)}s 泵间隔={呈现器.当前间隔毫秒}ms " &
                                                  $"已提交={呈现器.已提交次数 - 起始提交} 上限={N2(会话.读取呈现节律状态().帧率上限)} " &
                                                  $"丢拍={呈现器.已丢弃节拍数}")
                                下次打印 = 现在 + CLng(2.0 * Stopwatch.Frequency)
                            End If
                            Thread.Sleep(2)
                        Loop While Stopwatch.GetTimestamp() < 截止
                        Dim 总提交 = 呈现器.已提交次数 - 起始提交
                        Dim 末丢弃 = 呈现器.已丢弃节拍数
                        Dim 末节律 = 会话.读取呈现节律状态()
                        呈现器.释放()
                        Dim 末快照 = 会话.当前快照
                        会话.停止()
                        泵(0.3)
                        Dim 呈现帧 = CLng(末快照.已呈现视频帧数) - CLng(起始快照.已呈现视频帧数)
                        Dim 显示丢弃 = CLng(末节律.显示速率丢弃数) - CLng(起始节律.显示速率丢弃数)
                        Dim 管线丢弃 = CLng(末快照.已丢弃视频帧数) - CLng(起始快照.已丢弃视频帧数)
                        ' 首拍使用初始节拍；后续保持显示器目标节拍，只判稳态段。
                        Dim 稳态 = 间隔样本.Skip(Math.Min(3, 间隔样本.Count)).ToList()
                        Dim 稳态间隔 = If(稳态.Count > 0, 稳态.Average(), 16.0R)
                        Dim 期望提交率 = 1000.0R / 稳态间隔
                        Dim 实测提交率 = CDbl(总提交) / 播放秒
                        Console.WriteLine($"真呈现器：提交 {总提交} 次 / {N2(播放秒)}s ≈ {N2(实测提交率)} 次/s（稳态平均间隔 {N2(稳态间隔)}ms ⇒ 期望 ≈{N2(期望提交率)} 次/s）；" &
                                          $"泵间隔样本={String.Join("/", 间隔样本.Distinct())}ms；呈现视频帧 {呈现帧}；" &
                                          $"显示速率丢弃 {显示丢弃}；管线丢帧 {管线丢弃}；丢拍(启动/稳态)={If(稳态丢弃起点 < 0L, "未判", 稳态丢弃起点.ToString())}/{末丢弃}")
                        检查(总提交 > 播放秒 * 40, "真呈现器提交次数 > 播放秒×40（泵在跑）", $"{总提交} 次")
                        检查(间隔样本.Count >= CInt(播放秒) - 2, "泵间隔采样数达标", $"{间隔样本.Count} 采")
                        检查(稳态.Count > 0 AndAlso 稳态.Max() - 稳态.Min() <= 1,
                             "稳态泵间隔稳定（极差 ≤1ms，整数毫秒定时器允许 8/9 交替）",
                             String.Join("/", 稳态.Distinct()) & "ms；全部采样=" & String.Join("/", 间隔样本.Distinct()))
                        检查(Math.Abs(实测提交率 - 期望提交率) / 期望提交率 < 0.15R,
                             "实测提交率与稳态平均间隔自洽（偏差 <15%）",
                             N2(实测提交率) & " vs " & N2(期望提交率) & " 次/s")
                        检查(末节律.声明刷新率 > 0F, "声明刷新率 > 0", N2(末节律.声明刷新率))
                        Dim 实际刷新率 = 取实际刷新率(画面)
                        Console.WriteLine($"系统当前刷新率={N2(实际刷新率)}Hz")
                        If 实际刷新率 > 0.0 Then
                            Dim 峰值上限 = If(上限样本.Count > 0, 上限样本.Max(), 0.0F)
                            检查(Math.Abs(CDbl(末节律.声明刷新率) - 实际刷新率) / 实际刷新率 <= 0.02,
                                 "声明刷新率 = 系统当前刷新率（图层目标没有污染面板半）",
                                 $"{N2(末节律.声明刷新率)} vs {N2(实际刷新率)}Hz")
                            检查(CDbl(峰值上限) <= 实际刷新率 * 1.02,
                                 "帧率上限不超过实际面板",
                                 $"峰值 {N2(峰值上限)} vs 面板 {N2(实际刷新率)}Hz")
                            ' 片源提交率显著高于面板时，面板限速丢弃必须进显示速率桶；进管线桶
                            ' 会让自适应降采样去救一个面板上限。没有产生任何丢弃时这条分桶
                            ' 路径根本没被走到，只能记未判，不能判绿也不能判红。
                            If 实测提交率 > 实际刷新率 * 1.1 Then
                                If 显示丢弃 + 管线丢弃 > 0 Then
                                    检查(显示丢弃 > 0, "片源快于面板时丢弃进入显示速率桶",
                                         $"显示速率 {显示丢弃}；管线 {管线丢弃}")
                                    If 显示丢弃 > 0 Then
                                        ' 显示速率丢弃是"面板接不住"的账，不能再记进管线桶：
                                        ' 同一次丢弃两边都记会让自适应降采样去救一个面板上限。
                                        检查(管线丢弃 < 显示丢弃,
                                             "显示速率丢弃没有重复计入管线桶",
                                             $"显示速率 {显示丢弃}；管线 {管线丢弃}")
                                    End If
                                Else
                                    Console.WriteLine("  note 片源快于面板但本轮没有丢弃，分桶未判（换更高帧率夹具再测）")
                                End If
                            End If
                        Else
                            Console.WriteLine("  note 取不到系统刷新率，面板上限/分桶判据未判")
                        End If
                        If 稳态丢弃起点 >= 0L Then
                            ' PrecisionTimer 是普通系统定时器：偶发一次超时不该判产品失败。
                            ' 判据改成"稳态增长 ≤ 期望拍数的 1%"（至少 1 拍），把启动期和
                            ' 单次调度抖动分开；连续超时仍会红。
                            Dim 稳态秒 = Math.Max(0.0R, 播放秒 - 2.0R)
                            Dim 期望拍数 = 稳态秒 * 1000.0R / Math.Max(1.0R, 稳态间隔)
                            Dim 允许丢拍 = Math.Max(1L, CLng(Math.Ceiling(期望拍数 * 0.01R)))
                            Dim 稳态增长 = 末丢弃 - 稳态丢弃起点
                            检查(稳态增长 <= 允许丢拍,
                                 "稳态已丢弃节拍 ≤ 期望拍数的 1%（启动期单列）",
                                 $"t+2s 起 {稳态丢弃起点} → 末 {末丢弃}（增长 {稳态增长}，" &
                                 $"允许 {允许丢拍} / {CInt(期望拍数)} 拍）")
                        Else
                            Console.WriteLine("  note 播放短于 2s，稳态丢拍未判")
                        End If
                        检查(呈现帧 > 0, "视频在真播", $"{呈现帧} 帧")
                        Console.WriteLine("帧率上限全样本：" & String.Join(", ", 上限样本.ConvertAll(Function(v) N2(v))))
                    End Using
                ElseIf String.Equals(模式, "switch", StringComparison.OrdinalIgnoreCase) Then
                    ' ── 换片臂：同一呈现器跨媒体存活，验证解码计数归零后帧周期估计能重开窗口 ──
                    Dim 资料库 = 准备资料库(夹具)
                    If 资料库 Is Nothing OrElse 资料库.数量 = 0 Then
                        Console.WriteLine("NOT JUDGED：弹幕夹具解析出 0 条。")
                        会话.停止()
                        Return 2
                    End If
                    Dim 换片夹具 = If(参数.Length > 4, 参数(4),
                        Path.Combine(Path.GetDirectoryName(夹具), "clip_120fps.mp4"))
                    If Not File.Exists(换片夹具) Then
                        Console.WriteLine($"NOT JUDGED：换片夹具不存在：{换片夹具}")
                        会话.停止()
                        Return 2
                    End If
                    Const 后段秒 As Double = 4.0
                    Console.WriteLine($"换片臂：前段 {N2(播放秒)}s（{Path.GetFileName(夹具)}）→ " &
                                      $"后段 {N2(后段秒)}s（{Path.GetFileName(换片夹具)}）")
                    Dim 提交包装 As New Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single)(
                        Sub(画布 As Size, 命令 As IReadOnlyList(Of 定时文字命令), 序号 As ULong, 目标 As Single)
                            会话.设置弹幕图层(画布, 命令, 序号, 目标)
                        End Sub)
                    Using 呈现器 As New 播放器弹幕图层呈现器(
                        画面, Function() 会话.当前快照, 提交包装, Function() 资料库,
                        New 弹幕显示配置())
                        Dim 起始提交 = 呈现器.已提交次数
                        Dim 前段截止 = Stopwatch.GetTimestamp() + CLng(播放秒 * Stopwatch.Frequency)
                        Do
                            Application.DoEvents()
                            Thread.Sleep(2)
                        Loop While Stopwatch.GetTimestamp() < 前段截止
                        Dim 切换前间隔 = 呈现器.当前间隔毫秒
                        Dim 切换前节律 = 会话.读取呈现节律状态()
                        Dim 切换前快照 = 会话.当前快照

                        ' 开新片时继续泵消息：要看见状态与解码计数在切换窗里怎么走。
                        Dim 打开任务 = 会话.打开Async(换片夹具)
                        Dim 看见未播放 As Boolean = False
                        Dim 看见计数回落 As Boolean = False
                        Dim 上个解码 = 切换前快照.已解码视频帧数
                        Do
                            Application.DoEvents()
                            Dim 快照 = 会话.当前快照
                            If 快照.状态 <> 播放状态.正在播放 Then 看见未播放 = True
                            If 快照.已解码视频帧数 < 上个解码 Then 看见计数回落 = True
                            上个解码 = 快照.已解码视频帧数
                            Thread.Sleep(2)
                        Loop While Not 打开任务.IsCompleted
                        打开任务.GetAwaiter().GetResult()
                        会话.播放()
                        Dim 切换时刻 = Stopwatch.GetTimestamp()

                        ' 100 ms 一档采样 4 s：换片后 1 s 内必须回到 120fps 跟随。
                        Dim 时标 As New List(Of Double)()
                        Dim 间隔 As New List(Of Integer)()
                        Dim 上限 As New List(Of Single)()
                        Dim 下次采样 = 切换时刻
                        Dim 后段截止 = 切换时刻 + CLng(后段秒 * Stopwatch.Frequency)
                        Do
                            Application.DoEvents()
                            Dim 现在 = Stopwatch.GetTimestamp()
                            If 现在 >= 下次采样 Then
                                时标.Add(CDbl(现在 - 切换时刻) * 1000.0R / Stopwatch.Frequency)
                                间隔.Add(呈现器.当前间隔毫秒)
                                上限.Add(会话.读取呈现节律状态().帧率上限)
                                下次采样 = 现在 + CLng(0.1 * Stopwatch.Frequency)
                            End If
                            Thread.Sleep(2)
                        Loop While Stopwatch.GetTimestamp() < 后段截止

                        Dim 总提交 = 呈现器.已提交次数 - 起始提交
                        Dim 末节律 = 会话.读取呈现节律状态()
                        Dim 末快照 = 会话.当前快照
                        呈现器.释放()
                        会话.停止()
                        泵(0.3)

                        Dim 后段呈现 = CLng(末快照.已呈现视频帧数) - CLng(切换前快照.已呈现视频帧数)
                        Dim 首达快 = -1.0R
                        For i = 0 To 间隔.Count - 1
                            If 间隔(i) <= 9 Then
                                首达快 = 时标(i)
                                Exit For
                            End If
                        Next
                        Dim 一秒后 = New List(Of Integer)()
                        Dim 样本行 As New Text.StringBuilder()
                        For i = 0 To 间隔.Count - 1
                            If 时标(i) >= 1000.0R Then 一秒后.Add(间隔(i))
                            样本行.Append(间隔(i)).Append("@").Append(时标(i).ToString("F0", CultureInfo.InvariantCulture)).Append(" ")
                        Next
                        Dim 一秒后最大 = If(一秒后.Count > 0, 一秒后.Max(), 0)
                        Dim 后段峰值上限 = If(上限.Count > 0, 上限.Max(), 0.0F)
                        Console.WriteLine($"换片前：泵间隔 {切换前间隔}ms；帧率上限 {N2(切换前节律.帧率上限)}；" &
                                          $"解码 {切换前快照.已解码视频帧数}；切换窗看见未播放={看见未播放} 看见计数回落={看见计数回落}")
                        Console.WriteLine($"换片后：泵间隔样本（ms@时标ms）= {样本行.ToString().TrimEnd()}")
                        Console.WriteLine($"首达目标节拍时标={If(首达快 < 0.0R, "未达", 首达快.ToString("F0", CultureInfo.InvariantCulture) + "ms")}；" &
                                          $"1s 后最大间隔={一秒后最大}ms；后段帧率上限峰值={N2(后段峰值上限)}；" &
                                          $"后段提交 {总提交} 次；后段呈现 {后段呈现} 帧")

                        Dim 实际刷新率 = 取实际刷新率(画面)
                        Dim 目标刷新率 = 显示器节拍.计算目标帧率(CInt(Math.Round(实际刷新率)))
                        Dim 目标间隔 = Math.Max(1, CInt(Math.Floor(1000.0R / Math.Max(1, 目标刷新率))))
                        检查(Math.Abs(切换前间隔 - 目标间隔) <= 1,
                             "换片前泵间隔符合显示器目标节拍", $"{切换前间隔}ms / 目标 {目标间隔}ms")
                        检查(看见计数回落 OrElse 看见未播放,
                             "切换窗观察到解码计数回落或状态离开播放（估计器有条件重开窗口）",
                             $"计数回落={看见计数回落} 未播放={看见未播放}")
                        检查(首达快 >= 0.0R AndAlso 首达快 <= 1000.0R,
                             "换片后 1s 内泵间隔回到显示器目标",
                             If(首达快 < 0.0R, "整段未达", 首达快.ToString("F0", CultureInfo.InvariantCulture) & "ms"))
                        检查(一秒后.Count > 0 AndAlso 一秒后最大 <= 1000.0R / Math.Max(1, 目标刷新率) + 1,
                             "换片 1s 后泵间隔保持显示器目标",
                             $"1s 后样本={If(一秒后.Count = 0, "无", String.Join("/", 一秒后))}")
                        检查(总提交 > (播放秒 + 后段秒) * 40, "泵全程在跑（提交次数 > 总秒数×40）", $"{总提交} 次")
                        检查(后段呈现 > 0, "换片后视频在真播", $"{后段呈现} 帧")
                        Console.WriteLine($"系统当前刷新率={N2(实际刷新率)}Hz")
                        If 实际刷新率 > 0.0 Then
                            检查(Math.Abs(CDbl(末节律.声明刷新率) - 实际刷新率) / 实际刷新率 <= 0.02,
                                 "声明刷新率 = 系统当前刷新率（图层目标没有污染面板半）",
                                 $"{N2(末节律.声明刷新率)} vs {N2(实际刷新率)}Hz")
                            检查(CDbl(后段峰值上限) <= 实际刷新率 * 1.02,
                                 "换片后帧率上限不超过实际面板",
                                 $"峰值 {N2(后段峰值上限)} vs 面板 {N2(实际刷新率)}Hz")
                            If 实际刷新率 >= 120.0 Then
                                检查(CDbl(后段峰值上限) >= 120.0,
                                     "120Hz 以上显示器开放到 120fps",
                                     $"{N2(后段峰值上限)} 上限")
                            Else
                                Console.WriteLine("  note 面板低于 120Hz，120fps 上限判据未判")
                            End If
                        Else
                            Console.WriteLine("  note 取不到系统刷新率，面板上限判据未判")
                        End If
                    End Using
                Else
                    ' ── manual 臂：探针自带 16ms 泵（内核 bound 公式的两臂对照用）──
                    Dim 命令池 As New List(Of 定时文字命令)(8)
                For n = 1 To 5
                    命令池.Add(定时文字命令.创建文字(
                        "弹幕节拍实机探针条目" & n & "——滚动验证用文本",
                        "Microsoft YaHei UI", 36.0F,
                        New RectangleF(1280.0F, 40.0F * n, 420.0F, 44.0F),
                        &HFFFFFFFFUI, &HFF000000UI, 1.0F,
                        定时文字对齐.靠前, 定时文字对齐.靠前,
                        定时文字样式.无, 0UL, &HAA000000UI, 2.0F, 2.0F))
                Next
                Dim 序号 As ULong = 0UL
                Dim 提交数 As Long = 0
                Dim 上限样本 As New List(Of Single)()
                Dim 下次提交 = Stopwatch.GetTimestamp()
                Dim 下次采样 = Stopwatch.GetTimestamp()
                Dim 下次打印 = Stopwatch.GetTimestamp()
                Dim 截止 = Stopwatch.GetTimestamp() + CLng(播放秒 * Stopwatch.Frequency)
                Do
                    Application.DoEvents()
                    Dim 现在 = Stopwatch.GetTimestamp()
                    If 现在 >= 下次提交 Then
                        Dim 画布 As New Size(画面.ClientSize.Width, 画面.ClientSize.Height)
                        Dim 秒 = (播放秒 - CDbl(截止 - 现在) / Stopwatch.Frequency)
                        For Each c In 命令池
                            c.X = 1280.0F - CSng((秒 * 180.0R) Mod 1700.0R)
                        Next
                        序号 += 1UL
                        会话.设置弹幕图层(画布, 命令池, 序号, 62.5F)
                        提交数 += 1
                        下次提交 = 现在 + CLng(0.016 * Stopwatch.Frequency)
                    End If
                    If 现在 >= 下次采样 Then
                        Dim 节律 = 会话.读取呈现节律状态()
                        上限样本.Add(节律.帧率上限)
                        下次采样 = 现在 + CLng(0.5 * Stopwatch.Frequency)
                    End If
                    If 现在 >= 下次打印 Then
                        Dim 节律 = 会话.读取呈现节律状态()
                        Console.WriteLine($"  t+{N2(播放秒 - CDbl(截止 - 现在) / Stopwatch.Frequency)}s 帧率上限={N2(节律.帧率上限)} " &
                                          $"声明刷新率={N2(节律.声明刷新率)} 网格锁定={节律.网格锁定}")
                        下次打印 = 现在 + CLng(2.0 * Stopwatch.Frequency)
                    End If
                    Thread.Sleep(2)
                Loop While Stopwatch.GetTimestamp() < 截止

                Dim 末节律 = 会话.读取呈现节律状态()
                Dim 末快照 = 会话.当前快照
                会话.停止()
                泵(0.3)

                Dim 呈现帧 = CLng(末快照.已呈现视频帧数) - CLng(起始快照.已呈现视频帧数)
                Dim 未上屏 = CLng(末节律.从未上屏翻帧数) - CLng(起始节律.从未上屏翻帧数)
                Dim 丢显示 = CLng(末节律.显示速率丢弃数) - CLng(起始节律.显示速率丢弃数)
                Dim 实际刷新率 = 取实际刷新率(画面)
                Dim 峰值上限 = If(上限样本.Count > 0, 上限样本.Max(), 0.0F)
                Console.WriteLine($"提交弹幕层 {提交数} 次；播放期呈现视频帧 {呈现帧}；从未上屏翻帧 {未上屏}；显示速率丢弃 {丢显示}；" &
                                  $"系统当前刷新率 {N2(实际刷新率)}Hz；帧率上限峰值 {N2(峰值上限)}")

                检查(提交数 > 60, "弹幕层提交次数 > 60（16ms 泵真的在跑）", $"{提交数} 次")
                ' 声明刷新率在链创建前读是 0（PublishPacingStatus 随首次翻帧发布），只判末值。
                检查(末节律.声明刷新率 > 0F, "声明刷新率 > 0（面板半在岗）", N2(末节律.声明刷新率))
                检查(上限样本.Count >= CInt(播放秒 \ 2) - 1, "帧率上限采样数达标（每 0.5s 一采）",
                     $"{上限样本.Count} 采")
                Dim 不同值 As New HashSet(Of Single)(上限样本)
                检查(不同值.Count = 1, "帧率上限全程自洽（单值，无抖动切换）",
                     "取值=" & String.Join("/", 不同值) & " 样本=" & 上限样本.Count)
                ' 视频帧直呈会顶掉相撞的 overlay 翻帧，该计数结构性非零；只判"顶替没吞掉全部翻帧"。
                检查(未上屏 < 提交数, "从未上屏翻帧 < 提交数（overlay 翻帧大半落地）",
                     $"{未上屏}/{提交数}")
                检查(呈现帧 > 0, "视频在真播（呈现帧 > 0）", $"{呈现帧} 帧")
                If 实际刷新率 > 0.0 Then
                    检查(Math.Abs(CDbl(末节律.声明刷新率) - 实际刷新率) / 实际刷新率 <= 0.02,
                         "声明刷新率 = 系统当前刷新率（图层目标没有污染面板半）",
                         $"{N2(末节律.声明刷新率)} vs {N2(实际刷新率)}Hz")
                    检查(CDbl(峰值上限) <= 实际刷新率 * 1.02,
                         "帧率上限不超过实际面板",
                         $"峰值 {N2(峰值上限)} vs 面板 {N2(实际刷新率)}Hz")
                Else
                    Console.WriteLine("  note 取不到系统刷新率，面板上限判据未判")
                End If
                Console.WriteLine("帧率上限全样本：" & String.Join(", ", 上限样本.ConvertAll(Function(v) N2(v))))
                End If
            End Using
        Catch ex As Exception
            Console.WriteLine()
            Console.WriteLine("环境不具备，本轮不产出结论：" & ex.GetType().Name & " " & ex.Message)
            Return 2
        Finally
            窗体.Close()
            窗体.Dispose()
            泵(0.5)
        End Try

        Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
        If 失败 > 0 Then Return 1
        Return 0
    End Function
End Module
