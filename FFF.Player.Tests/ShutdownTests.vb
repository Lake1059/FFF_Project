Imports System.Diagnostics
Imports System.Drawing
Imports System.IO
Imports System.Reflection
Imports System.Runtime.InteropServices
Imports System.Windows.Forms
Imports FFF.Player

Friend Module 退出回归测试
    <DllImport("advapi32.dll")>
    Private Function OpenThreadWaitChainSession(flags As UInteger, callback As IntPtr) As IntPtr
    End Function
    <DllImport("advapi32.dll")>
    Private Function GetThreadWaitChain(session As IntPtr, context As IntPtr, flags As UInteger,
        thread As UInteger, ByRef count As UInteger, nodes As IntPtr, ByRef cycle As Boolean) As Boolean
    End Function
    <DllImport("advapi32.dll")>
    Private Sub CloseThreadWaitChainSession(session As IntPtr)
    End Sub

    Private Sub 输出等待链(进程 As Process)
        Dim 会话 = OpenThreadWaitChainSession(0, IntPtr.Zero)
        Dim 节点 = Marshal.AllocHGlobal(280 * 16)
        Try
            For Each 线程 As ProcessThread In 进程.Threads
                Dim 数量 As UInteger = 16, 循环 As Boolean
                If Not GetThreadWaitChain(会话, IntPtr.Zero, 0, CUInt(线程.Id), 数量, 节点, 循环) Then Continue For
                If 数量 < 2 Then Continue For
                Console.Write($"WAIT thread={线程.Id} cycle={循环}")
                For 索引 = 0 To CInt(数量) - 1
                    Dim 当前 = IntPtr.Add(节点, 索引 * 280)
                    Dim 类型 = Marshal.ReadInt32(当前)
                    Console.Write($" -> type={类型} status={Marshal.ReadInt32(当前, 4)}")
                    If 类型 = 8 Then Console.Write($" pid={Marshal.ReadInt32(当前, 8)} tid={Marshal.ReadInt32(当前, 12)}")
                Next
                Console.WriteLine()
            Next
        Finally
            Marshal.FreeHGlobal(节点)
            CloseThreadWaitChainSession(会话)
        End Try
    End Sub
    Public Sub 运行(媒体路径 As String)
        For Each 场景 In {"playing", "resize", "minimized", "opening", "playing", "resize"}
            Dim 启动 As New ProcessStartInfo(Environment.ProcessPath) With {
                .UseShellExecute = False, .CreateNoWindow = True,
                .RedirectStandardOutput = True, .RedirectStandardError = True}
            启动.ArgumentList.Add("--shutdown-child")
            启动.ArgumentList.Add(媒体路径)
            启动.ArgumentList.Add(场景)
            Using 子进程 = Process.Start(启动)
                Dim 标准输出 = 子进程.StandardOutput.ReadToEndAsync()
                Dim 错误输出 = 子进程.StandardError.ReadToEndAsync()
                If Not 子进程.WaitForExit(20000) Then
                    输出等待链(子进程)
                    子进程.Kill(True)
                    子进程.WaitForExit()
                    Console.Write(标准输出.GetAwaiter().GetResult())
                    Console.Write(错误输出.GetAwaiter().GetResult())
                    Throw New TimeoutException($"退出回归 {场景} 残留超过 20 秒，已清理测试子进程。")
                End If
                Console.Write(标准输出.GetAwaiter().GetResult())
                Console.Write(错误输出.GetAwaiter().GetResult())
                If 子进程.ExitCode <> 0 Then Throw New InvalidOperationException($"退出回归 {场景} 失败。")
            End Using
        Next
        Console.WriteLine("真实主窗体退出与连续重新启动通过。")
    End Sub

    Public Sub 运行子进程(媒体路径 As String, 场景 As String)
        Using 窗口 As New Form1(), 关闭计时器 As New Timer With {.Interval = 50}
            Dim 控制器 As 播放器控制器 = Nothing
            Dim 计时 As New Stopwatch()
            Dim 退出计时 As New Stopwatch()
            Dim 已请求关闭 = False
            Dim 已开始播放 = False
            AddHandler 窗口.Shown,
                Sub()
                    窗口.ShowInTaskbar = False
                    控制器 = DirectCast(GetType(Form1).GetField("播放控制器",
                        BindingFlags.Instance Or BindingFlags.NonPublic).GetValue(窗口), 播放器控制器)
                    控制器.设置音量(0.0F)
                    Console.WriteLine($"SHUTDOWN {场景}: opening")
                    窗口.打开命令行文件({媒体路径})
                    计时.Start()
                    关闭计时器.Start()
                End Sub
            AddHandler 关闭计时器.Tick,
                Sub()
                    If 已请求关闭 Then Return
                    Dim 快照 = 控制器.安全读取快照()
                    已开始播放 = 快照 IsNot Nothing AndAlso 快照.已呈现视频帧数 >= 15
                    If 场景 <> "opening" AndAlso Not 已开始播放 AndAlso 计时.Elapsed.TotalSeconds < 10 Then Return
                    If 场景 = "resize" Then 窗口.ClientSize = New Size(997, 613)
                    If 场景 = "minimized" Then 窗口.WindowState = FormWindowState.Minimized
                    已请求关闭 = True
                    Console.WriteLine($"SHUTDOWN {场景}: close-request playing={已开始播放}")
                    退出计时.Start()
                    窗口.Close()
                    Console.WriteLine($"SHUTDOWN {场景}: close-returned disposed={窗口.IsDisposed} forms={Application.OpenForms.Count}")
                End Sub
            AddHandler 窗口.FormClosed,
                Sub() Console.WriteLine($"SHUTDOWN {场景}: form-closed")
            Application.Run(窗口)
            If 场景 <> "opening" AndAlso Not 已开始播放 Then Throw New InvalidOperationException("测试未进入播放。")
            Console.WriteLine($"SHUTDOWN {场景}: message-loop-ended exit={退出计时.Elapsed.TotalMilliseconds:F1}ms")
            If 退出计时.Elapsed.TotalSeconds > 5 Then Throw New TimeoutException("主窗体退出耗时超过 5 秒。")
        End Using
    End Sub
End Module
