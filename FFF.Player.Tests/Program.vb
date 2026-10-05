Imports System
Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Text.Json
Imports System.Diagnostics
Imports System.Drawing
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module Program
    Private Const BufferTooSmall As Integer = -3
    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl)>
    Private Function FFF3FP_ProbeSubtitleStreams(<MarshalAs(UnmanagedType.LPUTF8Str)> path As String,
        output As IntPtr, capacity As UInteger, ByRef required As UInteger) As Integer
    End Function

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl)>
    Private Function FFF3FP_OpenAssSubtitle(<MarshalAs(UnmanagedType.LPUTF8Str)> path As String,
        fonts As IntPtr, stream As Integer, ByRef handle As IntPtr) As Integer
    End Function

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl)>
    Private Function FFF3FP_GetAssSubtitleLastError(handle As IntPtr, output As IntPtr,
        capacity As UInteger, ByRef required As UInteger) As Integer
    End Function

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl)>
    Private Sub FFF3FP_DestroyAssSubtitle(handle As IntPtr)
    End Sub

    <STAThread>
    Public Function Main(args As String()) As Integer
        Try
            Using session As New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
                Assert(session.当前快照.状态 = 播放状态.空闲, "Initial playback state")
                Assert(session.当前定时文字状态 IsNot Nothing, "Subtitle status API")
                Assert(session.当前弹幕状态 IsNot Nothing, "Danmaku status API")
                Assert(session.当前歌词状态 IsNot Nothing, "Lyrics status API")
                Assert(session.读取音频峰值().Length = 0 AndAlso session.读取输入音频峰值().Length = 0, "Empty audio peaks")
            End Using
            CheckPlaylist()
            CheckSubtitleBuffers()
            If args.Length > 0 Then CheckPlayback(args(0))
            Console.WriteLine("Core checks passed: session state, layer APIs, peaks, playlist and subtitle UTF-8 buffers.")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine(ex)
            Return 1
        End Try
    End Function

    Private Sub Assert(condition As Boolean, message As String)
        If Not condition Then Throw New InvalidOperationException(message)
    End Sub

    Private Sub CheckPlaylist()
        Dim fixture = Path.Combine(AppContext.BaseDirectory, "Fixtures", "sample.ass")
        Dim list As New 播放列表()
        Dim changed = 0
        AddHandler list.列表变化, Sub() changed += 1
        list.添加多个({fixture, fixture.ToUpperInvariant(), " "})
        Assert(list.取得项目().Count = 1 AndAlso changed = 1, "Batch ordering and case-insensitive duplicates")
        list.添加多个({fixture})
        Assert(changed = 1, "Duplicate-only batch must not notify")
        Dim m3u = Path.Combine(AppContext.BaseDirectory, "Fixtures", "regression.m3u8")
        list.导出M3U8(m3u)
        File.AppendAllLines(m3u, {"sample.ass", "SAMPLE.ASS"})
        list.导入M3U8(m3u)
        Assert(list.取得项目().Count = 1 AndAlso list.当前项目.路径 = fixture, "M3U import preserves selection and deduplicates")
    End Sub

    Private Sub CheckSubtitleBuffers()
        Dim subtitlePath = Path.Combine(AppContext.BaseDirectory, "Fixtures", "sample.ass")
        Dim required As UInteger
        Assert(FFF3FP_ProbeSubtitleStreams(subtitlePath, IntPtr.Zero, 0, required) = BufferTooSmall AndAlso required > 1, "Subtitle probe size query")
        Dim buffer = Marshal.AllocCoTaskMem(CInt(required))
        Try
            Assert(FFF3FP_ProbeSubtitleStreams(subtitlePath, buffer, required - 1, required) = BufferTooSmall, "Subtitle probe short buffer")
            Assert(FFF3FP_ProbeSubtitleStreams(subtitlePath, buffer, required, required) = 0, "Subtitle probe copy")
            Using document = JsonDocument.Parse(Marshal.PtrToStringUTF8(buffer))
                Assert(document.RootElement.GetProperty("streams").GetArrayLength() = 1, "Subtitle JSON shape")
            End Using
        Finally
            Marshal.FreeCoTaskMem(buffer)
        End Try
        Dim handle As IntPtr
        Assert(FFF3FP_OpenAssSubtitle(subtitlePath, IntPtr.Zero, -1, handle) = 0, "ASS open")
        Try
            Assert(FFF3FP_GetAssSubtitleLastError(handle, IntPtr.Zero, 0, required) = BufferTooSmall AndAlso required = 1, "Empty error includes terminator")
            buffer = Marshal.AllocCoTaskMem(1)
            Try
                Assert(FFF3FP_GetAssSubtitleLastError(handle, buffer, 1, required) = 0 AndAlso Marshal.ReadByte(buffer) = 0, "Error UTF-8 terminator")
            Finally
                Marshal.FreeCoTaskMem(buffer)
            End Try
        Finally
            FFF3FP_DestroyAssSubtitle(handle)
        End Try
    End Sub

    Private Sub CheckPlayback(mediaPath As String)
        For Each mode In {解码模式.CPU, 解码模式.GPU}
            Using window As New Form With {.ClientSize = New Size(640, 360)},
                  session As New 播放器会话(New 播放器配置 With {.解码器 = mode, .输出窗口句柄 = window.Handle})
                window.Show()
                session.设置音量(0.1F, True)
                session.打开Async(mediaPath).GetAwaiter().GetResult()
                Dim timer = Stopwatch.StartNew()
                session.播放()
                Do
                    Application.DoEvents()
                    Thread.Sleep(10)
                    Assert(timer.Elapsed.TotalSeconds < 15, "Playback timeout: " & session.最后错误消息)
                Loop While session.当前快照.播放位置 < TimeSpan.FromSeconds(2)
                Dim snapshot = session.当前快照
                Assert(snapshot.交换链呈现次数 > 0 AndAlso snapshot.已解码音频帧数 > 0, "Video presentation and audio decoding")
                Assert(session.读取音频峰值().Length > 0 AndAlso session.读取输入音频峰值().Length > 0, "Live input/output peaks")
                session.暂停()
                timer.Restart()
                Do
                    Application.DoEvents()
                    Thread.Sleep(10)
                    Assert(timer.Elapsed.TotalSeconds < 5, "Pause timeout")
                Loop While session.当前快照.状态 <> 播放状态.已暂停
                session.跳转(TimeSpan.FromSeconds(1))
                timer.Restart()
                Do
                    Application.DoEvents()
                    Thread.Sleep(10)
                    Assert(timer.Elapsed.TotalSeconds < 5, "Seek timeout")
                Loop While Math.Abs(session.当前快照.播放位置.TotalSeconds - 1) > 0.1
                Console.WriteLine($"Playback check: requested={mode}, actual={snapshot.解码器}, seek/pause/audio passed.")
            End Using
        Next
    End Sub
End Module
