Imports System.Globalization
Imports System.IO
Imports System.Text
Imports System.Text.RegularExpressions
Imports System.Threading

Public NotInheritable Class LRC歌词条目
    Friend Sub New(开始时间值 As TimeSpan, 文本值 As IReadOnlyList(Of String))
        开始时间 = 开始时间值
        文本 = 文本值
    End Sub

    Public ReadOnly Property 开始时间 As TimeSpan
    Public ReadOnly Property 文本 As IReadOnlyList(Of String)
End Class

Public NotInheritable Class LRC歌词资料
    Friend Sub New(路径值 As String, 条目值 As IReadOnlyList(Of LRC歌词条目))
        路径 = If(路径值, String.Empty)
        条目 = 条目值
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 条目 As IReadOnlyList(Of LRC歌词条目)

    Public Function 查找当前条目(播放位置 As TimeSpan) As Integer
        Dim left = 0
        Dim right = 条目.Count
        While left < right
            Dim middle = left + ((right - left) \ 2)
            If 条目(middle).开始时间 <= 播放位置 Then
                left = middle + 1
            Else
                right = middle
            End If
        End While
        Return left - 1
    End Function
End Class

Public NotInheritable Class LRC歌词解析器
    Private Shared ReadOnly 时间标签 As New Regex(
        "\G\[(?<minute>\d+):(?<second>[0-5]?\d)(?:[\.:](?<fraction>\d{1,3}))?\]",
        RegexOptions.Compiled Or RegexOptions.CultureInvariant)
    Private Shared ReadOnly 逐字时间标签 As New Regex(
        "<\d+:[0-5]?\d(?:[\.:]\d{1,3})?>",
        RegexOptions.Compiled Or RegexOptions.CultureInvariant)

    Private Sub New()
    End Sub

    Public Shared Function 解析文件(路径 As String,
                                Optional 取消令牌 As CancellationToken = Nothing) As LRC歌词资料
        ArgumentException.ThrowIfNullOrWhiteSpace(路径)
        Dim 完整路径 = Path.GetFullPath(路径)
        If Not File.Exists(完整路径) Then Throw New FileNotFoundException("歌词文件不存在。", 完整路径)
        If Not String.Equals(Path.GetExtension(完整路径), ".lrc", StringComparison.OrdinalIgnoreCase) Then
            Throw New NotSupportedException("仅支持 LRC 外挂歌词。")
        End If
        Dim 文件大小 = New FileInfo(完整路径).Length
        If 文件大小 > 4L * 1024L * 1024L Then Throw New InvalidDataException("LRC 歌词文件超过 4 MiB 限制。")
        取消令牌.ThrowIfCancellationRequested()
        Using stream = New FileStream(完整路径, FileMode.Open, FileAccess.Read, FileShare.ReadWrite,
                                      64 * 1024, FileOptions.SequentialScan)
            Using reader As New StreamReader(stream, New UTF8Encoding(False, True), True, 64 * 1024)
                Return 解析(reader, 完整路径, 取消令牌)
            End Using
        End Using
    End Function

    Public Shared Function 解析(reader As TextReader, Optional 路径 As String = "",
                              Optional 取消令牌 As CancellationToken = Nothing) As LRC歌词资料
        ArgumentNullException.ThrowIfNull(reader)
        Dim groups As New SortedDictionary(Of Long, List(Of String))()
        Do
            取消令牌.ThrowIfCancellationRequested()
            Dim line = reader.ReadLine()
            If line Is Nothing Then Exit Do
            line = line.TrimStart(ChrW(&HFEFF))
            Dim position = 0
            Dim timestamps As New HashSet(Of Long)()
            Do
                Dim match = 时间标签.Match(line, position)
                If Not match.Success Then Exit Do
                Dim ticks As Long
                If 尝试解析时间(match, ticks) Then timestamps.Add(ticks)
                position = match.Index + match.Length
            Loop
            If timestamps.Count = 0 Then Continue Do

            Dim text = 逐字时间标签.Replace(line.Substring(position), String.Empty).Trim()
            For Each ticks In timestamps
                Dim lines As List(Of String) = Nothing
                If Not groups.TryGetValue(ticks, lines) Then
                    lines = New List(Of String)()
                    groups.Add(ticks, lines)
                End If
                lines.Add(text)
            Next
        Loop
        If groups.Count = 0 Then
            Throw New NotSupportedException("歌词文件不包含支持的 LRC 时间轴。")
        End If
        Dim entries = groups.Select(
            Function(pair) New LRC歌词条目(TimeSpan.FromTicks(pair.Key), pair.Value.AsReadOnly())).ToArray()
        Return New LRC歌词资料(If(路径, String.Empty), Array.AsReadOnly(entries))
    End Function

    Private Shared Function 尝试解析时间(match As Match, ByRef ticks As Long) As Boolean
        Dim minutes As Long
        Dim seconds As Integer
        If Not Long.TryParse(match.Groups("minute").Value, NumberStyles.None,
                             CultureInfo.InvariantCulture, minutes) OrElse
           Not Integer.TryParse(match.Groups("second").Value, NumberStyles.None,
                                CultureInfo.InvariantCulture, seconds) Then Return False
        Dim fraction = match.Groups("fraction").Value
        Dim milliseconds As Integer
        If fraction.Length > 0 AndAlso
           Not Integer.TryParse(fraction.PadRight(3, "0"c), NumberStyles.None,
                                CultureInfo.InvariantCulture, milliseconds) Then Return False
        Dim remainder = (CLng(seconds) * TimeSpan.TicksPerSecond) +
            (CLng(milliseconds) * TimeSpan.TicksPerMillisecond)
        If minutes > (Long.MaxValue - remainder) \ TimeSpan.TicksPerMinute Then Return False
        ticks = (minutes * TimeSpan.TicksPerMinute) + remainder
        Return True
    End Function
End Class

Public NotInheritable Class LRC歌词自动加载器
    Private Sub New()
    End Sub

    Public Shared Function 是支持的歌词文件(路径 As String) As Boolean
        Return Not String.IsNullOrWhiteSpace(路径) AndAlso
            String.Equals(Path.GetExtension(路径), ".lrc", StringComparison.OrdinalIgnoreCase)
    End Function

    Public Shared Function 加载歌词Async(路径 As String,
                                     取消令牌 As CancellationToken) As Task(Of LRC歌词资料)
        Return Task.Run(Function() LRC歌词解析器.解析文件(路径, 取消令牌), 取消令牌)
    End Function

    Public Shared Function 尝试加载同名歌词Async(媒体路径 As String,
                                           取消令牌 As CancellationToken) As Task(Of LRC歌词资料)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Dim 歌词路径 = Path.ChangeExtension(Path.GetFullPath(媒体路径), ".lrc")
        If Not File.Exists(歌词路径) Then Return Task.FromResult(Of LRC歌词资料)(Nothing)
        Return 加载歌词Async(歌词路径, 取消令牌)
    End Function
End Class
