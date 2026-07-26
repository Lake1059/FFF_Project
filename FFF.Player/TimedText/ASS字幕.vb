Imports System.Collections.ObjectModel
Imports System.Globalization
Imports System.IO
Imports System.Text

Public NotInheritable Class ASS样式
    Friend Sub New()
    End Sub
    Public Property 名称 As String = "Default"
    Public Property 字体 As String = "Arial"
    Public Property 字号 As Single = 20.0F
    Public Property 主颜色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 次颜色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 描边颜色ARGB As UInteger = &HFF000000UI
    Public Property 背景颜色ARGB As UInteger = &HFF000000UI
    Public Property 粗体 As Boolean
    Public Property 斜体 As Boolean
    Public Property 下划线 As Boolean
    Public Property 删除线 As Boolean
    Public Property 水平缩放 As Single = 100.0F
    Public Property 垂直缩放 As Single = 100.0F
    Public Property 字间距 As Single
    Public Property 旋转角度 As Single
    Public Property 边框样式 As Integer = 1
    Public Property 描边宽度 As Single = 2.0F
    Public Property 阴影深度 As Single
    Public Property 对齐方式 As Integer = 2
    Public Property 左边距 As Integer
    Public Property 右边距 As Integer
    Public Property 垂直边距 As Integer
End Class

Public NotInheritable Class ASS覆盖标签
    Friend Sub New(名称值 As String, 参数值 As String, 原始值 As String)
        名称 = 名称值
        参数 = 参数值
        原始文本 = 原始值
    End Sub
    Public ReadOnly Property 名称 As String
    Public ReadOnly Property 参数 As String
    Public ReadOnly Property 原始文本 As String
End Class

Public NotInheritable Class ASS文本片段
    Friend Sub New(文本值 As String, 标签值 As IReadOnlyList(Of ASS覆盖标签))
        文本 = 文本值
        前置覆盖标签 = 标签值
    End Sub
    Public ReadOnly Property 文本 As String
    Public ReadOnly Property 前置覆盖标签 As IReadOnlyList(Of ASS覆盖标签)
End Class

Public NotInheritable Class ASS字幕提示
    Implements I时间轴项目

    Friend Sub New(layerValue As Integer, startValue As TimeSpan, endValue As TimeSpan, styleValue As String,
                   actorValue As String, marginLValue As Integer, marginRValue As Integer, marginVValue As Integer,
                   effectValue As String, rawTextValue As String, fragmentsValue As IReadOnlyList(Of ASS文本片段))
        图层 = layerValue
        开始时间 = startValue
        结束时间 = endValue
        样式名称 = styleValue
        说话人 = actorValue
        左边距 = marginLValue
        右边距 = marginRValue
        垂直边距 = marginVValue
        特效 = effectValue
        原始文本 = rawTextValue
        片段 = fragmentsValue
    End Sub

    Public ReadOnly Property 图层 As Integer
    Public ReadOnly Property 开始时间 As TimeSpan Implements I时间轴项目.开始时间
    Public ReadOnly Property 结束时间 As TimeSpan Implements I时间轴项目.结束时间
    Public ReadOnly Property 样式名称 As String
    Public ReadOnly Property 说话人 As String
    Public ReadOnly Property 左边距 As Integer
    Public ReadOnly Property 右边距 As Integer
    Public ReadOnly Property 垂直边距 As Integer
    Public ReadOnly Property 特效 As String
    Public ReadOnly Property 原始文本 As String
    Public ReadOnly Property 片段 As IReadOnlyList(Of ASS文本片段)
End Class

Public NotInheritable Class ASS字幕文档
    Friend Sub New(playResXValue As Integer, playResYValue As Integer, wrapStyleValue As Integer,
                   scaledBorderValue As Boolean, stylesValue As IDictionary(Of String, ASS样式), cuesValue As IReadOnlyList(Of ASS字幕提示))
        PlayResX = playResXValue
        PlayResY = playResYValue
        换行样式 = wrapStyleValue
        缩放边框与阴影 = scaledBorderValue
        样式 = New ReadOnlyDictionary(Of String, ASS样式)(stylesValue)
        提示 = cuesValue
        索引 = New 时间轴索引(Of ASS字幕提示)(cuesValue)
    End Sub
    Public ReadOnly Property PlayResX As Integer
    Public ReadOnly Property PlayResY As Integer
    Public ReadOnly Property 换行样式 As Integer
    Public ReadOnly Property 缩放边框与阴影 As Boolean
    Public ReadOnly Property 样式 As IReadOnlyDictionary(Of String, ASS样式)
    Public ReadOnly Property 提示 As IReadOnlyList(Of ASS字幕提示)
    Public ReadOnly Property 索引 As 时间轴索引(Of ASS字幕提示)
