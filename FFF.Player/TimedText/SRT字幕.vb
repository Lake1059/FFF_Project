Imports System.Globalization
Imports System.IO
Imports System.Text

Public Enum 字幕语言类型
    未知 = 0
    中文 = 1
    拉丁 = 2
    混合 = 3
End Enum

Public NotInheritable Class 字幕文本行
    Friend Sub New(文本值 As String, 语言值 As 字幕语言类型)
        文本 = 文本值
        语言 = 语言值
    End Sub
    Public ReadOnly Property 文本 As String
    Public ReadOnly Property 语言 As 字幕语言类型
End Class

Public NotInheritable Class SRT字幕提示
    Implements I时间轴项目

    Friend Sub New(编号值 As Integer, 开始值 As TimeSpan, 结束值 As TimeSpan, 原始文本值 As String, 行值 As IReadOnlyList(Of 字幕文本行))
        编号 = 编号值
        开始时间 = 开始值
        结束时间 = 结束值
        原始文本 = 原始文本值
        行 = 行值
    End Sub

    Public ReadOnly Property 编号 As Integer
    Public ReadOnly Property 开始时间 As TimeSpan Implements I时间轴项目.开始时间
    Public ReadOnly Property 结束时间 As TimeSpan Implements I时间轴项目.结束时间
    Public ReadOnly Property 原始文本 As String
    Public ReadOnly Property 行 As IReadOnlyList(Of 字幕文本行)
End Class

Public NotInheritable Class SRT字幕文档
    Friend Sub New(提示值 As IReadOnlyList(Of SRT字幕提示))
        提示 = 提示值
        索引 = New 时间轴索引(Of SRT字幕提示)(提示值)
    End Sub
    Public ReadOnly Property 提示 As IReadOnlyList(Of SRT字幕提示)
    Public ReadOnly Property 索引 As 时间轴索引(Of SRT字幕提示)
End Class

