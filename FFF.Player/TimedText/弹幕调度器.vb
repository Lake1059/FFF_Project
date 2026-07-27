Public Interface I弹幕文本测量器
    Function 测量宽度(文本 As String, 字体 As String, 字号像素 As Single) As Single
End Interface

Public NotInheritable Class 默认弹幕文本测量器
    Implements I弹幕文本测量器

    Public Function 测量宽度(文本 As String, 字体 As String, 字号像素 As Single) As Single Implements I弹幕文本测量器.测量宽度
        Dim units As Single
        For Each rune In 文本.EnumerateRunes()
            If rune.Value <= &H7F Then
                units += 0.58F
            ElseIf rune.Value >= &H2E80 Then
                units += 1.0F
            Else
                units += 0.75F
            End If
        Next
        Return Math.Max(字号像素, units * 字号像素)
    End Function
End Class

Public NotInheritable Class 弹幕显示配置
    ''' <summary>当前未提供交互设置，因此这里是播放器启用弹幕时的默认配置。</summary>
    Public Property 字体 As String = "Microsoft YaHei"
    Public Property 字号 As Single = 32.0F
    Public Property 使用源字号 As Boolean
    Public Property 使用源颜色 As Boolean = True
    Public Property 颜色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 滚动速度 As Single = 180.0F
    Public Property 目标帧率 As Single = 60.0F
    Public Property 同屏最大数量 As Integer = 100
    Public Property 常规滚动最大行数 As Integer = 5
    Public Property 顶部最大行数 As Integer = 5
    Public Property 行间距 As Single = 8.0F
    Public Property 顶部边距 As Single = 24.0F
    Public Property 固定弹幕持续秒数 As Single = 4.0F
    Public Property 基准视频高度 As Single = 1080.0F

    Friend Sub 验证()
        If String.IsNullOrWhiteSpace(字体) Then Throw New ArgumentException("弹幕字体不能为空。", NameOf(字体))
        If Not Single.IsFinite(字号) OrElse 字号 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(字号))
        If Not Single.IsFinite(滚动速度) OrElse 滚动速度 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(滚动速度))
        If Not Single.IsFinite(目标帧率) OrElse 目标帧率 < 1 OrElse 目标帧率 > 240 Then Throw New ArgumentOutOfRangeException(NameOf(目标帧率))
        If 同屏最大数量 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(同屏最大数量))
        If 常规滚动最大行数 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(常规滚动最大行数))
        If 顶部最大行数 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(顶部最大行数))
        If Not Single.IsFinite(行间距) OrElse 行间距 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(行间距))
        If Not Single.IsFinite(顶部边距) OrElse 顶部边距 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(顶部边距))
        If Not Single.IsFinite(固定弹幕持续秒数) OrElse 固定弹幕持续秒数 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(固定弹幕持续秒数))
        If Not Single.IsFinite(基准视频高度) OrElse 基准视频高度 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(基准视频高度))
    End Sub
End Class

Public Structure 弹幕绘制项
    Public ReadOnly 项目 As 弹幕项目
    Public ReadOnly X像素 As Single
    Public ReadOnly Y像素 As Single
    Public ReadOnly 宽度像素 As Single
    Public ReadOnly 高度像素 As Single
    Public ReadOnly 字体 As String
    Public ReadOnly 字号像素 As Single
    Public ReadOnly 颜色ARGB As UInteger
    Public ReadOnly 帧序号 As Long

    Friend Sub New(itemValue As 弹幕项目, xValue As Single, yValue As Single, widthValue As Single, heightValue As Single,
                   fontValue As String, fontSizeValue As Single, colorValue As UInteger, frameValue As Long)
        项目 = itemValue
        X像素 = xValue
        Y像素 = yValue
        宽度像素 = widthValue
        高度像素 = heightValue
        字体 = fontValue
        字号像素 = fontSizeValue
        颜色ARGB = colorValue
        帧序号 = frameValue
    End Sub
End Structure

Public NotInheritable Class 弹幕调度器
    Private NotInheritable Class 活动项
        Public 项目 As 弹幕项目
        Public 开始秒 As Double
        Public 结束秒 As Double
        Public 行号 As Integer
        Public 宽度 As Single
        Public 高度 As Single
        Public 字号 As Single
        Public 颜色 As UInteger
    End Class

    Private ReadOnly 资料库 As 弹幕资料库
    Private ReadOnly 配置 As 弹幕显示配置
    Private ReadOnly 测量器 As I弹幕文本测量器
    Private ReadOnly 活动列表 As New List(Of 活动项)(128)
    Private 过滤器 As 弹幕过滤器
    Private 游标 As Integer
    Private 上一帧 As Long = Long.MinValue
    Private 上一签名 As Integer

    Public Sub New(database As 弹幕资料库, settings As 弹幕显示配置,
                   Optional filter As 弹幕过滤器 = Nothing, Optional measurer As I弹幕文本测量器 = Nothing)
        ArgumentNullException.ThrowIfNull(database)
        ArgumentNullException.ThrowIfNull(settings)
        settings.验证()
        资料库 = database
        配置 = settings
        过滤器 = If(filter, New 弹幕过滤配置().创建快照())
        测量器 = If(measurer, New 默认弹幕文本测量器())
    End Sub

    Public ReadOnly Property 当前帧序号 As Long
        Get
            Return 上一帧
        End Get
    End Property

    Public Sub 设置过滤器(value As 弹幕过滤器)
        ArgumentNullException.ThrowIfNull(value)
        过滤器 = value
        重置()
    End Sub

    Public Sub 重置()
        活动列表.Clear()
        游标 = 0
        上一帧 = Long.MinValue
        上一签名 = 0
    End Sub

    Public Sub 生成帧(时间 As TimeSpan, 区域 As 视频显示区域, 结果 As ICollection(Of 弹幕绘制项))
        ArgumentNullException.ThrowIfNull(结果)
        If 时间 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(时间))
        配置.验证()
        Dim signature = HashCode.Combine(区域.X像素, 区域.Y像素, 区域.宽度像素, 区域.高度像素,
                                         配置.字体, 配置.字号, 配置.使用源字号, 配置.滚动速度)
        signature = HashCode.Combine(signature, 配置.目标帧率, 配置.同屏最大数量, 配置.常规滚动最大行数, 配置.顶部最大行数,
                                     配置.行间距, 配置.顶部边距, 配置.固定弹幕持续秒数)
        signature = HashCode.Combine(signature, 配置.基准视频高度)
        signature = HashCode.Combine(signature, 配置.使用源颜色, 配置.颜色ARGB)
        ' 就近量化到显示帧。TimeSpan 无法精确表示 1/60 秒；若始终向下取整，
        ' 小尺寸下会形成“停一帧、下一帧跳两像素”的可见闪烁。
        Dim frame = CLng(Math.Round(时间.TotalSeconds * 配置.目标帧率,
                                   MidpointRounding.AwayFromZero))
        Dim seconds = frame / CDbl(配置.目标帧率)
        Dim discontinuity = 上一帧 = Long.MinValue OrElse frame < 上一帧 OrElse frame - 上一帧 > Math.Ceiling(配置.目标帧率 * 2)
        If signature <> 上一签名 Then discontinuity = True
        If discontinuity Then 重建(seconds, 区域)
        清除过期(seconds)
        推进到(seconds, 区域)
        For Each active In 活动列表
            If active.开始秒 > seconds OrElse active.结束秒 <= seconds Then Continue For
            Dim x, y As Single
            Select Case active.项目.类型
                Case 弹幕类型.常规滚动
                    x = 区域.X像素 + 区域.宽度像素 - CSng((seconds - active.开始秒) * 实际滚动速度(区域))
                    y = 区域.Y像素 + 实际顶部边距(区域) + active.行号 * 实际行高(active.字号, 区域)
                Case 弹幕类型.逆向滚动
                    x = 区域.X像素 - active.宽度 + CSng((seconds - active.开始秒) * 实际滚动速度(区域))
                    y = 区域.Y像素 + 实际顶部边距(区域) + active.行号 * 实际行高(active.字号, 区域)
                Case 弹幕类型.顶部
                    x = 区域.X像素 + (区域.宽度像素 - active.宽度) * 0.5F
                    y = 区域.Y像素 + 实际顶部边距(区域) + active.行号 * 实际行高(active.字号, 区域)
                Case 弹幕类型.底部
                    x = 区域.X像素 + (区域.宽度像素 - active.宽度) * 0.5F
                    y = 区域.Y像素 + 区域.高度像素 - 实际顶部边距(区域) - (active.行号 + 1) * 实际行高(active.字号, 区域)
                Case Else
                    Continue For
            End Select
            结果.Add(New 弹幕绘制项(active.项目, x, y, active.宽度, active.高度, 配置.字体,
                                     active.字号, active.颜色, frame))
        Next
        上一帧 = frame
        上一签名 = signature
    End Sub

    Private Sub 重建(seconds As Double, area As 视频显示区域)
        活动列表.Clear()
        Dim maxWidth = area.宽度像素 + area.高度像素 * 10
        Dim lookback = Math.Max(配置.固定弹幕持续秒数, maxWidth / 实际滚动速度(area))
        Dim startSeconds = Math.Max(0, seconds - Math.Min(lookback, 120.0R))
        游标 = 资料库.首个开始不早于(TimeSpan.FromSeconds(startSeconds).Ticks)
        推进到(seconds, area)
        清除过期(seconds)
    End Sub

    Private Sub 推进到(seconds As Double, area As 视频显示区域)
        While 游标 < 资料库.项目.Count
            Dim item = 资料库.项目(游标)
            If item.出现时间.TotalSeconds > seconds Then Exit While
            游标 += 1
            If Not 过滤器.允许(item) Then Continue While
            If item.类型 = 弹幕类型.高级 OrElse item.类型 = 弹幕类型.脚本 Then Continue While
            清除过期(item.出现时间.TotalSeconds)
            If 活动列表.Count >= 配置.同屏最大数量 Then Continue While
            尝试加入(item, area)
        End While
    End Sub

    Private Sub 尝试加入(item As 弹幕项目, area As 视频显示区域)
        Dim scale = area.高度像素 / 配置.基准视频高度
        Dim fontSize = 配置.字号 * scale
        If 配置.使用源字号 Then fontSize *= item.原始字号 / 25.0F
        Dim height = fontSize * 1.2F
        Dim width = 测量器.测量宽度(item.文本, 配置.字体, fontSize)
        Dim lineHeight = 实际行高(fontSize, area)
        Dim availableLines = Math.Max(1, CInt(Math.Floor((area.高度像素 - 实际顶部边距(area) * 2) / lineHeight)))
        Dim maxLines = If(item.类型 = 弹幕类型.常规滚动 OrElse item.类型 = 弹幕类型.逆向滚动,
                          Math.Min(配置.常规滚动最大行数, availableLines),
                          If(item.类型 = 弹幕类型.顶部,
                             Math.Min(配置.顶部最大行数, availableLines), availableLines))
        Dim startSeconds = item.出现时间.TotalSeconds
        Dim lane = 查找可用行(item.类型, maxLines, startSeconds, width, area)
        If lane < 0 Then Return
        Dim duration = If(item.类型 = 弹幕类型.常规滚动 OrElse item.类型 = 弹幕类型.逆向滚动,
                          (area.宽度像素 + width) / 实际滚动速度(area), 配置.固定弹幕持续秒数)
        活动列表.Add(New 活动项 With {
            .项目 = item, .开始秒 = startSeconds, .结束秒 = startSeconds + duration, .行号 = lane,
            .宽度 = width, .高度 = height, .字号 = fontSize,
            .颜色 = If(配置.使用源颜色, item.颜色ARGB, 配置.颜色ARGB)})
    End Sub

    Private Function 查找可用行(type As 弹幕类型, maxLines As Integer, seconds As Double,
                               newWidth As Single, area As 视频显示区域) As Integer
        Dim gap = Math.Max(8.0F, 配置.行间距 * area.高度像素 / 配置.基准视频高度)
        For lane = 0 To maxLines - 1
            Dim available = True
            For Each active In 活动列表
                If active.行号 <> lane OrElse active.项目.类型 <> type OrElse active.结束秒 <= seconds Then Continue For
                If type = 弹幕类型.常规滚动 Then
                    Dim previousX = area.X像素 + area.宽度像素 - CSng((seconds - active.开始秒) * 实际滚动速度(area))
                    If previousX + active.宽度 + gap > area.X像素 + area.宽度像素 Then available = False : Exit For
                ElseIf type = 弹幕类型.逆向滚动 Then
                    Dim previousX = area.X像素 - active.宽度 + CSng((seconds - active.开始秒) * 实际滚动速度(area))
                    If previousX - gap < area.X像素 Then available = False : Exit For
                Else
                    available = False
                    Exit For
                End If
            Next
            If available Then Return lane
        Next
        Return -1
    End Function

    Private Sub 清除过期(seconds As Double)
        ' 稳定压缩一次完成清理；连续 RemoveAt 会在密集弹幕同时过期时反复移动尾部。
        Dim writeIndex = 0
        For readIndex = 0 To 活动列表.Count - 1
            Dim active = 活动列表(readIndex)
            If active.结束秒 <= seconds Then Continue For
            If writeIndex <> readIndex Then 活动列表(writeIndex) = active
            writeIndex += 1
        Next
        If writeIndex < 活动列表.Count Then
            活动列表.RemoveRange(writeIndex, 活动列表.Count - writeIndex)
        End If
    End Sub

    Private Function 实际滚动速度(area As 视频显示区域) As Single
        Return 配置.滚动速度 * area.高度像素 / 配置.基准视频高度
    End Function

    Private Function 实际顶部边距(area As 视频显示区域) As Single
        Return 配置.顶部边距 * area.高度像素 / 配置.基准视频高度
    End Function

    Private Function 实际行高(fontSize As Single, area As 视频显示区域) As Single
        Return fontSize * 1.2F + 配置.行间距 * area.高度像素 / 配置.基准视频高度
    End Function
End Class
