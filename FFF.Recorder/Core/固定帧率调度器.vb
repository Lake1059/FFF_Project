Imports System.Diagnostics
Imports System.Threading

Friend Interface I自有捕获帧
    ReadOnly Property 纹理指针 As IntPtr
    Sub 释放()
End Interface

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
    Private 最新帧 As I自有捕获帧
    Private 当前帧 As I自有捕获帧
    Private 工作线程 As Thread
    Private 请求停止 As Boolean
    Private 已启动 As Boolean
    Private 已释放 As Boolean
    Private 已丢弃源帧数值 As ULong
    Private 已重复输出帧数值 As ULong

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

    Public ReadOnly Property 已丢弃源帧数 As ULong
        Get
            SyncLock 同步锁
                Return 已丢弃源帧数值
            End SyncLock
        End Get
    End Property

    Public ReadOnly Property 已重复输出帧数 As ULong
        Get
            SyncLock 同步锁
                Return 已重复输出帧数值
            End SyncLock
        End Get
    End Property

    Public Sub 开始()
        确保未释放()
        SyncLock 同步锁
            If 已启动 Then Throw New InvalidOperationException("固定帧率调度器已经启动。")
            请求停止 = False
            工作线程 = New Thread(AddressOf 运行调度循环) With {
                .IsBackground = True,
                .Name = "FFF CFR 调度"
            }
            已启动 = True
            工作线程.Start()
        End SyncLock
    End Sub

    Public Sub 提交捕获帧(帧 As 显示器捕获帧)
        ArgumentNullException.ThrowIfNull(帧)
        接收自有帧(帧)
    End Sub

    Public Sub 提交捕获帧(帧 As 窗口捕获帧)
        ArgumentNullException.ThrowIfNull(帧)
        接收自有帧(帧)
    End Sub

    Public Sub 提交捕获帧(帧 As 处理后视频帧)
        ArgumentNullException.ThrowIfNull(帧)
        接收自有帧(帧)
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
            已启动 = False
            工作线程 = Nothing
            最新帧?.释放()
            最新帧 = Nothing
            当前帧?.释放()
            当前帧 = Nothing
        End SyncLock
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        停止()
        唤醒事件.Dispose()
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 接收自有帧(帧 As I自有捕获帧)
        确保未释放()
        SyncLock 同步锁
            If Not 已启动 Then
                帧.释放()
                Throw New InvalidOperationException("固定帧率调度器尚未启动。")
            End If
            If 最新帧 IsNot Nothing Then
                最新帧.释放()
                已丢弃源帧数值 += 1UL
                会话.报告丢弃视频帧(1UI)
            End If
            最新帧 = 帧
        End SyncLock
        唤醒事件.Set()
    End Sub

    Private Sub 运行调度循环()
        Try
            Dim 下个Tick = Stopwatch.GetTimestamp()
            Dim 基础步长 = Stopwatch.Frequency * CLng(帧率分母) \ CLng(帧率分子)
            Dim 余数步长 = Stopwatch.Frequency * CLng(帧率分母) Mod CLng(帧率分子)
            Dim 累计余数 As Long
            Dim 上一Tick使用新帧 As Boolean
            Do
                SyncLock 同步锁
                    If 请求停止 Then Exit Do
                End SyncLock
                Dim 当前时间 = Stopwatch.GetTimestamp()
                If 当前时间 < 下个Tick Then
                    Dim 剩余毫秒 = CInt(Math.Max(0, (下个Tick - 当前时间) * 1000 \ Stopwatch.Frequency - 1))
                    唤醒事件.WaitOne(Math.Min(剩余毫秒, 20))
                    Continue Do
                End If

                Dim 待提交 As I自有捕获帧
                SyncLock 同步锁
                    上一Tick使用新帧 = 最新帧 IsNot Nothing
                    If 最新帧 IsNot Nothing Then
                        当前帧?.释放()
                        当前帧 = 最新帧
                        最新帧 = Nothing
                    ElseIf 当前帧 IsNot Nothing Then
                        已重复输出帧数值 += 1UL
                    End If
                    待提交 = 当前帧
                End SyncLock
                If 待提交 IsNot Nothing Then
                    Dim 重复帧 = Not 上一Tick使用新帧
                    图形.执行图形命令(Sub() 会话.提交视频纹理(
                        待提交.纹理指针, 下个Tick, 0UI, 重复帧))
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
