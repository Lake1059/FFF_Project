Imports System.IO
Imports System.Runtime.InteropServices

''' <summary>
''' 查找只属于当前媒体的字体目录。目录内字体不会安装到 Windows；原生 ASS
''' 渲染句柄将字体数据直接载入自己的 libass 字体库。
''' </summary>
Friend NotInheritable Class ASS媒体字体发现器
    Private Shared ReadOnly 字体扩展名 As HashSet(Of String) =
        New HashSet(Of String)(StringComparer.OrdinalIgnoreCase) From {".ttf", ".otf", ".ttc"}

    Private Sub New()
    End Sub

    Friend Shared Function 查找字体目录(媒体路径 As String) As IReadOnlyList(Of String)
        If String.IsNullOrWhiteSpace(媒体路径) Then Return Array.Empty(Of String)()
        Dim 完整媒体路径 = Path.GetFullPath(媒体路径)
        Dim 媒体目录 = If(Directory.Exists(完整媒体路径), 完整媒体路径, Path.GetDirectoryName(完整媒体路径))
        If String.IsNullOrWhiteSpace(媒体目录) OrElse Not Directory.Exists(媒体目录) Then
            Return Array.Empty(Of String)()
        End If
        Dim 结果 As New List(Of String)(3)
        For Each 候选 In {媒体目录, Path.Combine(媒体目录, "Font"), Path.Combine(媒体目录, "Fonts")}
            If 结果.Any(Function(已添加) String.Equals(已添加, 候选, StringComparison.OrdinalIgnoreCase)) Then Continue For
            If 包含字体文件(候选) Then 结果.Add(候选)
        Next
        Return 结果.AsReadOnly()
    End Function

    Private Shared Function 包含字体文件(目录 As String) As Boolean
        If Not Directory.Exists(目录) Then Return False
        Try
            Return Directory.EnumerateFiles(目录, "*", SearchOption.TopDirectoryOnly).
                Any(Function(路径) 字体扩展名.Contains(Path.GetExtension(路径)))
        Catch ex As UnauthorizedAccessException
            Return False
        Catch ex As IOException
            Return False
        End Try
    End Function
End Class

Friend NotInheritable Class ASS特效字幕帧
    Friend Sub New(信息 As 原生位图字幕帧, 像素 As Byte())
        画布宽度 = 信息.画布宽度
        画布高度 = 信息.画布高度
        X = 信息.X
        Y = 信息.Y
        宽度 = 信息.宽度
        高度 = 信息.高度
        行跨度 = 信息.行跨度
        像素BGRA = 像素
        内容标识 = CULng(Math.Max(0, 信息.序号))
    End Sub

    Public ReadOnly Property 画布宽度 As Integer
    Public ReadOnly Property 画布高度 As Integer
    Public ReadOnly Property X As Integer
    Public ReadOnly Property Y As Integer
    Public ReadOnly Property 宽度 As Integer
    Public ReadOnly Property 高度 As Integer
    Public ReadOnly Property 行跨度 As Integer
    Public ReadOnly Property 像素BGRA As Byte()
    Public ReadOnly Property 内容标识 As ULong
End Class