End Class

Public NotInheritable Class ASS字幕绘制项
    Friend Sub New(cueValue As ASS字幕提示, styleValue As ASS样式, xScaleValue As Single, yScaleValue As Single,
                   xOffsetValue As Single, yOffsetValue As Single)
        提示 = cueValue
        基础样式 = styleValue
        脚本到像素水平缩放 = xScaleValue
        脚本到像素垂直缩放 = yScaleValue
        X偏移像素 = xOffsetValue
        Y偏移像素 = yOffsetValue
    End Sub
    Public ReadOnly Property 提示 As ASS字幕提示
    Public ReadOnly Property 基础样式 As ASS样式
    Public ReadOnly Property 脚本到像素水平缩放 As Single
    Public ReadOnly Property 脚本到像素垂直缩放 As Single
    Public ReadOnly Property X偏移像素 As Single
    Public ReadOnly Property Y偏移像素 As Single
End Class

Public NotInheritable Class ASS字幕帧生成器
    Private ReadOnly 文档 As ASS字幕文档
    Private ReadOnly 活动提示 As New List(Of ASS字幕提示)()
    Private ReadOnly 默认样式 As ASS样式

    Public Sub New(文档值 As ASS字幕文档)
        ArgumentNullException.ThrowIfNull(文档值)
        文档 = 文档值
        If Not 文档.样式.TryGetValue("Default", 默认样式) Then 默认样式 = New ASS样式()
    End Sub

    Public Sub 生成帧(时间 As TimeSpan, 区域 As 视频显示区域, 结果 As ICollection(Of ASS字幕绘制项))
        ArgumentNullException.ThrowIfNull(结果)
        活动提示.Clear()
        文档.索引.查询时刻(时间, 活动提示)
        活动提示.Sort(Function(left, right) left.图层.CompareTo(right.图层))
        Dim scaleX = 区域.宽度像素 / 文档.PlayResX
        Dim scaleY = 区域.高度像素 / 文档.PlayResY
        For Each cue In 活动提示
            Dim style As ASS样式 = Nothing
            If Not 文档.样式.TryGetValue(cue.样式名称, style) Then style = 默认样式
            结果.Add(New ASS字幕绘制项(cue, style, scaleX, scaleY, 区域.X像素, 区域.Y像素))
        Next
    End Sub
End Class

