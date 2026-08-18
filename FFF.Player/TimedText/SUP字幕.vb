Imports System.Runtime.InteropServices
Imports System.Drawing

Public NotInheritable Class SUP字幕事件
    Friend Sub New(info As 原生位图字幕帧, pixels As Byte())
        开始时间 = TimeSpan.FromTicks(info.开始100纳秒)
        结束时间 = If(info.结束100纳秒 > info.开始100纳秒, TimeSpan.FromTicks(info.结束100纳秒), TimeSpan.Zero)
        画布宽度 = info.画布宽度
        画布高度 = info.画布高度
        X = info.X
        Y = info.Y
        宽度 = info.宽度
        高度 = info.高度
        行跨度 = info.行跨度
        像素BGRA = pixels
        是清除事件 = (info.标志 And 原生位图字幕标志.清除) <> 0
        是强制字幕 = (info.标志 And 原生位图字幕标志.强制) <> 0
        仍需读取 = (info.标志 And 原生位图字幕标志.仍需读取) <> 0
        序号 = info.序号
    End Sub

    Public ReadOnly Property 开始时间 As TimeSpan
    Public ReadOnly Property 结束时间 As TimeSpan
    Public ReadOnly Property 画布宽度 As Integer
    Public ReadOnly Property 画布高度 As Integer
    Public ReadOnly Property X As Integer
    Public ReadOnly Property Y As Integer
    Public ReadOnly Property 宽度 As Integer
    Public ReadOnly Property 高度 As Integer
    Public ReadOnly Property 行跨度 As Integer
    Public ReadOnly Property 像素BGRA As Byte()
    Public ReadOnly Property 是清除事件 As Boolean
    Public ReadOnly Property 是强制字幕 As Boolean
    Public ReadOnly Property 仍需读取 As Boolean
    Public ReadOnly Property 序号 As Long
End Class

Public NotInheritable Class SUP字幕解码器
    Implements IDisposable

    Private ReadOnly 句柄 As 位图字幕原生句柄
    Private 已释放 As Boolean

    Public Sub New(路径 As String, Optional 流索引 As Integer = -1)
        ArgumentException.ThrowIfNullOrWhiteSpace(路径)
        Dim pathPointer = Marshal.StringToCoTaskMemUTF8(路径)
        Dim nativePointer = IntPtr.Zero
        Try
            Dim result = 播放器原生接口.FFF3FP_OpenBitmapSubtitle(pathPointer, 流索引, nativePointer)
            If nativePointer <> IntPtr.Zero Then 句柄 = New 位图字幕原生句柄(nativePointer)
            If result <> 原生播放器结果.成功 Then
                Dim message = If(句柄 Is Nothing, "无法打开位图字幕。", 读取错误())
                句柄?.Dispose()
                Throw New InvalidOperationException(message)
            End If
        Finally
            Marshal.FreeCoTaskMem(pathPointer)
        End Try
    End Sub

    Public Function 读取下一事件() As SUP字幕事件
        检查未释放()
        Dim info As New 原生位图字幕帧 With {
            .大小 = CUInt(Marshal.SizeOf(Of 原生位图字幕帧)()), .版本 = 1UI}
        检查结果(播放器原生接口.FFF3FP_ReadBitmapSubtitle(句柄, info))
        If (info.标志 And 原生位图字幕标志.流结束) <> 0 Then Return Nothing
        If (info.标志 And 原生位图字幕标志.仍需读取) <> 0 Then
            Return New SUP字幕事件(info, Array.Empty(Of Byte)())
        End If
        If info.像素字节数 > Integer.MaxValue Then Throw New InvalidOperationException("SUP 字幕帧过大。")
        Dim pixels = If(info.像素字节数 = 0, Array.Empty(Of Byte)(), New Byte(CInt(info.像素字节数) - 1) {})
        Dim pinned As GCHandle
        Try
            Dim pointer = IntPtr.Zero
            If pixels.Length > 0 Then
                pinned = GCHandle.Alloc(pixels, GCHandleType.Pinned)
                pointer = pinned.AddrOfPinnedObject()
            End If
            检查结果(播放器原生接口.FFF3FP_CopyBitmapSubtitlePixels(句柄, pointer, info.像素字节数))
        Finally
            If pinned.IsAllocated Then pinned.Free()
        End Try
        Return New SUP字幕事件(info, pixels)
    End Function

    Public Sub 跳转(位置 As TimeSpan)
        检查未释放()
        If 位置 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(位置))
        检查结果(播放器原生接口.FFF3FP_SeekBitmapSubtitle(句柄, 位置.Ticks))
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        句柄.Dispose()
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 检查结果(result As 原生播放器结果)
        If result = 原生播放器结果.成功 Then Return
        Throw New InvalidOperationException(读取错误())
    End Sub

    Private Function 读取错误() As String
        Dim required As UInteger
        Dim first = 播放器原生接口.FFF3FP_GetBitmapSubtitleLastError(句柄, IntPtr.Zero, 0, required)
        If first <> 原生播放器结果.缓冲区不足 OrElse required = 0 Then Return "位图字幕解码失败。"
        Dim buffer = Marshal.AllocCoTaskMem(CInt(required))
        Try
            If 播放器原生接口.FFF3FP_GetBitmapSubtitleLastError(句柄, buffer, required, required) <> 原生播放器结果.成功 Then
                Return "位图字幕解码失败。"
            End If
            Return If(Marshal.PtrToStringUTF8(buffer), "位图字幕解码失败。")
        Finally
            Marshal.FreeCoTaskMem(buffer)
        End Try
    End Function

    Private Sub 检查未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub
