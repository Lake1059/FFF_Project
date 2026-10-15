Imports System.Diagnostics
Imports System.Threading

Friend Module 录制统计助手
    Friend Sub 安全报告丢帧(会话 As 录制会话, Optional 数量 As UInteger = 1UI)
        安全报告丢帧(Sub(帧数) 会话.报告丢弃视频帧(帧数), 数量)
    End Sub

    Friend Sub 安全报告丢帧(报告丢帧 As Action(Of UInteger), Optional 数量 As UInteger = 1UI)
        Try
            报告丢帧.Invoke(数量)
        Catch
            ' 统计上报可能与暂停/停止交错，不能因此中断实时线程。
        End Try
    End Sub
End Module

Public NotInheritable Class 帧率调度错误事件参数
    Inherits EventArgs

    Public Sub New(错误 As Exception)
        异常 = 错误
    End Sub

    Public ReadOnly Property 异常 As Exception
End Class

Public NotInheritable Class 固定帧率调度器
    Implements IDisposable

    Private ReadOnly 提交视频纹理 As Action(Of IntPtr, Long, UInteger, Boolean)
    Private ReadOnly 报告丢弃视频帧 As Action(Of UInteger)
    Private ReadOnly 帧率分子 As UInteger
    Private ReadOnly 帧率分母 As UInteger
    Private ReadOnly 同步锁 As New Object
    Private ReadOnly 唤醒事件 As New AutoResetEvent(False)
    Private 最新帧 As 处理后视频帧
    Private 当前帧 As 处理后视频帧
    Private 工作线程 As Thread
    Private 请求停止 As Boolean
    Private 停止时间戳 As Long
    Private 调度起始时间戳 As Long
    Private 停止后保留当前帧 As Boolean
    Private 已启动 As Boolean
    Private 已释放 As Boolean

    Public Sub New(录制会话 As 录制会话, 输出帧率分子 As UInteger, 输出帧率分母 As UInteger)
        ArgumentNullException.ThrowIfNull(录制会话)
        If 输出帧率分子 = 0 OrElse 输出帧率分母 = 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(输出帧率分子), "输出帧率必须大于零。")
        End If
        提交视频纹理 = Sub(纹理指针, 时间戳, 数组索引, 重复帧) 录制会话.提交视频纹理(纹理指针, 时间戳, 数组索引, 重复帧)
        报告丢弃视频帧 = Sub(帧数) 录制会话.报告丢弃视频帧(帧数)
        帧率分子 = 输出帧率分子
        帧率分母 = 输出帧率分母
    End Sub

    Friend Sub New(提交视频 As Action(Of IntPtr, Long, UInteger, Boolean),
        报告丢帧 As Action(Of UInteger), 输出帧率分子 As UInteger, 输出帧率分母 As UInteger)
        ArgumentNullException.ThrowIfNull(提交视频)
        ArgumentNullException.ThrowIfNull(报告丢帧)
        If 输出帧率分子 = 0 OrElse 输出帧率分母 = 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(输出帧率分子), "输出帧率必须大于零。")
        End If
        提交视频纹理 = 提交视频
        报告丢弃视频帧 = 报告丢帧
        帧率分子 = 输出帧率分子
        帧率分母 = 输出帧率分母
    End Sub

    Public Event 调度失败 As EventHandler(Of 帧率调度错误事件参数)

    Public Sub 开始(Optional 起始QPC时间戳 As Long = 0)
        确保未释放()
        If 起始QPC时间戳 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(起始QPC时间戳))
        SyncLock 同步锁
            If 已启动 Then Throw New InvalidOperationException("固定帧率调度器已经启动。")
            请求停止 = False
            停止时间戳 = 0
            调度起始时间戳 = 起始QPC时间戳
            停止后保留当前帧 = False
            工作线程 = New Thread(AddressOf 运行调度循环) With {
                .IsBackground = True,
                .Name = "FFF CFR 调度",
                .Priority = ThreadPriority.AboveNormal
            }
            已启动 = True
            工作线程.Start()
        End SyncLock
    End Sub

    Public Sub 提交帧(帧 As 处理后视频帧)
        ArgumentNullException.ThrowIfNull(帧)
        确保未释放()
        SyncLock 同步锁
            If Not 已启动 Then
                帧.释放()
                Throw New InvalidOperationException("固定帧率调度器尚未启动。")
            End If
            If 最新帧 IsNot Nothing Then
                最新帧.释放()
            End If
            最新帧 = 帧
        End SyncLock
        唤醒事件.Set()
    End Sub

    Public Sub 停止(Optional 结束QPC时间戳 As Long = 0, Optional 保留当前帧 As Boolean = False)
        If 结束QPC时间戳 <= 0 Then 结束QPC时间戳 = Stopwatch.GetTimestamp()
        Dim 待等待线程 As Thread
        SyncLock 同步锁
            If Not 已启动 Then
                If Not 保留当前帧 Then
                    最新帧?.释放()
                    最新帧 = Nothing
                    当前帧?.释放()
                    当前帧 = Nothing
                End If
                Return
            End If
            请求停止 = True
            停止时间戳 = 结束QPC时间戳
            停止后保留当前帧 = 保留当前帧
            待等待线程 = 工作线程
        End SyncLock
        唤醒事件.Set()
        If 待等待线程 IsNot Thread.CurrentThread Then 待等待线程.Join()
        SyncLock 同步锁
            已启动 = False
            工作线程 = Nothing
            最新帧?.释放()
            最新帧 = Nothing
            If Not 停止后保留当前帧 Then
                当前帧?.释放()
                当前帧 = Nothing
            End If
            停止后保留当前帧 = False
        End SyncLock
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        停止()
        SyncLock 同步锁
            最新帧?.释放()
            最新帧 = Nothing
            当前帧?.释放()
            当前帧 = Nothing
        End SyncLock
        唤醒事件.Dispose()
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 运行调度循环()
        Try
            Dim 下个Tick As Long
            Dim 基础步长 = Stopwatch.Frequency * CLng(帧率分母) \ CLng(帧率分子)
            Dim 余数步长 = Stopwatch.Frequency * CLng(帧率分母) Mod CLng(帧率分子)
            Dim 累计余数 As Long
            Do
                Dim 正在停止 As Boolean
                Dim 截止时间戳 As Long
                SyncLock 同步锁
                    正在停止 = 请求停止
                    截止时间戳 = 停止时间戳
                    If 下个Tick = 0 Then
                        If 当前帧 IsNot Nothing AndAlso 调度起始时间戳 > 0 Then
                            下个Tick = 调度起始时间戳
                        ElseIf 最新帧 IsNot Nothing Then
                            下个Tick = If(调度起始时间戳 > 0, 调度起始时间戳, 最新帧.QPC时间戳)
                        End If
                    End If
                End SyncLock
                If 下个Tick = 0 Then
                    If 正在停止 Then Exit Do
                    唤醒事件.WaitOne(20)
                    Continue Do
                End If
                If 正在停止 AndAlso 下个Tick > 截止时间戳 Then Exit Do

                Dim 当前时间 = Stopwatch.GetTimestamp()
                If Not 正在停止 AndAlso 当前时间 < 下个Tick Then
                    Dim 剩余毫秒 = CInt(Math.Max(0, (下个Tick - 当前时间) * 1000 \ Stopwatch.Frequency - 1))
                    唤醒事件.WaitOne(Math.Min(剩余毫秒, 20))
                    Continue Do
                End If

                Dim 待提交 As 处理后视频帧
                Dim 使用新帧 As Boolean = False
                SyncLock 同步锁
                    ' The capture timestamp can legitimately be a few milliseconds
                    ' ahead of the output tick because WGC delivery and GPU
                    ' processing happen asynchronously.  Gating on QPC here lets
                    ' the single-slot queue overwrite every frame before it is
                    ' considered usable, producing long visible freezes.  A frame
                    ' that has arrived is the newest real frame available for this
                    ' tick; when none has arrived we intentionally repeat 当前帧.
                    If 最新帧 IsNot Nothing Then
                        当前帧?.释放()
                        当前帧 = 最新帧
                        最新帧 = Nothing
                        使用新帧 = True
                    End If
                    待提交 = 当前帧
                End SyncLock
                If 待提交 IsNot Nothing Then
                    Dim 重复帧 = Not 使用新帧
                    提交视频纹理.Invoke(待提交.原生纹理指针, 下个Tick, 0UI, 重复帧)
                End If

                推进Tick(下个Tick, 累计余数, 基础步长, 余数步长)
            Loop
        Catch 错误 As Exception
            RaiseEvent 调度失败(Me, New 帧率调度错误事件参数(错误))
            SyncLock 同步锁
                请求停止 = True
            End SyncLock
        End Try
    End Sub

    Private Sub 推进Tick(ByRef Tick As Long, ByRef 累计余数 As Long,
        基础步长 As Long, 余数步长 As Long)
        Tick += 基础步长
        累计余数 += 余数步长
        If 累计余数 >= 帧率分子 Then
            Tick += 累计余数 \ 帧率分子
            累计余数 = 累计余数 Mod 帧率分子
        End If
    End Sub

    Private Sub 确保未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub

End Class

Public NotInheritable Class 可变帧率编码器
    Implements IDisposable

    Private ReadOnly 提交视频纹理 As Action(Of IntPtr, Long, UInteger, Boolean)
    Private ReadOnly 报告丢弃视频帧 As Action(Of UInteger)
    Private ReadOnly 同步锁 As New Object
    Private ReadOnly 唤醒事件 As New AutoResetEvent(False)
    Private 最新帧 As 处理后视频帧
    Private 最新帧时间戳 As Long
    Private 工作线程 As Thread
    Private 请求停止 As Boolean
    Private 已启动 As Boolean
    Private 已释放 As Boolean

    Public Sub New(录制会话 As 录制会话)
        ArgumentNullException.ThrowIfNull(录制会话)
        提交视频纹理 = Sub(纹理指针, 时间戳, 数组索引, 重复帧) 录制会话.提交视频纹理(纹理指针, 时间戳, 数组索引, 重复帧)
        报告丢弃视频帧 = Sub(帧数) 录制会话.报告丢弃视频帧(帧数)
    End Sub

    Friend Sub New(提交视频 As Action(Of IntPtr, Long, UInteger, Boolean),
        报告丢帧 As Action(Of UInteger))
        ArgumentNullException.ThrowIfNull(提交视频)
        ArgumentNullException.ThrowIfNull(报告丢帧)
        提交视频纹理 = 提交视频
        报告丢弃视频帧 = 报告丢帧
    End Sub

    Public Event 编码失败 As EventHandler(Of 帧率调度错误事件参数)

    Public Sub 开始()
        确保未释放()
        SyncLock 同步锁
            If 已启动 Then Throw New InvalidOperationException("可变帧率编码器已经启动。")
            请求停止 = False
            工作线程 = New Thread(AddressOf 运行编码循环) With {
                .IsBackground = True,
                .Name = "FFF VFR 编码",
                .Priority = ThreadPriority.AboveNormal
            }
            已启动 = True
            工作线程.Start()
        End SyncLock
    End Sub

    Public Sub 提交帧(帧 As 处理后视频帧, Optional 提交QPC时间戳 As Long = 0)
        ArgumentNullException.ThrowIfNull(帧)
        确保未释放()
        If 提交QPC时间戳 <= 0 Then 提交QPC时间戳 = 帧.QPC时间戳
        Dim 丢弃一帧 As Boolean
        SyncLock 同步锁
            If Not 已启动 OrElse 请求停止 Then
                帧.释放()
                Return
            End If
            If 最新帧 IsNot Nothing Then
                最新帧.释放()
                丢弃一帧 = True
            End If
            最新帧 = 帧
            最新帧时间戳 = 提交QPC时间戳
        End SyncLock
        If 丢弃一帧 Then 安全报告丢帧(报告丢弃视频帧)
        唤醒事件.Set()
    End Sub

    Public Sub 停止()
        Dim 待等待线程 As Thread
        SyncLock 同步锁
            If Not 已启动 Then Return
            请求停止 = True
            待等待线程 = 工作线程
        End SyncLock
        唤醒事件.Set()
        If 待等待线程 IsNot Thread.CurrentThread Then 待等待线程.Join()
        SyncLock 同步锁
            最新帧?.释放()
            最新帧 = Nothing
            最新帧时间戳 = 0
            工作线程 = Nothing
            已启动 = False
        End SyncLock
    End Sub

    Private Sub 运行编码循环()
        Try
            Do
                Dim 待编码 As 处理后视频帧 = Nothing
                Dim 待编码时间戳 As Long = 0
                SyncLock 同步锁
                    If 最新帧 IsNot Nothing Then
                        待编码 = 最新帧
                        待编码时间戳 = 最新帧时间戳
                        最新帧 = Nothing
                        最新帧时间戳 = 0
                    ElseIf 请求停止 Then
                        Exit Do
                    End If
                End SyncLock
                If 待编码 Is Nothing Then
                    唤醒事件.WaitOne(20)
                    Continue Do
                End If
                Try
                    提交视频纹理.Invoke(待编码.原生纹理指针, 待编码时间戳, 0UI, False)
                Finally
                    待编码.释放()
                End Try
            Loop
        Catch 错误 As Exception
            RaiseEvent 编码失败(Me, New 帧率调度错误事件参数(错误))
            SyncLock 同步锁
                请求停止 = True
            End SyncLock
        End Try
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        停止()
        唤醒事件.Dispose()
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 确保未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub

End Class