Public NotInheritable Class ASS字幕解析器
    Private Shared ReadOnly 已知标签 As String() = {
        "iclip", "fscx", "fscy", "alpha", "xbord", "ybord", "xshad", "yshad", "fade",
        "frx", "fry", "frz", "move", "clip", "bord", "shad", "blur", "be", "fax", "fay",
        "fsp", "org", "pos", "pbo", "fad", "fn", "fs", "an", "q", "r", "1c", "2c", "3c", "4c",
        "1a", "2a", "3a", "4a", "kf", "ko", "k", "K", "t", "p", "b", "i", "u", "s"}

    Private Sub New()
    End Sub

    Public Shared Function 解析文件(路径 As String) As ASS字幕文档
        ArgumentException.ThrowIfNullOrWhiteSpace(路径)
        Using stream = New FileStream(路径, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024, FileOptions.SequentialScan)
            Using reader As New StreamReader(stream, New UTF8Encoding(False, True), True, 64 * 1024)
                Return 解析(reader)
            End Using
        End Using
    End Function

    Public Shared Function 解析(reader As TextReader) As ASS字幕文档
        ArgumentNullException.ThrowIfNull(reader)
        Dim playResX = 384
        Dim playResY = 288
        Dim wrapStyle = 0
        Dim scaledBorder = True
        Dim section = String.Empty
        Dim styleFormat As String() = Nothing
        Dim eventFormat As String() = Nothing
        Dim styles As New Dictionary(Of String, ASS样式)(StringComparer.OrdinalIgnoreCase)
        Dim cues As New List(Of ASS字幕提示)()
        Dim line As String
        Do
            line = reader.ReadLine()
            If line Is Nothing Then Exit Do
            line = line.TrimStart(ChrW(&HFEFF))
            If line.Length = 0 OrElse line(0) = ";"c Then Continue Do
            If line(0) = "["c AndAlso line.EndsWith("]", StringComparison.Ordinal) Then
                section = line
                Continue Do
            End If
            Dim colon = line.IndexOf(":"c)
            If colon < 0 Then Continue Do
            Dim key = line.Substring(0, colon).Trim()
            Dim value = line.Substring(colon + 1).Trim()
            If section.Equals("[Script Info]", StringComparison.OrdinalIgnoreCase) Then
                Select Case key.ToLowerInvariant()
                    Case "playresx" : Integer.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, playResX)
                    Case "playresy" : Integer.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, playResY)
                    Case "wrapstyle" : Integer.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, wrapStyle)
                    Case "scaledborderandshadow" : scaledBorder = value.Equals("yes", StringComparison.OrdinalIgnoreCase)
                End Select
            ElseIf section.Equals("[V4+ Styles]", StringComparison.OrdinalIgnoreCase) OrElse
                   section.Equals("[V4 Styles]", StringComparison.OrdinalIgnoreCase) Then
                If key.Equals("Format", StringComparison.OrdinalIgnoreCase) Then
                    styleFormat = value.Split(","c).Select(Function(x) x.Trim()).ToArray()
                ElseIf key.Equals("Style", StringComparison.OrdinalIgnoreCase) AndAlso styleFormat IsNot Nothing Then
                    Dim style = 解析样式(styleFormat, 拆分CSV(value, styleFormat.Length),
                                     section.Equals("[V4 Styles]", StringComparison.OrdinalIgnoreCase))
                    If style IsNot Nothing Then styles(style.名称) = style
                End If
            ElseIf section.Equals("[Events]", StringComparison.OrdinalIgnoreCase) Then
                If key.Equals("Format", StringComparison.OrdinalIgnoreCase) Then
                    eventFormat = value.Split(","c).Select(Function(x) x.Trim()).ToArray()
                ElseIf key.Equals("Dialogue", StringComparison.OrdinalIgnoreCase) AndAlso eventFormat IsNot Nothing Then
                    Dim cue = 解析事件(eventFormat, 拆分CSV(value, eventFormat.Length))
                    If cue IsNot Nothing Then cues.Add(cue)
                End If
            End If
        Loop
        If playResX <= 0 Then playResX = 384
        If playResY <= 0 Then playResY = 288
        If Not styles.ContainsKey("Default") Then styles("Default") = New ASS样式()
        Return New ASS字幕文档(playResX, playResY, wrapStyle, scaledBorder, styles, cues.AsReadOnly())
    End Function

    Private Shared Function 解析样式(format As String(), values As String(), ssaV4 As Boolean) As ASS样式
        Dim map = 建立字段映射(format, values)
        Dim style As New ASS样式()
        style.名称 = 取值(map, "Name", "Default")
        style.字体 = 取值(map, "Fontname", "Arial")
        style.字号 = 取单精度(map, "Fontsize", 20)
        style.主颜色ARGB = 取颜色(map, "PrimaryColour", &HFFFFFFFFUI)
        style.次颜色ARGB = 取颜色(map, "SecondaryColour", &HFFFFFFFFUI)
        style.描边颜色ARGB = 取颜色(map, If(ssaV4, "TertiaryColour", "OutlineColour"), &HFF000000UI)
        style.背景颜色ARGB = 取颜色(map, "BackColour", &HFF000000UI)
        style.粗体 = 取整数(map, "Bold", 0) <> 0
        style.斜体 = 取整数(map, "Italic", 0) <> 0
        style.下划线 = 取整数(map, "Underline", 0) <> 0
        style.删除线 = 取整数(map, "StrikeOut", 0) <> 0
        style.水平缩放 = 取单精度(map, "ScaleX", 100)
        style.垂直缩放 = 取单精度(map, "ScaleY", 100)
        style.字间距 = 取单精度(map, "Spacing", 0)
        style.旋转角度 = 取单精度(map, "Angle", 0)
        style.边框样式 = 取整数(map, "BorderStyle", 1)
        style.描边宽度 = 取单精度(map, "Outline", 2)
        style.阴影深度 = 取单精度(map, "Shadow", 0)
        style.对齐方式 = 取整数(map, "Alignment", 2)
        If ssaV4 Then style.对齐方式 = 转换SSA对齐(style.对齐方式)
        style.左边距 = 取整数(map, "MarginL", 0)
        style.右边距 = 取整数(map, "MarginR", 0)
        style.垂直边距 = 取整数(map, "MarginV", 0)
        Return style
    End Function

    Private Shared Function 解析事件(format As String(), values As String()) As ASS字幕提示
        Dim map = 建立字段映射(format, values)
        Dim startValue, endValue As TimeSpan
        If Not 尝试解析时间(取值(map, "Start", String.Empty), startValue) OrElse
           Not 尝试解析时间(取值(map, "End", String.Empty), endValue) OrElse endValue <= startValue Then Return Nothing
        Dim raw = 取值(map, "Text", String.Empty)
        Return New ASS字幕提示(取整数(map, "Layer", 0), startValue, endValue,
                             取值(map, "Style", "Default"), 取值(map, "Name", 取值(map, "Actor", String.Empty)),
                             取整数(map, "MarginL", 0), 取整数(map, "MarginR", 0), 取整数(map, "MarginV", 0),
                             取值(map, "Effect", String.Empty), raw, 解析片段(raw))
    End Function

    Private Shared Function 转换SSA对齐(value As Integer) As Integer
        Select Case value
            Case 1, 2, 3 : Return value
            Case 5 : Return 7
            Case 6 : Return 8
            Case 7 : Return 9
            Case 9 : Return 4
            Case 10 : Return 5
            Case 11 : Return 6
            Case Else : Return 2
        End Select
    End Function

    Private Shared Function 解析片段(raw As String) As IReadOnlyList(Of ASS文本片段)
        Dim result As New List(Of ASS文本片段)()
        Dim pending As New List(Of ASS覆盖标签)()
        Dim text As New StringBuilder()
        Dim i = 0
        While i < raw.Length
            If raw(i) = "{"c Then
                Dim close = raw.IndexOf("}"c, i + 1)
                If close >= 0 Then
                    If text.Length > 0 Then
                        result.Add(New ASS文本片段(text.ToString(), pending.ToArray()))
                        text.Clear()
                        pending.Clear()
                    End If
                    解析标签块(raw.Substring(i + 1, close - i - 1), pending)
                    i = close + 1
                    Continue While
                End If
            End If
            If raw(i) = "\"c AndAlso i + 1 < raw.Length Then
                Select Case raw(i + 1)
                    Case "N"c, "n"c : text.Append(vbLf) : i += 2 : Continue While
                    Case "h"c : text.Append(ChrW(&HA0)) : i += 2 : Continue While
                End Select
            End If
            text.Append(raw(i))
            i += 1
        End While
        If text.Length > 0 OrElse pending.Count > 0 Then result.Add(New ASS文本片段(text.ToString(), pending.ToArray()))
        Return result.AsReadOnly()
    End Function

    Private Shared Sub 解析标签块(block As String, target As ICollection(Of ASS覆盖标签))
        Dim i = 0
        While i < block.Length
            If block(i) <> "\"c Then
                i += 1
                Continue While
            End If
            Dim start = i
            i += 1
            Dim nextSlash = 查找下个标签(block, i)
            Dim rawTag = block.Substring(i, nextSlash - i)
            Dim name = String.Empty
            For Each known In 已知标签
                If rawTag.StartsWith(known, StringComparison.Ordinal) Then
                    name = known
                    Exit For
                End If
            Next
            If name.Length = 0 Then
                Dim nameLength = 0
                While nameLength < rawTag.Length AndAlso Char.IsLetterOrDigit(rawTag(nameLength))
                    nameLength += 1
                End While
                name = rawTag.Substring(0, nameLength)
            End If
            Dim argument = If(name.Length <= rawTag.Length, rawTag.Substring(name.Length), String.Empty)
            target.Add(New ASS覆盖标签(name, argument, block.Substring(start, nextSlash - start)))
            i = nextSlash
        End While
    End Sub

    Private Shared Function 查找下个标签(value As String, start As Integer) As Integer
        Dim depth = 0
        For i = start To value.Length - 1
            Select Case value(i)
                Case "("c : depth += 1
                Case ")"c : If depth > 0 Then depth -= 1
                Case "\"c : If depth = 0 Then Return i
            End Select
        Next
        Return value.Length
    End Function

    Private Shared Function 拆分CSV(value As String, maximumFields As Integer) As String()
        Dim result As New List(Of String)(maximumFields)
        Dim start = 0
        For i = 1 To maximumFields - 1
            Dim comma = value.IndexOf(","c, start)
            If comma < 0 Then Exit For
            result.Add(value.Substring(start, comma - start).Trim())
            start = comma + 1
        Next
        result.Add(value.Substring(start).Trim())
        Return result.ToArray()
    End Function

    Private Shared Function 建立字段映射(format As String(), values As String()) As Dictionary(Of String, String)
        Dim result As New Dictionary(Of String, String)(StringComparer.OrdinalIgnoreCase)
        For i = 0 To Math.Min(format.Length, values.Length) - 1
            result(format(i)) = values(i)
        Next
        Return result
    End Function

    Private Shared Function 取值(map As Dictionary(Of String, String), key As String, fallback As String) As String
        Dim value As String = Nothing
        Return If(map.TryGetValue(key, value), value, fallback)
    End Function

    Private Shared Function 取整数(map As Dictionary(Of String, String), key As String, fallback As Integer) As Integer
        Dim result As Integer
        Return If(Integer.TryParse(取值(map, key, String.Empty), NumberStyles.Integer, CultureInfo.InvariantCulture, result), result, fallback)
    End Function

    Private Shared Function 取单精度(map As Dictionary(Of String, String), key As String, fallback As Single) As Single
        Dim result As Single
        Return If(Single.TryParse(取值(map, key, String.Empty), NumberStyles.Float, CultureInfo.InvariantCulture, result), result, fallback)
    End Function

    Private Shared Function 取颜色(map As Dictionary(Of String, String), key As String, fallback As UInteger) As UInteger
        Dim raw = 取值(map, key, String.Empty).Trim()
        If raw.StartsWith("&H", StringComparison.OrdinalIgnoreCase) Then raw = raw.Substring(2)
        raw = raw.TrimEnd("&"c)
        Dim ass As UInteger
        If Not UInteger.TryParse(raw, NumberStyles.HexNumber, CultureInfo.InvariantCulture, ass) Then Return fallback
        Dim alpha = CByte((ass >> 24) And &HFFUI)
        Dim blue = CByte((ass >> 16) And &HFFUI)
        Dim green = CByte((ass >> 8) And &HFFUI)
        Dim red = CByte(ass And &HFFUI)
        Return (CUInt(255 - alpha) << 24) Or (CUInt(red) << 16) Or (CUInt(green) << 8) Or blue
    End Function

    Private Shared Function 尝试解析时间(value As String, ByRef result As TimeSpan) As Boolean
        Dim parts = value.Trim().Split({":"c, "."c, ","c})
        If parts.Length <> 4 Then Return False
        Dim h, m, s, fraction As Integer
        If Not Integer.TryParse(parts(0), NumberStyles.None, CultureInfo.InvariantCulture, h) OrElse
           Not Integer.TryParse(parts(1), NumberStyles.None, CultureInfo.InvariantCulture, m) OrElse
           Not Integer.TryParse(parts(2), NumberStyles.None, CultureInfo.InvariantCulture, s) OrElse
           Not Integer.TryParse(parts(3).PadRight(3, "0"c).Substring(0, 3), NumberStyles.None, CultureInfo.InvariantCulture, fraction) Then Return False
        If h < 0 OrElse m < 0 OrElse m >= 60 OrElse s < 0 OrElse s >= 60 Then Return False
        result = TimeSpan.FromMilliseconds((CLng(h) * 3600 + CLng(m) * 60 + s) * 1000 + fraction)
        Return True
    End Function
End Class

Public NotInheritable Class SSA字幕解析器
    Private Sub New()
    End Sub

    Public Shared Function 解析文件(路径 As String) As ASS字幕文档
        Return ASS字幕解析器.解析文件(路径)
    End Function

    Public Shared Function 解析(reader As TextReader) As ASS字幕文档
        Return ASS字幕解析器.解析(reader)
    End Function
End Class
