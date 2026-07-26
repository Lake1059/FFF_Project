Imports System.Collections.ObjectModel

Public Interface I时间轴项目
    ReadOnly Property 开始时间 As TimeSpan
    ReadOnly Property 结束时间 As TimeSpan
End Interface

Public NotInheritable Class 时间轴索引(Of T As I时间轴项目)
    Private ReadOnly 项目数组 As T()
    Private ReadOnly 开始刻度 As Long()
    Private ReadOnly 前缀最大结束刻度 As Long()
    Private ReadOnly 只读项目 As ReadOnlyCollection(Of T)

    Public Sub New(项目 As IEnumerable(Of T))
        ArgumentNullException.ThrowIfNull(项目)
        项目数组 = 项目.OrderBy(Function(x) x.开始时间).ThenBy(Function(x) x.结束时间).ToArray()
        开始刻度 = New Long(项目数组.Length - 1) {}
        前缀最大结束刻度 = New Long(项目数组.Length - 1) {}
        Dim 最大结束 = Long.MinValue
        For i = 0 To 项目数组.Length - 1
            开始刻度(i) = 项目数组(i).开始时间.Ticks
            最大结束 = Math.Max(最大结束, 项目数组(i).结束时间.Ticks)
            前缀最大结束刻度(i) = 最大结束
        Next
        只读项目 = Array.AsReadOnly(项目数组)
    End Sub

    Public ReadOnly Property 数量 As Integer
        Get
            Return 项目数组.Length
        End Get
    End Property

    Public ReadOnly Property 项目 As IReadOnlyList(Of T)
        Get
            Return 只读项目
        End Get
    End Property

    Public Function 查询时刻(时间 As TimeSpan, 结果 As ICollection(Of T)) As Integer
        ArgumentNullException.ThrowIfNull(结果)
        Dim 原数量 = 结果.Count
        Dim 时刻 = 时间.Ticks
        Dim 起点 = 首个前缀结束晚于(时刻)
        For i = 起点 To 项目数组.Length - 1
            If 开始刻度(i) > 时刻 Then Exit For
            If 项目数组(i).结束时间.Ticks > 时刻 Then 结果.Add(项目数组(i))
        Next
        Return 结果.Count - 原数量
    End Function

    Public Function 查询范围(开始 As TimeSpan, [结束] As TimeSpan, 结果 As ICollection(Of T)) As Integer
        ArgumentNullException.ThrowIfNull(结果)
        If 开始 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(开始))
        If [结束] < 开始 Then Throw New ArgumentOutOfRangeException(NameOf([结束]))
        Dim 原数量 = 结果.Count
        Dim 起点 = 首个前缀结束晚于(开始.Ticks)
        For i = 起点 To 项目数组.Length - 1
            If 开始刻度(i) >= [结束].Ticks Then Exit For
            If 项目数组(i).结束时间 > 开始 Then 结果.Add(项目数组(i))
        Next
        Return 结果.Count - 原数量
    End Function

    Friend Function 首个开始不早于(时刻 As Long) As Integer
        Dim 左 = 0
        Dim 右 = 开始刻度.Length
        While 左 < 右
            Dim 中 = 左 + ((右 - 左) \ 2)
            If 开始刻度(中) < 时刻 Then
                左 = 中 + 1
            Else
                右 = 中
            End If
        End While
        Return 左
    End Function

    Private Function 首个前缀结束晚于(时刻 As Long) As Integer
        Dim 左 = 0
        Dim 右 = 前缀最大结束刻度.Length
        While 左 < 右
            Dim 中 = 左 + ((右 - 左) \ 2)
            If 前缀最大结束刻度(中) <= 时刻 Then
                左 = 中 + 1
            Else
                右 = 中
            End If
        End While
        Return 左
    End Function
End Class

Public Structure 视频显示区域
    Public ReadOnly X像素 As Single
    Public ReadOnly Y像素 As Single
    Public ReadOnly 宽度像素 As Single
    Public ReadOnly 高度像素 As Single
    Public ReadOnly 缩放系数 As Single
    Public ReadOnly DPI As Single

    Public Sub New(x As Single, y As Single, width As Single, height As Single, scale As Single, dpiValue As Single)
        X像素 = x
        Y像素 = y
        宽度像素 = width
        高度像素 = height
        缩放系数 = scale
        DPI = dpiValue
    End Sub

    Public Shared Function 计算(输出宽度DIP As Single, 输出高度DIP As Single, dpiValue As Single,
                              视频宽度 As Integer, 视频高度 As Integer,
                              Optional 基准视频高度 As Single = 1080.0F) As 视频显示区域
        If Not Single.IsFinite(输出宽度DIP) OrElse 输出宽度DIP <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(输出宽度DIP))
        If Not Single.IsFinite(输出高度DIP) OrElse 输出高度DIP <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(输出高度DIP))
        If Not Single.IsFinite(dpiValue) OrElse dpiValue <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(dpiValue))
        If 视频宽度 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(视频宽度))
        If 视频高度 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(视频高度))
        If Not Single.IsFinite(基准视频高度) OrElse 基准视频高度 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(基准视频高度))

        Dim dpiScale = dpiValue / 96.0F
        Dim outputWidth = 输出宽度DIP * dpiScale
        Dim outputHeight = 输出高度DIP * dpiScale
        Dim videoScale = Math.Min(outputWidth / 视频宽度, outputHeight / 视频高度)
        Dim width = 视频宽度 * videoScale
        Dim height = 视频高度 * videoScale
        Return New 视频显示区域((outputWidth - width) * 0.5F, (outputHeight - height) * 0.5F,
                              width, height, height / 基准视频高度, dpiValue)
    End Function
End Structure
