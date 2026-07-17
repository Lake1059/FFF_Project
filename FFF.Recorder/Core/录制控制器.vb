Imports System.Diagnostics
Imports System.Threading

Public NotInheritable Class 录制失败事件参数
    Inherits EventArgs

    Public Sub New(错误 As Exception)
        异常 = 错误
    End Sub

    Public ReadOnly Property 异常 As Exception
End Class

Public NotInheritable Class 录制控制器
    Implements IDisposable

    Private ReadOnly 同步锁 As New Object
    Private ReadOnly 配置值 As 录制配置
    Private ReadOnly 处理器 As 视频处理器
    Private ReadOnly 会话 As 录制会话
    Private ReadOnly 调度器 As 固定帧率调度器
    Private ReadOnly 显示器捕获 As 显示器捕获器
    Private ReadOnly 窗口捕获 As 窗口捕获器
    Private ReadOnly 捕获光标值 As Boolean
    Private ReadOnly 首帧事件 As New ManualResetEventSlim(False)
    Private 显示器线程 As Thread
    Private 待提交首帧 As 处理后视频帧
    Private 首帧错误 As Exception
    Private 请求停止 As Boolean
    Private 已开始值 As Boolean
    Private 已暂停值 As Boolean
    Private 已停止值 As Boolean
    Private 已释放 As Boolean
    Private 已报告失败 As Boolean

    Private Sub New(配置 As 录制配置, 视频配置 As 视频处理配置, 捕获器 As 显示器捕获器)
        配置值 = 配置
        显示器捕获 = 捕获器
        捕获器.应用到配置(配置)
        处理器 = New 视频处理器(捕获器.设备, 视频配置)
        处理器.应用到配置(配置)
        配置.验证()
        会话 = 录制引擎.创建会话(配置, 捕获器.设备.原生设备指针)
        AddHandler 会话.收到诊断事件, AddressOf 转发诊断事件
        捕获器.绑定诊断会话(会话)
        调度器 = New 固定帧率调度器(会话, 捕获器.设备, 配置.帧率分子, 配置.帧率分母)
        AddHandler 调度器.调度失败, AddressOf 处理调度失败
    End Sub

    Private Sub New(配置 As 录制配置, 视频配置 As 视频处理配置, 捕获器 As 窗口捕获器,
        捕获光标 As Boolean)
        配置值 = 配置
        窗口捕获 = 捕获器
        捕获光标值 = 捕获光标
        捕获器.应用到配置(配置)
        处理器 = New 视频处理器(捕获器.设备, 视频配置)
        处理器.应用到配置(配置)
        配置.验证()
        会话 = 录制引擎.创建会话(配置, 捕获器.设备.原生设备指针)
        AddHandler 会话.收到诊断事件, AddressOf 转发诊断事件
        捕获器.绑定诊断会话(会话)
        调度器 = New 固定帧率调度器(会话, 捕获器.设备, 配置.帧率分子, 配置.帧率分母)
        AddHandler 调度器.调度失败, AddressOf 处理调度失败
        AddHandler 捕获器.收到帧, AddressOf 处理窗口帧
        AddHandler 捕获器.捕获失败, AddressOf 处理窗口失败
        AddHandler 捕获器.捕获已关闭, AddressOf 处理窗口关闭
    End Sub

    Public Event 录制失败 As EventHandler(Of 录制失败事件参数)
    Public Event 收到诊断事件 As EventHandler(Of 录制诊断事件参数)

    Public Shared Function 创建显示器录制(显示器 As 显示器信息, 配置 As 录制配置,
        视频配置 As 视频处理配置, Optional 启用调试层 As Boolean = False) As 录制控制器
        ArgumentNullException.ThrowIfNull(显示器)
        ArgumentNullException.ThrowIfNull(配置)
        ArgumentNullException.ThrowIfNull(视频配置)
        视频配置.验证()
        Dim 请求HDR源 = 视频配置.输出HDR10 OrElse 视频配置.允许HDR转SDR
        Dim 捕获器 = 显示器捕获器.创建(显示器, 请求HDR源, 启用调试层)
        Try
            Return New 录制控制器(配置, 视频配置, 捕获器)
        Catch
            捕获器.释放()
            Throw
        End Try
    End Function

    Public Shared Function 创建窗口录制(窗口句柄 As IntPtr, 配置 As 录制配置,
        视频配置 As 视频处理配置, Optional 捕获光标 As Boolean = True) As 录制控制器
        ArgumentNullException.ThrowIfNull(配置)
        ArgumentNullException.ThrowIfNull(视频配置)
        视频配置.验证()
        Dim 请求HDR源 = 视频配置.输出HDR10 OrElse 视频配置.允许HDR转SDR
        Dim 捕获器 = 窗口捕获器.创建(窗口句柄, 请求HDR源)
        Try
            Return New 录制控制器(配置, 视频配置, 捕获器, 捕获光标)
        Catch
            捕获器.释放()
            Throw
        End Try
    End Function

    Public ReadOnly Property 录制配置 As 录制配置
        Get
            Return 配置值
        End Get
    End Property

    Public ReadOnly Property 已开始 As Boolean
        Get
            SyncLock 同步锁
                Return 已开始值
            End SyncLock
        End Get
    End Property

    Public ReadOnly Property 已暂停 As Boolean
        Get
            SyncLock 同步锁
                Return 已暂停值
            End SyncLock
        End Get
    End Property

    Public Sub 开始(Optional 首帧超时毫秒 As Integer = 3000)
        确保未释放()
        If 首帧超时毫秒 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(首帧超时毫秒))
        SyncLock 同步锁
            If 已开始值 Then Throw New InvalidOperationException("录制控制器已经开始。")
            请求停止 = False
            首帧错误 = Nothing
            已报告失败 = False
            首帧事件.Reset()
        End SyncLock

        If 窗口捕获 IsNot Nothing Then
            窗口捕获.开始(捕获光标值)
            If Not 首帧事件.Wait(首帧超时毫秒) Then
                窗口捕获.停止()
                Throw New TimeoutException("等待首个有效 WGC 帧超时。")
            End If
        Else
            待提交首帧 = 捕获首个显示器帧(首帧超时毫秒)
        End If
        If 首帧错误 IsNot Nothing Then
            窗口捕获?.停止()
            Throw New InvalidOperationException("首个捕获帧验证失败。", 首帧错误)
        End If
        If 待提交首帧 Is Nothing Then
            窗口捕获?.停止()
            Throw New InvalidOperationException("未获得可编码的首帧。")
        End If

        Try
            会话.开始()
            调度器.开始()
            Dim 首帧 As 处理后视频帧
            SyncLock 同步锁
                已开始值 = True
                已停止值 = False
                已暂停值 = False
                首帧 = 待提交首帧
                待提交首帧 = Nothing
            End SyncLock
            调度器.提交捕获帧(首帧)
            If 显示器捕获 IsNot Nothing Then
                显示器线程 = New Thread(AddressOf 运行显示器捕获) With {
                    .IsBackground = True, .Name = "FFF DXGI 捕获"
                }
                显示器线程.Start()
            End If
        Catch
            待提交首帧?.释放()
            待提交首帧 = Nothing
            窗口捕获?.停止()
            Throw
        End Try
    End Sub

    Public Sub 暂停()
        确保未释放()
        SyncLock 同步锁
            If Not 已开始值 OrElse 已暂停值 OrElse 已停止值 Then Throw New InvalidOperationException("当前状态不能暂停。")
            已暂停值 = True
        End SyncLock
        调度器.停止()
        会话.暂停()
        会话.记录诊断事件("controller_paused")
    End Sub

    Public Sub 恢复()
        确保未释放()
        SyncLock 同步锁
            If Not 已开始值 OrElse Not 已暂停值 OrElse 已停止值 Then Throw New InvalidOperationException("当前状态不能恢复。")
        End SyncLock
        会话.恢复()
        调度器.开始()
        SyncLock 同步锁
            已暂停值 = False
        End SyncLock
        会话.记录诊断事件("controller_resumed")
    End Sub

    Public Sub 停止()
        确保未释放()
        Dim 捕获线程 As Thread
        SyncLock 同步锁
            If 已停止值 Then Return
            请求停止 = True
            捕获线程 = 显示器线程
            已停止值 = True
        End SyncLock
        窗口捕获?.停止()
        If 捕获线程 IsNot Nothing AndAlso 捕获线程 IsNot Thread.CurrentThread Then 捕获线程.Join()
        调度器.停止()
        会话.停止()
        SyncLock 同步锁
            已暂停值 = False
        End SyncLock
    End Sub

    Public Function 读取统计() As 录制统计
        确保未释放()
        Return 会话.读取统计()
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        Try
            If 已开始值 AndAlso Not 已停止值 Then 停止()
        Finally
            RemoveHandler 调度器.调度失败, AddressOf 处理调度失败
            If 窗口捕获 IsNot Nothing Then
                RemoveHandler 窗口捕获.收到帧, AddressOf 处理窗口帧
                RemoveHandler 窗口捕获.捕获失败, AddressOf 处理窗口失败
                RemoveHandler 窗口捕获.捕获已关闭, AddressOf 处理窗口关闭
            End If
            待提交首帧?.释放()
            调度器.释放()
            RemoveHandler 会话.收到诊断事件, AddressOf 转发诊断事件
            会话.释放()
            处理器.释放()
            窗口捕获?.释放()
            显示器捕获?.释放()
            首帧事件.Dispose()
            已释放 = True
            GC.SuppressFinalize(Me)
        End Try
    End Sub

    Private Function 捕获首个显示器帧(超时毫秒 As Integer) As 处理后视频帧
        Dim 截止 = Stopwatch.GetTimestamp() + CLng(超时毫秒) * Stopwatch.Frequency \ 1000L
        Do While Stopwatch.GetTimestamp() < 截止
            Using 帧 = 显示器捕获.捕获一帧(50UI)
                If 帧 IsNot Nothing Then
                    Dim 结果 = 处理器.处理帧(帧)
                    If 结果 IsNot Nothing Then Return 结果
                End If
            End Using
        Loop
        Return Nothing
    End Function

    Private Sub 运行显示器捕获()
        Try
            Do
                Dim 当前暂停 As Boolean
                SyncLock 同步锁
                    If 请求停止 Then Exit Do
                    当前暂停 = 已暂停值
                End SyncLock
                If 当前暂停 Then
                    Thread.Sleep(10)
                    Continue Do
                End If
                Using 帧 = 显示器捕获.捕获一帧(50UI)
                    If 帧 Is Nothing Then Continue Do
                    Dim 处理帧 = 处理器.处理帧(帧)
                    If 处理帧 Is Nothing Then Continue Do
                    SyncLock 同步锁
                        ' 状态复核与提交必须原子化；否则暂停可能在取帧和提交之间停掉调度器。
                        If Not 已开始值 OrElse 已暂停值 OrElse 已停止值 Then
                            处理帧.释放()
                        Else
                            调度器.提交捕获帧(处理帧)
                        End If
                    End SyncLock
                End Using
            Loop
        Catch 错误 As Exception
            触发失败(错误)
        End Try
    End Sub

    Private Sub 处理窗口帧(发送者 As Object, 参数 As 窗口捕获帧事件参数)
        Try
            Using 帧 = 参数.帧
                Dim 处理帧 = 处理器.处理帧(帧)
                If 处理帧 Is Nothing Then Return
                SyncLock 同步锁
                    If Not 已开始值 Then
                        待提交首帧?.释放()
                        待提交首帧 = 处理帧
                        首帧事件.Set()
                        Return
                    End If
                    If 已暂停值 OrElse 已停止值 Then
                        处理帧.释放()
                        Return
                    End If
                    ' 与暂停/停止共用控制器锁，保证调度器停止后不会再出现迟到提交。
                    调度器.提交捕获帧(处理帧)
                End SyncLock
            End Using
        Catch 错误 As Exception
            首帧错误 = 错误
            首帧事件.Set()
            触发失败(错误)
        End Try
    End Sub

    Private Sub 处理窗口失败(发送者 As Object, 参数 As 窗口捕获错误事件参数)
        首帧错误 = 参数.异常
        首帧事件.Set()
        触发失败(参数.异常)
    End Sub

    Private Sub 处理窗口关闭(发送者 As Object, 参数 As EventArgs)
        Dim 错误 As New InvalidOperationException("目标窗口已关闭。")
        首帧错误 = 错误
        首帧事件.Set()
        触发失败(错误)
    End Sub

    Private Sub 处理调度失败(发送者 As Object, 参数 As 帧率调度错误事件参数)
        触发失败(参数.异常)
    End Sub

    Private Sub 转发诊断事件(发送者 As Object, 参数 As 录制诊断事件参数)
        RaiseEvent 收到诊断事件(Me, 参数)
    End Sub

    Private Sub 触发失败(错误 As Exception)
        SyncLock 同步锁
            If 已报告失败 Then Return
            已报告失败 = True
        End SyncLock
        Try
            会话.记录诊断事件("recording_controller_failed", 错误.Message)
        Catch
        End Try
        RaiseEvent 录制失败(Me, New 录制失败事件参数(错误))
        ThreadPool.QueueUserWorkItem(
            Sub()
                Try
                    If 已开始值 AndAlso Not 已停止值 Then 停止()
                Catch
                    Try
                        会话.强制中止()
                    Catch
                    End Try
                End Try
            End Sub)
    End Sub

    Private Sub 确保未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub
End Class
