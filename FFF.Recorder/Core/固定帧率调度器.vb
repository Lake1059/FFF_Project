Imports System.Diagnostics
Imports System.Threading

Public NotInheritable Class 帧率调度错误事件参数
    Inherits EventArgs

    Public Sub New(错误 As Exception)
        异常 = 错误
    End Sub

    Public ReadOnly Property 异常 As Exception
End Class

Public NotInheritable Class 固定帧率调度器
    Implements IDisposable

    Private ReadOnly 会话 As 录制会话
    Private ReadOnly 图形 As 图形设备
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

    Public Sub New(录制会话 As 录制会话, 图形设备 As 图形设备,
        输出帧率分子 As UInteger, 输出帧率分母 As UInteger)
        ArgumentNullException.ThrowIfNull(录制会话)
        ArgumentNullException.ThrowIfNull(图形设备)
        If 输出帧率分子 = 0 OrElse 输出帧率分母 = 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(输出帧率分子), "输出帧率必须大于零。")
        End If
        会话 = 录制会话
        图形 = 图形设备
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
                .Name = "FFF CFR 调度"
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
                会话.报告丢弃视频帧(1UI)
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
                Dim 使用新帧 As Boolean
                SyncLock 同步锁
                    使用新帧 = 最新帧 IsNot Nothing
                    If 最新帧 IsNot Nothing Then
                        当前帧?.释放()
                        当前帧 = 最新帧
                        最新帧 = Nothing
                    End If
                    待提交 = 当前帧
                End SyncLock
                If 待提交 IsNot Nothing Then
                    Dim 重复帧 = Not 使用新帧
                    图形.执行图形命令(Sub() 会话.提交视频纹理(
                        待提交.原生纹理指针, 下个Tick, 0UI, 重复帧))
                End If

                下个Tick += 基础步长
                累计余数 += 余数步长
                If 累计余数 >= 帧率分子 Then
                    下个Tick += 累计余数 \ 帧率分子
                    累计余数 = 累计余数 Mod 帧率分子
                End If
            Loop
        Catch 错误 As Exception
            RaiseEvent 调度失败(Me, New 帧率调度错误事件参数(错误))
            SyncLock 同步锁
                请求停止 = True
            End SyncLock
        End Try
    End Sub

    Private Sub 确保未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub
End Class
