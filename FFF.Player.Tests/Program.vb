Imports System.Diagnostics
Imports System.Drawing
Imports System.IO
Imports System.Reflection
Imports System.Runtime.InteropServices
Imports System.Text
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module Program
    Private Const 测量秒数 As Double = 12.0
    Private Const 目标帧率 As Double = 24000.0 / 1001.0

    <StructLayout(LayoutKind.Sequential)>
    Private Structure 原生色彩变换
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 色彩模式 As UInteger
        Public 传递函数 As UInteger
        Public 源是BT2020 As UInteger
        Public 保留 As UInteger
        Public 输入红 As Single
        Public 输入绿 As Single
        Public 输入蓝 As Single
        Public SDR峰值尼特 As Single
        Public 源峰值尼特 As Single
        Public 纸白尼特 As Single
        Public 输出红 As Single
        Public 输出绿 As Single
        Public 输出蓝 As Single
    End Structure

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Private Function FFF3FP_EvaluateColorTransform(ByRef 变换 As 原生色彩变换) As Integer
    End Function

    <STAThread>
    Public Function Main(参数 As String()) As Integer
        Try
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--gpu-decode-matrix", StringComparison.OrdinalIgnoreCase) Then
                测试GPU解码矩阵(Path.GetFullPath(参数(1)))
                Console.WriteLine("GPU 解码规格接受与 CPU 回退矩阵通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--clip-focus-regression", StringComparison.OrdinalIgnoreCase) Then
                测试剪辑模式焦点()
                Console.WriteLine("剪辑模式键盘焦点与出入点保留回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--ass-render-benchmark", StringComparison.OrdinalIgnoreCase) Then
                测试ASS渲染性能()
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--vcb-ass-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim ASS视频路径 = Path.GetFullPath(参数(1))
                Dim ASS字幕路径 = Path.GetFullPath(参数(2))
                检查文件(ASS视频路径)
                检查文件(ASS字幕路径)
                测试ASS特效字幕(ASS视频路径, ASS字幕路径)
                Console.WriteLine("ASS/SSA libass 特效、媒体字体与资源释放回归全部通过。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--targeted-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 专项视频路径 = Path.GetFullPath(参数(1))
                Dim 专项SUP路径 = Path.GetFullPath(参数(2))
                检查文件(专项视频路径)
                检查文件(专项SUP路径)
                测试音频规格回归(专项视频路径)
                测试播放中字幕替换(专项视频路径, 专项SUP路径)
                测试播放中弹幕替换(专项视频路径)
                Console.WriteLine("连续 PCM 音频与播放中字幕/弹幕替换专项回归全部通过。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--performance-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim SDR路径 = Path.GetFullPath(参数(1))
                Dim HDR路径 = Path.GetFullPath(参数(2))
                检查文件(SDR路径)
                检查文件(HDR路径)
                测试性能回归(SDR路径, HDR路径)
                Console.WriteLine("解码、渲染、字幕/弹幕和音频性能回归全部通过。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--color-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim SDR路径 = Path.GetFullPath(参数(1))
                Dim HDR路径 = Path.GetFullPath(参数(2))
                检查文件(SDR路径)
                检查文件(HDR路径)
                测试色彩回归(SDR路径, HDR路径)
                Console.WriteLine("SDR/HDR 色彩回归测试全部通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--clip-step-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 逐帧视频路径 = Path.GetFullPath(参数(1))
                检查文件(逐帧视频路径)
                测试剪辑区间逐帧(逐帧视频路径)
                Console.WriteLine("剪辑区间前后逐帧回归通过。")
                Return 0
            End If
            If 参数.Length < 2 Then
                Console.Error.WriteLine("用法: FFF.Player.Tests <视频.mp4> <弹幕.xml> [字幕.ass] [字幕.srt]")
                Console.Error.WriteLine("   或: FFF.Player.Tests --color-regression <SDR视频> <HDR视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --performance-regression <SDR视频> <HDR视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --targeted-regression <视频> <字幕.sup>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --vcb-ass-regression <视频> <字幕.ass>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --clip-step-regression <视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --clip-focus-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --gpu-decode-matrix <视频目录>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --ass-render-benchmark")
                Return 2
            End If
            Dim 视频路径 = Path.GetFullPath(参数(0))
            Dim 弹幕路径 = Path.GetFullPath(参数(1))
            Dim ASS路径 = 参数.Skip(2).FirstOrDefault(Function(x) Path.GetExtension(x).Equals(".ass", StringComparison.OrdinalIgnoreCase) OrElse
                                                        Path.GetExtension(x).Equals(".ssa", StringComparison.OrdinalIgnoreCase))
            Dim SRT路径 = 参数.Skip(2).FirstOrDefault(Function(x) Path.GetExtension(x).Equals(".srt", StringComparison.OrdinalIgnoreCase))
            If Not String.IsNullOrWhiteSpace(ASS路径) Then ASS路径 = Path.GetFullPath(ASS路径)
            If Not String.IsNullOrWhiteSpace(SRT路径) Then SRT路径 = Path.GetFullPath(SRT路径)
            检查文件(视频路径)
            检查文件(弹幕路径)
            If Not String.IsNullOrWhiteSpace(ASS路径) Then 检查文件(ASS路径)
            If Not String.IsNullOrWhiteSpace(SRT路径) Then 检查文件(SRT路径)

            If Not String.IsNullOrWhiteSpace(ASS路径) AndAlso Not String.IsNullOrWhiteSpace(SRT路径) Then
                测试字幕(视频路径, ASS路径, SRT路径)
            End If
            Dim 弹幕资料库 = B站弹幕解析器.解析文件(弹幕路径)
            Dim 弹幕测试位置 = 测试弹幕(视频路径, 弹幕资料库)
            Dim CPU结果 = 测试播放(视频路径, SRT路径, Nothing, TimeSpan.Zero, 解码模式.CPU)
            输出播放报告(解码模式.CPU, CPU结果)
            验证播放结果(CPU结果, "CPU 顺播")
            Dim GPU结果 = 测试播放(视频路径, ASS路径, 弹幕资料库, 弹幕测试位置, 解码模式.GPU)
            输出播放报告(解码模式.GPU, GPU结果)
            验证播放结果(GPU结果, "GPU 顺播")
            Console.WriteLine("全部诊断测试通过。")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine($"测试失败：{ex.Message}")
            Return 1
        End Try
    End Function

    Private Sub 测试ASS渲染性能()
        测试ASS半透明像素(Path.GetTempPath())
        Dim 临时路径 = Path.Combine(Path.GetTempPath(),
            "fff-player-ass-render-benchmark-" & Guid.NewGuid().ToString("N") & ".ass")
        Dim 脚本 = String.Join(vbLf, {
            "[Script Info]",
            "ScriptType: v4.00+",
            "PlayResX: 3840",
            "PlayResY: 2160",
            "[V4+ Styles]",
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding",
            "Style: Default,Arial,96,&H80FFFFFF,&H80FFFFFF,&H80000000,&H80000000,0,0,0,0,100,100,0,0,1,3,2,7,0,0,0,1",
            "[Events]",
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
            "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\move(1320,900,1620,900,0,10000)\blur3\p1}m 0 0 l 1200 0 1200 300 0 300{\p0}"})
        Try
            File.WriteAllText(临时路径, 脚本, New UTF8Encoding(False))
            Using 生成器 As New ASS特效字幕帧生成器(临时路径, Path.GetTempPath())
                Dim 预热 = 生成器.生成帧(TimeSpan.Zero, 3840, 2160)
                Dim 样本 As New List(Of Double)(180)
                Dim 内容变化数 = 0
                Dim 上一内容标识 As ULong = 0
                For index = 0 To 179
                    Dim 时间 = TimeSpan.FromTicks(CLng(index * TimeSpan.TicksPerSecond / 60.0R))
                    Dim 开始 = Stopwatch.GetTimestamp()
                    Dim 帧 = 生成器.生成帧(时间, 3840, 2160)
                    Dim 毫秒 = (Stopwatch.GetTimestamp() - 开始) * 1000.0R / Stopwatch.Frequency
                    If 帧 Is Nothing OrElse 帧.像素BGRA.Length = 0 Then
                        Throw New InvalidOperationException("ASS 基准没有生成字幕位图。")
                    End If
                    If Not 是预乘BGRA(帧.像素BGRA) Then
                        Throw New InvalidOperationException("ASS 基准输出不是预乘 BGRA。")
                    End If
                    If 上一内容标识 <> 0 AndAlso 上一内容标识 <> 帧.内容标识 Then 内容变化数 += 1
                    上一内容标识 = 帧.内容标识
                    样本.Add(毫秒)
                Next
                Dim 排序 = 样本.OrderBy(Function(x) x).ToArray()
                Dim 平均 = 样本.Average()
                Dim P95 = 排序(Math.Min(排序.Length - 1, CInt(Math.Floor(排序.Length * 0.95R))))
                Dim 最大 = 排序(排序.Length - 1)
                Console.WriteLine($"ASS 4K 局部遮罩基准：平均 {平均:F2} ms，P95 {P95:F2} ms，最大 {最大:F2} ms，" &
                                  $"理论 {1000.0R / 平均:F1} FPS，内容变化 {内容变化数}/{样本.Count - 1}。")
            End Using
        Finally
            If File.Exists(临时路径) Then File.Delete(临时路径)
        End Try
    End Sub

    Private Sub 测试剪辑模式焦点()
        Using 窗口 As New Form1 With {.ShowInTaskbar = False, .Opacity = 0}
            窗口.Show()
            Application.DoEvents()
            Dim 标志 = BindingFlags.Instance Or BindingFlags.Public Or BindingFlags.NonPublic
            Dim 窗口类型 = GetType(Form1)
            Dim 切换处理 = 窗口类型.GetMethod("MB_剪辑区间模式_Click", 标志)
            Dim 画面字段 = 窗口类型.GetField("画面控件", 标志)
            Dim 模式字段 = 窗口类型.GetField("剪辑区间模式已启用", 标志)
            Dim 模式按钮 = 窗口.Controls.Find("MB_剪辑区间模式", True).FirstOrDefault()
            断言(切换处理 IsNot Nothing AndAlso 画面字段 IsNot Nothing AndAlso
                   模式字段 IsNot Nothing AndAlso 模式按钮 IsNot Nothing,
               "无法取得剪辑模式焦点测试所需的窗口控件。")
            Dim 画面 = DirectCast(画面字段.GetValue(窗口), Control)
            断言(画面 IsNot Nothing, "播放器画面尚未在窗口加载时创建。")

            断言(Not 模式按钮.TabStop, "剪辑模式按钮仍参与键盘焦点导航。")
            For Each 预期模式 In {True, False}
                模式按钮.Focus()
                切换处理.Invoke(窗口, {窗口, EventArgs.Empty})
                Application.DoEvents()
                断言(CBool(模式字段.GetValue(窗口)) = 预期模式,
                   "剪辑模式字段没有在按钮点击时立即更新。")
                断言(画面.CanFocus AndAlso 画面.Focused AndAlso ReferenceEquals(窗口.ActiveControl, 画面),
                   "切换剪辑模式后，键盘焦点没有立即返回视频画面。")
            Next

            Dim 时间轴字段 = 窗口类型.GetField("剪辑区间进度条", 标志)
            Dim 媒体打开处理 = 窗口类型.GetMethod("播放控制器_媒体已打开", 标志)
            断言(时间轴字段 IsNot Nothing AndAlso 媒体打开处理 IsNot Nothing,
               "无法取得剪辑区间保留测试所需的窗口成员。")
            Dim 时间轴 = 时间轴字段.GetValue(窗口)
            Dim 时间轴类型 = 时间轴.GetType()
            Dim 更新播放状态 = 时间轴类型.GetMethod("更新播放状态", 标志)
            Dim 设为入点 = 时间轴类型.GetMethod("设为入点", 标志)
            Dim 设为出点 = 时间轴类型.GetMethod("设为出点", 标志)
            Dim 入点属性 = 时间轴类型.GetProperty("入点", 标志)
            Dim 出点属性 = 时间轴类型.GetProperty("出点", 标志)
            断言(更新播放状态 IsNot Nothing AndAlso 设为入点 IsNot Nothing AndAlso
                   设为出点 IsNot Nothing AndAlso 入点属性 IsNot Nothing AndAlso 出点属性 IsNot Nothing,
               "剪辑时间轴成员不完整。")

            Dim 入点 = TimeSpan.FromSeconds(2)
            Dim 出点 = TimeSpan.FromSeconds(8)
            更新播放状态.Invoke(时间轴, {TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(10)})
            设为入点.Invoke(时间轴, {入点})
            设为出点.Invoke(时间轴, {出点})
            媒体打开处理.Invoke(窗口,
                {窗口, New 播放器媒体事件参数("same.mp4", Nothing, Nothing, True)})
            断言(DirectCast(入点属性.GetValue(时间轴), TimeSpan) = 入点 AndAlso
                   DirectCast(出点属性.GetValue(时间轴), TimeSpan) = 出点,
               "同一媒体切换解码模式后没有保留出入点。")

            媒体打开处理.Invoke(窗口,
                {窗口, New 播放器媒体事件参数("other.mp4", Nothing, Nothing, False)})
            断言(入点属性.GetValue(时间轴) Is Nothing AndAlso 出点属性.GetValue(时间轴) Is Nothing,
               "真正打开另一媒体时没有清除出入点。")
            窗口.Close()
        End Using
    End Sub

    Private Sub 测试剪辑区间逐帧(视频路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
            会话.设置音量(0.0F, True)
            会话.打开Async(视频路径).GetAwaiter().GetResult()
            Dim 初始 = 会话.当前快照
            Dim 目标位置 = TimeSpan.FromTicks(Math.Max(TimeSpan.FromSeconds(1).Ticks, 初始.总时长.Ticks \ 2))
            If 目标位置 >= 初始.总时长 Then 目标位置 = TimeSpan.FromTicks(初始.总时长.Ticks \ 2)
            Dim 跳转前呈现帧数 = 初始.已呈现视频帧数
            会话.跳转(目标位置)
            Dim 当前帧 = 等待快照(会话,
                Function(x) x.原始帧PTS <> Long.MinValue AndAlso x.已呈现视频帧数 > 跳转前呈现帧数 AndAlso
                            Math.Abs((x.播放位置 - 目标位置).TotalMilliseconds) < 100.0,
                "跳转到逐帧测试位置")

            会话.上一帧()
            Dim 上一帧 = 等待快照(会话,
                Function(x) x.状态 = 播放状态.已暂停 AndAlso x.原始帧PTS < 当前帧.原始帧PTS,
                "倒退一帧")
            会话.下一帧()
            Dim 返回帧 = 等待快照(会话,
                Function(x) x.状态 = 播放状态.已暂停 AndAlso x.原始帧PTS > 上一帧.原始帧PTS,
                "前进一帧")
            断言(返回帧.原始帧PTS = 当前帧.原始帧PTS,
               $"倒退再前进没有返回原帧：{当前帧.原始帧PTS} → {上一帧.原始帧PTS} → {返回帧.原始帧PTS}。")
            Console.WriteLine($"逐帧 PTS：{当前帧.原始帧PTS} → {上一帧.原始帧PTS} → {返回帧.原始帧PTS}")

            Dim 游标 = 返回帧
            Dim 连续倒退次数 = 0
            While 游标.播放位置 > TimeSpan.FromMilliseconds(20) AndAlso 连续倒退次数 < 300
                Dim 倒退前PTS = 游标.原始帧PTS
                Dim 倒退前位置 = 游标.播放位置
                会话.上一帧()
                游标 = 等待快照(会话,
                    Function(x) x.状态 = 播放状态.已暂停 AndAlso x.原始帧PTS < 倒退前PTS AndAlso
                                x.播放位置 < 倒退前位置,
                    $"连续倒退第 {连续倒退次数 + 1} 帧")
                连续倒退次数 += 1
            End While
            断言(游标.播放位置 <= TimeSpan.FromMilliseconds(20),
               $"连续倒退 {连续倒退次数} 帧后停在 {游标.播放位置.TotalSeconds:F3}s。")
            Console.WriteLine($"连续倒退 {连续倒退次数} 帧到 {游标.播放位置.TotalSeconds:F3}s。")

            Dim 突发起点 = TimeSpan.FromTicks(初始.总时长.Ticks * 2 \ 5)
            会话.跳转(突发起点)
            等待快照(会话,
                Function(x) Math.Abs((x.播放位置 - 突发起点).TotalMilliseconds) < 100.0,
                "跳转到突发逐帧测试位置")

            Using 跳转完成 As New ManualResetEventSlim(False)
                Dim 完成处理 As EventHandler(Of 播放器事件参数) =
                    Sub(sender, e)
                        If e.详情JSON.Contains("""operation"":""seek""", StringComparison.Ordinal) Then
                            跳转完成.Set()
                        End If
                    End Sub
                AddHandler 会话.操作完成, 完成处理
                Try
                    Dim 响应计时 = Stopwatch.StartNew()
                    For 索引 = 1 To 1000
                        会话.上一帧()
                    Next
                    会话.跳转(TimeSpan.FromTicks(初始.总时长.Ticks \ 3))
                    断言(跳转完成.Wait(TimeSpan.FromSeconds(5)),
                       "突发逐帧请求阻塞了后续跳转，逐帧命令队列可能仍在无界增长。")
                    Console.WriteLine($"1000 次突发倒退后的跳转响应：{响应计时.Elapsed.TotalMilliseconds:F0} ms。")

                    跳转完成.Reset()
                    响应计时.Restart()
                    For 索引 = 1 To 1000
                        会话.上一关键帧()
                    Next
                    会话.跳转(TimeSpan.FromTicks(初始.总时长.Ticks \ 4))
                    断言(跳转完成.Wait(TimeSpan.FromSeconds(5)),
                       "突发关键帧请求阻塞了后续跳转，导航命令队列可能仍在无界增长。")
                    Console.WriteLine($"1000 次突发关键帧倒退后的跳转响应：{响应计时.Elapsed.TotalMilliseconds:F0} ms。")
                Finally
                    RemoveHandler 会话.操作完成, 完成处理
                End Try
            End Using
        End Using
    End Sub

    Private Function 等待快照(会话 As 播放器会话, 条件 As Func(Of 播放器快照, Boolean), 操作 As String) As 播放器快照
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            If 条件(快照) Then Return 快照
            If 快照.状态 = 播放状态.失败 Then Throw New InvalidOperationException($"{操作}时播放器失败。")
            If 计时.Elapsed >= TimeSpan.FromSeconds(10) Then
                Throw New TimeoutException($"等待{操作}超时：位置 {快照.播放位置.TotalSeconds:F3}s，PTS {快照.原始帧PTS}。")
            End If
            Thread.Sleep(5)
        Loop
    End Function

    Private Sub 测试GPU解码矩阵(目录 As String)
        If Not Directory.Exists(目录) Then Throw New DirectoryNotFoundException(目录)
        Dim 视频 = Directory.EnumerateFiles(目录).
            Where(Function(x) {".mp4", ".mkv", ".mov", ".webm"}.
                Contains(Path.GetExtension(x), StringComparer.OrdinalIgnoreCase)).
            OrderBy(Function(x) x, StringComparer.OrdinalIgnoreCase).
            ToArray()
        断言(视频.Length > 0, $"GPU 解码矩阵目录没有视频：{目录}")

        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(320, 180),
            .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .ShowInTaskbar = False,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(-10000, -10000)
        }
            输出窗口.Show()
            Application.DoEvents()
            For Each 路径 In 视频
                Using 会话 As New 播放器会话(New 播放器配置 With {
                    .解码器 = 解码模式.GPU,
                    .色彩模式 = 色彩输出模式.映射到SDR,
                    .输出窗口句柄 = 输出窗口.Handle
                })
                    会话.设置音量(0.0F, True)
                    会话.打开Async(路径).GetAwaiter().GetResult()
                    Dim 打开快照 = 会话.当前快照
                    断言(打开快照.解码器 = 解码模式.GPU OrElse 打开快照.解码器 = 解码模式.CPU,
                       $"{Path.GetFileName(路径)} 返回了无效解码模式。")
                    会话.播放()
                    Dim 解码快照 = 等待快照(会话,
                        Function(x) x.已解码视频帧数 > 0 AndAlso x.已呈现视频帧数 > 0,
                        $"{Path.GetFileName(路径)} 首帧")
                    Console.WriteLine($"{Path.GetFileName(路径)}：{解码快照.解码器}，" &
                                      $"解码 {解码快照.已解码视频帧数} 帧，呈现 {解码快照.已呈现视频帧数} 帧")
                End Using
            Next
        End Using
    End Sub

    Private Sub 测试ASS特效字幕(视频路径 As String, ASS路径 As String)
        Dim 字体目录 = ASS媒体字体发现器.查找字体目录(视频路径)
        断言(字体目录.Any(Function(x) String.Equals(Path.GetFileName(x), "Fonts", StringComparison.OrdinalIgnoreCase)),
           "没有发现视频目录下的 Fonts 文件夹。")

        Dim 已释放生成器 As ASS特效字幕帧生成器
        Using 轨道 = 外部字幕自动加载器.加载字幕(ASS路径, 视频路径)
            断言(轨道.ASS特效生成器 IsNot Nothing, "ASS 轨道没有使用 libass。")
            已释放生成器 = 轨道.ASS特效生成器
            断言(已释放生成器.生成帧(TimeSpan.Zero, 1280, 720) Is Nothing,
               "无字幕时刻没有生成透明清空帧。")
            Dim OP帧 = 已释放生成器.生成帧(TimeSpan.FromSeconds(97), 1280, 720)
            断言(OP帧 IsNot Nothing AndAlso OP帧.像素BGRA.Length > 0,
               "OP 的模糊、定位字幕没有生成位图。")
            断言(是预乘BGRA(OP帧.像素BGRA), "libass 位图没有转换为 Direct2D 所需的预乘 BGRA。")
            测试ASS半透明像素(视频路径)
            Using 画面 As New 播放器画面控件()
                Using 呈现器 As New 播放器定时文字图层呈现器(画面, Function() Nothing,
                    Function() 轨道, Sub(size, commands, sequence, frameRate) Return)
                    Dim 命令 = 呈现器.生成命令(New Size(1280, 720), 1280UI, 720UI,
                        TimeSpan.FromSeconds(97), 轨道)
                    断言(命令.Count = 1 AndAlso 命令(0).是位图 AndAlso 命令(0).位图像素BGRA.Length > 0,
                       "ASS 特效帧没有进入独立 GPU 字幕图层。")
                End Using
            End Using

            Dim 淡入前 = 已释放生成器.生成帧(TimeSpan.FromSeconds(1227.2), 1280, 720)
            Dim 淡入中 = 已释放生成器.生成帧(TimeSpan.FromSeconds(1227.25), 1280, 720)
            Dim 淡入后 = 已释放生成器.生成帧(TimeSpan.FromSeconds(1227.35), 1280, 720)
            断言(淡入前 Is Nothing AndAlso 淡入中 IsNot Nothing AndAlso 淡入后 IsNot Nothing AndAlso
               淡入中.内容标识 <> 淡入后.内容标识,
               "ASS 的 fad/blur 动画没有随媒体时间更新。")
        End Using
        Dim 已释放 = False
        Try
            已释放生成器.生成帧(TimeSpan.Zero, 1280, 720)
        Catch ex As ObjectDisposedException
            已释放 = True
        End Try
        断言(已释放, "ASS 轨道释放后原生 libass 句柄仍可使用。")
        Dim 字体文件 = Directory.EnumerateFiles(字体目录.First()).First(
            Function(x) {".ttf", ".otf", ".ttc"}.Contains(Path.GetExtension(x), StringComparer.OrdinalIgnoreCase))
        Using 字体流 = New FileStream(字体文件, FileMode.Open, FileAccess.Read, FileShare.None)
            断言(字体流.Length > 0, "释放 libass 后字体文件不可读取。")
        End Using
        Console.WriteLine($"ASS 字体目录：{String.Join("；", 字体目录)}")
    End Sub

    Private Function 是预乘BGRA(像素 As Byte()) As Boolean
        For index = 0 To 像素.Length - 4 Step 4
            Dim alpha = 像素(index + 3)
            If 像素(index) > alpha OrElse 像素(index + 1) > alpha OrElse 像素(index + 2) > alpha Then Return False
        Next
        Return True
    End Function

    Private Sub 测试ASS半透明像素(媒体路径 As String)
        Dim 临时路径 = Path.Combine(Path.GetTempPath(),
            "fff-player-ass-alpha-" & Guid.NewGuid().ToString("N") & ".ass")
        Dim 脚本 = String.Join(vbLf, {
            "[Script Info]",
            "ScriptType: v4.00+",
            "PlayResX: 320",
            "PlayResY: 180",
            "",
            "[V4+ Styles]",
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding",
            "Style: Default,Arial,48,&H80FFFFFF,&H80FFFFFF,&H80000000,&H80000000,0,0,0,0,100,100,0,0,1,0,0,5,0,0,0,1",
            "",
            "[Events]",
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
            "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\p1}m 80 50 l 240 50 240 130 80 130{\p0}"})
        Try
            File.WriteAllText(临时路径, 脚本, New UTF8Encoding(False))
            Using 生成器 As New ASS特效字幕帧生成器(临时路径, 媒体路径)
                Dim 帧 = 生成器.生成帧(TimeSpan.FromSeconds(1), 320, 180)
                断言(帧 IsNot Nothing AndAlso 帧.像素BGRA.Length > 0,
                   "半透明 ASS 测试没有生成位图。")
                Dim 最大Alpha索引 = 3
                For index = 7 To 帧.像素BGRA.Length - 1 Step 4
                    If 帧.像素BGRA(index) > 帧.像素BGRA(最大Alpha索引) Then 最大Alpha索引 = index
                Next
                Dim alpha = CInt(帧.像素BGRA(最大Alpha索引))
                Dim blue = CInt(帧.像素BGRA(最大Alpha索引 - 3))
                Dim green = CInt(帧.像素BGRA(最大Alpha索引 - 2))
                Dim red = CInt(帧.像素BGRA(最大Alpha索引 - 1))
                断言(alpha >= 120 AndAlso alpha <= 132 AndAlso
                   blue >= alpha - 16 AndAlso green >= alpha - 16 AndAlso red >= alpha - 16 AndAlso
                   blue <= alpha AndAlso green <= alpha AndAlso red <= alpha,
                   $"半透明 ASS 像素发生重复预乘：BGRA={blue},{green},{red},{alpha}。")
            End Using
        Finally
            If File.Exists(临时路径) Then File.Delete(临时路径)
        End Try
    End Sub

    Private Sub 测试音频规格回归(视频路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU,
            .色彩模式 = 色彩输出模式.映射到SDR,
            .SDR峰值尼特 = 100.0F,
            .HDR峰值尼特 = 1000.0F,
            .SDR纸白尼特 = 203.0F
        })
            会话.设置音量(0.0F, True)
            会话.打开Async(视频路径).GetAwaiter().GetResult()
            会话.播放()
            等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(60))
            Dim 开头结果 = 采样播放(会话, 4.0, Nothing)
            Console.WriteLine($"《你的名字》开头音频：{格式化播放报告(开头结果)}，" &
                              $"PTS抖动 {开头结果.音频时间戳抖动帧数}、断点 {开头结果.音频不连续次数}、" &
                              $"补零/裁样 {开头结果.音频插入静音帧数}/{开头结果.音频丢弃重叠帧数}")
            验证连续PCM结果(开头结果, "《你的名字》开头")

            会话.跳转(TimeSpan.FromSeconds(1000))
            等待预热(会话, TimeSpan.FromSeconds(1001), TimeSpan.FromSeconds(90))
            Dim 跳转结果 = 采样播放(会话, 4.0, Nothing)
            Console.WriteLine($"《你的名字》1000 秒音频：{格式化播放报告(跳转结果)}，" &
                              $"PTS抖动 {跳转结果.音频时间戳抖动帧数}、断点 {跳转结果.音频不连续次数}、" &
                              $"补零/裁样 {跳转结果.音频插入静音帧数}/{跳转结果.音频丢弃重叠帧数}")
            验证连续PCM结果(跳转结果, "《你的名字》1000 秒跳转")
        End Using
    End Sub

    Private Sub 验证连续PCM结果(结果 As 播放测量结果, 阶段 As String)
        验证播放结果(结果, 阶段)
        验证音频结果(结果, 阶段)
        断言(结果.平均音画差毫秒 <= 40.0 AndAlso 结果.最大音画差毫秒 <= 100.0,
           $"{阶段}连续 PCM/设备时钟仍有可见音画偏差：" &
           $"{结果.平均音画差毫秒:F1}/{结果.最大音画差毫秒:F1} ms。")
        断言(结果.音频时间戳抖动帧数 > 0, $"{阶段}没有观测到该片源已知的 AAC PTS 量化抖动。")
        断言(结果.音频不连续次数 = 0 AndAlso 结果.音频插入静音帧数 = 0 AndAlso
           结果.音频丢弃重叠帧数 = 0,
           $"{阶段}把连续 AAC PTS 抖动误判为断点：{结果.音频不连续次数}/" &
           $"{结果.音频插入静音帧数}/{结果.音频丢弃重叠帧数}。")
    End Sub

    Private Sub 测试播放中字幕替换(视频路径 As String, SUP路径 As String)
        Dim 临时目录 = Path.Combine(Path.GetTempPath(), "fff-player-subtitle-regression-" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(临时目录)
        Try
            Dim SRT路径 = Path.Combine(临时目录, "replacement.srt")
            Dim ASS路径 = Path.Combine(临时目录, "replacement.ass")
            Dim SSA路径 = Path.Combine(临时目录, "replacement.ssa")
            File.WriteAllText(SRT路径, "1" & vbLf & "00:00:00,000 --> 02:00:00,000" & vbLf & "SRT replacement" & vbLf)
            Dim 脚本 = "[Script Info]" & vbLf & "ScriptType: v4.00+" & vbLf &
                "PlayResX: 1920" & vbLf & "PlayResY: 1080" & vbLf &
                "[V4+ Styles]" & vbLf &
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding" & vbLf &
                "Style: Default,Microsoft YaHei,48,&H00FFFFFF,&H000000FF,&H80000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,20,1" & vbLf &
                "[Events]" & vbLf &
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text" & vbLf &
                "Dialogue: 0,0:00:00.00,2:00:00.00,Default,,0,0,0,,replacement" & vbLf
            File.WriteAllText(ASS路径, 脚本)
            File.WriteAllText(SSA路径, 脚本)

            Using 控制器 As New 播放器控制器(Function() IntPtr.Zero, Nothing)
                打开并等待(控制器, 视频路径)
                控制器.切换播放暂停()
                Dim 基线 = 控制器.安全读取快照()
                Dim 媒体打开次数 = 0
                AddHandler 控制器.媒体已打开, Sub() 媒体打开次数 += 1
                For Each 字幕路径 In {SUP路径, SRT路径, ASS路径, SSA路径}
                    Dim 已加载 As New ManualResetEventSlim(False)
                    Dim 预期路径 = Path.GetFullPath(字幕路径)
                    Dim 处理器 As EventHandler(Of 播放器字幕事件参数) =
                        Sub(sender, e)
                            If String.Equals(Path.GetFullPath(e.路径), 预期路径, StringComparison.OrdinalIgnoreCase) Then 已加载.Set()
                        End Sub
                    AddHandler 控制器.外部字幕已加载, 处理器
                    控制器.替换字幕(字幕路径)
                    断言(已加载.Wait(TimeSpan.FromSeconds(30)), $"替换 {Path.GetExtension(字幕路径)} 字幕超时。")
                    RemoveHandler 控制器.外部字幕已加载, 处理器
                    断言(控制器.当前字幕 IsNot Nothing AndAlso
                       String.Equals(Path.GetFullPath(控制器.当前字幕.路径), 预期路径, StringComparison.OrdinalIgnoreCase),
                       $"{Path.GetExtension(字幕路径)} 没有成为当前字幕轨。")
                Next
                Dim 末快照 = 控制器.安全读取快照()
                断言(媒体打开次数 = 0 AndAlso 末快照 IsNot Nothing AndAlso 基线 IsNot Nothing AndAlso
                   末快照.总时长 = 基线.总时长 AndAlso
                   末快照.当前视频流 = 基线.当前视频流 AndAlso 末快照.当前音频流 = 基线.当前音频流,
                   "字幕替换错误地重建或改动了当前媒体会话。")
            End Using
            Console.WriteLine("播放中 SUP→SRT→ASS→SSA 原子替换通过，媒体会话和流选择保持不变。")
        Finally
            If Directory.Exists(临时目录) Then Directory.Delete(临时目录, True)
        End Try
    End Sub

    Private Sub 测试播放中弹幕替换(视频路径 As String)
        Dim 临时目录 = Path.Combine(Path.GetTempPath(), "fff-player-danmaku-regression-" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(临时目录)
        Try
            Dim 第一份路径 = Path.Combine(临时目录, "first.xml")
            Dim 第二份路径 = Path.Combine(临时目录, "second.XML")
            Dim 损坏路径 = Path.Combine(临时目录, "broken.xml")
            File.WriteAllText(第一份路径,
                "<?xml version=""1.0"" encoding=""UTF-8""?><i>" &
                "<d p=""1.0,1,25,16777215,0,0,user-a,101"">first</d>" &
                "<d p=""2.0,5,30,65280,0,0,user-b,102"">second</d></i>")
            File.WriteAllText(第二份路径,
                "<?xml version=""1.0"" encoding=""UTF-8""?><i>" &
                "<d p=""3.0,1,28,16711680,0,0,user-c,201"">replacement</d></i>")
            File.WriteAllText(损坏路径, "<i><d p=""1.0,1,25,1"">broken")

            断言(弹幕自动加载器.是支持的弹幕文件(第二份路径), "大写 XML 扩展名没有被识别为弹幕文件。")
            Using 控制器 As New 播放器控制器(Function() IntPtr.Zero, Nothing)
                打开并等待(控制器, 视频路径)
                控制器.切换播放暂停()
                Dim 基线 = 控制器.安全读取快照()
                Dim 媒体打开次数 = 0
                AddHandler 控制器.媒体已打开, Sub() 媒体打开次数 += 1

                For Each 测试项 In {(路径:=第一份路径, 数量:=2, 文本:="first"),
                                  (路径:=第二份路径, 数量:=1, 文本:="replacement")}
                    Using 已加载 As New ManualResetEventSlim(False)
                        Dim 预期路径 = Path.GetFullPath(测试项.路径)
                        Dim 处理器 As EventHandler(Of 播放器弹幕事件参数) =
                            Sub(sender, e)
                                If String.Equals(Path.GetFullPath(e.路径), 预期路径, StringComparison.OrdinalIgnoreCase) Then 已加载.Set()
                            End Sub
                        AddHandler 控制器.外部弹幕已加载, 处理器
                        控制器.替换弹幕(测试项.路径)
                        断言(已加载.Wait(TimeSpan.FromSeconds(30)), $"替换弹幕 {Path.GetFileName(测试项.路径)} 超时。")
                        RemoveHandler 控制器.外部弹幕已加载, 处理器
                    End Using
                    Dim 当前资料库 = 控制器.当前弹幕
                    断言(当前资料库 IsNot Nothing AndAlso 当前资料库.数量 = 测试项.数量 AndAlso
                       当前资料库.项目.Any(Function(x) String.Equals(x.文本, 测试项.文本, StringComparison.Ordinal)),
                       $"{Path.GetFileName(测试项.路径)} 没有原子替换当前弹幕。")
                Next

                Dim 替换前资料库 = 控制器.当前弹幕
                Using 已失败 As New ManualResetEventSlim(False)
                    Dim 错误处理器 As EventHandler(Of 播放器错误事件参数) =
                        Sub(sender, e)
                            If String.Equals(e.标题, "无法加载弹幕", StringComparison.Ordinal) Then 已失败.Set()
                        End Sub
                    AddHandler 控制器.播放错误, 错误处理器
                    控制器.替换弹幕(损坏路径)
                    断言(已失败.Wait(TimeSpan.FromSeconds(30)), "损坏 XML 弹幕没有报告加载失败。")
                    RemoveHandler 控制器.播放错误, 错误处理器
                End Using
                断言(ReferenceEquals(控制器.当前弹幕, 替换前资料库), "损坏 XML 替换清空或改动了原有弹幕。")

                Dim 末快照 = 控制器.安全读取快照()
                断言(媒体打开次数 = 0 AndAlso 末快照 IsNot Nothing AndAlso 基线 IsNot Nothing AndAlso
                   末快照.总时长 = 基线.总时长 AndAlso
                   末快照.当前视频流 = 基线.当前视频流 AndAlso 末快照.当前音频流 = 基线.当前音频流 AndAlso
                   Math.Abs((末快照.播放位置 - 基线.播放位置).TotalMilliseconds) <= 100.0,
                   "弹幕替换错误地重建会话，或改变了播放位置及音视频流选择。")
            End Using
            Console.WriteLine("播放中 XML→XML 原子替换及损坏文件回退通过，媒体会话、位置和流选择保持不变。")
        Finally
            If Directory.Exists(临时目录) Then Directory.Delete(临时目录, True)
        End Try
    End Sub

    Private Sub 测试色彩回归(SDR路径 As String, HDR路径 As String)
        测试数值色彩映射()

        Dim SDR源峰值 = 读取源峰值(SDR路径, False)
        Dim HDR源峰值 = 读取源峰值(HDR路径, True)
        断言(SDR源峰值 = 100UI, $"SDR 源的内部峰值不是 100 nit：{SDR源峰值}。")
        断言(HDR源峰值 = 1242UI, $"HDR 源没有采用 MaxCLL 1242 nit：{HDR源峰值}。")
        测试SDR拒绝真实HDR输出(SDR路径)

        Using 控制器 As New 播放器控制器(Function() IntPtr.Zero, Nothing)
            打开并等待(控制器, HDR路径)
            Dim HDR快照 = 控制器.安全读取快照()
            断言(HDR快照 IsNot Nothing AndAlso HDR快照.是HDR源, "HDR 样本没有被播放器识别为 PQ/HLG。")
            断言(HDR快照.请求色彩模式 = 色彩输出模式.映射到SDR,
               "HDR 样本没有以 SDR 映射作为初始模式。")

            控制器.切换HDR模式()
            等待请求色彩模式(控制器, 色彩输出模式.原始HDR按SDR呈现)
            控制器.切换HDR模式()
            等待请求色彩模式(控制器, 色彩输出模式.峰值映射HDR)
            断言(控制器.色彩模式 = 色彩输出模式.峰值映射HDR,
               "测试前置条件失败：HDR 样本没有切换到真实 HDR 请求。")

            打开并等待(控制器, SDR路径)
            Dim SDR快照 = 控制器.安全读取快照()
            断言(SDR快照 IsNot Nothing AndAlso Not SDR快照.是HDR源, "SDR 样本被错误识别为 HDR。")
            断言(控制器.色彩模式 = 色彩输出模式.映射到SDR AndAlso
               SDR快照.请求色彩模式 = 色彩输出模式.映射到SDR AndAlso
               SDR快照.实际色彩模式 = 色彩输出模式.映射到SDR,
               "HDR→SDR 换片后仍沿用了 PQ/BT.2020 真实 HDR 输出状态。")
        End Using

        Console.WriteLine($"内部色彩状态：SDR {SDR源峰值} nit，HDR MaxCLL {HDR源峰值} nit；HDR→SDR 换片已回到 BT.709 SDR。")
    End Sub

    Private Sub 测试SDR拒绝真实HDR输出(SDR路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU,
            .色彩模式 = 色彩输出模式.峰值映射HDR,
            .SDR峰值尼特 = 100.0F,
            .HDR峰值尼特 = 1000.0F,
            .SDR纸白尼特 = 203.0F
        })
            会话.打开Async(SDR路径).GetAwaiter().GetResult()
            Dim 快照 = 会话.当前快照
            断言(Not 快照.是HDR源 AndAlso 快照.请求色彩模式 = 色彩输出模式.映射到SDR AndAlso
               快照.实际色彩模式 = 色彩输出模式.映射到SDR,
               "原生会话接受了 SDR 文件的真实 HDR 输出请求。")
        End Using
    End Sub

    Private Sub 测试数值色彩映射()
        Dim SDR = 执行色彩变换(0UI, 0UI, 0.18F, 0.5F, 1.0F, 100.0F, 203.0F)
        断言(Math.Abs(SDR.输出红 - 0.18F) < 0.0001F AndAlso
           Math.Abs(SDR.输出绿 - 0.5F) < 0.0001F AndAlso
           Math.Abs(SDR.输出蓝 - 1.0F) < 0.0001F,
           "纯 SDR→SDR 路径改变了 BT.709 码值。")

        Dim 一百尼特 = 执行色彩变换(0UI, 1UI, PQ码值(100.0F), PQ码值(100.0F), PQ码值(100.0F), 1242.0F, 203.0F)
        Dim 纸白 = 执行色彩变换(0UI, 1UI, PQ码值(203.0F), PQ码值(203.0F), PQ码值(203.0F), 1242.0F, 203.0F)
        Dim 峰值 = 执行色彩变换(0UI, 1UI, PQ码值(1242.0F), PQ码值(1242.0F), PQ码值(1242.0F), 1242.0F, 203.0F)
        断言(一百尼特.输出红 > 0.5F AndAlso 一百尼特.输出红 < 纸白.输出红,
           "HDR→SDR 中间调没有保持单调且合理的亮度。")
        断言(纸白.输出红 >= 0.68F AndAlso 纸白.输出红 <= 0.76F,
           $"HDR 203 nit 纸白被推得过亮：SDR 码值 {纸白.输出红:F4}。")
        断言(峰值.输出红 >= 0.995F AndAlso 峰值.输出红 <= 1.0F,
           $"HDR MaxCLL 没有映射到 SDR 峰值：{峰值.输出红:F4}。")
        Console.WriteLine($"数值映射：PQ 100/203/1242 nit → SDR {一百尼特.输出红:F4}/{纸白.输出红:F4}/{峰值.输出红:F4}。")
    End Sub

    Private Function 执行色彩变换(模式 As UInteger, 传递函数 As UInteger,
                              红 As Single, 绿 As Single, 蓝 As Single,
                              源峰值 As Single, 纸白 As Single) As 原生色彩变换
        Dim 变换 As New 原生色彩变换 With {
            .大小 = CUInt(Marshal.SizeOf(Of 原生色彩变换)()),
            .版本 = 1UI,
            .色彩模式 = 模式,
            .传递函数 = 传递函数,
            .输入红 = 红,
            .输入绿 = 绿,
            .输入蓝 = 蓝,
            .SDR峰值尼特 = 100.0F,
            .源峰值尼特 = 源峰值,
            .纸白尼特 = 纸白
        }
        Dim 结果 = FFF3FP_EvaluateColorTransform(变换)
        断言(结果 = 0, $"原生色彩变换诊断失败：{结果}。")
        Return 变换
    End Function

    Private Function PQ码值(尼特 As Single) As Single
        Const m1 As Double = 2610.0 / 16384.0
        Const m2 As Double = 2523.0 / 32.0
        Const c1 As Double = 3424.0 / 4096.0
        Const c2 As Double = 2413.0 / 128.0
        Const c3 As Double = 2392.0 / 128.0
        Dim 线性 = Math.Pow(Math.Clamp(CDbl(尼特) / 10000.0, 0.0, 1.0), m1)
        Return CSng(Math.Pow((c1 + c2 * 线性) / (1.0 + c3 * 线性), m2))
    End Function

    Private Function 读取源峰值(路径 As String, 应为HDR As Boolean) As UInteger
        Using 隐藏输出 As New Form With {.ClientSize = New Drawing.Size(320, 180), .ShowInTaskbar = False}
            Dim 输出句柄 = 隐藏输出.Handle
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .SDR峰值尼特 = 100.0F,
                .HDR峰值尼特 = 1000.0F,
                .SDR纸白尼特 = 203.0F,
                .输出窗口句柄 = 输出句柄
            })
                会话.设置音量(0.0F, True)
                会话.打开Async(路径).GetAwaiter().GetResult()
                断言(会话.当前快照.是HDR源 = 应为HDR, $"片源 HDR 标记与预期不符：{路径}")
                会话.播放()
                Dim 计时 = Stopwatch.StartNew()
                Do
                    Application.DoEvents()
                    Dim 快照 = 会话.当前快照
                    If 快照.已呈现视频帧数 > 0 AndAlso 快照.源峰值尼特 > 0 Then Return 快照.源峰值尼特
                    If 快照.状态 = 播放状态.失败 Then Throw New InvalidOperationException("读取色彩内部数据时播放失败。")
                    If 计时.Elapsed >= TimeSpan.FromSeconds(30) Then Throw New TimeoutException("等待色彩内部数据超时。")
                    Thread.Sleep(5)
                Loop
            End Using
        End Using
    End Function

    Private Sub 打开并等待(控制器 As 播放器控制器, 路径 As String)
        Using 已打开 As New ManualResetEventSlim(False)
            Dim 失败消息 As String = Nothing
            Dim 打开处理 As EventHandler(Of 播放器媒体事件参数) =
                Sub(sender, e)
                    If String.Equals(e.文件路径, 路径, StringComparison.OrdinalIgnoreCase) Then 已打开.Set()
                End Sub
            Dim 错误处理 As EventHandler(Of 播放器错误事件参数) =
                Sub(sender, e)
                    失败消息 = e.消息
                    已打开.Set()
                End Sub
            AddHandler 控制器.媒体已打开, 打开处理
            AddHandler 控制器.播放错误, 错误处理
            Try
                控制器.打开媒体(路径)
                Dim 计时 = Stopwatch.StartNew()
                Do Until 已打开.IsSet
                    Application.DoEvents()
                    If 计时.Elapsed >= TimeSpan.FromSeconds(30) Then Throw New TimeoutException($"等待打开媒体超时：{路径}")
                    Thread.Sleep(5)
                Loop
                If Not String.IsNullOrEmpty(失败消息) Then Throw New InvalidOperationException(失败消息)
            Finally
                RemoveHandler 控制器.媒体已打开, 打开处理
                RemoveHandler 控制器.播放错误, 错误处理
            End Try
        End Using
    End Sub

    Private Sub 等待请求色彩模式(控制器 As 播放器控制器, 模式 As 色彩输出模式)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 快照 = 控制器.安全读取快照()
            If 快照 IsNot Nothing AndAlso 快照.请求色彩模式 = 模式 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(5) Then Throw New TimeoutException($"等待色彩模式 {模式} 超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 测试性能回归(SDR路径 As String, HDR路径 As String)
        Dim 弹幕 = 创建性能弹幕资料库()
        Dim 弹幕配置 As New 弹幕显示配置 With {
            .字体 = "Microsoft YaHei", .字号 = 8.0F, .使用源字号 = False,
            .目标帧率 = 60.0F, .同屏最大数量 = 100,
            .常规滚动最大行数 = 100, .顶部最大行数 = 100,
            .行间距 = 0.0F, .顶部边距 = 0.0F,
            .固定弹幕持续秒数 = 120.0F, .基准视频高度 = 1080.0F}
        Using 字幕 = 创建性能字幕轨道()
            ' 创建真实 HWND 和交换链，但窗口从不 Show；所有判定只读内部计数器。
            Using 输出窗口 As New Form With {
                .ClientSize = New Drawing.Size(1280, 720), .ShowInTaskbar = False,
                .FormBorderStyle = FormBorderStyle.None, .StartPosition = FormStartPosition.Manual,
                .Location = New Drawing.Point(-32000, -32000)}
                Using 画面控件 As New 播放器画面控件 With {.Dock = DockStyle.Fill}
                    输出窗口.Controls.Add(画面控件)
                    Dim 窗口句柄 = 输出窗口.Handle
                    Dim 输出句柄 = 画面控件.输出窗口句柄
                    ' 联合压力固定走 CPU 解码，覆盖用户报告的 4K 软件解码加
                    ' 60 Hz 弹幕路径；GPU 解码仍由常规诊断单独覆盖。
                    Using 会话 As New 播放器会话(New 播放器配置 With {
                        .解码器 = 解码模式.CPU, .色彩模式 = 色彩输出模式.映射到SDR,
                        .SDR峰值尼特 = 100.0F, .HDR峰值尼特 = 1000.0F,
                        .SDR纸白尼特 = 203.0F, .输出窗口句柄 = 输出句柄})
                        会话.设置音量(0.0F, True)
                        Using 字幕呈现器 As New 播放器定时文字图层呈现器(
                            画面控件, Function() 会话.当前快照, Function() 字幕,
                            AddressOf 会话.设置定时文字图层, Nothing, Nothing,
                            定时文字图层内容.仅字幕)
                            Using 弹幕呈现器 As New 播放器定时文字图层呈现器(
                                画面控件, Function() 会话.当前快照, Function() Nothing,
                                AddressOf 会话.设置弹幕图层, Function() 弹幕, 弹幕配置,
                                定时文字图层内容.仅弹幕)
                                ' 两个生产者使用各自的高精度泵和原生图层槽位。
                                Dim 图层泵 As Action = Nothing

                            ' 先播 HDR，再在同一个原生会话和同一个 HWND 上打开 SDR；这条路径
                            ' 能直接捕获 PQ/BT.2020 交换链或真实 HDR 请求被错误继承的问题。
                            会话.打开Async(HDR路径).GetAwaiter().GetResult()
                            断言(会话.当前快照.是HDR源, "性能回归的 HDR 样本没有被识别为 HDR。")
                            会话.播放()
                            等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(30))
                            等待压力图层(字幕呈现器, 弹幕呈现器, 会话)
                            Dim HDR帧率 = 读取视频帧率(会话)
                            Dim HDR结果 = 采样播放(会话, 6.0, 画面控件, 图层泵,
                                                 Function() 会话.当前弹幕状态)
                            Console.WriteLine($"HDR→SDR 联合压力：{格式化播放报告(HDR结果)}")
                            验证性能结果(HDR结果, HDR帧率, "HDR→SDR 联合压力")

                            会话.设置色彩模式(色彩输出模式.峰值映射HDR, 100.0F, 1000.0F, 203.0F)
                            等待色彩模式(会话, 色彩输出模式.峰值映射HDR)
                            会话.打开Async(SDR路径).GetAwaiter().GetResult()
                            Dim SDR打开快照 = 会话.当前快照
                            断言(Not SDR打开快照.是HDR源 AndAlso
                               SDR打开快照.请求色彩模式 = 色彩输出模式.映射到SDR AndAlso
                               SDR打开快照.实际色彩模式 = 色彩输出模式.映射到SDR,
                               "同会话 HDR→SDR 换片后仍继承了真实 HDR/PQ 输出状态。")
                            会话.播放()
                            等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(30))
                            等待压力图层(字幕呈现器, 弹幕呈现器, 会话)
                            断言(会话.当前快照.源峰值尼特 = 100UI,
                               $"SDR 换片后的源峰值不是 100 nit：{会话.当前快照.源峰值尼特}。")
                            Dim SDR帧率 = 读取视频帧率(会话)
                            Dim SDR结果 = 采样播放(会话, 6.0, 画面控件, 图层泵,
                                                 Function() 会话.当前弹幕状态)
                            Console.WriteLine($"SDR 联合压力：{格式化播放报告(SDR结果)}")
                            验证性能结果(SDR结果, SDR帧率, "SDR 联合压力")

                            测试音频切换与跳转(会话, SDR路径, SDR帧率, 画面控件, 图层泵,
                                           Function() 会话.当前弹幕状态)
                            End Using
                        End Using
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Function 创建性能弹幕资料库() As 弹幕资料库
        Dim items = Enumerable.Range(0, 100).Select(
            Function(index) New 弹幕项目(TimeSpan.Zero,
                弹幕类型.常规滚动, 1, 25.0F,
                &HFFFFFFFFUI, 0, 0, "performance", index + 1,
                $"弹幕压力 {index + 1:000} / danmaku performance"))
        Return New 弹幕资料库(items)
    End Function

    Private Function 创建性能字幕轨道() As 外部字幕轨道
        Const 内容 As String = "1" & vbLf &
            "00:00:00,000 --> 03:00:00,000" & vbLf &
            "字幕与弹幕联合性能回归 / subtitle performance contract" & vbLf
        Using reader As New StringReader(内容)
            Dim document = SRT字幕解析器.解析(reader)
            Return New 外部字幕轨道("internal-performance.srt", 外部字幕格式.SRT,
                New SRT字幕帧生成器(document, New SRT字幕样式()), Nothing)
        End Using
    End Function

    Private Sub 等待压力图层(字幕呈现器 As 播放器定时文字图层呈现器,
                         弹幕呈现器 As 播放器定时文字图层呈现器, 会话 As 播放器会话)
        Dim 失效前字幕序号 = 会话.当前定时文字状态.已提交序号
        Dim 失效前弹幕序号 = 会话.当前弹幕状态.已提交序号
        字幕呈现器.使图层失效()
        弹幕呈现器.使图层失效()
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 字幕状态 = 会话.当前定时文字状态
            Dim 弹幕状态 = 会话.当前弹幕状态
            If 字幕状态.已提交序号 > 失效前字幕序号 AndAlso
                字幕状态.已提交序号 = 字幕状态.已绘制序号 AndAlso 字幕状态.命令数 = 1 AndAlso
                字幕状态.可见像素数 > 0 AndAlso
                弹幕状态.已提交序号 > 失效前弹幕序号 AndAlso
                弹幕状态.已提交序号 = 弹幕状态.已绘制序号 AndAlso 弹幕状态.命令数 = 100 AndAlso
                弹幕状态.可见像素数 > 0 Then
                Dim 字幕序号 = 字幕状态.已提交序号
                Dim 弹幕序号 = 弹幕状态.已提交序号
                Dim 字幕合成帧数 = 字幕状态.图层呈现帧数
                Dim 弹幕合成帧数 = 弹幕状态.图层呈现帧数
                Dim 后备缓冲获取次数 = 字幕状态.后备缓冲获取次数
                Dim 独立计时 = Stopwatch.StartNew()
                While 独立计时.Elapsed < TimeSpan.FromMilliseconds(250)
                    Application.DoEvents()
                    Thread.Sleep(5)
                End While
                字幕状态 = 会话.当前定时文字状态
                弹幕状态 = 会话.当前弹幕状态
                断言(字幕状态.已提交序号 = 字幕序号,
                   "静态字幕被弹幕刷新率驱动并重复提交。")
                断言(弹幕状态.已提交序号 > 弹幕序号,
                   "弹幕没有使用独立于字幕的刷新序号。")
                ' 字幕不需要重绘，但每个最终交换链帧仍必须合成现有字幕与弹幕。
                ' 这条契约捕获双 Present 源交替提交而导致整层闪烁的回归。
                断言(字幕状态.图层呈现帧数 > 字幕合成帧数 AndAlso
                   弹幕状态.图层呈现帧数 > 弹幕合成帧数,
                   "静态字幕或弹幕没有持续进入最终交换链合成帧。")
                Dim 合成增量 = CULng(字幕状态.图层呈现帧数 - 字幕合成帧数)
                Dim 获取增量 = 字幕状态.后备缓冲获取次数 - 后备缓冲获取次数
                断言(获取增量 >= 合成增量 AndAlso 获取增量 <= 合成增量 + 1UL,
                   $"最终合成没有逐帧重新获取 D3D11 flip-model 逻辑后备缓冲：" &
                   $"获取/合成 {获取增量}/{合成增量}。")
                断言(字幕状态.合成像素着色器调用次数 > 0UL AndAlso
                   弹幕状态.合成像素着色器调用次数 > 0UL,
                   "字幕或弹幕最终全屏合成没有产生 GPU 像素着色器调用。")
                Console.WriteLine($"最终合成 GPU 像素调用：字幕 {字幕状态.合成像素着色器调用次数}，" &
                                  $"弹幕 {弹幕状态.合成像素着色器调用次数}。")
                Return
            End If
            If 计时.Elapsed >= TimeSpan.FromSeconds(5) Then
                Throw New TimeoutException($"独立字幕/弹幕图层没有收敛，最后命令数 " &
                    $"{字幕状态.命令数}/{弹幕状态.命令数}。")
            End If
            Thread.Sleep(10)
        Loop
    End Sub

    Private Function 读取视频帧率(会话 As 播放器会话) As Double
        Dim 快照 = 会话.当前快照
        Dim 信息 = 会话.当前媒体信息
        Dim 视频 = 信息?.流.FirstOrDefault(Function(x) x.索引 = 快照.当前视频流 AndAlso x.类型 = "video")
        断言(视频 IsNot Nothing AndAlso 视频.平均帧率 > 0, "媒体信息没有有效的视频平均帧率。")
        Return 视频.平均帧率
    End Function

    Private Sub 测试音频切换与跳转(会话 As 播放器会话, 音频路径 As String,
                               源帧率 As Double, 画面控件 As 播放器画面控件,
                               图层泵 As Action, 图层状态提供器 As Func(Of 定时文字状态))
        会话.加载外部音轨(音频路径)
        等待外部音轨状态(会话, True)
        Dim 外部起点 = 会话.当前快照.播放位置
        等待预热(会话, 外部起点 + TimeSpan.FromSeconds(0.5), TimeSpan.FromSeconds(30))
        Dim 外部结果 = 采样播放(会话, 2.5, 画面控件, 图层泵, 图层状态提供器)
        Console.WriteLine($"外部音轨：{格式化播放报告(外部结果)}")
        验证音频结果(外部结果, "外部音轨")
        验证性能结果(外部结果, 源帧率, "外部音轨", False)

        会话.设置外部音轨偏移(TimeSpan.FromMilliseconds(125))
        等待外部音轨偏移(会话, TimeSpan.FromMilliseconds(125))
        会话.跳转(TimeSpan.FromSeconds(30))
        等待预热(会话, TimeSpan.FromSeconds(30.5), TimeSpan.FromSeconds(30))
        Dim 跳转结果 = 采样播放(会话, 2.5, 画面控件, 图层泵, 图层状态提供器)
        Console.WriteLine($"外部音轨跳转：{格式化播放报告(跳转结果)}")
        验证音频结果(跳转结果, "外部音轨跳转")
        验证性能结果(跳转结果, 源帧率, "外部音轨跳转", False)

        Dim 恢复位置 = 会话.当前快照.播放位置
        会话.清除外部音轨()
        等待外部音轨状态(会话, False)
        等待预热(会话, 恢复位置 + TimeSpan.FromSeconds(0.5), TimeSpan.FromSeconds(30))
        Dim 内置结果 = 采样播放(会话, 2.5, 画面控件, 图层泵, 图层状态提供器)
        Console.WriteLine($"恢复内置音轨：{格式化播放报告(内置结果)}")
        验证音频结果(内置结果, "恢复内置音轨")
        验证性能结果(内置结果, 源帧率, "恢复内置音轨", False)
    End Sub

    Private Sub 等待外部音轨状态(会话 As 播放器会话, 目标 As Boolean)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            If 会话.当前快照.正在使用外部音轨 = 目标 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(10) Then Throw New TimeoutException("等待外部音轨切换超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 等待外部音轨偏移(会话 As 播放器会话, 目标 As TimeSpan)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            If 会话.当前快照.外部音轨偏移 = 目标 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(10) Then Throw New TimeoutException("等待外部音轨偏移更新超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Function 测试弹幕(视频路径 As String, 资料库 As 弹幕资料库) As TimeSpan
        断言(资料库 IsNot Nothing AndAlso 资料库.数量 > 0, "弹幕 XML 没有解析出任何条目。")
        Dim 自动加载资料库 = 弹幕自动加载器.尝试加载同名弹幕Async(视频路径, CancellationToken.None).
            GetAwaiter().GetResult()
        断言(自动加载资料库 IsNot Nothing AndAlso 自动加载资料库.数量 = 资料库.数量,
           "打开媒体时没有加载同名 XML 弹幕。")
        Dim 配置 As New 弹幕显示配置()
        断言(配置.同屏最大数量 = 100, "弹幕默认同屏上限不是 100。")
        断言(配置.字号 = 32.0F, "弹幕默认基础字号不是 32。")
        断言(配置.常规滚动最大行数 = 5, "弹幕默认显示行数上限不是 5。")
        断言(配置.顶部最大行数 = 5, "顶部弹幕默认显示行数上限不是 5。")
        断言(String.Equals(配置.字体, "Microsoft YaHei", StringComparison.Ordinal), "弹幕默认字体不是微软雅黑。")
        断言(配置.目标帧率 = 60.0F, "弹幕默认帧率不是 60 FPS。")

        Dim 区域 = 视频显示区域.计算(1280, 720, 96.0F, 3840, 2160)
        Dim 调度器 As New 弹幕调度器(资料库, 配置)
        Dim 绘制项 As New List(Of 弹幕绘制项)(配置.同屏最大数量)
        Dim 测试位置 = TimeSpan.Zero
        For Each 项 In 资料库.项目
            If (项.类型 And 弹幕类型.常用) = 0 Then Continue For
            绘制项.Clear()
            调度器.生成帧(项.出现时间 + TimeSpan.FromMilliseconds(250), 区域, 绘制项)
            If 绘制项.Count = 0 Then Continue For
            测试位置 = 项.出现时间 + TimeSpan.FromMilliseconds(250)
            Exit For
        Next
        断言(测试位置 > TimeSpan.Zero OrElse 绘制项.Count > 0, "没有生成可显示的基础弹幕。")
        断言(绘制项.Count > 0 AndAlso 绘制项.Count <= 配置.同屏最大数量,
           "弹幕调度器没有遵守同屏数量上限。")
        断言(绘制项.All(Function(x) x.字号像素 > 0 AndAlso x.宽度像素 > 0 AndAlso
                         x.高度像素 > 0 AndAlso Not String.IsNullOrWhiteSpace(x.项目.文本)),
           "弹幕绘制指令包含无效文字或几何数据。")
        测试弹幕行数上限(配置)
        测试弹幕相对缩放(配置)
        测试小尺寸滚动连续性(配置)
        Console.WriteLine($"弹幕：{资料库.数量} 条，默认 100 条/5 行/32 号微软雅黑/60 FPS，" &
                          $"{绘制项.Count} 条基础弹幕在 {测试位置.TotalSeconds:F3}s 的调度通过。")
        Return 测试位置
    End Function

    Private Sub 测试弹幕行数上限(配置 As 弹幕显示配置)
        Dim 区域 = 视频显示区域.计算(1920, 1080, 96.0F, 1920, 1080)
        For Each 类型 In {弹幕类型.常规滚动, 弹幕类型.顶部}
            Dim 密集项目 = Enumerable.Range(1, 20).Select(
                Function(index) New 弹幕项目(TimeSpan.Zero, 类型, 5, 25.0F,
                    &HFFFFFFFFUI, 0, 0, "test", index, $"密集弹幕 {index}"))
            Dim 调度器 As New 弹幕调度器(New 弹幕资料库(密集项目), 配置)
            Dim 绘制项 As New List(Of 弹幕绘制项)()
            调度器.生成帧(TimeSpan.FromMilliseconds(100), 区域, 绘制项)
            Dim 行数 = 绘制项.Select(Function(item) Math.Round(item.Y像素, 3)).Distinct().Count()
            断言(行数 <= 5, $"{类型}弹幕生成了 {行数} 行，超过 5 行上限。")
        Next
    End Sub

    Private Sub 测试小尺寸滚动连续性(配置 As 弹幕显示配置)
        Dim 资料库 As New 弹幕资料库({New 弹幕项目(TimeSpan.Zero, 弹幕类型.常规滚动, 5, 25.0F,
            &HFFFFFFFFUI, 0, 0, "test", 1, "小尺寸连续滚动测试")})
        For Each 高度 In {360, 720, 1080, 2160}
            Dim 区域 = 视频显示区域.计算(CInt(高度 * 16.0 / 9.0), 高度, 96.0F, 1920, 1080)
            Dim 调度器 As New 弹幕调度器(资料库, 配置)
            Dim 上一X As Single = Single.NaN
            Dim 预期步长 = 配置.滚动速度 * 区域.高度像素 / 配置.基准视频高度 / 配置.目标帧率
            For 帧 = 1 To 120
                Dim 绘制项 As New List(Of 弹幕绘制项)(1)
                调度器.生成帧(TimeSpan.FromSeconds(帧 / CDbl(配置.目标帧率)), 区域, 绘制项)
                断言(绘制项.Count = 1, $"{高度} 高度的连续滚动测试丢失了第 {帧} 帧。")
                If Single.IsFinite(上一X) Then
                    Dim 实际步长 = 上一X - 绘制项(0).X像素
                    断言(Math.Abs(实际步长 - 预期步长) < 0.01F,
                       $"{高度} 高度的弹幕滚动步长不连续：{实际步长:F3}/{预期步长:F3}。")
                End If
                上一X = 绘制项(0).X像素
            Next
        Next
    End Sub

    Private Sub 测试弹幕相对缩放(配置 As 弹幕显示配置)
        Dim 单条资料库 As New 弹幕资料库({New 弹幕项目(TimeSpan.Zero, 弹幕类型.顶部, 5, 25.0F,
            &HFFFFFFFFUI, 0, 0, "test", 1, "DPI 缩放测试")})
        Using 画面控件 As New 播放器画面控件()
            Using 呈现器 As New 播放器定时文字图层呈现器(画面控件, Function() Nothing,
                Function() Nothing, Sub(size, commands, sequence, frameRate) Return, Function() 单条资料库, 配置,
                定时文字图层内容.仅弹幕)
                Dim 七百二十命令 = 呈现器.生成命令(New Size(1280, 720), 3840UI, 2160UI,
                    TimeSpan.FromMilliseconds(100), Nothing, 96.0F).Single()
                Dim 一千零八十命令 = 呈现器.生成命令(New Size(1920, 1080), 3840UI, 2160UI,
                    TimeSpan.FromMilliseconds(100), Nothing, 96.0F).Single()
                Dim 高DPI命令 = 呈现器.生成命令(New Size(1920, 1080), 3840UI, 2160UI,
                    TimeSpan.FromMilliseconds(100), Nothing, 144.0F).Single()
                Dim 四K命令 = 呈现器.生成命令(New Size(3840, 2160), 3840UI, 2160UI,
                    TimeSpan.FromMilliseconds(100), Nothing, 192.0F).Single()
                断言(Math.Abs(七百二十命令.字号 - (32.0F * 2.0F / 3.0F)) < 0.01F AndAlso
                   Math.Abs(一千零八十命令.字号 - 32.0F) < 0.01F AndAlso
                   Math.Abs(高DPI命令.字号 - 32.0F) < 0.01F AndAlso
                   Math.Abs(四K命令.字号 - 64.0F) < 0.01F,
                   $"弹幕 DPI/视频相对字号异常：{七百二十命令.字号:F2}/" &
                   $"{一千零八十命令.字号:F2}/{高DPI命令.字号:F2}/{四K命令.字号:F2}。")
                断言(一千零八十命令.描边色ARGB = &H80000000UI AndAlso
                   Math.Abs(一千零八十命令.描边宽度 - 1.0F) < 0.01F,
                   "弹幕浅描边没有按 32 号基准生成。")
                呈现器.目标帧率 = 120
                断言(配置.目标帧率 = 120.0F,
                   "弹幕高刷选项没有同步更新调度器的媒体时间量化频率。")
                呈现器.目标帧率 = 60
            End Using
        End Using
    End Sub

    Private Sub 测试字幕(视频路径 As String, ASS路径 As String, SRT路径 As String)
        Dim SRT文档 = SRT字幕解析器.解析文件(SRT路径)
        断言(SRT文档.提示.Count = 2384, $"SRT 条目数异常：{SRT文档.提示.Count}。")

        Dim 区域 = 视频显示区域.计算(1280, 720, 96.0F, 3840, 2160)
        Dim SRT绘制项 As New List(Of SRT字幕绘制项)()
        Dim SRT生成器 As New SRT字幕帧生成器(SRT文档, New SRT字幕样式())
        SRT生成器.生成帧(TimeSpan.FromSeconds(75.5), 区域, SRT绘制项)
        断言(SRT绘制项.Count > 0 AndAlso SRT绘制项.Any(
               Function(x) x.行.Any(Function(y) Not String.IsNullOrWhiteSpace(y.文本))),
               "SRT 在 1:15.5 没有生成可见文本。")

        Using 画面控件 As New 播放器画面控件()
            Using 呈现器 As New 播放器定时文字图层呈现器(画面控件, Function() Nothing,
                                                        Function() Nothing,
                                                        Sub(size, commands, sequence, frameRate) Return)
                Using ASS轨道 = 外部字幕自动加载器.加载字幕(ASS路径, 视频路径)
                    断言(ASS轨道.ASS特效生成器 IsNot Nothing, "ASS 没有创建 libass 渲染器。")
                    断言(统计字幕命令(呈现器, ASS轨道) > 0, "ASS 没有生成 GPU 特效位图命令。")
                End Using
                Using SRT轨道 As New 外部字幕轨道(SRT路径, 外部字幕格式.SRT,
                                                New SRT字幕帧生成器(SRT文档, New SRT字幕样式()),
                                                Nothing)
                    断言(统计字幕命令(呈现器, SRT轨道) > 0, "SRT 没有生成 GPU 文字命令。")
                End Using
            End Using
        End Using

        Using 自动轨道 = 外部字幕自动加载器.尝试加载同名字幕Async(
            视频路径, CancellationToken.None).GetAwaiter().GetResult()
            断言(自动轨道 IsNot Nothing, "后台字幕加载器没有找到同名字幕。")
            断言(自动轨道.格式 = 外部字幕格式.SRT, "后台字幕加载优先级没有选择 SRT。")
        End Using
        Console.WriteLine($"字幕：ASS libass 特效位图、SRT {SRT文档.提示.Count} 条，后台加载与 1:15.5 帧生成通过。")
    End Sub

    Private Function 统计字幕命令(呈现器 As 播放器定时文字图层呈现器,
                                字幕 As 外部字幕轨道) As Integer
        Return 呈现器.生成命令(New Size(1280, 720), 3840UI, 2160UI,
                           TimeSpan.FromSeconds(75.5), 字幕).Count
    End Function

    Private Function 测试播放(视频路径 As String, 字幕路径 As String, 弹幕 As 弹幕资料库,
                          弹幕测试位置 As TimeSpan, 模式 As 解码模式) As 播放测量结果
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(1280, 720),
            .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(20, 20),
            .Text = $"3FP {模式} 自动性能测试"
        }
            Dim 画面控件 As 播放器画面控件 = Nothing
            Dim 字幕呈现器 As 播放器定时文字图层呈现器 = Nothing
            Dim 弹幕呈现器 As 播放器定时文字图层呈现器 = Nothing
            Dim 字幕轨道 As 外部字幕轨道 = Nothing
            If Not String.IsNullOrEmpty(字幕路径) OrElse 弹幕 IsNot Nothing Then
                画面控件 = New 播放器画面控件 With {.Dock = DockStyle.Fill}
                输出窗口.Controls.Add(画面控件)
            End If
            If Not String.IsNullOrEmpty(字幕路径) Then
                Select Case Path.GetExtension(字幕路径).ToLowerInvariant()
                    Case ".srt"
                        Dim 文档 = SRT字幕解析器.解析文件(字幕路径)
                        字幕轨道 = New 外部字幕轨道(字幕路径, 外部字幕格式.SRT,
                            New SRT字幕帧生成器(文档, New SRT字幕样式()), Nothing)
                    Case ".ass", ".ssa"
                        字幕轨道 = 外部字幕自动加载器.加载字幕(字幕路径, 视频路径)
                    Case Else
                        Throw New NotSupportedException($"测试不支持字幕格式：{字幕路径}")
                End Select
            End If
            输出窗口.Show()
            Application.DoEvents()
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 模式,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .SDR峰值尼特 = 100.0F,
                .HDR峰值尼特 = 1000.0F,
                .SDR纸白尼特 = 203.0F,
                .输出窗口句柄 = If(画面控件 Is Nothing, 输出窗口.Handle, 画面控件.输出窗口句柄)
            })
                Try
                    If 画面控件 IsNot Nothing Then
                        If 字幕轨道 IsNot Nothing Then
                            字幕呈现器 = New 播放器定时文字图层呈现器(画面控件,
                                Function() 会话.当前快照, Function() 字幕轨道,
                                AddressOf 会话.设置定时文字图层, Nothing, Nothing,
                                定时文字图层内容.仅字幕)
                        End If
                        If 弹幕 IsNot Nothing Then
                            弹幕呈现器 = New 播放器定时文字图层呈现器(画面控件,
                                Function() 会话.当前快照, Function() Nothing,
                                AddressOf 会话.设置弹幕图层, Function() 弹幕, Nothing,
                                定时文字图层内容.仅弹幕)
                        End If
                    End If
                    会话.设置音量(0.0F, True)
                    会话.打开Async(视频路径).GetAwaiter().GetResult()
                    Dim 信息 = 会话.当前媒体信息
                    断言(信息 IsNot Nothing AndAlso 信息.流.Any(
                           Function(x) x.类型 = "video" AndAlso x.编码 = "av1" AndAlso
                               x.宽度 = 3840 AndAlso x.高度 = 2160),
                           "测试视频没有被识别为 4K AV1。")
                    测试HDR映射状态(会话)
                    会话.播放()
                    等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(30))
                    Dim 图层状态提供器 As Func(Of 定时文字状态) = Nothing
                    If 弹幕 IsNot Nothing Then 图层状态提供器 = Function() 会话.当前弹幕状态
                    Dim 顺播结果 = 采样播放(会话, 测量秒数, 画面控件, Nothing, 图层状态提供器)
                    测试暂停停钟(会话)
                    会话.跳转(TimeSpan.FromSeconds(75))
                    等待预热(会话, TimeSpan.FromSeconds(75.5), TimeSpan.FromSeconds(30))
                    Dim 跳转结果 = 采样播放(会话, 6.0, 画面控件, Nothing, 图层状态提供器)
                    验证播放结果(跳转结果, "跳转后")
                    Console.WriteLine($"跳转后：{格式化播放报告(跳转结果)}")
                    If 画面控件 IsNot Nothing AndAlso 字幕轨道 IsNot Nothing Then
                        会话.跳转(TimeSpan.FromSeconds(75))
                        等待预热(会话, TimeSpan.FromSeconds(75.5), TimeSpan.FromSeconds(30))
                        测试屏幕字幕(会话, 画面控件)
                    End If
                    If 画面控件 IsNot Nothing AndAlso 弹幕 IsNot Nothing Then
                        测试屏幕弹幕(会话, 画面控件, 弹幕呈现器, 弹幕测试位置)
                    End If
                    Return 顺播结果
                Finally
                    弹幕呈现器?.释放()
                    字幕呈现器?.释放()
                    字幕轨道?.释放()
                    画面控件?.Dispose()
                End Try
            End Using
        End Using
    End Function

    Private Sub 测试HDR映射状态(会话 As 播放器会话)
        Dim 初始快照 = 会话.当前快照
        If Not 初始快照.是HDR源 Then Return
        断言(初始快照.请求色彩模式 = 色彩输出模式.映射到SDR AndAlso
           初始快照.实际色彩模式 = 色彩输出模式.映射到SDR, "HDR 文件打开后没有使用 SDR 映射。")
        会话.设置色彩模式(色彩输出模式.原始HDR按SDR呈现, 100.0F, 1000.0F, 203.0F)
        等待色彩模式(会话, 色彩输出模式.原始HDR按SDR呈现)
        会话.设置色彩模式(色彩输出模式.峰值映射HDR, 100.0F, 1000.0F, 203.0F)
        等待色彩模式(会话, 色彩输出模式.峰值映射HDR)
        会话.设置色彩模式(色彩输出模式.映射到SDR, 100.0F, 1000.0F, 203.0F)
        等待色彩模式(会话, 色彩输出模式.映射到SDR)
        Dim 最终快照 = 会话.当前快照
        断言(最终快照.实际色彩模式 = 色彩输出模式.映射到SDR,
           "HDR 模式循环后没有恢复到 SDR 映射。")
        Console.WriteLine("HDR：原始 SDR、真实 HDR 请求与再次映射 SDR 的原生状态切换通过。")
    End Sub

    Private Sub 等待色彩模式(会话 As 播放器会话, 请求模式 As 色彩输出模式)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            If 快照.请求色彩模式 = 请求模式 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(3) Then
                Throw New TimeoutException($"等待色彩模式 {请求模式} 超时。")
            End If
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 等待预热(会话 As 播放器会话, 目标位置 As TimeSpan, 超时 As TimeSpan)
        Dim 计时 = Stopwatch.StartNew()
        Dim 上限 = 目标位置 + TimeSpan.FromSeconds(2)
        Do
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            If 快照.状态 = 播放状态.失败 Then
                Throw New InvalidOperationException($"播放器在预热阶段失败：{会话.最后错误消息}")
            End If
            If 快照.播放位置 >= 目标位置 AndAlso 快照.播放位置 < 上限 AndAlso
                快照.帧序号 >= 0 Then Return
            If 计时.Elapsed >= 超时 Then Throw New TimeoutException("播放预热超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 测试暂停停钟(会话 As 播放器会话)
        会话.暂停()
        等待状态(会话, 播放状态.已暂停, TimeSpan.FromSeconds(3))
        Dim 暂停位置 = 会话.当前快照.播放位置
        Thread.Sleep(750)
        Application.DoEvents()
        Dim 漂移 = Math.Abs((会话.当前快照.播放位置 - 暂停位置).TotalMilliseconds)
        断言(漂移 <= 10.0, $"暂停期间媒体时钟漂移 {漂移:F1} ms。")
        会话.播放()
        等待状态(会话, 播放状态.正在播放, TimeSpan.FromSeconds(3))
        Console.WriteLine($"暂停：750 ms 内媒体时钟漂移 {漂移:F1} ms。")
    End Sub

    Private Sub 测试屏幕字幕(会话 As 播放器会话, 画面控件 As 播放器画面控件)
        会话.暂停()
        等待状态(会话, 播放状态.已暂停, TimeSpan.FromSeconds(3))
        Dim 显示计时 = Stopwatch.StartNew()
        While 显示计时.Elapsed < TimeSpan.FromSeconds(3)
            Application.DoEvents()
            Thread.Sleep(16)
        End While
        Dim 状态 As 定时文字状态 = Nothing
        Dim 收敛计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            状态 = 会话.当前定时文字状态
            If 状态.已绘制序号 > 0 AndAlso 状态.已绘制序号 = 状态.已提交序号 AndAlso
                状态.命令数 > 0 Then Exit Do
            If 收敛计时.Elapsed >= TimeSpan.FromSeconds(3) Then Exit Do
            Thread.Sleep(10)
        Loop
        断言(状态.已绘制序号 > 0 AndAlso 状态.已绘制序号 = 状态.已提交序号,
           "GPU 定时文字图层没有绘制最新命令。")
        断言(状态.命令数 > 0, "GPU 定时文字图层没有可见字幕命令。")
        断言(状态.可见像素数 > 100, $"GPU 定时文字图层只有 {状态.可见像素数} 个可见像素。")
        Console.WriteLine($"GPU 屏幕字幕：在 1:15.5 的暂停帧上持续显示 3 秒，" &
                          $"绘制 {状态.命令数} 条命令/{状态.可见像素数} 个可见像素。")
        会话.播放()
        等待状态(会话, 播放状态.正在播放, TimeSpan.FromSeconds(3))
    End Sub

    Private Sub 测试屏幕弹幕(会话 As 播放器会话, 画面控件 As 播放器画面控件,
                           呈现器 As 播放器定时文字图层呈现器, 位置 As TimeSpan)
        断言(呈现器 IsNot Nothing, "弹幕测试没有创建 GPU 定时文字呈现器。")
        会话.暂停()
        等待状态(会话, 播放状态.已暂停, TimeSpan.FromSeconds(3))
        Dim 快照 = 会话.当前快照
        呈现器.提交当前帧(画面控件.ClientSize, 快照.视频宽度, 快照.视频高度, 位置, Nothing,
                       画面控件.DeviceDpi)
        Dim 状态 As 定时文字状态 = Nothing
        Dim 收敛计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            状态 = 会话.当前弹幕状态
            If 状态.已绘制序号 > 0 AndAlso 状态.已绘制序号 = 状态.已提交序号 AndAlso
                状态.命令数 > 0 Then Exit Do
            If 收敛计时.Elapsed >= TimeSpan.FromSeconds(3) Then Exit Do
            Thread.Sleep(10)
        Loop
        断言(状态.已绘制序号 > 0 AndAlso 状态.已绘制序号 = 状态.已提交序号,
           "GPU 弹幕图层没有绘制最新命令。")
        断言(状态.命令数 > 0 AndAlso 状态.命令数 <= 100,
           $"GPU 弹幕命令数 {状态.命令数} 未落在 1 到 100 的范围内。")
        断言(状态.可见像素数 > 10, $"GPU 弹幕图层只有 {状态.可见像素数} 个可见像素。")
        Console.WriteLine($"GPU 弹幕：绘制 {状态.命令数} 条命令、{状态.可见像素数} 个可见像素。")
        会话.播放()
        等待状态(会话, 播放状态.正在播放, TimeSpan.FromSeconds(3))
        Dim 初始图层呈现数 = 会话.当前弹幕状态.图层呈现帧数
        Dim 帧率计时 = Stopwatch.StartNew()
        While 帧率计时.Elapsed < TimeSpan.FromSeconds(2)
            Application.DoEvents()
            Thread.Sleep(1)
        End While
        Dim 最终图层呈现数 = 会话.当前弹幕状态.图层呈现帧数
        Dim 图层呈现帧率 = (CULng(最终图层呈现数) - 初始图层呈现数) / 帧率计时.Elapsed.TotalSeconds
        断言(图层呈现帧率 >= 55.0 AndAlso 图层呈现帧率 <= 65.0,
           $"弹幕图层实际呈现为 {图层呈现帧率:F2} FPS，偏离 60 FPS 目标。")
        Console.WriteLine($"GPU 弹幕实际呈现：{图层呈现帧率:F2} FPS（视频源 {目标帧率:F2} FPS）。")
    End Sub

    Private Sub 等待状态(会话 As 播放器会话, 目标 As 播放状态, 超时 As TimeSpan)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            If 会话.当前快照.状态 = 目标 Then Return
            If 计时.Elapsed >= 超时 Then Throw New TimeoutException($"等待状态 {目标} 超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Function 采样播放(会话 As 播放器会话, 秒数 As Double,
                          画面控件 As 播放器画面控件,
                          Optional 图层泵 As Action = Nothing,
                          Optional 图层状态提供器 As Func(Of 定时文字状态) = Nothing) As 播放测量结果
        Dim 进程 = Process.GetCurrentProcess()
        Dim 首快照 = 会话.当前快照
        Dim 首位置 = 首快照.播放位置
        Dim 首呈现帧数 = 首快照.已呈现视频帧数
        Dim 首丢帧数 = 首快照.已丢弃视频帧数
        Dim 首解码视频帧数 = 首快照.已解码视频帧数
        Dim 首解码音频帧数 = 首快照.已解码音频帧数
        Dim 首音频欠载次数 = 首快照.音频欠载次数
        Dim 首时间戳抖动帧数 = 首快照.音频时间戳抖动帧数
        Dim 首音频不连续次数 = 首快照.音频不连续次数
        Dim 首音频插入静音帧数 = 首快照.音频插入静音帧数
        Dim 首音频丢弃重叠帧数 = 首快照.音频丢弃重叠帧数
        Dim 首图层状态 = If(画面控件 Is Nothing, Nothing,
                         If(图层状态提供器 Is Nothing, 会话.当前定时文字状态, 图层状态提供器()))
        Dim 首图层呈现数 = If(首图层状态 Is Nothing, 0UL, CULng(首图层状态.图层呈现帧数))
        Dim 首图层提交序号 = If(首图层状态 Is Nothing, 0UL, 首图层状态.已提交序号)
        Dim 首精灵缓存命中 = If(首图层状态 Is Nothing, 0UL, 首图层状态.精灵缓存命中次数)
        Dim 首精灵缓存未命中 = If(首图层状态 Is Nothing, 0UL, 首图层状态.精灵缓存未命中次数)
        ' 状态读取可能执行一次 GPU 诊断回读；测量墙钟必须在所有基线都取得后启动。
        Dim CPU开始 = 进程.TotalProcessorTime
        Dim 托管分配开始 = GC.GetTotalAllocatedBytes(False)
        Dim 第0代开始 = GC.CollectionCount(0)
        Dim 第1代开始 = GC.CollectionCount(1)
        Dim 第2代开始 = GC.CollectionCount(2)
        Dim 墙钟 = Stopwatch.StartNew()
        Dim 上次PTS = 首快照.原始帧PTS
        Dim 媒体视频差总毫秒 As Double
        Dim 媒体视频差样本数 As Integer
        Dim 最大媒体视频差毫秒 As Double
        Dim 音画差总毫秒 As Double
        Dim 音画差样本数 As Integer
        Dim 最大音画差毫秒 As Double
        Dim 视频队列总和 As Long
        Dim 视频队列样本数 As Integer
        Dim 最大视频队列 As Integer
        Dim 音频缓冲总毫秒 As Double
        Dim 最小音频缓冲毫秒 As Double = Double.PositiveInfinity
        Dim 最大音频缓冲毫秒 As Double
        Dim 音频缓冲样本数 As Integer
        Dim 下次图层秒 As Double

        While 墙钟.Elapsed.TotalSeconds < 秒数
            Application.DoEvents()
            If 图层泵 IsNot Nothing AndAlso 墙钟.Elapsed.TotalSeconds >= 下次图层秒 Then
                图层泵()
                下次图层秒 += 1.0R / 60.0R
            End If
            Dim 快照 = 会话.当前快照
            If 快照.状态 = 播放状态.失败 Then Throw New InvalidOperationException("播放器在测量阶段失败。")
            视频队列总和 += 快照.视频队列帧数
            视频队列样本数 += 1
            最大视频队列 = Math.Max(最大视频队列, 快照.视频队列帧数)
            Dim 音频缓冲毫秒 = 快照.音频缓冲时长.TotalMilliseconds
            音频缓冲总毫秒 += 音频缓冲毫秒
            最小音频缓冲毫秒 = Math.Min(最小音频缓冲毫秒, 音频缓冲毫秒)
            最大音频缓冲毫秒 = Math.Max(最大音频缓冲毫秒, 音频缓冲毫秒)
            音频缓冲样本数 += 1
            If 快照.原始帧PTS <> Long.MinValue AndAlso 快照.原始帧PTS <> 上次PTS Then
                上次PTS = 快照.原始帧PTS
            End If
            If 快照.帧时间基分母 > 0 AndAlso 快照.原始帧PTS <> Long.MinValue Then
                Dim 视频位置 = TimeSpan.FromSeconds(快照.原始帧PTS *
                    CDbl(快照.帧时间基分子) / 快照.帧时间基分母)
                Dim 差值 = Math.Abs((快照.播放位置 - 视频位置).TotalMilliseconds)
                媒体视频差总毫秒 += 差值
                媒体视频差样本数 += 1
                最大媒体视频差毫秒 = Math.Max(最大媒体视频差毫秒, 差值)
                If 快照.已解码音频帧数 > 0 Then
                    Dim 音画差 = Math.Abs((快照.音频位置 - 视频位置).TotalMilliseconds)
                    音画差总毫秒 += 音画差
                    音画差样本数 += 1
                    最大音画差毫秒 = Math.Max(最大音画差毫秒, 音画差)
                End If
            End If
            Thread.Sleep(If(图层泵 Is Nothing, 5, 1))
        End While

        墙钟.Stop()
        Dim 测量时长秒 = 墙钟.Elapsed.TotalSeconds
        Dim CPU结束 = 进程.TotalProcessorTime
        Dim 托管分配结束 = GC.GetTotalAllocatedBytes(False)
        Dim 第0代结束 = GC.CollectionCount(0)
        Dim 第1代结束 = GC.CollectionCount(1)
        Dim 第2代结束 = GC.CollectionCount(2)
        Dim 末快照 = 会话.当前快照
        Dim 末位置 = 末快照.播放位置
        Dim 呈现帧数 = 末快照.已呈现视频帧数 - 首呈现帧数
        Dim 丢帧数 = 末快照.已丢弃视频帧数 - 首丢帧数
        Dim 末图层状态 = If(画面控件 Is Nothing, Nothing,
                         If(图层状态提供器 Is Nothing, 会话.当前定时文字状态, 图层状态提供器()))
        Dim 图层呈现数 = If(末图层状态 Is Nothing, 0UL,
                          CULng(末图层状态.图层呈现帧数) - 首图层呈现数)
        进程.Refresh()
        Return New 播放测量结果 With {
            .实际呈现帧率 = CDbl(呈现帧数) / 测量时长秒,
            .丢帧数 = CLng(丢帧数),
            .播放速度 = (末位置 - 首位置).TotalSeconds / 测量时长秒,
            .平均媒体视频差毫秒 = If(媒体视频差样本数 = 0, Double.PositiveInfinity,
                              媒体视频差总毫秒 / 媒体视频差样本数),
            .最大媒体视频差毫秒 = 最大媒体视频差毫秒,
            .平均音画差毫秒 = If(音画差样本数 = 0, Double.PositiveInfinity,
                            音画差总毫秒 / 音画差样本数),
            .最大音画差毫秒 = 最大音画差毫秒,
            .进程CPU占用百分比 = (CPU结束 - CPU开始).TotalSeconds /
                              测量时长秒 / Environment.ProcessorCount * 100.0,
            .平均视频队列帧数 = If(视频队列样本数 = 0, 0.0, CDbl(视频队列总和) / 视频队列样本数),
            .最大视频队列帧数 = 最大视频队列,
            .平均音频缓冲毫秒 = If(音频缓冲样本数 = 0, 0.0, 音频缓冲总毫秒 / 音频缓冲样本数),
            .最小音频缓冲毫秒 = If(Double.IsPositiveInfinity(最小音频缓冲毫秒), 0.0, 最小音频缓冲毫秒),
            .最大音频缓冲毫秒 = 最大音频缓冲毫秒,
            .已解码视频帧数 = CLng(末快照.已解码视频帧数 - 首解码视频帧数),
            .已解码音频帧数 = CLng(末快照.已解码音频帧数 - 首解码音频帧数),
            .音频欠载次数 = CLng(末快照.音频欠载次数 - 首音频欠载次数),
            .音频时间戳抖动帧数 = CLng(末快照.音频时间戳抖动帧数 - 首时间戳抖动帧数),
            .音频不连续次数 = CLng(末快照.音频不连续次数 - 首音频不连续次数),
            .音频插入静音帧数 = CLng(末快照.音频插入静音帧数 - 首音频插入静音帧数),
            .音频丢弃重叠帧数 = CLng(末快照.音频丢弃重叠帧数 - 首音频丢弃重叠帧数),
            .图层呈现帧率 = CDbl(图层呈现数) / 测量时长秒,
            .图层提交帧率 = If(末图层状态 Is Nothing, 0.0,
                           CDbl(末图层状态.已提交序号 - 首图层提交序号) / 测量时长秒),
            .精灵缓存命中次数 = If(末图层状态 Is Nothing, 0L,
                                CLng(末图层状态.精灵缓存命中次数 - 首精灵缓存命中)),
            .精灵缓存未命中次数 = If(末图层状态 Is Nothing, 0L,
                                  CLng(末图层状态.精灵缓存未命中次数 - 首精灵缓存未命中)),
            .托管分配字节每秒 = (托管分配结束 - 托管分配开始) / 测量时长秒,
            .第0代回收次数 = 第0代结束 - 第0代开始,
            .第1代回收次数 = 第1代结束 - 第1代开始,
            .第2代回收次数 = 第2代结束 - 第2代开始
        }
    End Function

    Private Sub 输出播放报告(模式 As 解码模式, 结果 As 播放测量结果)
        Console.WriteLine($"{模式}：{格式化播放报告(结果)}")
    End Sub

    Private Function 格式化播放报告(结果 As 播放测量结果) As String
        Return $"{结果.实际呈现帧率:F2} fps，丢帧 {结果.丢帧数}，时钟 {结果.播放速度:F4}x，" &
               $"媒体/视频 {结果.平均媒体视频差毫秒:F1}/{结果.最大媒体视频差毫秒:F1} ms，" &
               $"音频/视频 {结果.平均音画差毫秒:F1}/{结果.最大音画差毫秒:F1} ms，" &
               $"视频队列均值/峰值 {结果.平均视频队列帧数:F1}/{结果.最大视频队列帧数}，" &
               $"音频缓冲均值/最小/最大 {结果.平均音频缓冲毫秒:F1}/{结果.最小音频缓冲毫秒:F1}/{结果.最大音频缓冲毫秒:F1} ms，" &
               $"音频帧 {结果.已解码音频帧数}、欠载 {结果.音频欠载次数}，" &
               $"图层提交/呈现 {结果.图层提交帧率:F1}/{结果.图层呈现帧率:F1} fps，" &
               $"GC {结果.托管分配字节每秒 / 1024.0 / 1024.0:F2} MiB/s " &
               $"精灵缓存 {结果.精灵缓存命中次数}/{结果.精灵缓存未命中次数}，" &
               $"({结果.第0代回收次数}/{结果.第1代回收次数}/{结果.第2代回收次数})，" &
               $"进程 CPU {结果.进程CPU占用百分比:F1}%"
    End Function

    Private Sub 验证播放结果(结果 As 播放测量结果, 阶段 As String)
        断言(结果.实际呈现帧率 >= 目标帧率 * 0.94,
           $"{阶段}呈现吞吐不足：{结果.实际呈现帧率:F2} fps。")
        断言(结果.丢帧数 <= 2, $"{阶段}测量窗口内丢弃了 {结果.丢帧数} 帧。")
        断言(结果.播放速度 >= 0.97 AndAlso 结果.播放速度 <= 1.03,
           $"{阶段}媒体时钟速度异常：{结果.播放速度:F4}x。")
        断言(结果.平均媒体视频差毫秒 <= 80.0,
           $"{阶段}平均媒体/视频时钟差过大：{结果.平均媒体视频差毫秒:F1} ms。")
        断言(结果.最大媒体视频差毫秒 <= 180.0,
           $"{阶段}最大媒体/视频时钟差过大：{结果.最大媒体视频差毫秒:F1} ms。")
    End Sub

    Private Sub 验证性能结果(结果 As 播放测量结果, 源帧率 As Double, 阶段 As String,
                         Optional 验证图层 As Boolean = True)
        验证播放结果(结果, 阶段)
        断言(结果.实际呈现帧率 >= 源帧率 * 0.94,
           $"{阶段}没有跟上源帧率：{结果.实际呈现帧率:F2}/{源帧率:F2} fps。")
        断言(结果.最大视频队列帧数 <= 8,
           $"{阶段}视频队列超过 8 帧的有界合同：{结果.最大视频队列帧数}。")
        If 验证图层 Then
            ' 这是 100 条同时移动且带描边文字的上限压力，远高于产品默认 5 行；
            ' GPU 精灵批处理和独立媒体时钟仍须维持 55–65 FPS。
            断言(结果.图层呈现帧率 >= 55.0 AndAlso 结果.图层呈现帧率 <= 65.0,
               $"{阶段}字幕/弹幕图层呈现率异常：{结果.图层呈现帧率:F2} FPS。")
            断言(结果.精灵缓存命中次数 > 0 AndAlso 结果.精灵缓存未命中次数 <= 5,
               $"{阶段}滚动文字没有复用 GPU 精灵：命中/未命中 " &
               $"{结果.精灵缓存命中次数}/{结果.精灵缓存未命中次数}。")
        End If
    End Sub

    Private Sub 验证音频结果(结果 As 播放测量结果, 阶段 As String)
        断言(结果.已解码音频帧数 > 0, $"{阶段}没有解码任何音频帧。")
        断言(结果.平均音频缓冲毫秒 > 5.0,
           $"{阶段}音频缓冲没有建立：{结果.平均音频缓冲毫秒:F1} ms。")
        断言(结果.音频欠载次数 <= 1,
           $"{阶段}音频出现 {结果.音频欠载次数} 次欠载。")
        断言(结果.平均音画差毫秒 <= 350.0 AndAlso 结果.最大音画差毫秒 <= 650.0,
           $"{阶段}音频/视频时钟差过大：{结果.平均音画差毫秒:F1}/{结果.最大音画差毫秒:F1} ms。")
    End Sub

    Private Sub 检查文件(路径 As String)
        If Not File.Exists(路径) Then Throw New FileNotFoundException("测试文件不存在。", 路径)
    End Sub

    Private Sub 断言(条件 As Boolean, 消息 As String)
        If Not 条件 Then Throw New InvalidOperationException(消息)
    End Sub

    Private NotInheritable Class 播放测量结果
        Public Property 实际呈现帧率 As Double
        Public Property 丢帧数 As Long
        Public Property 播放速度 As Double
        Public Property 平均媒体视频差毫秒 As Double
        Public Property 最大媒体视频差毫秒 As Double
        Public Property 平均音画差毫秒 As Double
        Public Property 最大音画差毫秒 As Double
        Public Property 进程CPU占用百分比 As Double
        Public Property 平均视频队列帧数 As Double
        Public Property 最大视频队列帧数 As Integer
        Public Property 平均音频缓冲毫秒 As Double
        Public Property 最小音频缓冲毫秒 As Double
        Public Property 最大音频缓冲毫秒 As Double
        Public Property 已解码视频帧数 As Long
        Public Property 已解码音频帧数 As Long
        Public Property 音频欠载次数 As Long
        Public Property 音频时间戳抖动帧数 As Long
        Public Property 音频不连续次数 As Long
        Public Property 音频插入静音帧数 As Long
        Public Property 音频丢弃重叠帧数 As Long
        Public Property 图层呈现帧率 As Double
        Public Property 图层提交帧率 As Double
        Public Property 精灵缓存命中次数 As Long
        Public Property 精灵缓存未命中次数 As Long
        Public Property 托管分配字节每秒 As Double
        Public Property 第0代回收次数 As Integer
        Public Property 第1代回收次数 As Integer
        Public Property 第2代回收次数 As Integer
    End Class
End Module
