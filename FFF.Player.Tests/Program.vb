Imports System.Diagnostics
Imports System.Drawing
Imports System.IO
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module Program
    Private Const 测量秒数 As Double = 12.0
    Private Const 目标帧率 As Double = 24000.0 / 1001.0

    <STAThread>
    Public Function Main(参数 As String()) As Integer
        Try
            If 参数.Length < 3 Then
                Console.Error.WriteLine("用法: FFF.Player.Tests <视频.mp4> <字幕.ass> <字幕.srt>")
                Return 2
            End If
            Dim 视频路径 = Path.GetFullPath(参数(0))
            Dim ASS路径 = Path.GetFullPath(参数(1))
            Dim SRT路径 = Path.GetFullPath(参数(2))
            检查文件(视频路径)
            检查文件(ASS路径)
            检查文件(SRT路径)

            测试字幕(视频路径, ASS路径, SRT路径)
            Dim CPU结果 = 测试播放(视频路径, SRT路径, 解码模式.CPU)
            输出播放报告(解码模式.CPU, CPU结果)
            验证播放结果(CPU结果, "CPU 顺播")
            Dim GPU结果 = 测试播放(视频路径, ASS路径, 解码模式.GPU)
            输出播放报告(解码模式.GPU, GPU结果)
            验证播放结果(GPU结果, "GPU 顺播")
            Console.WriteLine("全部诊断测试通过。")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine($"测试失败：{ex.Message}")
            Return 1
        End Try
    End Function

    Private Sub 测试字幕(视频路径 As String, ASS路径 As String, SRT路径 As String)
        Dim ASS文档 = ASS字幕解析器.解析文件(ASS路径)
        Dim SRT文档 = SRT字幕解析器.解析文件(SRT路径)
        断言(ASS文档.提示.Count = 2015, $"ASS 条目数异常：{ASS文档.提示.Count}。")
        断言(SRT文档.提示.Count = 2384, $"SRT 条目数异常：{SRT文档.提示.Count}。")

        Dim 区域 = 视频显示区域.计算(1280, 720, 96.0F, 3840, 2160)
        Dim ASS绘制项 As New List(Of ASS字幕绘制项)()
        Dim SRT绘制项 As New List(Of SRT字幕绘制项)()
        Dim ASS生成器 As New ASS字幕帧生成器(ASS文档)
        Dim SRT生成器 As New SRT字幕帧生成器(SRT文档, New SRT字幕样式())
        ASS生成器.生成帧(TimeSpan.FromSeconds(75.5), 区域, ASS绘制项)
        SRT生成器.生成帧(TimeSpan.FromSeconds(75.5), 区域, SRT绘制项)
        断言(ASS绘制项.Count > 0 AndAlso ASS绘制项.Any(
               Function(x) x.提示.片段.Any(Function(y) Not String.IsNullOrWhiteSpace(y.文本))),
               "ASS 在 1:15.5 没有生成可见文本。")
        断言(SRT绘制项.Count > 0 AndAlso SRT绘制项.Any(
               Function(x) x.行.Any(Function(y) Not String.IsNullOrWhiteSpace(y.文本))),
               "SRT 在 1:15.5 没有生成可见文本。")

        Using 画面控件 As New 播放器画面控件()
            Using 呈现器 As New 播放器定时文字图层呈现器(画面控件, Function() Nothing,
                                                        Function() Nothing,
                                                        Sub(size, commands, sequence) Return)
                Using ASS轨道 As New 外部字幕轨道(ASS路径, 外部字幕格式.ASS, Nothing,
                                                New ASS字幕帧生成器(ASS文档), Nothing)
                    断言(统计字幕命令(呈现器, ASS轨道) > 0, "ASS 没有生成 GPU 文字命令。")
                End Using
                Using SRT轨道 As New 外部字幕轨道(SRT路径, 外部字幕格式.SRT,
                                                New SRT字幕帧生成器(SRT文档, New SRT字幕样式()),
                                                Nothing, Nothing)
                    断言(统计字幕命令(呈现器, SRT轨道) > 0, "SRT 没有生成 GPU 文字命令。")
                End Using
            End Using
        End Using

        Using 自动轨道 = 外部字幕自动加载器.尝试加载同名字幕Async(
            视频路径, CancellationToken.None).GetAwaiter().GetResult()
            断言(自动轨道 IsNot Nothing, "后台字幕加载器没有找到同名字幕。")
            断言(自动轨道.格式 = 外部字幕格式.SRT, "后台字幕加载优先级没有选择 SRT。")
        End Using
        Console.WriteLine($"字幕：ASS {ASS文档.提示.Count} 条，SRT {SRT文档.提示.Count} 条，后台加载与 1:15.5 帧生成通过。")
    End Sub

    Private Function 统计字幕命令(呈现器 As 播放器定时文字图层呈现器,
                                字幕 As 外部字幕轨道) As Integer
        Return 呈现器.生成命令(New Size(1280, 720), 3840UI, 2160UI,
                           TimeSpan.FromSeconds(75.5), 字幕).Count
    End Function

    Private Function 测试播放(视频路径 As String, 字幕路径 As String,
                          模式 As 解码模式) As 播放测量结果
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(1280, 720),
            .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(20, 20),
            .Text = $"3FP {模式} 自动性能测试"
        }
            Dim 画面控件 As 播放器画面控件 = Nothing
            Dim 字幕呈现器 As 播放器定时文字图层呈现器 = Nothing
            Dim 字幕轨道 As 外部字幕轨道 = Nothing
            If Not String.IsNullOrEmpty(字幕路径) Then
                画面控件 = New 播放器画面控件 With {.Dock = DockStyle.Fill}
                输出窗口.Controls.Add(画面控件)
                Select Case Path.GetExtension(字幕路径).ToLowerInvariant()
                    Case ".srt"
                        Dim 文档 = SRT字幕解析器.解析文件(字幕路径)
                        字幕轨道 = New 外部字幕轨道(字幕路径, 外部字幕格式.SRT,
                            New SRT字幕帧生成器(文档, New SRT字幕样式()), Nothing, Nothing)
                    Case ".ass", ".ssa"
                        Dim 文档 = ASS字幕解析器.解析文件(字幕路径)
                        Dim 格式 = If(Path.GetExtension(字幕路径).Equals(".ssa", StringComparison.OrdinalIgnoreCase),
                                    外部字幕格式.SSA, 外部字幕格式.ASS)
                        字幕轨道 = New 外部字幕轨道(字幕路径, 格式, Nothing,
                            New ASS字幕帧生成器(文档), Nothing)
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
                        字幕呈现器 = New 播放器定时文字图层呈现器(画面控件,
                            Function() 会话.当前快照, Function() 字幕轨道,
                            AddressOf 会话.设置定时文字图层)
                    End If
                    会话.设置音量(0.0F, True)
                    会话.打开Async(视频路径).GetAwaiter().GetResult()
                    Dim 信息 = 会话.当前媒体信息
                    断言(信息 IsNot Nothing AndAlso 信息.流.Any(
                           Function(x) x.类型 = "video" AndAlso x.编码 = "av1" AndAlso
                               x.宽度 = 3840 AndAlso x.高度 = 2160),
                           "测试视频没有被识别为 4K AV1。")
                    会话.播放()
                    等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(30))
                    Dim 顺播结果 = 采样播放(会话, 测量秒数, 画面控件)
                    测试暂停停钟(会话)
                    会话.跳转(TimeSpan.FromSeconds(75))
                    等待预热(会话, TimeSpan.FromSeconds(75.5), TimeSpan.FromSeconds(30))
                    Dim 跳转结果 = 采样播放(会话, 6.0, 画面控件)
                    验证播放结果(跳转结果, "跳转后")
                    Console.WriteLine($"跳转后：{格式化播放报告(跳转结果)}")
                    If 画面控件 IsNot Nothing Then
                        会话.跳转(TimeSpan.FromSeconds(75))
                        等待预热(会话, TimeSpan.FromSeconds(75.5), TimeSpan.FromSeconds(30))
                        测试屏幕字幕(会话, 画面控件)
                    End If
                    Return 顺播结果
                Finally
                    字幕呈现器?.释放()
                    字幕轨道?.释放()
                    画面控件?.Dispose()
                End Try
            End Using
        End Using
    End Function

    Private Sub 等待预热(会话 As 播放器会话, 目标位置 As TimeSpan, 超时 As TimeSpan)
        Dim 计时 = Stopwatch.StartNew()
        Dim 上限 = 目标位置 + TimeSpan.FromSeconds(2)
        Do
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            If 快照.状态 = 播放状态.失败 Then Throw New InvalidOperationException("播放器在预热阶段失败。")
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
                          画面控件 As 播放器画面控件) As 播放测量结果
        Dim 墙钟 = Stopwatch.StartNew()
        Dim 进程 = Process.GetCurrentProcess()
        Dim CPU开始 = 进程.TotalProcessorTime
        Dim 首快照 = 会话.当前快照
        Dim 首位置 = 首快照.播放位置
        Dim 首呈现帧数 = 首快照.已呈现视频帧数
        Dim 首丢帧数 = 首快照.已丢弃视频帧数
        Dim 上次PTS = 首快照.原始帧PTS
        Dim 音画差总毫秒 As Double
        Dim 音画差样本数 As Integer
        Dim 最大音画差毫秒 As Double

        While 墙钟.Elapsed.TotalSeconds < 秒数
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            If 快照.状态 = 播放状态.失败 Then Throw New InvalidOperationException("播放器在测量阶段失败。")
            If 快照.原始帧PTS <> Long.MinValue AndAlso 快照.原始帧PTS <> 上次PTS Then
                上次PTS = 快照.原始帧PTS
            End If
            If 快照.帧时间基分母 > 0 AndAlso 快照.原始帧PTS <> Long.MinValue Then
                Dim 视频位置 = TimeSpan.FromSeconds(快照.原始帧PTS *
                    CDbl(快照.帧时间基分子) / 快照.帧时间基分母)
                Dim 差值 = Math.Abs((快照.播放位置 - 视频位置).TotalMilliseconds)
                音画差总毫秒 += 差值
                音画差样本数 += 1
                最大音画差毫秒 = Math.Max(最大音画差毫秒, 差值)
            End If
            Thread.Sleep(5)
        End While

        Dim 末快照 = 会话.当前快照
        Dim 末位置 = 末快照.播放位置
        Dim 呈现帧数 = 末快照.已呈现视频帧数 - 首呈现帧数
        Dim 丢帧数 = 末快照.已丢弃视频帧数 - 首丢帧数
        进程.Refresh()
        Return New 播放测量结果 With {
            .实际呈现帧率 = CDbl(呈现帧数) / 墙钟.Elapsed.TotalSeconds,
            .丢帧数 = CLng(丢帧数),
            .播放速度 = (末位置 - 首位置).TotalSeconds / 墙钟.Elapsed.TotalSeconds,
            .平均音画差毫秒 = If(音画差样本数 = 0, Double.PositiveInfinity,
                            音画差总毫秒 / 音画差样本数),
            .最大音画差毫秒 = 最大音画差毫秒,
            .进程CPU占用百分比 = (进程.TotalProcessorTime - CPU开始).TotalSeconds /
                              墙钟.Elapsed.TotalSeconds / Environment.ProcessorCount * 100.0
        }
    End Function

    Private Sub 输出播放报告(模式 As 解码模式, 结果 As 播放测量结果)
        Console.WriteLine($"{模式}：{格式化播放报告(结果)}")
    End Sub

    Private Function 格式化播放报告(结果 As 播放测量结果) As String
        Return $"{结果.实际呈现帧率:F2} fps，丢帧 {结果.丢帧数}，时钟 {结果.播放速度:F4}x，" &
               $"音画差平均/最大 {结果.平均音画差毫秒:F1}/{结果.最大音画差毫秒:F1} ms，" &
               $"进程 CPU {结果.进程CPU占用百分比:F1}%"
    End Function

    Private Sub 验证播放结果(结果 As 播放测量结果, 阶段 As String)
        断言(结果.实际呈现帧率 >= 目标帧率 * 0.94,
           $"{阶段}呈现吞吐不足：{结果.实际呈现帧率:F2} fps。")
        断言(结果.丢帧数 <= 2, $"{阶段}测量窗口内丢弃了 {结果.丢帧数} 帧。")
        断言(结果.播放速度 >= 0.97 AndAlso 结果.播放速度 <= 1.03,
           $"{阶段}媒体时钟速度异常：{结果.播放速度:F4}x。")
        断言(结果.平均音画差毫秒 <= 80.0,
           $"{阶段}平均音画差过大：{结果.平均音画差毫秒:F1} ms。")
        断言(结果.最大音画差毫秒 <= 180.0,
           $"{阶段}最大音画差过大：{结果.最大音画差毫秒:F1} ms。")
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
        Public Property 平均音画差毫秒 As Double
        Public Property 最大音画差毫秒 As Double
        Public Property 进程CPU占用百分比 As Double
    End Class
End Module
