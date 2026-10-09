Imports System

Friend Module Program
    <STAThread>
    Public Function Main(args As String()) As Integer
        Try
            ' Dedicated entry so the lyrics-carriage kit can be run on its own; without
            ' this it would need a media path and be indistinguishable from playback.
            If args.Length > 0 AndAlso String.Equals(args(0), "--lyrics-kit-regression",
                    StringComparison.OrdinalIgnoreCase) Then
                Return LyricsKitRegression.运行(args.Skip(1).ToArray())
            End If
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
End Module
