Imports System.Runtime.InteropServices
Imports System.Collections.Concurrent
Imports System.Text.Json
Imports System.Threading

Public NotInheritable Class 播放器会话
    Implements IDisposable

    Private NotInheritable Class 回调状态
        Public Property 目标 As WeakReference(Of 播放器会话)
    End Class

    Private Shared ReadOnly 共享原生回调 As 原生播放器回调 = AddressOf 接收原生事件
    Private ReadOnly 句柄 As 播放器原生句柄
    Private ReadOnly 同步上下文 As SynchronizationContext
    Private ReadOnly 释放取消源 As New CancellationTokenSource()
    Private ReadOnly 待处理事件 As New ConcurrentQueue(Of 播放器事件参数)()
    Private Const 定时文字UTF8缓存上限 As Integer = 2048
    Private Shared ReadOnly 原生定时文字命令大小 As UInteger = CUInt(Marshal.SizeOf(Of 原生定时文字命令)())
    Private ReadOnly 定时文字提交锁 As New Object()
    Private ReadOnly 定时文字UTF8缓存 As New Dictionary(Of String, IntPtr)(StringComparer.Ordinal)
    Private ReadOnly 定时文字临时指针 As New List(Of IntPtr)()
    Private ReadOnly 定时文字位图固定句柄 As New List(Of GCHandle)()
    Private 定时文字原生缓冲 As 原生定时文字命令() = Array.Empty(Of 原生定时文字命令)()
    Private 定时文字原生缓冲句柄 As GCHandle
    Private 已释放 As Integer
    Private 事件排程中 As Integer

    Public Sub New(配置 As 播放器配置)
        ArgumentNullException.ThrowIfNull(配置)
        配置.验证()
        If 播放器原生接口.FFF3FP_GetApiVersion() <> 4UI Then Throw New InvalidOperationException("FFF.Native 的 3FP API 版本不兼容。")
        同步上下文 = 配置.事件同步上下文
        Dim 状态 = New 回调状态()
        Dim 回调句柄 = GCHandle.Alloc(状态)
        Dim 端点指针 = IntPtr.Zero
        Try
            If Not String.IsNullOrEmpty(配置.音频端点标识) Then 端点指针 = Marshal.StringToCoTaskMemUTF8(配置.音频端点标识)
            Dim 原生配置 As New 原生播放器配置 With {
                .大小 = CUInt(Marshal.SizeOf(Of 原生播放器配置)()), .版本 = 4UI,
                .输出窗口 = 配置.输出窗口句柄, .解码器 = CUInt(配置.解码器),
                .色彩模式 = CUInt(配置.色彩模式), .SDR峰值 = 配置.SDR峰值尼特,
                .HDR峰值 = 配置.HDR峰值尼特, .SDR纸白 = 配置.SDR纸白尼特,
                .音频端点UTF8 = 端点指针,
                .回调 = Marshal.GetFunctionPointerForDelegate(共享原生回调),
                .回调上下文 = GCHandle.ToIntPtr(回调句柄)
            }
            Dim 原生指针 = IntPtr.Zero
            Dim 结果 = 播放器原生接口.FFF3FP_Create(原生配置, 原生指针)
            If 结果 <> 原生播放器结果.成功 Then Throw New 播放器异常(CInt(结果), "创建 3FP 播放器会话失败。")
            句柄 = New 播放器原生句柄(原生指针, 回调句柄)
            状态.目标 = New WeakReference(Of 播放器会话)(Me)
        Catch
            If 回调句柄.IsAllocated Then 回调句柄.Free()
            Throw
        Finally
            If 端点指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(端点指针)
        End Try
    End Sub

    Public Event 播放器事件 As EventHandler(Of 播放器事件参数)
    Public Event 状态变化 As EventHandler(Of 播放器事件参数)
    Public Event 打开完成 As EventHandler(Of 播放器事件参数)
    Public Event 操作完成 As EventHandler(Of 播放器事件参数)
    Public Event 播放结束 As EventHandler(Of 播放器事件参数)
    Public Event 错误 As EventHandler(Of 播放器事件参数)
    Public Event 色彩模式变化 As EventHandler(Of 播放器事件参数)
    Public Event 设备变化 As EventHandler(Of 播放器事件参数)

    Public Sub 打开(本地路径 As String)
        调用路径(AddressOf 播放器原生接口.FFF3FP_Open, 本地路径)
    End Sub

    Public Function 打开Async(本地路径 As String, Optional 取消标记 As CancellationToken = Nothing) As Task
        If 取消标记.IsCancellationRequested Then Return Task.FromCanceled(取消标记)
        Dim 完成源 As New TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously)
        Dim 联合取消 = CancellationTokenSource.CreateLinkedTokenSource(取消标记, 释放取消源.Token)
        Dim 取消注册 As CancellationTokenRegistration
        Dim 已结束 As Integer
        Dim 完成处理 As EventHandler(Of 播放器事件参数) = Nothing
        Dim 错误处理 As EventHandler(Of 播放器事件参数) = Nothing
        Dim 清理 As Action = Sub()
                               RemoveHandler 打开完成, 完成处理
                               RemoveHandler 错误, 错误处理
                               取消注册.Unregister()
                               联合取消.Dispose()
                           End Sub
        完成处理 = Sub(sender, e)
                   If Interlocked.Exchange(已结束, 1) <> 0 Then Return
                   完成源.TrySetResult()
                   清理()
               End Sub
        错误处理 = Sub(sender, e)
                   If Interlocked.Exchange(已结束, 1) <> 0 Then Return
                   完成源.TrySetException(New 播放器异常(-1, 读取事件消息(e.详情JSON)))
                   清理()
               End Sub
        AddHandler 打开完成, 完成处理
        AddHandler 错误, 错误处理
        取消注册 = 联合取消.Token.Register(
            Sub()
                If Interlocked.Exchange(已结束, 1) <> 0 Then Return
                完成源.TrySetCanceled(If(取消标记.IsCancellationRequested, 取消标记, 联合取消.Token))
                If Volatile.Read(已释放) = 0 Then
                    Try
                        播放器原生接口.FFF3FP_Close(取得句柄())
                    Catch
                    End Try
                End If
                清理()
            End Sub)
        Try
            打开(本地路径)
        Catch
            If Interlocked.Exchange(已结束, 1) = 0 Then 清理()
            Throw
        End Try
        Return 完成源.Task
    End Function

    Public Sub 播放()
        检查结果(播放器原生接口.FFF3FP_Play(取得句柄()))
    End Sub
    Public Sub 暂停()
        检查结果(播放器原生接口.FFF3FP_Pause(取得句柄()))
    End Sub
    Public Sub 停止()
        检查结果(播放器原生接口.FFF3FP_Stop(取得句柄()))
    End Sub
    Public Sub 关闭媒体()
        检查结果(播放器原生接口.FFF3FP_Close(取得句柄()))
    End Sub
    Public Sub 跳转(位置 As TimeSpan)
        If 位置 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(位置))
        检查结果(播放器原生接口.FFF3FP_Seek(取得句柄(), 位置.Ticks))
    End Sub
    Public Sub 跳转到关键帧(位置 As TimeSpan)
        If 位置 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(位置))
        检查结果(播放器原生接口.FFF3FP_SeekKeyframe(取得句柄(), 位置.Ticks))
    End Sub
    Public Sub 跳转到帧(帧序号 As Long)
        If 帧序号 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(帧序号))
        检查结果(播放器原生接口.FFF3FP_SeekFrame(取得句柄(), 帧序号))
    End Sub
    Public Sub 上一帧()
        检查结果(播放器原生接口.FFF3FP_StepFrame(取得句柄(), -1))
    End Sub
    Public Sub 下一帧()
        检查结果(播放器原生接口.FFF3FP_StepFrame(取得句柄(), 1))
    End Sub
    Public Sub 上一关键帧()
        检查结果(播放器原生接口.FFF3FP_StepKeyframe(取得句柄(), -1))
    End Sub
    Public Sub 下一关键帧()
        检查结果(播放器原生接口.FFF3FP_StepKeyframe(取得句柄(), 1))
    End Sub
    Public Sub 选择视频流(索引 As Integer)
        检查结果(播放器原生接口.FFF3FP_SelectVideoStream(取得句柄(), 索引))
    End Sub
    Public Sub 选择音频流(索引 As Integer)
        检查结果(播放器原生接口.FFF3FP_SelectAudioStream(取得句柄(), 索引))
    End Sub
    Public Sub 加载外部音轨(本地路径 As String, Optional 流索引 As Integer = -1, Optional 偏移 As TimeSpan = Nothing)
        调用路径(Function(h, p) 播放器原生接口.FFF3FP_LoadExternalAudio(h, p, 流索引, 偏移.Ticks), 本地路径)
    End Sub
    Public Sub 清除外部音轨()
        检查结果(播放器原生接口.FFF3FP_ClearExternalAudio(取得句柄()))
    End Sub
    Public Sub 设置外部音轨偏移(偏移 As TimeSpan)
        检查结果(播放器原生接口.FFF3FP_SetExternalAudioOffset(取得句柄(), 偏移.Ticks))
    End Sub
    Public Sub 设置色彩模式(模式 As 色彩输出模式, SDR峰值尼特 As Single, HDR峰值尼特 As Single, SDR纸白尼特 As Single)
        检查结果(播放器原生接口.FFF3FP_SetColorMode(取得句柄(), CUInt(模式), SDR峰值尼特, HDR峰值尼特, SDR纸白尼特))
    End Sub
    Public Sub 设置输出窗口(窗口句柄 As IntPtr)
        检查结果(播放器原生接口.FFF3FP_SetOutputWindow(取得句柄(), 窗口句柄))
    End Sub
    Public Sub 设置音频端点(端点标识 As String)
        调用路径(AddressOf 播放器原生接口.FFF3FP_SetAudioEndpoint, If(端点标识, String.Empty))
    End Sub
    Public Sub 设置音量(音量 As Single, Optional 静音 As Boolean = False)
        If Not Single.IsFinite(音量) OrElse 音量 < 0 OrElse 音量 > 1 Then Throw New ArgumentOutOfRangeException(NameOf(音量))
        检查结果(播放器原生接口.FFF3FP_SetVolume(取得句柄(), 音量, If(静音, 1UI, 0UI)))
    End Sub

    Public Sub 设置定时文字图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                           序号 As ULong, 目标帧率 As Single)
        设置定时文字图层核心(画布大小, 命令, 序号, 目标帧率, 原生定时文字图层槽位.字幕)
    End Sub

    Public Sub 设置弹幕图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                         序号 As ULong, 目标帧率 As Single)
        设置定时文字图层核心(画布大小, 命令, 序号, 目标帧率, 原生定时文字图层槽位.弹幕)
    End Sub

    Private Sub 设置定时文字图层核心(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                                序号 As ULong, 目标帧率 As Single,
                                图层槽位 As 原生定时文字图层槽位)
        If 画布大小.Width <= 0 OrElse 画布大小.Height <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(画布大小))
        If Not Single.IsFinite(目标帧率) OrElse 目标帧率 < 1.0F OrElse 目标帧率 > 240.0F Then
            Throw New ArgumentOutOfRangeException(NameOf(目标帧率))
        End If
        ArgumentNullException.ThrowIfNull(命令)
        SyncLock 定时文字提交锁
            ' 此缓冲和 UTF-8 指针只为同步 P/Invoke 提供稳定地址。原生层必须在返回前
            ' 深拷贝命令；不得保留这些地址。缓存有界，避免 60 Hz 弹幕提交反复分配。
            确保定时文字原生容量(命令.Count)
            定时文字临时指针.Clear()
            定时文字位图固定句柄.Clear()
            Try
                For index = 0 To 命令.Count - 1
                    Dim source = 命令(index)
                    ArgumentNullException.ThrowIfNull(source)
                    Dim value As New 原生定时文字命令 With {
                        .大小 = 原生定时文字命令大小, .版本 = 1UI,
                        .类型 = If(source.是位图, 原生定时文字命令类型.位图, 原生定时文字命令类型.文字),
                        .标志 = CType(CUInt(source.样式), 原生定时文字标志),
                        .X = source.X, .Y = source.Y, .宽度 = source.宽度, .高度 = source.高度,
                        .前景色ARGB = source.前景色ARGB, .描边色ARGB = source.描边色ARGB,
                        .字号 = source.字号, .描边宽度 = source.描边宽度,
                        .水平对齐 = CType(CUInt(source.水平对齐), 原生定时文字对齐),
                        .垂直对齐 = CType(CUInt(source.垂直对齐), 原生定时文字对齐),
                        .内容标识 = source.内容标识}
                    If source.是位图 Then
                        ArgumentNullException.ThrowIfNull(source.位图像素BGRA)
                        Dim pinned = GCHandle.Alloc(source.位图像素BGRA, GCHandleType.Pinned)
                        定时文字位图固定句柄.Add(pinned)
                        value.位图BGRA = pinned.AddrOfPinnedObject()
                        value.位图宽度 = CUInt(source.位图宽度)
                        value.位图高度 = CUInt(source.位图高度)
                        value.位图行跨度 = CUInt(source.位图行跨度)
                        value.位图字节数 = CUInt(source.位图像素BGRA.Length)
                    Else
                        value.文本UTF8 = 取得定时文字UTF8指针(If(source.文本, String.Empty))
                        value.字体UTF8 = 取得定时文字UTF8指针(If(source.字体, String.Empty))
                    End If
                    定时文字原生缓冲(index) = value
                Next
                Dim layer As New 原生定时文字图层 With {
                    .大小 = CUInt(Marshal.SizeOf(Of 原生定时文字图层)()), .版本 = 1UI,
                    .画布宽度 = CUInt(画布大小.Width), .画布高度 = CUInt(画布大小.Height),
                    .命令数 = CUInt(命令.Count), .图层槽位 = 图层槽位, .序号 = 序号,
                    .命令 = If(命令.Count = 0, IntPtr.Zero, 定时文字原生缓冲句柄.AddrOfPinnedObject()),
                    .目标帧率 = 目标帧率}
                检查结果(播放器原生接口.FFF3FP_SetTimedTextLayer(取得句柄(), layer))
            Finally
                For Each pinned In 定时文字位图固定句柄
                    If pinned.IsAllocated Then pinned.Free()
                Next
                For Each pointer In 定时文字临时指针
                    If pointer <> IntPtr.Zero Then Marshal.FreeCoTaskMem(pointer)
                Next
                定时文字位图固定句柄.Clear()
                定时文字临时指针.Clear()
            End Try
        End SyncLock
    End Sub

    Private Sub 确保定时文字原生容量(所需 As Integer)
        If 所需 <= 定时文字原生缓冲.Length Then Return
        Dim capacity = Math.Max(16, 定时文字原生缓冲.Length)
        While capacity < 所需
            capacity *= 2
        End While
        If 定时文字原生缓冲句柄.IsAllocated Then 定时文字原生缓冲句柄.Free()
        ReDim 定时文字原生缓冲(capacity - 1)
        定时文字原生缓冲句柄 = GCHandle.Alloc(定时文字原生缓冲, GCHandleType.Pinned)
    End Sub

    Private Function 取得定时文字UTF8指针(值 As String) As IntPtr
        Dim pointer As IntPtr
        If 定时文字UTF8缓存.TryGetValue(值, pointer) Then Return pointer
        pointer = Marshal.StringToCoTaskMemUTF8(值)
        If 定时文字UTF8缓存.Count < 定时文字UTF8缓存上限 Then
            定时文字UTF8缓存.Add(值, pointer)
        Else
            定时文字临时指针.Add(pointer)
        End If
        Return pointer
    End Function

    Public ReadOnly Property 当前定时文字状态 As 定时文字状态
        Get
            Dim 值 As New 原生定时文字状态 With {
                .大小 = CUInt(Marshal.SizeOf(Of 原生定时文字状态)()), .版本 = 1UI}
            检查结果(播放器原生接口.FFF3FP_GetTimedTextStatus(取得句柄(), 值))
            Return New 定时文字状态(值)
        End Get
    End Property

    Public ReadOnly Property 当前弹幕状态 As 定时文字状态
        Get
            Dim 值 As New 原生定时文字状态 With {
                .大小 = CUInt(Marshal.SizeOf(Of 原生定时文字状态)()), .版本 = 1UI}
            检查结果(播放器原生接口.FFF3FP_GetDanmakuStatus(取得句柄(), 值))
            Return New 定时文字状态(值)
        End Get
    End Property

    Public ReadOnly Property 当前快照 As 播放器快照
        Get
            Dim 值 As New 原生播放器快照 With {.大小 = CUInt(Marshal.SizeOf(Of 原生播放器快照)()), .版本 = 4UI}
            检查结果(播放器原生接口.FFF3FP_GetSnapshot(取得句柄(), 值))
            Return New 播放器快照(值)
        End Get
    End Property

    Public ReadOnly Property 当前媒体信息 As 媒体信息
        Get
            Dim JSON = 读取原生文本(AddressOf 播放器原生接口.FFF3FP_GetMediaInfo)
            If String.IsNullOrWhiteSpace(JSON) Then Return Nothing
            Return JsonSerializer.Deserialize(Of 媒体信息)(JSON)
        End Get
    End Property

    Public ReadOnly Property 最后错误消息 As String
        Get
            Return 读取原生文本(AddressOf 播放器原生接口.FFF3FP_GetLastError)
        End Get
    End Property

    Public Sub 释放() Implements IDisposable.Dispose
        If Interlocked.Exchange(已释放, 1) <> 0 Then Return
        释放取消源.Cancel()
        SyncLock 定时文字提交锁
            句柄.Dispose()
            If 定时文字原生缓冲句柄.IsAllocated Then 定时文字原生缓冲句柄.Free()
            For Each pointer In 定时文字UTF8缓存.Values
                If pointer <> IntPtr.Zero Then Marshal.FreeCoTaskMem(pointer)
            Next
            定时文字UTF8缓存.Clear()
            定时文字原生缓冲 = Array.Empty(Of 原生定时文字命令)()
        End SyncLock
        释放取消源.Dispose()
        Dim 忽略 As 播放器事件参数 = Nothing
        While 待处理事件.TryDequeue(忽略)
        End While
        GC.SuppressFinalize(Me)
    End Sub

    Private Delegate Function 路径调用(原生句柄 As 播放器原生句柄, 路径UTF8 As IntPtr) As 原生播放器结果
    Private Delegate Function 文本调用(原生句柄 As 播放器原生句柄, 输出 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果

    Private Sub 调用路径(调用 As 路径调用, 值 As String)
        ArgumentException.ThrowIfNullOrWhiteSpace(值)
        Dim 指针 = Marshal.StringToCoTaskMemUTF8(值)
        Try
            检查结果(调用(取得句柄(), 指针))
        Finally
            Marshal.FreeCoTaskMem(指针)
        End Try
    End Sub

    Private Function 读取原生文本(调用 As 文本调用) As String
        Dim 所需 As UInteger
        Dim 首次 = 调用(取得句柄(), IntPtr.Zero, 0UI, 所需)
        If 首次 <> 原生播放器结果.缓冲区不足 AndAlso 首次 <> 原生播放器结果.成功 Then 检查结果(首次)
        If 所需 <= 1 Then Return String.Empty
        Dim 缓冲区 = Marshal.AllocHGlobal(CInt(所需))
        Try
            检查结果(调用(取得句柄(), 缓冲区, 所需, 所需))
            Return Marshal.PtrToStringUTF8(缓冲区)
        Finally
            Marshal.FreeHGlobal(缓冲区)
        End Try
    End Function

    Private Function 取得句柄() As 播放器原生句柄
        ObjectDisposedException.ThrowIf(Volatile.Read(已释放) <> 0, Me)
        Return 句柄
    End Function

    Private Sub 检查结果(结果 As 原生播放器结果)
        If 结果 = 原生播放器结果.成功 Then Return
        Dim 消息 = $"3FP 操作失败（{CInt(结果)}）。"
        Try
            Dim 原生消息 = 读取原生文本(AddressOf 播放器原生接口.FFF3FP_GetLastError)
            If Not String.IsNullOrWhiteSpace(原生消息) Then 消息 = 原生消息
        Catch
        End Try
        Throw New 播放器异常(CInt(结果), 消息)
    End Sub

    Private Shared Sub 接收原生事件(上下文 As IntPtr, 事件类型 As UInteger, 详情UTF8 As IntPtr)
        Try
            Dim 状态 = TryCast(GCHandle.FromIntPtr(上下文).Target, 回调状态)
            Dim 目标 As 播放器会话 = Nothing
            If 状态?.目标 IsNot Nothing AndAlso 状态.目标.TryGetTarget(目标) Then
                目标.投递事件(New 播放器事件参数(CType(事件类型, 播放器事件类型), Marshal.PtrToStringUTF8(详情UTF8)))
            End If
        Catch
        End Try
    End Sub

    Private Sub 投递事件(参数 As 播放器事件参数)
        If Volatile.Read(已释放) <> 0 Then Return
        If 同步上下文 IsNot Nothing Then
            同步上下文.Post(Sub(state)
                                  If Volatile.Read(已释放) = 0 Then 引发事件(参数)
                              End Sub, Nothing)
            Return
        End If
        待处理事件.Enqueue(参数)
        If Interlocked.CompareExchange(事件排程中, 1, 0) = 0 Then
            ThreadPool.QueueUserWorkItem(Sub(state) 排空事件())
        End If
    End Sub

    Private Sub 排空事件()
        Do
            Dim 参数 As 播放器事件参数 = Nothing
            While Volatile.Read(已释放) = 0 AndAlso 待处理事件.TryDequeue(参数)
                Try
                    引发事件(参数)
                Catch
                End Try
            End While
            Interlocked.Exchange(事件排程中, 0)
            If 待处理事件.IsEmpty OrElse Interlocked.CompareExchange(事件排程中, 1, 0) <> 0 Then Exit Do
        Loop
    End Sub

    Private Sub 引发事件(参数 As 播放器事件参数)
        RaiseEvent 播放器事件(Me, 参数)
        Select Case 参数.类型
            Case 播放器事件类型.状态变化 : RaiseEvent 状态变化(Me, 参数)
            Case 播放器事件类型.打开完成 : RaiseEvent 打开完成(Me, 参数)
            Case 播放器事件类型.操作完成 : RaiseEvent 操作完成(Me, 参数)
            Case 播放器事件类型.播放结束 : RaiseEvent 播放结束(Me, 参数)
            Case 播放器事件类型.错误 : RaiseEvent 错误(Me, 参数)
            Case 播放器事件类型.色彩模式变化 : RaiseEvent 色彩模式变化(Me, 参数)
            Case 播放器事件类型.设备变化 : RaiseEvent 设备变化(Me, 参数)
        End Select
    End Sub

    Private Shared Function 读取事件消息(JSON As String) As String
        Try
            Using 文档 = JsonDocument.Parse(JSON)
                Dim 值 As JsonElement
                If 文档.RootElement.TryGetProperty("message", 值) Then Return 值.GetString()
            End Using
        Catch
        End Try
        Return "打开媒体失败。"
    End Function
End Class
