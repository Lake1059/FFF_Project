Option Strict On
Option Explicit On

Imports System.Collections.Concurrent

''' <summary>
''' AI 字幕状态机：收 worker 的识别结果，按"最新 wins + 陈旧守卫 + 代次清空"策略
''' 回答"此刻该显示什么文本"。
'''
''' 为什么是这三条策略（docs/ai-subtitles/IMPLEMENTATION_ANALYSIS.md §7）：
'''   * latest-wins：批式推理必然滞后于画面（5s 窗 + 0.2s 推理 ≈ 1.2~2.7s），
'''     新条目到达即成为当前文本，显示到下一条到达——这是物理下限，不是缺陷；
'''   * 陈旧守卫：条目的媒体结束时间 + 2×窗口仍早于播放位置 → 隐藏。推理卡住时
'''     字幕不会僵在屏上（用户红线：ASR 失败绝不能表现为播放卡顿或鬼影字幕）；
'''   * 代次跳变：seek 作废在途结果，立即清空，不等 worker 确认。
'''
''' 并发契约：result 线程只调 <see cref="提交结果"/>；PrecisionTimer 线程每帧只调
''' <see cref="取当前条目"/>。所有 public 方法绝不抛异常——字幕是可选图层，任何
''' 失败只当"本帧没有 AI 字幕"（若把异常抛进字幕呈现器，整帧图层都不提交，
''' 外部字幕也会跟着消失，那是不可接受的连带损伤）。
''' </summary>
Friend NotInheritable Class Ai字幕状态机
    Implements IDisposable

    ''' <summary>一条识别结果。时间是媒体时间（100ns 单位），与墙钟无关。</summary>
    Public NotInheritable Class Ai字幕条目
        Public ReadOnly Property 代次 As ULong
        Public ReadOnly Property 起始100纳秒 As Long
        Public ReadOnly Property 结束100纳秒 As Long
        Public ReadOnly Property 文本 As String
        Public ReadOnly Property 语言 As String

        Public Sub New(代次 As ULong, 起始100纳秒 As Long, 结束100纳秒 As Long,
                       文本 As String, 语言 As String)
            Me.代次 = 代次
            Me.起始100纳秒 = 起始100纳秒
            Me.结束100纳秒 = 结束100纳秒
            Me.文本 = If(文本, String.Empty)
            Me.语言 = If(语言, String.Empty)
        End Sub
    End Class

    Private ReadOnly 收件箱 As New BlockingCollection(Of Ai字幕条目)(128)

    ' 当前条目与其代次。锁只护这两个字段的替换，临界区 < 1μs。
    Private ReadOnly 锁 As New Object()
    Private 当前条目 As Ai字幕条目 = Nothing
    Private 当前条目代次 As ULong = 0UL
    Private 已见代次 As Boolean = False

    ' worker 生命周期信号（由管道在 worker 退出时置位）。
    Private _worker已退出 As Boolean = False

    ''' <summary>条目过期余量：媒体结束时间 + 2×窗口 仍早于播放位置则隐藏。</summary>
    Public Property 陈旧余量100纳秒 As Long = 10L * 10_000_000L   ' 默认 2×5s 窗口

    ''' <summary>跨时间条守卫：条目起始时间比播放位置还超前 3×窗口 → 它属于另一条
    ''' 时间线（切文件/切列表项时 worker 在途的旧结果），隐藏并清空。
    ''' 正常 latest-wins 下条目起始只可能略早于播放位置（推理滞后），不会误触。</summary>
    Public Property 未来余量100纳秒 As Long = 15L * 10_000_000L   ' 默认 3×5s 窗口

    Public Sub 提交结果(条目 As Ai字幕条目)
        If 条目 Is Nothing Then Return
        Try
            If Not 收件箱.TryAdd(条目, 50) Then
                ' 消费端从未读取（状态机未接入呈现循环）时丢弃最旧的，绝不阻塞调用方。
                Try
                    收件箱.Take()
                    收件箱.TryAdd(条目, 0)
                Catch
                End Try
            End If
        Catch
            ' 状态机是可选功能：提交失败只当本帧没有 AI 字幕。
        End Try
    End Sub

    ''' <summary>worker 退出通知：此后 worker 在途的一切结果作废，显示立即清空。</summary>
    Public Sub 通知Worker已退出()
        _worker已退出 = True
        SyncLock 锁
            当前条目 = Nothing
        End SyncLock
    End Sub

    ''' <summary>
    ''' 撤销"worker 已退出"。**每次起 worker 前必须调用**（<see cref="重置"/> 已包含）。
    '''
    ''' 这是一个曾经吞掉整个功能的陷阱：标志只置位、从没复位过，而
    ''' <see cref="取当前条目"/> 进门就看它。于是"停止一次 / 关开关再开 /
    ''' 换推理后端"（三条都要先停管道，停管道必调 <see cref="通知Worker已退出"/>）
    ''' 之后，进程余生所有结果都被丢弃，字幕永久空白，只有重启播放器能救。
    ''' </summary>
    Public Sub 复位Worker退出()
        _worker已退出 = False
    End Sub

    ''' <summary>重置（切文件/切列表项/起 worker 前调用）：清空当前条目与在途结果，
    ''' 并复位 worker 退出标志——调用它的场合都意味着"要重新开始收结果"。</summary>
    Public Sub 重置()
        Try
            While True
                Dim 丢弃 As Ai字幕条目 = Nothing
                Try
                    If Not 收件箱.TryTake(丢弃) Then Exit While
                Catch
                    Exit While
                End Try
            End While
        Catch
            ' 重置失败不影响播放；下一条目的守卫会兜住旧结果。
        End Try
        SyncLock 锁
            当前条目 = Nothing
            已见代次 = False
        End SyncLock
        ' 清在途结果的同时把"worker 已死"也清掉：调用方（停止后的重启、切文件、
        ' 起 worker）都在等下一批结果，标志留着就会把它们全部丢掉。
        复位Worker退出()
    End Sub

    ''' <summary>处理积压的结果队列。由呈现循环每帧调用（同一线程），保持无锁读路径。</summary>
    Public Sub 处理待决结果()
        While True
            Dim 条目 As Ai字幕条目 = Nothing
            Try
                If Not 收件箱.TryTake(条目) Then Return
            Catch
                Return
            End Try
            If 条目 Is Nothing OrElse _worker已退出 Then Continue While
            SyncLock 锁
                当前条目 = 条目
                当前条目代次 = 条目.代次
                已见代次 = True
            End SyncLock
        End While
    End Sub

    ''' <summary>取当前应显示的文本；无有效条目返回空串。只在 PrecisionTimer 线程调用。</summary>
    Public Function 取当前条目(播放位置 As TimeSpan, 时间轴代次 As ULong) As String
        Try
            处理待决结果()
            If _worker已退出 Then Return String.Empty
            SyncLock 锁
                If 当前条目 Is Nothing OrElse Not 已见代次 Then Return String.Empty
                If 时间轴代次 <> 0UL AndAlso 当前条目代次 <> 时间轴代次 Then
                    ' seek 已作废这批结果：立即清空，不等 worker 确认。
                    当前条目 = Nothing
                    Return String.Empty
                End If
                If String.IsNullOrWhiteSpace(当前条目.文本) Then Return String.Empty
                Dim 播放位置100纳秒 = CLng(播放位置.Ticks)
                ' 跨时间条守卫：切文件后 worker 在途的旧结果（起始远大播放位置）。
                If 当前条目.起始100纳秒 - 播放位置100纳秒 > 未来余量100纳秒 Then
                    当前条目 = Nothing
                    Return String.Empty
                End If
                ' 陈旧守卫：推理卡住时字幕不得僵在屏上。
                If 当前条目.结束100纳秒 + 陈旧余量100纳秒 < 播放位置100纳秒 Then
                    当前条目 = Nothing
                    Return String.Empty
                End If
                Return 当前条目.文本
            End SyncLock
        Catch
            ' 任何意外都只当"本帧没有 AI 字幕"。
            Return String.Empty
        End Try
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        Try
            收件箱.Dispose()
        Catch
        End Try
        GC.SuppressFinalize(Me)
    End Sub
End Class