''' <summary>使用 libass 局部遮罩生成预乘 BGRA ASS/SSA 特效帧。</summary>
Friend NotInheritable Class ASS特效字幕帧生成器
    Implements IDisposable

    Private ReadOnly 句柄 As ASS字幕原生句柄
    Private 像素缓冲 As Byte() = Array.Empty(Of Byte)()
    Private 上一帧 As ASS特效字幕帧
    Private 已释放 As Boolean

    Public Sub New(字幕路径 As String, 媒体路径 As String, Optional 流索引 As Integer = -1)
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        Dim 完整字幕路径 = Path.GetFullPath(字幕路径)
        Dim 字体目录 = ASS媒体字体发现器.查找字体目录(媒体路径)
        Dim 路径指针 = Marshal.StringToCoTaskMemUTF8(完整字幕路径)
        Dim 字体目录指针 = Marshal.StringToCoTaskMemUTF8(String.Join(vbLf, 字体目录))
        Dim 原生指针 = IntPtr.Zero
        Try
            Dim 结果 = 播放器原生接口.FFF3FP_OpenAssSubtitle(
                路径指针, 字体目录指针, 流索引, 原生指针)
            If 原生指针 <> IntPtr.Zero Then 句柄 = New ASS字幕原生句柄(原生指针)
            If 结果 <> 原生播放器结果.成功 Then
                Dim 消息 = If(句柄 Is Nothing, "无法打开文字字幕。", 读取错误())
                句柄?.Dispose()
                If 结果 = 原生播放器结果.不支持 Then
                    Throw New NotSupportedException("当前原生运行库不支持此文字字幕。")
                End If
                Throw New InvalidOperationException(消息)
            End If
        Finally
            Marshal.FreeCoTaskMem(字体目录指针)
            Marshal.FreeCoTaskMem(路径指针)
        End Try
    End Sub

    Public Function 生成帧(时间 As TimeSpan, 画布宽度 As Integer, 画布高度 As Integer) As ASS特效字幕帧
        检查未释放()
        If 时间 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(时间))
        If 画布宽度 <= 0 OrElse 画布高度 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(画布宽度))
        Dim 信息 As New 原生位图字幕帧 With {
            .大小 = CUInt(Marshal.SizeOf(Of 原生位图字幕帧)()), .版本 = 1UI}
        检查结果(播放器原生接口.FFF3FP_RenderAssSubtitle(
            句柄, 时间.Ticks, 画布宽度, 画布高度, 信息))
        If (信息.标志 And 原生位图字幕标志.未变化) <> 0 Then Return 上一帧
        If 信息.像素字节数 = 0 OrElse (信息.标志 And 原生位图字幕标志.清除) <> 0 Then
            检查结果(播放器原生接口.FFF3FP_CopyAssSubtitlePixels(句柄, IntPtr.Zero, 0UI))
            上一帧 = Nothing
            Return Nothing
        End If
        If 信息.像素字节数 > Integer.MaxValue Then Throw New InvalidOperationException("ASS 特效字幕帧过大。")
        If 像素缓冲.Length <> CInt(信息.像素字节数) Then ReDim 像素缓冲(CInt(信息.像素字节数) - 1)
        Dim 固定句柄 As GCHandle
        Try
            固定句柄 = GCHandle.Alloc(像素缓冲, GCHandleType.Pinned)
            检查结果(播放器原生接口.FFF3FP_CopyAssSubtitlePixels(
                句柄, 固定句柄.AddrOfPinnedObject(), 信息.像素字节数))
        Finally
            If 固定句柄.IsAllocated Then 固定句柄.Free()
        End Try
        上一帧 = New ASS特效字幕帧(信息, 像素缓冲)
        Return 上一帧
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        句柄.Dispose()
        上一帧 = Nothing
        像素缓冲 = Array.Empty(Of Byte)()
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 检查结果(结果 As 原生播放器结果)
        If 结果 = 原生播放器结果.成功 Then Return
        Throw New InvalidOperationException(读取错误())
    End Sub

    Private Function 读取错误() As String
        Dim 所需 As UInteger
        Dim 首次 = 播放器原生接口.FFF3FP_GetAssSubtitleLastError(句柄, IntPtr.Zero, 0UI, 所需)
        If 首次 <> 原生播放器结果.缓冲区不足 OrElse 所需 = 0 Then Return "ASS/SSA 特效字幕渲染失败。"
        Dim 缓冲 = Marshal.AllocCoTaskMem(CInt(所需))
        Try
            If 播放器原生接口.FFF3FP_GetAssSubtitleLastError(句柄, 缓冲, 所需, 所需) <> 原生播放器结果.成功 Then
                Return "ASS/SSA 特效字幕渲染失败。"
            End If
            Return If(Marshal.PtrToStringUTF8(缓冲), "ASS/SSA 特效字幕渲染失败。")
        Finally
            Marshal.FreeCoTaskMem(缓冲)
        End Try
    End Function

    Private Sub 检查未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub
End Class