Public NotInheritable Class SRT字幕样式
    Public Property 中文字体 As String = "Microsoft YaHei UI"
    Public Property 拉丁字体 As String = "Segoe UI"
    Public Property 字号 As Single = 48
    Public Property 颜色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 描边颜色ARGB As UInteger = &HC0000000UI
    Public Property 描边宽度 As Single = 2.0F
    Public Property 阴影颜色ARGB As UInteger = &H70000000UI
    ''' <summary>基准分辨率下向右下方 45 度投影时，X/Y 轴各自的偏移。</summary>
    Public Property 阴影偏移 As Single = 2.0F
    Public Property 行间距 As Single = 8.0F
    Public Property 底部边距 As Single = 54.0F
    Public Property 基准视频高度 As Single = 1080.0F
    Public Property 第一行字体 As String = String.Empty
    Public Property 第一行字号 As Single?
    Public Property 第一行字体样式 As FontStyle?
    Public Property 第一行颜色ARGB As UInteger?
    Public Property 第二行字体 As String = String.Empty
    Public Property 第二行字号 As Single?
    Public Property 第二行字体样式 As FontStyle?
    Public Property 第二行颜色ARGB As UInteger?
    Public Property 其他行字体 As String = String.Empty
    Public Property 其他行字号 As Single?
    Public Property 其他行字体样式 As FontStyle?
    Public Property 底部对齐方式 As Integer
    Public Property 尺寸缩放方式 As Integer

    Friend Sub 验证()
        If String.IsNullOrWhiteSpace(中文字体) Then Throw New ArgumentException("中文字体不能为空。", NameOf(中文字体))
        If String.IsNullOrWhiteSpace(拉丁字体) Then Throw New ArgumentException("拉丁字体不能为空。", NameOf(拉丁字体))
        If Not Single.IsFinite(字号) OrElse 字号 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(字号))
        If Not Single.IsFinite(描边宽度) OrElse 描边宽度 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(描边宽度))
        If Not Single.IsFinite(阴影偏移) OrElse 阴影偏移 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(阴影偏移))
        If Not Single.IsFinite(行间距) OrElse 行间距 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(行间距))
        If Not Single.IsFinite(底部边距) OrElse 底部边距 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(底部边距))
        If Not Single.IsFinite(基准视频高度) OrElse 基准视频高度 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(基准视频高度))
        If 第一行字号.HasValue AndAlso (Not Single.IsFinite(第一行字号.Value) OrElse 第一行字号.Value <= 0) Then Throw New ArgumentOutOfRangeException(NameOf(第一行字号))
        If 第二行字号.HasValue AndAlso (Not Single.IsFinite(第二行字号.Value) OrElse 第二行字号.Value <= 0) Then Throw New ArgumentOutOfRangeException(NameOf(第二行字号))
        If 其他行字号.HasValue AndAlso (Not Single.IsFinite(其他行字号.Value) OrElse 其他行字号.Value <= 0) Then Throw New ArgumentOutOfRangeException(NameOf(其他行字号))
        If 底部对齐方式 < 0 OrElse 底部对齐方式 > 1 Then Throw New ArgumentOutOfRangeException(NameOf(底部对齐方式))
        If 尺寸缩放方式 < 0 OrElse 尺寸缩放方式 > 3 Then Throw New ArgumentOutOfRangeException(NameOf(尺寸缩放方式))
    End Sub
End Class

Public NotInheritable Class SRT字幕绘制行
    Friend Sub New(文本值 As String, 字体值 As String, 字号像素值 As Single,
                   颜色值 As UInteger, 描边颜色值 As UInteger, 描边宽度像素值 As Single,
                   阴影颜色值 As UInteger, 阴影偏移像素值 As Single,
                   Optional 字体样式值 As FontStyle = FontStyle.Regular)
        文本 = 文本值
        字体 = 字体值
        字号像素 = 字号像素值
        颜色ARGB = 颜色值
        描边颜色ARGB = 描边颜色值
        描边宽度像素 = 描边宽度像素值
        阴影颜色ARGB = 阴影颜色值
        阴影偏移像素 = 阴影偏移像素值
        字体样式 = 字体样式值
    End Sub
    Public ReadOnly Property 文本 As String
    Public ReadOnly Property 字体 As String
    Public ReadOnly Property 字号像素 As Single
    Public ReadOnly Property 颜色ARGB As UInteger
    Public ReadOnly Property 描边颜色ARGB As UInteger
    Public ReadOnly Property 描边宽度像素 As Single
    Public ReadOnly Property 阴影颜色ARGB As UInteger
    Public ReadOnly Property 阴影偏移像素 As Single
    Public ReadOnly Property 字体样式 As FontStyle
End Class

Public NotInheritable Class SRT字幕绘制项
    Friend Sub New(提示值 As SRT字幕提示, 行值 As IReadOnlyList(Of SRT字幕绘制行), x值 As Single, y值 As Single, 行间距值 As Single)
        提示 = 提示值
        行 = 行值
        X中心像素 = x值
        Y底部像素 = y值
        行间距像素 = 行间距值
    End Sub
    Public ReadOnly Property 提示 As SRT字幕提示
    Public ReadOnly Property 行 As IReadOnlyList(Of SRT字幕绘制行)
    Public ReadOnly Property X中心像素 As Single
    Public ReadOnly Property Y底部像素 As Single
    Public ReadOnly Property 行间距像素 As Single
End Class

Public NotInheritable Class SRT字幕帧生成器
    Private ReadOnly 文档 As SRT字幕文档
    Private 样式 As SRT字幕样式
    Private ReadOnly 活动提示 As New List(Of SRT字幕提示)()

    Public Sub New(文档值 As SRT字幕文档, 样式值 As SRT字幕样式)
        ArgumentNullException.ThrowIfNull(文档值)
        ArgumentNullException.ThrowIfNull(样式值)
        样式值.验证()
        文档 = 文档值
        样式 = 样式值
    End Sub

    Public ReadOnly Property 条目数 As Integer
        Get
            Return 文档.提示.Count
        End Get
    End Property

    Public Sub 设置样式(value As SRT字幕样式)
        ArgumentNullException.ThrowIfNull(value)
        value.验证()
        Threading.Volatile.Write(样式, value)
    End Sub

    Public Sub 生成帧(时间 As TimeSpan, 区域 As 视频显示区域, 结果 As ICollection(Of SRT字幕绘制项),
                  Optional 画布宽度 As Single = 0.0F, Optional 画布高度 As Single = 0.0F)
        ArgumentNullException.ThrowIfNull(结果)
        Dim 当前样式 = Threading.Volatile.Read(样式)
        当前样式.验证()
        活动提示.Clear()
        文档.索引.查询时刻(时间, 活动提示)
        Dim scale As Single
        Select Case 当前样式.尺寸缩放方式
            Case 1 : scale = If(画布宽度 > 0, 画布宽度 / 1920.0F, 区域.缩放系数)
            Case 2 : scale = If(画布高度 > 0, 画布高度 / 当前样式.基准视频高度, 区域.缩放系数)
            Case 3 : scale = 1.0F
            Case Else : scale = 区域.高度像素 / 当前样式.基准视频高度
        End Select
        For Each cue In 活动提示
            Dim lines As New List(Of SRT字幕绘制行)(cue.行.Count)
            For lineIndex = 0 To cue.行.Count - 1
                Dim line = cue.行(lineIndex)
                Dim fallbackFont = If(line.语言 = 字幕语言类型.拉丁, 当前样式.拉丁字体, 当前样式.中文字体)
                Dim fontName As String, fontSize As Single, fontStyle As FontStyle, color As UInteger
                Select Case lineIndex
                    Case 0
                        fontName = If(String.IsNullOrWhiteSpace(当前样式.第一行字体), fallbackFont, 当前样式.第一行字体)
                        fontSize = 当前样式.第一行字号.GetValueOrDefault(当前样式.字号)
                        fontStyle = 当前样式.第一行字体样式.GetValueOrDefault(FontStyle.Regular)
                        color = 当前样式.第一行颜色ARGB.GetValueOrDefault(当前样式.颜色ARGB)
                    Case 1
                        fontName = If(String.IsNullOrWhiteSpace(当前样式.第二行字体), fallbackFont, 当前样式.第二行字体)
                        fontSize = 当前样式.第二行字号.GetValueOrDefault(当前样式.字号)
                        fontStyle = 当前样式.第二行字体样式.GetValueOrDefault(FontStyle.Regular)
                        color = 当前样式.第二行颜色ARGB.GetValueOrDefault(当前样式.颜色ARGB)
                    Case Else
                        fontName = If(String.IsNullOrWhiteSpace(当前样式.其他行字体), fallbackFont, 当前样式.其他行字体)
                        fontSize = 当前样式.其他行字号.GetValueOrDefault(当前样式.字号)
                        fontStyle = 当前样式.其他行字体样式.GetValueOrDefault(FontStyle.Regular)
                        color = 当前样式.颜色ARGB
                End Select
                lines.Add(New SRT字幕绘制行(line.文本, fontName, fontSize * scale,
                                             color, 当前样式.描边颜色ARGB, 当前样式.描边宽度 * scale,
                                             当前样式.阴影颜色ARGB, 当前样式.阴影偏移 * scale, fontStyle))
            Next
            Dim bottom = If(当前样式.底部对齐方式 = 1 AndAlso 画布高度 > 0,
                            画布高度, 区域.Y像素 + 区域.高度像素)
            结果.Add(New SRT字幕绘制项(cue, lines, 区域.X像素 + 区域.宽度像素 * 0.5F,
                                        bottom - 当前样式.底部边距 * scale,
                                        当前样式.行间距 * scale))
        Next
    End Sub
End Class

Public NotInheritable Class SRT字幕解析器
    Private Sub New()
    End Sub

    Public Shared Function 解析文件(路径 As String) As SRT字幕文档
        ArgumentException.ThrowIfNullOrWhiteSpace(路径)
        Using stream = New FileStream(路径, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024, FileOptions.SequentialScan)
            Using reader As New StreamReader(stream, New UTF8Encoding(False, True), True, 64 * 1024)
                Return 解析(reader)
            End Using
        End Using
    End Function

    Public Shared Function 解析(reader As TextReader) As SRT字幕文档
        ArgumentNullException.ThrowIfNull(reader)
        Dim cues As New List(Of SRT字幕提示)()
        Dim block As New List(Of String)(6)
        Do
            block.Clear()
            Dim line As String
            Do
                line = reader.ReadLine()
                If line Is Nothing Then Exit Do
            Loop While String.IsNullOrWhiteSpace(line)
            If line Is Nothing Then Exit Do
            block.Add(line.TrimStart(ChrW(&HFEFF)))
            Do
                line = reader.ReadLine()
                If line Is Nothing OrElse String.IsNullOrWhiteSpace(line) Then Exit Do
                block.Add(line)
            Loop
            Dim cue = 解析块(block, cues.Count + 1)
            If cue IsNot Nothing Then cues.Add(cue)
            If line Is Nothing Then Exit Do
        Loop
        Return New SRT字幕文档(cues.AsReadOnly())
    End Function

    Private Shared Function 解析块(block As List(Of String), fallbackNumber As Integer) As SRT字幕提示
        If block.Count < 2 Then Return Nothing
        Dim number = fallbackNumber
        Dim timingIndex = 0
        If Integer.TryParse(block(0).Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, number) Then timingIndex = 1
        If timingIndex >= block.Count Then Return Nothing
        Dim arrow = block(timingIndex).IndexOf("-->", StringComparison.Ordinal)
        If arrow < 0 Then Return Nothing
        Dim startValue As TimeSpan
        Dim endValue As TimeSpan
        If Not 尝试解析时间(block(timingIndex).Substring(0, arrow).Trim(), startValue) Then Return Nothing
        Dim right = block(timingIndex).Substring(arrow + 3).Trim()
        Dim settingSpace = right.IndexOf(" "c)
        If settingSpace >= 0 Then right = right.Substring(0, settingSpace)
        If Not 尝试解析时间(right, endValue) OrElse endValue <= startValue Then Return Nothing

        Dim raw = String.Join(vbLf, block.Skip(timingIndex + 1))
        Dim lines As New List(Of 字幕文本行)()
        For i = timingIndex + 1 To block.Count - 1
            Dim text = 清理显示文本(block(i))
            If text.Length > 0 Then lines.Add(New 字幕文本行(text, 检测语言(text)))
        Next
        If lines.Count = 0 Then Return Nothing
        Return New SRT字幕提示(number, startValue, endValue, raw, lines.AsReadOnly())
    End Function

    Private Shared Function 尝试解析时间(value As String, ByRef result As TimeSpan) As Boolean
        Dim parts = value.Replace("."c, ","c).Split({":"c, ","c})
        If parts.Length <> 4 Then Return False
        Dim h, m, s, ms As Integer
        If Not Integer.TryParse(parts(0), NumberStyles.None, CultureInfo.InvariantCulture, h) OrElse
           Not Integer.TryParse(parts(1), NumberStyles.None, CultureInfo.InvariantCulture, m) OrElse
           Not Integer.TryParse(parts(2), NumberStyles.None, CultureInfo.InvariantCulture, s) OrElse
           Not Integer.TryParse(parts(3).PadRight(3, "0"c).Substring(0, 3), NumberStyles.None, CultureInfo.InvariantCulture, ms) Then Return False
        If h < 0 OrElse m < 0 OrElse m >= 60 OrElse s < 0 OrElse s >= 60 OrElse ms < 0 OrElse ms >= 1000 Then Return False
        result = TimeSpan.FromMilliseconds((CLng(h) * 3600 + CLng(m) * 60 + s) * 1000 + ms)
        Return True
    End Function

    Private Shared Function 清理显示文本(value As String) As String
        Dim builder As New StringBuilder(value.Length)
        Dim i = 0
        While i < value.Length
            If value(i) = "{"c AndAlso i + 1 < value.Length AndAlso value(i + 1) = "\"c Then
                Dim close = value.IndexOf("}"c, i + 2)
                If close >= 0 Then
                    i = close + 1
                    Continue While
                End If
            End If
            If value(i) = "<"c Then
                Dim close = value.IndexOf(">"c, i + 1)
                If close >= 0 Then
                    i = close + 1
                    Continue While
                End If
            End If
            builder.Append(value(i))
            i += 1
        End While
        Return System.Net.WebUtility.HtmlDecode(builder.ToString()).Trim()
    End Function

    Private Shared Function 检测语言(value As String) As 字幕语言类型
        Dim hasCjk = False
        Dim hasLatin = False
        For Each rune In value.EnumerateRunes()
            Dim code = rune.Value
            If (code >= &H3400 AndAlso code <= &H9FFF) OrElse (code >= &HF900 AndAlso code <= &HFAFF) Then
                hasCjk = True
            ElseIf (code >= AscW("A"c) AndAlso code <= AscW("Z"c)) OrElse (code >= AscW("a"c) AndAlso code <= AscW("z"c)) Then
                hasLatin = True
            End If
        Next
        If hasCjk AndAlso hasLatin Then Return 字幕语言类型.混合
        If hasCjk Then Return 字幕语言类型.中文
        If hasLatin Then Return 字幕语言类型.拉丁
        Return 字幕语言类型.未知
    End Function
End Class
