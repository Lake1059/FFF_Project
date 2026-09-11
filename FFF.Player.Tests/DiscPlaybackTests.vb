Imports System.Diagnostics
Imports System.Drawing
Imports System.Drawing.Imaging
Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module 光盘播放测试
    Friend Sub 解码切换回归(路径 As String)
        Using 窗口 As New Form With {.ClientSize = New Size(960, 640), .ShowInTaskbar = False},
            控制器 As New 播放器控制器(Function() 窗口.Handle, Nothing, 解码模式.CPU)
            Dim 错误 = String.Empty
            AddHandler 控制器.播放错误, Sub(s, e) 错误 = e.消息
            Dim 打开 = 控制器.打开媒体Async(路径)
            等待(Function() 打开.IsCompleted, 30, "decoder switch open")
            打开.GetAwaiter().GetResult()
            控制器.设置音量(0)
            控制器.光盘导航(光盘命令.标题, 1)
            等待(Function() 控制器.读取光盘状态().当前标题 = 1 AndAlso
                         控制器.安全读取快照().播放位置.TotalSeconds >= 5, 30, "disc title before decoder switch")
            Dim 切换前 = 控制器.安全读取快照().播放位置
            控制器.切换解码器()
            等待(Function() Not 控制器.是否正在切换 AndAlso 控制器.读取光盘状态().当前标题 = 1, 30, "disc title decoder switch")
            Dim 切换后 = 控制器.安全读取快照().播放位置
            If 切换后 < 切换前 - TimeSpan.FromSeconds(3) Then Throw New Exception($"Decoder switch restarted Blu-ray playback: {切换前.TotalSeconds:F1}s -> {切换后.TotalSeconds:F1}s.")
            控制器.光盘导航(光盘命令.根菜单)
            等待(Function() 控制器.读取光盘状态().菜单可见, 20, "disc menu before decoder switch")
            控制器.切换解码器()
            等待(Function() Not 控制器.是否正在切换 AndAlso 控制器.读取光盘状态().菜单可见, 30, "disc menu decoder switch")
            If 错误 <> String.Empty Then Throw New Exception(错误)
            Console.WriteLine($"DISC SWITCH PASS title={控制器.读取光盘状态().当前标题} position={切换后.TotalSeconds:F1}s")
        End Using
    End Sub

    Friend Sub 边界回归(路径 As String, 输出目录 As String)
        Directory.CreateDirectory(输出目录)
        Using 窗口 As New Form With {.ClientSize = New Size(960, 640)},
            播放器 As New 播放器会话(New 播放器配置 With {.解码器 = If(Environment.GetEnvironmentVariable("FFF_DISC_TEST_GPU") = "1", 解码模式.GPU, 解码模式.CPU), .输出窗口句柄 = 窗口.Handle})
            Dim 错误 As String = ""
            AddHandler 播放器.错误, Sub(s, e)
                                      错误 = 播放器.最后错误消息
                                      Console.WriteLine("ERROR " & 错误)
                                  End Sub
            Dim 打开 = 播放器.打开Async(路径)
            等待(Function() 打开.IsCompleted, 20, "edge open")
            打开.GetAwaiter().GetResult()
            播放器.设置音量(0, True) : 播放器.播放()
            If Environment.GetEnvironmentVariable("FFF_DISC_EARLY_MENU") = "1" Then
                等待时间(0.5)
                播放器.光盘导航(光盘命令.根菜单)
            End If
            For i = 1 To 20
                等待时间(1)
                Dim 状态 = 播放器.当前光盘状态
                Console.WriteLine($"EDGE t={i} menu={状态.菜单可见} hold={状态.正在等待} title={状态.当前标题} seq={状态.图形代次} state={播放器.当前快照.状态}")
                If 播放器.当前快照.状态 = 播放状态.失败 Then Throw New Exception(播放器.最后错误消息)
                If 错误 <> "" Then Throw New Exception(错误)
                If i = 15 Then
                    Using 图 = 播放器.读取SDR合成帧()
                        图.Save(Path.Combine(输出目录, $"frame-{i}.png"), ImageFormat.Png)
                    End Using
                End If
                If 状态.菜单可见 AndAlso i = 15 Then
                    Dim 之前 As Byte()
                    Using 图 = 播放器.读取SDR合成帧(True)
                        之前 = 帧字节(图)
                        图.Save(Path.Combine(输出目录, "focus-before.png"), ImageFormat.Png)
                    End Using
                    播放器.光盘导航(光盘命令.鼠标移动, 800, 950)
                    播放器.光盘导航(光盘命令.鼠标确认, 800, 950)
                    播放器.光盘导航(光盘命令.鼠标确认, 800, 950)
                    等待时间(0.5)
                    Using 图 = 播放器.读取SDR合成帧(True)
                        图.Save(Path.Combine(输出目录, "focus-after.png"), ImageFormat.Png)
                        If 帧字节(图).Where(Function(v, n) n Mod 4 = 3).All(Function(v) v = 0) Then Throw New Exception("Menu focus disappeared.")
                        If 之前.SequenceEqual(帧字节(图)) Then Throw New Exception("Menu focus did not move.")
                    End Using
                    If Not 播放器.当前光盘状态.菜单可见 Then Throw New Exception("Menu focus navigation left menu unexpectedly.")
                    播放器.光盘导航(光盘命令.鼠标确认, 850, 800)
                    等待时间(1)
                End If
            Next
            If 错误 <> "" Then Throw New Exception(错误)
            Console.WriteLine("EDGE PASS " & 播放器.当前快照.解码器.ToString())
        End Using
    End Sub
    Friend Sub 滑条回归(路径 As String)
        Using 窗口 As New Form With {.ClientSize = New Size(640, 480)},
            控制器 As New 播放器控制器(Function() 窗口.Handle, SynchronizationContext.Current)
            Dim 输出窗口 = 窗口.Handle
            控制器.设置音量(0)
            Dim 打开 = 控制器.打开媒体Async(路径)
            等待(Function() 打开.IsCompleted, 30, "slider open")
            打开.GetAwaiter().GetResult()
            控制器.光盘导航(光盘命令.标题, 1)
            等待(Function() 控制器.读取光盘状态().当前标题 = 1 AndAlso 控制器.安全读取快照().已呈现视频帧数 > 5, 20, "slider title")
            控制器.跳转到关键帧(TimeSpan.FromSeconds(60))
            等待(Function() 控制器.安全读取快照().播放位置.TotalSeconds >= 50, 20, "playing slider seek")
            控制器.切换播放暂停()
            等待(Function() 控制器.安全读取快照().状态 = 播放状态.已暂停, 5, "slider pause")
            Dim 暂停帧 = 控制器.安全读取快照().已呈现视频帧数
            控制器.跳转到关键帧(TimeSpan.FromSeconds(120))
            等待(Function() 控制器.安全读取快照().播放位置.TotalSeconds >= 110 AndAlso 控制器.安全读取快照().已呈现视频帧数 > 暂停帧, 20, "paused slider preview")
            If 控制器.安全读取快照().状态 <> 播放状态.已暂停 Then Throw New Exception("Slider changed pause state.")
            Console.WriteLine("SLIDER PASS playing=60s paused=120s")
        End Using
    End Sub
    Friend Sub 发布探针(路径 As String)
        Using 窗口 As New Form With {.ClientSize = New Size(640, 480)},
            播放器 As New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU, .输出窗口句柄 = 窗口.Handle})
            Dim 打开 = 播放器.打开Async(路径)
            等待(Function() 打开.IsCompleted, 30, "published open")
            打开.GetAwaiter().GetResult()
            播放器.设置音量(0, True)
            播放器.播放()
            等待(Function() 播放器.当前快照.已呈现视频帧数 > 5, 20, "published frames")
            Dim 缓存目录 = Environment.GetEnvironmentVariable("DOTNET_BUNDLE_EXTRACT_BASE_DIR")
            If String.IsNullOrEmpty(缓存目录) Then Throw New Exception("Set the bundle extraction directory for this probe.")
            Dim 名称 = "bluray-3.dll"
            Dim 模块 = Process.GetCurrentProcess().Modules.Cast(Of ProcessModule)().ToArray()
            For Each 库 In {"FFF.Native.dll", 名称}
                Dim 项 = 模块.Single(Function(m) String.Equals(m.ModuleName, 库, StringComparison.OrdinalIgnoreCase))
                If Not 项.FileName.StartsWith(缓存目录, StringComparison.OrdinalIgnoreCase) Then Throw New Exception("Native library was not extracted from bundle: " & 项.FileName)
                Console.WriteLine("BUNDLED " & 项.FileName)
            Next
            For Each 项 In 模块.Where(Function(m) m.ModuleName.StartsWith("avcodec", StringComparison.OrdinalIgnoreCase) OrElse m.ModuleName.StartsWith("avformat", StringComparison.OrdinalIgnoreCase))
                If 项.FileName.StartsWith(缓存目录, StringComparison.OrdinalIgnoreCase) Then Throw New Exception("FFmpeg must remain external.")
                Console.WriteLine("EXTERNAL " & 项.FileName)
            Next
            Console.WriteLine("PUBLISH PASS " & 播放器.当前光盘状态.类型)
        End Using
    End Sub
    Friend Sub 运行(路径 As String, 输出目录 As String)
        Directory.CreateDirectory(输出目录)
        Using 窗口 As New Form With {.ClientSize = New Size(960, 640), .Text = "3FP Disc Regression", .ShowInTaskbar = False}
            Dim 输出窗口 = 窗口.Handle
            Dim 解码器 = If(Environment.GetEnvironmentVariable("FFF_DISC_TEST_GPU") = "1", 解码模式.GPU, 解码模式.CPU)
            Using 播放器 As New 播放器会话(New 播放器配置 With {.解码器 = 解码器, .输出窗口句柄 = 窗口.Handle})
                Dim 错误 As String = ""
                AddHandler 播放器.错误, Sub(s, e)
                                          错误 = 播放器.最后错误消息
                                          Console.WriteLine("ERROR " & 错误)
                                      End Sub
                Dim 打开 = 播放器.打开Async(路径)
                等待(Function() 打开.IsCompleted, 30, "disc open")
                打开.GetAwaiter().GetResult()
                Console.WriteLine("OPEN " & System.Text.Json.JsonSerializer.Serialize(播放器.当前光盘状态))
                播放器.设置音量(0, True)
                播放器.播放()
                等待(Function() 播放器.当前快照.已呈现视频帧数 >= 5, 30, "initial video")
                Dim 表 = Stopwatch.StartNew()
                While 表.Elapsed.TotalSeconds < 12
                    Application.DoEvents() : Thread.Sleep(10)
                End While
                等待(Function() 播放器.当前光盘状态.菜单可见, 20, "automatic Blu-ray menu")
                等待(Function() 播放器.当前光盘状态.菜单可见 AndAlso 播放器.当前光盘状态.图形代次 > 0, 60, "interactive menu")
                Dim 菜单状态 = 播放器.当前光盘状态
                Console.WriteLine("MENU " & System.Text.Json.JsonSerializer.Serialize(菜单状态))
                For i = 1 To 2
                    等待时间(1)
                    Dim 快照 = 播放器.当前快照
                    Console.WriteLine($"MENU VIDEO pos={快照.播放位置.TotalSeconds:F3} frames={快照.已呈现视频帧数} decoded={快照.已解码视频帧数} queue={快照.视频队列帧数}")
                Next
                Dim 有色像素 = 0
                For y = 80 To 560 Step 80
                    For x = 80 To 880 Step 80
                        Dim 像素 = 播放器.读取视频输出像素(x, y)
                        If 像素.R + CInt(像素.G) + 像素.B > 30 Then 有色像素 += 1
                    Next
                Next
                Console.WriteLine("MENU PIXELS " & 有色像素)
                Using 图 = 播放器.读取SDR合成帧()
                    图.Save(Path.Combine(输出目录, "menu-internal.png"), ImageFormat.Png)
                End Using
                Dim 原图层 As Byte()
                Using 图 = 播放器.读取SDR合成帧(True)
                    原图层 = 帧字节(图)
                    图.Save(Path.Combine(输出目录, "menu-overlay-internal.png"), ImageFormat.Png)
                End Using
                If 有色像素 < 5 Then Throw New Exception("Disc menu back buffer is blank.")
                播放器.光盘导航(光盘命令.右)
                等待时间(0.3)
                Using 图 = 播放器.读取SDR合成帧(True)
                    If 原图层.SequenceEqual(帧字节(图)) Then Throw New Exception("Menu highlight did not change.")
                    图.Save(Path.Combine(输出目录, "highlight-internal.png"), ImageFormat.Png)
                End Using
                播放器.光盘导航(光盘命令.左)
                等待时间(0.3)
                Dim 菜单代次 = 播放器.当前快照.时间轴代次
                    播放器.光盘导航(光盘命令.右)
                    等待时间(0.2)
                    播放器.光盘导航(光盘命令.确认)
                    等待时间(1)
                    If 错误 <> "" Then Throw New Exception("Submenu navigation: " & 错误)
                    If 播放器.当前快照.时间轴代次 <> 菜单代次 Then Throw New Exception("Overlay-only submenu reopened the media stream.")
                    Using 图 = 播放器.读取SDR合成帧()
                        图.Save(Path.Combine(输出目录, "submenu-internal.png"), ImageFormat.Png)
                    End Using
                    For i = 1 To 20
                        播放器.光盘导航(光盘命令.鼠标移动, 600 + i, 800)
                    Next
                    等待时间(0.3)
                    If 错误 <> "" OrElse 播放器.当前快照.时间轴代次 <> 菜单代次 Then Throw New Exception("Submenu hover disrupted playback.")
                    播放器.光盘导航(光盘命令.鼠标确认, 1440, 875)
                    等待时间(0.3)
                播放器.光盘导航(光盘命令.鼠标移动, 500, 950)
                等待时间(0.2)
                Dim 之前 = 播放器.当前快照.已呈现视频帧数
                播放器.光盘导航(光盘命令.确认)
                等待(Function() Not 播放器.当前光盘状态.菜单可见 AndAlso 播放器.当前快照.已呈现视频帧数 > 之前 + 5, 30, "menu activation video")
                Console.WriteLine("ACTIVATED " & System.Text.Json.JsonSerializer.Serialize(播放器.当前光盘状态))
                播放器.暂停()
                等待时间(0.3)
                播放器.播放()
                等待时间(2)
                Dim 跳转前帧 = 播放器.当前快照.已呈现视频帧数
                播放器.跳转(TimeSpan.FromSeconds(60))
                等待(Function() 播放器.当前快照.播放位置.TotalSeconds >= 50 AndAlso 播放器.当前快照.已呈现视频帧数 > 跳转前帧 + 5, 20, "disc seek")
                播放器.光盘导航(光盘命令.根菜单)
                等待(Function() 播放器.当前光盘状态.菜单可见, 20, "return menu")
                等待时间(1)
                窗口.ClientSize = New Size(720, 960)
                播放器.设置输出窗口(窗口.Handle)
                等待时间(0.5)
                Using 图 = 播放器.读取SDR合成帧()
                    If 图.Width <> 720 OrElse 图.Height <> 960 Then Throw New Exception("Menu resize did not apply.")
                    图.Save(Path.Combine(输出目录, "menu-portrait-internal.png"), ImageFormat.Png)
                End Using
                Dim 鼠标前帧 = 播放器.当前快照.已呈现视频帧数
                播放器.光盘导航(光盘命令.鼠标移动, 500, 950)
                播放器.光盘导航(光盘命令.鼠标确认, 500, 950)
                等待(Function() Not 播放器.当前光盘状态.菜单可见 AndAlso 播放器.当前快照.已呈现视频帧数 > 鼠标前帧 + 5, 20, "mouse activation")
                If 播放器.当前快照.状态 = 播放状态.失败 Then Throw New Exception(错误)
                If 错误 <> "" Then Throw New Exception("Disc navigation reported an error: " & 错误)
                Console.WriteLine("PASS decode=" & 播放器.当前快照.解码器.ToString() & " rendered=" & 播放器.当前快照.已呈现视频帧数)
                Dim 关闭 = Stopwatch.StartNew()
                播放器.释放()
                If 关闭.Elapsed.TotalSeconds > 5 Then Throw New TimeoutException("Disc shutdown exceeded five seconds.")
            End Using
            窗口.Close()
        End Using
    End Sub
    Private Function 帧字节(图 As Bitmap) As Byte()
        Dim 锁 = 图.LockBits(New Rectangle(0, 0, 图.Width, 图.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb)
        Try
            Dim 结果(锁.Stride * 图.Height - 1) As Byte
            Marshal.Copy(锁.Scan0, 结果, 0, 结果.Length)
            Return 结果
        Finally
            图.UnlockBits(锁)
        End Try
    End Function
    Private Sub 等待(条件 As Func(Of Boolean), 秒 As Double, 阶段 As String)
        Dim 表 = Stopwatch.StartNew()
        While Not 条件()
            If 表.Elapsed.TotalSeconds > 秒 Then Throw New TimeoutException(阶段)
            Application.DoEvents() : Thread.Sleep(10)
        End While
    End Sub
    Private Sub 等待时间(秒 As Double)
        Dim 表 = Stopwatch.StartNew()
        等待(Function() 表.Elapsed.TotalSeconds >= 秒, 秒 + 2, "delay")
    End Sub
End Module