End Class

Public Structure SUP字幕绘制项
    Public ReadOnly 事件 As SUP字幕事件
    Public ReadOnly X像素 As Single
    Public ReadOnly Y像素 As Single
    Public ReadOnly 宽度像素 As Single
    Public ReadOnly 高度像素 As Single

    Friend Sub New(eventValue As SUP字幕事件, xValue As Single, yValue As Single, widthValue As Single, heightValue As Single)
        事件 = eventValue
        X像素 = xValue
        Y像素 = yValue
        宽度像素 = widthValue
        高度像素 = heightValue
    End Sub
End Structure

Public NotInheritable Class SUP字幕帧生成器
    Implements IDisposable

    Private ReadOnly 解码器 As SUP字幕解码器
    Private 当前事件 As SUP字幕事件
    Private 下一事件 As SUP字幕事件
    Private 上次时间 As TimeSpan = TimeSpan.MinValue
    Private 流已结束 As Boolean

    Public Sub New(路径 As String, Optional 流索引 As Integer = -1)
        解码器 = New SUP字幕解码器(路径, 流索引)
    End Sub

    Public Sub 生成帧(时间 As TimeSpan, 区域 As 视频显示区域, 结果 As ICollection(Of SUP字幕绘制项))
        ArgumentNullException.ThrowIfNull(结果)
        If 时间 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(时间))
        If 上次时间 = TimeSpan.MinValue OrElse 时间 < 上次时间 OrElse 时间 - 上次时间 > TimeSpan.FromSeconds(2) Then
            ' PGS 的图像对象与调色板可能早于当前显示集。跳转后必须从目标前方
            ' 预读一段时间，才能恢复完整解码状态以及跨越目标点的活动字幕。
            Dim replayStart = If(时间 > TimeSpan.FromSeconds(30), 时间 - TimeSpan.FromSeconds(30), TimeSpan.Zero)
            解码器.跳转(replayStart)
            当前事件 = Nothing
            下一事件 = Nothing
            流已结束 = False
        End If
        If 下一事件 Is Nothing AndAlso Not 流已结束 Then
            读取下一批次()
        End If
        While 下一事件 IsNot Nothing AndAlso 下一事件.开始时间 <= 时间
            If 下一事件.是清除事件 Then
                当前事件 = Nothing
            Else
                当前事件 = 下一事件
            End If
            下一事件 = Nothing
            If Not 流已结束 Then 读取下一批次()
        End While
        If 当前事件 IsNot Nothing AndAlso 当前事件.结束时间 > TimeSpan.Zero AndAlso 当前事件.结束时间 <= 时间 Then 当前事件 = Nothing
        If 当前事件 IsNot Nothing AndAlso 当前事件.画布宽度 > 0 AndAlso 当前事件.画布高度 > 0 Then
            Dim 绘制区域 = 计算绘制区域(当前事件, 区域)
            If 绘制区域.Width > 0 AndAlso 绘制区域.Height > 0 Then
                结果.Add(New SUP字幕绘制项(当前事件,
                    绘制区域.X, 绘制区域.Y, 绘制区域.Width, 绘制区域.Height))
            End If
        End If
        上次时间 = 时间
    End Sub

    ''' <summary>
    ''' 将 PGS 画布等比适配到视频区域。PGS 画布通常固定为 16:9；
    ''' 宽屏视频的显示区域可能更宽，分别缩放 X/Y 会把位图压扁。
    ''' </summary>
    Friend Shared Function 计算绘制区域(事件 As SUP字幕事件, 区域 As 视频显示区域) As RectangleF
        If 事件 Is Nothing OrElse 事件.画布宽度 <= 0 OrElse 事件.画布高度 <= 0 OrElse
            事件.宽度 <= 0 OrElse 事件.高度 <= 0 OrElse 区域.宽度像素 <= 0 OrElse 区域.高度像素 <= 0 Then
            Return RectangleF.Empty
        End If

        Dim scale = Math.Min(区域.宽度像素 / CSng(事件.画布宽度),
                             区域.高度像素 / CSng(事件.画布高度))
        If Not Single.IsFinite(scale) OrElse scale <= 0 Then Return RectangleF.Empty

        Dim scaledCanvasWidth = 事件.画布宽度 * scale
        Dim scaledCanvasHeight = 事件.画布高度 * scale
        Dim offsetX = (区域.宽度像素 - scaledCanvasWidth) * 0.5F
        Dim offsetY = (区域.高度像素 - scaledCanvasHeight) * 0.5F
        Return New RectangleF(
            区域.X像素 + offsetX + 事件.X * scale,
            区域.Y像素 + offsetY + 事件.Y * scale,
            事件.宽度 * scale,
            事件.高度 * scale)
    End Function

    Private Sub 读取下一批次()
        Dim 事件 = 解码器.读取下一事件()
        If 事件 Is Nothing Then
            流已结束 = True
        ElseIf Not 事件.仍需读取 Then
            下一事件 = 事件
        End If
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        解码器.Dispose()
    End Sub
End Class
