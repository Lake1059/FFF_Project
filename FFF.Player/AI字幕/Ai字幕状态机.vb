''' <summary>按媒体时间排队，seek 时清除上一代。已经推理出的未来字幕不会抢占当前字幕。</summary>
Friend NotInheritable Class Ai字幕状态机
    Implements IDisposable

    Public NotInheritable Class Ai字幕条目
        Public ReadOnly Property 代次 As ULong
        Public ReadOnly Property 起始100纳秒 As Long
        Public ReadOnly Property 结束100纳秒 As Long
        Public ReadOnly Property 文本 As String
        Public ReadOnly Property 语言 As String
        Public ReadOnly Property 副文本 As String
        Public ReadOnly Property 后续页面 As IReadOnlyList(Of (上行 As String, 下行 As String))
        Public ReadOnly Property 推理开始计时 As Long
        Public ReadOnly Property 实时音频 As Boolean
        Public ReadOnly Property 最晚结束100纳秒 As Double

        Public Sub New(代次 As ULong, 起始100纳秒 As Long, 结束100纳秒 As Long,
                       文本 As String, 语言 As String, Optional 副文本 As String = Nothing,
                       Optional 后续页面 As IReadOnlyList(Of (上行 As String, 下行 As String)) = Nothing,
                       Optional 推理开始计时 As Long = 0, Optional 实时音频 As Boolean = False,
                       Optional 推理输入结束100纳秒 As Long = 0)
            Me.代次 = 代次
            Me.起始100纳秒 = 起始100纳秒
            Me.结束100纳秒 = 结束100纳秒
            Me.文本 = If(文本, String.Empty)
            Me.语言 = If(语言, String.Empty)
            Me.副文本 = If(副文本, String.Empty)
            Me.后续页面 = If(后续页面, Array.Empty(Of (上行 As String, 下行 As String))())
            Me.推理开始计时 = 推理开始计时
            Me.实时音频 = 实时音频
            最晚结束100纳秒 = CDbl(结束100纳秒)
            If 实时音频 Then
                ' 等候分句所用的后续音频也在实时播放中。用 worker 已读到的
                ' 媒体时间计算固定期限，避免用每次绘制的时间重新延长。
                最晚结束100纳秒 = CDbl(Math.Max(结束100纳秒, 推理输入结束100纳秒)) + 实时迟到余量
            End If
        End Sub
    End Class

    Private ReadOnly 锁 As New Object()
    Private ReadOnly 队列 As New List(Of Ai字幕条目)()
    Private 当前条目 As Ai字幕条目
    Private 当前代次 As ULong?
    Private 首次显示100纳秒 As Long
    Private Worker已退出 As Boolean
    Private 已释放 As Boolean

    ' 无法预读的实时音频需要给迟到的结果留出短暂的阅读时间。
    Private Const 实时迟到余量 As Long = 2 * TimeSpan.TicksPerSecond
    ' 支持内核最多 24 秒预读，再留出设置允许的时间偏移与一帧余量。
    Public Property 未来余量100纳秒 As Long = 28 * TimeSpan.TicksPerSecond
    Public Property 时间偏移100纳秒 As Long

    Public Sub 提交结果(条目 As Ai字幕条目)
        If 条目 Is Nothing OrElse String.IsNullOrWhiteSpace(条目.文本) OrElse 条目.结束100纳秒 <= 条目.起始100纳秒 Then Return
        SyncLock 锁
            If Worker已退出 OrElse 已释放 OrElse (当前代次.HasValue AndAlso 条目.代次 < 当前代次.Value) Then Return
            队列.Add(条目)
            If 队列.Count > 128 Then 队列.RemoveAt(0)
        End SyncLock
    End Sub

    Public Sub 通知Worker已退出()
        SyncLock 锁
            Worker已退出 = True
            队列.Clear()
            当前条目 = Nothing
        End SyncLock
    End Sub

    Public Sub 复位Worker退出()
        SyncLock 锁
            Worker已退出 = False
        End SyncLock
    End Sub

    Public Sub 重置()
        SyncLock 锁
            队列.Clear()
            当前条目 = Nothing
            当前代次 = Nothing
            Worker已退出 = False
        End SyncLock
    End Sub

    Public Function 取当前条目(播放位置 As TimeSpan, 时间轴代次 As ULong) As String
        Return 取当前两行(播放位置, 时间轴代次).主
    End Function

    Public Function 取当前两行(播放位置 As TimeSpan, 时间轴代次 As ULong,
                            Optional ByRef 推理开始计时 As Long = 0) As (主 As String, 副 As String)
        推理开始计时 = 0
        SyncLock 锁
            If Worker已退出 OrElse 已释放 Then Return (String.Empty, String.Empty)
            If Not 当前代次.HasValue OrElse 当前代次.Value <> 时间轴代次 Then
                当前代次 = 时间轴代次
                当前条目 = Nothing
            End If
            Dim 位置 = 播放位置.Ticks - 时间偏移100纳秒
            队列.RemoveAll(Function(项) 项.代次 < 时间轴代次 OrElse
                (项.代次 = 时间轴代次 AndAlso (位置 >= 项.最晚结束100纳秒 OrElse
                    CDbl(项.起始100纳秒) - 位置 > 未来余量100纳秒)))
            Dim 最佳 As Ai字幕条目 = Nothing
            For Each 项 In 队列
                If 项.代次 = 时间轴代次 AndAlso 项.起始100纳秒 <= 位置 AndAlso
                    (最佳 Is Nothing OrElse 项.起始100纳秒 >= 最佳.起始100纳秒) Then 最佳 = 项
            Next
            If 最佳 Is Nothing Then
                当前条目 = Nothing
                Return (String.Empty, String.Empty)
            End If
            If Not ReferenceEquals(当前条目, 最佳) Then
                当前条目 = 最佳
                首次显示100纳秒 = 位置
            End If
            Dim 起点 = 最佳.起始100纳秒
            Dim 终点 = CDbl(最佳.结束100纳秒)
            If 最佳.实时音频 AndAlso 首次显示100纳秒 >= 最佳.结束100纳秒 Then
                起点 = 首次显示100纳秒
                终点 = 最佳.最晚结束100纳秒
            End If
            If 位置 >= 终点 Then Return (String.Empty, String.Empty)
            推理开始计时 = 最佳.推理开始计时
            If 最佳.后续页面.Count = 0 Then Return (最佳.文本, 最佳.副文本)
            ' 仅旧 worker 使用均分分页；新 worker 已独立安排每个短句的起止时间。
            Dim 可读区间 = Math.Max(1.0, (终点 - 起点) / (最佳.后续页面.Count + 1))
            Dim 页 = CInt(Math.Clamp(Math.Floor((CDbl(位置) - 起点) / 可读区间),
                                     0, 最佳.后续页面.Count))
            If 页 > 0 Then Return 最佳.后续页面(页 - 1)
            Return (最佳.文本, 最佳.副文本)
        End SyncLock
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        SyncLock 锁
            已释放 = True
            队列.Clear()
            当前条目 = Nothing
        End SyncLock
    End Sub
End Class
