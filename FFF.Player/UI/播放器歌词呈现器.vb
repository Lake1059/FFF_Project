Imports System.Globalization
Imports System.Threading

''' <summary>把 LRC 歌词生成为独立 GPU 图层；不占用字幕、弹幕或播放器信息图层。</summary>
Friend NotInheritable Class 播放器歌词呈现器
    Implements IDisposable

    ' 歌词视觉参数集中放在这里，后续可直接迁移为播放器设置项。
    Private Const 普通歌词字体 As String = "Microsoft YaHei UI"
    Private Const 当前歌词字体 As String = "Microsoft YaHei UI"
    Private Const 普通歌词字号DIP As Single = 18.0F
    Private Const 当前歌词字号DIP As Single = 24.0F
    Private Const 歌词行高DIP As Single = 24.0F
    Private Const 歌词组间距DIP As Single = 12.0F
    Private Const 普通歌词描边宽度DIP As Single = 1.0F
    Private Const 当前歌词描边宽度DIP As Single = 1.2F
    Private Const 歌词阴影X偏移DIP As Single = 1.0F
    Private Const 歌词阴影Y偏移DIP As Single = 1.0F
    Private Const 当前歌词切入过渡毫秒 As Single = 240.0F
    Private Const 当前歌词切出过渡毫秒 As Single = 240.0F
    Private Const 平滑滚动开始比例 As Single = 0.68F
    Private Const 平滑滚动持续比例 As Single = 0.32F
    Private Const 前后可见歌词组数 As Integer = 7
    Private Const 目标帧率 As Integer = 60

    Private Const 封面区域宽度百分比 As Single = 40
    Private Const 歌词区域宽度百分比 As Single = 60
    Private Const 封面区域左内边距百分比 As Single = 20.0F
    Private Const 封面区域右内边距百分比 As Single = 0.0F
    Private Const 封面区域垂直内边距百分比 As Single = 7.5F
    Private Const 歌词区域水平内边距百分比 As Single = 5.0F
    Private Const 歌词区域垂直内边距百分比 As Single = 10.0F
    Private Const 歌词换行水平安全边距百分比 As Single = 1.25F

    Private Const 封面毛玻璃半径 As Single = 20.0F
    Private Const 封面毛玻璃次数 As Integer = 5
    Private Const 封面毛玻璃下采样倍率 As Integer = 4

    Private Shared ReadOnly 当前歌词颜色 As UInteger = RGB(255, 255, 255)
    Private Shared ReadOnly 相邻歌词颜色 As UInteger = ARGB(200, 255, 255, 255)
    Private Shared ReadOnly 次相邻歌词颜色 As UInteger = ARGB(180, 255, 255, 255)
    Private Shared ReadOnly 远端歌词颜色 As UInteger = ARGB(120, 255, 255, 255)
    Private Shared ReadOnly 歌词描边颜色 As UInteger = ARGB(120, 0, 0, 0)
    Private Shared ReadOnly 歌词阴影颜色 As UInteger = ARGB(120, 0, 0, 0)
    Private Shared ReadOnly 封面毛玻璃遮罩颜色 As UInteger = ARGB(100, 0, 0, 0)
    Private Shared ReadOnly 呈现设置 As New 歌词呈现设置(
        封面毛玻璃半径, 封面毛玻璃次数, 封面毛玻璃下采样倍率, 封面毛玻璃遮罩颜色,
        封面区域宽度百分比, 歌词区域宽度百分比,
        封面区域左内边距百分比, 封面区域右内边距百分比,
        封面区域垂直内边距百分比)
    Private Const 最大字体缓存数 As Integer = 256
    Private Const 最大换行缓存数 As Integer = 4096
    Private Shared ReadOnly 文本测量标志 As TextFormatFlags =
        TextFormatFlags.NoPadding Or TextFormatFlags.NoPrefix Or TextFormatFlags.SingleLine

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 歌词提供器 As Func(Of LRC歌词资料)
    Private ReadOnly 封面状态提供器 As Func(Of Boolean)
    Private ReadOnly 提交图层 As Func(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single,
        歌词呈现设置, Boolean)
    Private ReadOnly 图层命令 As New List(Of 定时文字命令)(32)
    Private ReadOnly 命令对象池 As New List(Of 定时文字命令)(32)
    Private ReadOnly 字体缓存 As New Dictionary(
        Of (字体 As String, 字号位元 As Integer, 样式 As FontStyle), Font)()
    Private ReadOnly 换行缓存 As New Dictionary(
        Of (文本 As String, 字体 As String, 字号位元 As Integer, 样式 As FontStyle, 最大宽度 As Integer), String())()
    Private ReadOnly 刷新计时器 As LakeUI.PrecisionTimer
    Private ReadOnly 生命周期锁 As New Object()
    Private ReadOnly 刷新空闲 As New ManualResetEventSlim(True)
    Private 缓存客户区宽度 As Integer
    Private 缓存客户区高度 As Integer
    Private 缓存DPI位元 As Integer
    Private 命令对象使用数 As Integer
    Private 图层序号 As ULong
    Private 上次图层签名 As ULong
    Private 图层签名有效标志 As Integer
    Private 活动刷新数 As Integer
    Private 已释放标志 As Integer

    Friend Sub New(画面控件值 As 播放器画面控件, 快照提供器值 As Func(Of 播放器快照),
                   歌词提供器值 As Func(Of LRC歌词资料), 封面状态提供器值 As Func(Of Boolean),
                   提交图层值 As Func(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single,
                       歌词呈现设置, Boolean))
        ArgumentNullException.ThrowIfNull(画面控件值)
        ArgumentNullException.ThrowIfNull(快照提供器值)
        ArgumentNullException.ThrowIfNull(歌词提供器值)
        ArgumentNullException.ThrowIfNull(封面状态提供器值)
        ArgumentNullException.ThrowIfNull(提交图层值)
        画面控件 = 画面控件值
        快照提供器 = 快照提供器值
        歌词提供器 = 歌词提供器值
        封面状态提供器 = 封面状态提供器值
        提交图层 = 提交图层值
        刷新计时器 = New LakeUI.PrecisionTimer With {
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .Interval = 16
        }
        更新画面快照()
        AddHandler 画面控件.ClientSizeChanged, AddressOf 画面几何已变化
        AddHandler 画面控件.DpiChangedAfterParent, AddressOf 画面几何已变化
        AddHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick
        刷新计时器.Start()
    End Sub

    Private Sub 刷新计时器_Tick(sender As Object, e As EventArgs)
        SyncLock 生命周期锁
            If 已释放标志 <> 0 Then Return
            活动刷新数 += 1
            刷新空闲.Reset()
        End SyncLock
        Try
            Dim snapshot = 快照提供器()
            Dim size = New Size(Volatile.Read(缓存客户区宽度), Volatile.Read(缓存客户区高度))
            Dim dpi = BitConverter.Int32BitsToSingle(Volatile.Read(缓存DPI位元))
            If snapshot Is Nothing OrElse size.Width <= 0 OrElse size.Height <= 0 Then Return
            提交当前帧(size, snapshot.播放位置, 歌词提供器(), 封面状态提供器(), dpi)
        Finally
            SyncLock 生命周期锁
                活动刷新数 -= 1
                If 活动刷新数 = 0 Then 刷新空闲.Set()
            End SyncLock
        End Try
    End Sub

    Private Sub 画面几何已变化(sender As Object, e As EventArgs)
        更新画面快照()
    End Sub

    Private Sub 更新画面快照()
        Volatile.Write(缓存客户区宽度, 画面控件.ClientSize.Width)
        Volatile.Write(缓存客户区高度, 画面控件.ClientSize.Height)
        Volatile.Write(缓存DPI位元, BitConverter.SingleToInt32Bits(画面控件.DeviceDpi))
    End Sub

    Friend Function 生成命令(客户区大小 As Size, 播放位置 As TimeSpan, 歌词 As LRC歌词资料,
                         有封面 As Boolean, Optional DPI As Single = 96.0F) As IReadOnlyList(Of 定时文字命令)
        图层命令.Clear()
        命令对象使用数 = 0
        If Volatile.Read(已释放标志) <> 0 OrElse 歌词 Is Nothing OrElse 歌词.条目.Count = 0 OrElse
            客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then Return 图层命令
        If Not Single.IsFinite(DPI) OrElse DPI <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(DPI))

        Dim region = 计算歌词区域(客户区大小, 有封面)
        If region.Width <= 1.0F OrElse region.Height <= 1.0F Then Return 图层命令
        Dim dpiScale = DPI / 96.0F
        Dim normalFontSize = 普通歌词字号DIP * dpiScale
        Dim activeFontSize = 当前歌词字号DIP * dpiScale
        Dim lineHeight = 歌词行高DIP * dpiScale
        Dim groupGap = 歌词组间距DIP * dpiScale
        ' 同一时间组内的多语言/多行歌词也使用相同的组间距，避免它们
        ' 挤成一块；组高、滚动距离和实际命令位置必须共享这一几何模型。
        Dim lineStep = lineHeight + groupGap
        Dim maximumTextWidth = Math.Max(1.0F, region.Width *
            (1.0F - 歌词换行水平安全边距百分比 / 100.0F * 2.0F))
        Dim current = 歌词.查找当前条目(播放位置)
        Dim anchor = Math.Max(0, current)
        Dim first = Math.Max(0, anchor - 前后可见歌词组数)
        Dim last = Math.Min(歌词.条目.Count - 1, anchor + 前后可见歌词组数)
        Dim displayLines(last - first)() As String
        For index = first To last
            ' 统一按较大的当前歌词字形换行，避免尺寸过渡期间突然改变行数。
            displayLines(index - first) = 生成显示行(歌词.条目(index), 当前歌词字体,
                activeFontSize, FontStyle.Regular, maximumTextWidth)
        Next

        Dim transition = 0.0F
        If current >= 0 AndAlso current + 1 < 歌词.条目.Count Then
            Dim duration = 歌词.条目(current + 1).开始时间 - 歌词.条目(current).开始时间
            If duration > TimeSpan.Zero Then
                Dim ratio = CSng((播放位置 - 歌词.条目(current).开始时间).Ticks / CDbl(duration.Ticks))
                Dim value = Math.Clamp((ratio - 平滑滚动开始比例) / 平滑滚动持续比例, 0.0F, 1.0F)
                transition = value * value * (3.0F - 2.0F * value)
            End If
        End If

        Dim anchorHeight = 取得组高度(displayLines(anchor - first), lineHeight, groupGap)
        Dim scrollDistance = 0.0F
        If transition > 0 AndAlso anchor + 1 < 歌词.条目.Count Then
            Dim nextHeight = 取得组高度(displayLines(anchor + 1 - first), lineHeight, groupGap)
            scrollDistance = ((anchorHeight + nextHeight) * 0.5F + groupGap) * transition
        End If
        Dim anchorTop = region.Top + region.Height * 0.5F - anchorHeight * 0.5F - scrollDistance

        Dim tops(last - first) As Single
        tops(anchor - first) = anchorTop
        For index = anchor - 1 To first Step -1
            tops(index - first) = tops(index + 1 - first) -
                取得组高度(displayLines(index - first), lineHeight, groupGap) - groupGap
        Next
        For index = anchor + 1 To last
            tops(index - first) = tops(index - 1 - first) +
                取得组高度(displayLines(index - 1 - first), lineHeight, groupGap) + groupGap
        Next

        For index = first To last
            Dim lines = displayLines(index - first)
            Dim top = tops(index - first)
            Dim height = 取得组高度(lines, lineHeight, groupGap)
            If top + height < region.Top OrElse top > region.Bottom Then Continue For
            Dim isActive = current >= 0 AndAlso index = current
            Dim distance = Math.Abs(index - anchor)
            Dim color = If(isActive, 当前歌词颜色,
                If(distance <= 1, 相邻歌词颜色, If(distance <= 2, 次相邻歌词颜色, 远端歌词颜色)))
            Dim activeWeight = If(isActive, 计算当前歌词尺寸权重(歌词, index, 播放位置), 0.0F)
            Dim fontSize = normalFontSize + (activeFontSize - normalFontSize) * activeWeight
            Dim fontFamily = If(isActive, 当前歌词字体, 普通歌词字体)
            For lineIndex = 0 To lines.Length - 1
                Dim text = lines(lineIndex)
                If String.IsNullOrEmpty(text) Then Continue For
                Dim command = 取得复用命令()
                command.设置文字(text, fontFamily, fontSize,
                    New RectangleF(region.Left, top + lineIndex * lineStep, region.Width, lineHeight),
                    color, 歌词描边颜色,
                    (普通歌词描边宽度DIP +
                     (当前歌词描边宽度DIP - 普通歌词描边宽度DIP) * activeWeight) * dpiScale,
                    定时文字对齐.居中, 定时文字对齐.居中, 定时文字样式.无,
                    阴影色值:=歌词阴影颜色,
                    阴影X值:=歌词阴影X偏移DIP * dpiScale,
                    阴影Y值:=歌词阴影Y偏移DIP * dpiScale)
                图层命令.Add(command)
            Next
        Next
        Return 图层命令
    End Function

    Private Shared Function 计算当前歌词尺寸权重(歌词 As LRC歌词资料, index As Integer,
                                          播放位置 As TimeSpan) As Single
        Dim item = 歌词.条目(index)
        Dim elapsedMilliseconds = CSng((播放位置 - item.开始时间).TotalMilliseconds)
        If elapsedMilliseconds < 0.0F Then Return 0.0F
        Dim enterDuration = Math.Max(1.0F, 当前歌词切入过渡毫秒)
        Dim exitWeight = 1.0F
        If index + 1 < 歌词.条目.Count Then
            Dim intervalMilliseconds = CSng(
                (歌词.条目(index + 1).开始时间 - item.开始时间).TotalMilliseconds)
            If intervalMilliseconds > 0.0F Then
                enterDuration = Math.Min(enterDuration, intervalMilliseconds * 0.5F)
                Dim exitDuration = Math.Min(Math.Max(1.0F, 当前歌词切出过渡毫秒),
                                            intervalMilliseconds * 0.5F)
                exitWeight = Math.Clamp(
                    (intervalMilliseconds - elapsedMilliseconds) / exitDuration, 0.0F, 1.0F)
            End If
        End If
        Dim enterWeight = Math.Clamp(elapsedMilliseconds / enterDuration, 0.0F, 1.0F)
        Return 平滑步进(Math.Min(enterWeight, exitWeight))
    End Function

    Private Shared Function 平滑步进(value As Single) As Single
        value = Math.Clamp(value, 0.0F, 1.0F)
        Return value * value * (3.0F - 2.0F * value)
    End Function

    Friend Shared Function 计算歌词区域(客户区大小 As Size, 有封面 As Boolean) As RectangleF
        If 客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then Return RectangleF.Empty
        Dim left = If(有封面, 计算封面区域宽度(客户区大小.Width), 0)
        Dim width = 客户区大小.Width - left
        Dim horizontalPadding = Math.Min(
            CSng(Math.Round(width * 歌词区域水平内边距百分比 / 100.0F,
                            MidpointRounding.AwayFromZero)),
            Math.Max(0.0F, (width - 1.0F) * 0.5F))
        Dim verticalPadding = Math.Min(
            CSng(Math.Round(客户区大小.Height * 歌词区域垂直内边距百分比 / 100.0F,
                            MidpointRounding.AwayFromZero)),
            Math.Max(0.0F, (客户区大小.Height - 1.0F) * 0.5F))
        Return New RectangleF(left + horizontalPadding, verticalPadding,
                              Math.Max(1.0F, width - horizontalPadding * 2.0F),
                              Math.Max(1.0F, 客户区大小.Height - verticalPadding * 2.0F))
    End Function

    Private Shared Function 计算封面区域宽度(总宽度 As Integer) As Integer
        Dim total = Math.Max(0.0001F, 封面区域宽度百分比 + 歌词区域宽度百分比)
        Return Math.Clamp(CInt(Math.Round(总宽度 * 封面区域宽度百分比 / total,
                                          MidpointRounding.AwayFromZero)), 0, 总宽度)
    End Function

    Private Function 生成显示行(item As LRC歌词条目, fontFamily As String, fontSize As Single,
                          fontStyle As FontStyle, maximumWidth As Single) As String()
        Dim result As New List(Of String)(Math.Max(1, item.文本.Count))
        For Each lyricText As String In item.文本
            result.AddRange(自动换行(If(lyricText, String.Empty), fontFamily, fontSize,
                                  fontStyle, maximumWidth))
        Next
        If result.Count = 0 Then result.Add(String.Empty)
        Return result.ToArray()
    End Function

    Private Function 自动换行(text As String, fontFamily As String, fontSize As Single,
                         fontStyle As FontStyle, maximumWidth As Single) As String()
        If String.IsNullOrEmpty(text) Then Return {String.Empty}
        Dim width = Math.Max(1, CInt(Math.Floor(maximumWidth)))
        Dim key = (文本:=text, 字体:=fontFamily,
            字号位元:=BitConverter.SingleToInt32Bits(fontSize), 样式:=fontStyle, 最大宽度:=width)
        Dim cached As String() = Nothing
        If 换行缓存.TryGetValue(key, cached) Then Return cached

        Dim font = 取得测量字体(fontFamily, fontSize, fontStyle)
        If 测量文本宽度(text, font) <= width Then
            cached = {text}
        Else
            cached = 按字素换行(text, font, width)
        End If
        If 换行缓存.Count >= 最大换行缓存数 Then 换行缓存.Clear()
        换行缓存(key) = cached
        Return cached
    End Function

    Private Function 取得测量字体(fontFamily As String, fontSize As Single,
                            fontStyle As FontStyle) As Font
        Dim key = (字体:=fontFamily, 字号位元:=BitConverter.SingleToInt32Bits(fontSize), 样式:=fontStyle)
        Dim result As Font = Nothing
        If 字体缓存.TryGetValue(key, result) Then Return result
        If 字体缓存.Count >= 最大字体缓存数 Then
            For Each cachedFont In 字体缓存.Values
                cachedFont.Dispose()
            Next
            字体缓存.Clear()
        End If
        result = New Font(fontFamily, fontSize, fontStyle, GraphicsUnit.Pixel)
        字体缓存.Add(key, result)
        Return result
    End Function

    Private Shared Function 按字素换行(text As String, font As Font, maximumWidth As Integer) As String()
        Dim starts = StringInfo.ParseCombiningCharacters(text)
        If starts.Length = 0 Then Return {String.Empty}
        Dim result As New List(Of String)()
        Dim startElement = 0
        While startElement < starts.Length
            While startElement < starts.Length AndAlso
                Char.IsWhiteSpace(text, starts(startElement))
                startElement += 1
            End While
            If startElement >= starts.Length Then Exit While

            Dim low = startElement + 1
            Dim high = starts.Length
            Dim fittedEnd = low
            While low <= high
                Dim middle = low + ((high - low) \ 2)
                Dim candidate = 取得字素范围(text, starts, startElement, middle)
                If 测量文本宽度(candidate, font) <= maximumWidth Then
                    fittedEnd = middle
                    low = middle + 1
                Else
                    high = middle - 1
                End If
            End While

            Dim lineEnd = fittedEnd
            If fittedEnd < starts.Length Then
                For index = fittedEnd - 1 To startElement + 1 Step -1
                    If Char.IsWhiteSpace(text, starts(index)) Then
                        lineEnd = index
                        Exit For
                    End If
                Next
            End If
            Dim line = 取得字素范围(text, starts, startElement, lineEnd).TrimEnd()
            If line.Length > 0 Then result.Add(line)
            startElement = Math.Max(lineEnd, startElement + 1)
        End While
        If result.Count = 0 Then result.Add(String.Empty)
        Return result.ToArray()
    End Function

    Private Shared Function 取得字素范围(text As String, starts As Integer(),
                                   startElement As Integer, endElement As Integer) As String
        Dim startIndex = starts(startElement)
        Dim endIndex = If(endElement < starts.Length, starts(endElement), text.Length)
        Return text.Substring(startIndex, endIndex - startIndex)
    End Function

    Private Shared Function 测量文本宽度(text As String, font As Font) As Integer
        Return TextRenderer.MeasureText(text, font, New Size(Integer.MaxValue, Integer.MaxValue),
                                        文本测量标志).Width
    End Function

    Private Shared Function 取得组高度(lines As String(), lineHeight As Single,
                                  groupGap As Single) As Single
        Dim lineCount = Math.Max(1, lines.Length)
        Return lineCount * lineHeight + Math.Max(0, lineCount - 1) * groupGap
    End Function

    Private Function 取得复用命令() As 定时文字命令
        Dim result As 定时文字命令
        If 命令对象使用数 < 命令对象池.Count Then
            result = 命令对象池(命令对象使用数)
        Else
            result = New 定时文字命令()
            命令对象池.Add(result)
        End If
        命令对象使用数 += 1
        Return result
    End Function

    Private Sub 提交当前帧(客户区大小 As Size, 播放位置 As TimeSpan,
                       歌词 As LRC歌词资料, 有封面 As Boolean, DPI As Single)
        Try
            Dim commands = 生成命令(客户区大小, 播放位置, 歌词, 有封面, DPI)
            Dim signature = 计算图层签名(客户区大小, 有封面, commands, 呈现设置)
            If Volatile.Read(图层签名有效标志) <> 0 AndAlso signature = 上次图层签名 Then Return
            Dim nextSequence = 图层序号 + 1UL
            If Not 提交图层(客户区大小, commands, nextSequence, CSng(目标帧率), 呈现设置) Then Return
            图层序号 = nextSequence
            上次图层签名 = signature
            Volatile.Write(图层签名有效标志, 1)
        Catch
            ' 歌词是可选图层，解析或呈现失败不能中断音频播放。
        End Try
    End Sub

    Friend Sub 使图层失效()
        Volatile.Write(图层签名有效标志, 0)
    End Sub

    Private Shared Function 计算图层签名(画布大小 As Size, 有封面 As Boolean,
                                      commands As IReadOnlyList(Of 定时文字命令),
                                      settings As 歌词呈现设置) As ULong
        Dim hash As ULong = &HCBF29CE484222325UL
        混合签名(hash, CULng(CUInt(画布大小.Width)))
        混合签名(hash, CULng(CUInt(画布大小.Height)))
        混合签名(hash, If(有封面, 1UL, 0UL))
        混合签名(hash, CULng(commands.Count))
        混合签名(hash, BitConverter.SingleToUInt32Bits(settings.模糊半径))
        混合签名(hash, CULng(CUInt(settings.模糊次数)))
        混合签名(hash, CULng(CUInt(settings.下采样倍率)))
        混合签名(hash, settings.遮罩颜色ARGB)
        混合签名(hash, BitConverter.SingleToUInt32Bits(settings.封面区域宽度百分比))
        混合签名(hash, BitConverter.SingleToUInt32Bits(settings.歌词区域宽度百分比))
        混合签名(hash, BitConverter.SingleToUInt32Bits(settings.封面左内边距百分比))
        混合签名(hash, BitConverter.SingleToUInt32Bits(settings.封面右内边距百分比))
        混合签名(hash, BitConverter.SingleToUInt32Bits(settings.封面垂直内边距百分比))
        For Each item In commands
            混合签名(hash, If(item.是位图, 1UL, 0UL))
            混合签名(hash, item.内容标识)
            混合签名(hash, CULng(CUInt(Math.Max(0, item.位图宽度))))
            混合签名(hash, CULng(CUInt(Math.Max(0, item.位图高度))))
            混合签名(hash, CULng(CUInt(Math.Max(0, item.位图行跨度))))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.X))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.Y))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.宽度))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.高度))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.字号))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.描边宽度))
            混合签名(hash, item.前景色ARGB)
            混合签名(hash, item.描边色ARGB)
            混合签名(hash, item.阴影色ARGB)
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.阴影X偏移))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.阴影Y偏移))
            混合签名(hash, CULng(item.水平对齐))
            混合签名(hash, CULng(item.垂直对齐))
            混合签名(hash, CULng(item.样式))
        Next
        Return hash
    End Function

    Private Shared Sub 混合签名(ByRef hash As ULong, value As ULong)
        hash = Numerics.BitOperations.RotateLeft(hash, 7) Xor value
    End Sub

    Private Shared Function ARGB(alpha As Byte, red As Byte, green As Byte, blue As Byte) As UInteger
        Return (CUInt(alpha) << 24) Or (CUInt(red) << 16) Or (CUInt(green) << 8) Or blue
    End Function

    Private Shared Function RGB(red As Byte, green As Byte, blue As Byte) As UInteger
        Return ARGB(Byte.MaxValue, red, green, blue)
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        SyncLock 生命周期锁
            If 已释放标志 <> 0 Then Return
            已释放标志 = 1
        End SyncLock
        刷新计时器.Stop()
        RemoveHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick
        RemoveHandler 画面控件.ClientSizeChanged, AddressOf 画面几何已变化
        RemoveHandler 画面控件.DpiChangedAfterParent, AddressOf 画面几何已变化
        刷新空闲.Wait()
        For Each font In 字体缓存.Values
            font.Dispose()
        Next
        字体缓存.Clear()
        换行缓存.Clear()
        刷新计时器.Dispose()
        刷新空闲.Dispose()
    End Sub
End Class
