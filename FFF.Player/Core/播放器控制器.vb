Imports System.IO
Imports System.Text.Json
Imports System.Threading

''' <summary>
''' 管理原生播放器会话的完整生命周期，以及所有与媒体状态相关的操作。
''' 窗体不直接持有 <see cref="播放器会话"/>，以免界面代码参与资源释放和异步切换。
''' </summary>
Public NotInheritable Class 播放器控制器
    Implements IDisposable

    Private Const SDR峰值尼特 As Single = 100.0F
    Private Const HDR峰值尼特 As Single = 1000.0F
    Private Const SDR纸白尼特 As Single = 203.0F

    Private ReadOnly 输出窗口提供器 As Func(Of IntPtr)
    Private ReadOnly 事件同步上下文 As SynchronizationContext
    Private ReadOnly 会话操作锁 As New SemaphoreSlim(1, 1)

    Private 会话 As 播放器会话
    Private 会话操作取消 As CancellationTokenSource
    Private 字幕加载取消 As CancellationTokenSource
    Private 弹幕加载取消 As CancellationTokenSource
    Private 当前外部字幕 As 外部字幕轨道
    Private 当前弹幕资料库 As 弹幕资料库
    Private 当前文件路径 As String = String.Empty
    Private 当前解码器 As 解码模式 = 解码模式.CPU
    Private 当前色彩输出 As 色彩输出模式 = 色彩输出模式.映射到SDR
    Private 当前音量 As Single = 1.0F
    Private 已静音 As Boolean
    Private 正在切换会话 As Boolean
    Private 已释放 As Boolean

    Public Sub New(输出窗口提供器 As Func(Of IntPtr), 事件同步上下文 As SynchronizationContext)
        ArgumentNullException.ThrowIfNull(输出窗口提供器)
        Me.输出窗口提供器 = 输出窗口提供器
        Me.事件同步上下文 = 事件同步上下文
    End Sub

    Public Event 状态已变化 As EventHandler
    Public Event 媒体已打开 As EventHandler(Of 播放器媒体事件参数)
    Public Event 播放错误 As EventHandler(Of 播放器错误事件参数)
    Public Event HDR输出状态已确认 As EventHandler(Of 播放器HDR状态事件参数)
    Public Event 外部字幕已加载 As EventHandler(Of 播放器字幕事件参数)
    Public Event 外部弹幕已加载 As EventHandler(Of 播放器弹幕事件参数)

    Public ReadOnly Property 解码器 As 解码模式
        Get
            Return 当前解码器
        End Get
    End Property

    Public ReadOnly Property 色彩模式 As 色彩输出模式
        Get
            Return 当前色彩输出
        End Get
    End Property

    Public ReadOnly Property 音量 As Single
        Get
            Return 当前音量
        End Get
    End Property

    Public ReadOnly Property 静音 As Boolean
        Get
            Return 已静音
        End Get
    End Property

    Public ReadOnly Property 是否正在切换 As Boolean
        Get
            Return 正在切换会话
        End Get
    End Property

    Public ReadOnly Property 是否有媒体 As Boolean
        Get
            Return 会话 IsNot Nothing AndAlso Not String.IsNullOrEmpty(当前文件路径)
        End Get
    End Property

    Public ReadOnly Property 当前字幕 As 外部字幕轨道
        Get
            Return 当前外部字幕
        End Get
    End Property

    Public ReadOnly Property 当前弹幕 As 弹幕资料库
        Get
            Return 当前弹幕资料库
        End Get
    End Property

    Friend Sub 提交定时文字图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令), 序号 As ULong)
        Dim 目标 = 会话
        If 已释放 OrElse 目标 Is Nothing Then Return
        Try
            目标.设置定时文字图层(画布大小, 命令, 序号)
        Catch ex As ObjectDisposedException
        Catch ex As 播放器异常
        End Try
    End Sub

    Friend Function 读取定时文字状态() As 定时文字状态
        Try
            Return 会话?.当前定时文字状态
        Catch ex As ObjectDisposedException
            Return Nothing
        Catch ex As 播放器异常
            Return Nothing
        End Try
    End Function

    Public Function 安全读取快照() As 播放器快照
        Try
            Return 会话?.当前快照
        Catch ex As ObjectDisposedException
            Return Nothing
        Catch ex As 播放器异常
            Return Nothing
        End Try
    End Function

    Public Sub 打开媒体(路径 As String)
        If 已释放 OrElse String.IsNullOrWhiteSpace(路径) OrElse Not File.Exists(路径) Then Return
        切换媒体会话(Path.GetFullPath(路径), 当前解码器, TimeSpan.Zero, True, -1, -1)
    End Sub

    Public Sub 切换播放暂停()
        Dim 目标 = 会话
        If 目标 Is Nothing OrElse 正在切换会话 Then Return

        Try
            Dim 快照 = 目标.当前快照
            Select Case 快照.状态
                Case 播放状态.正在播放
                    目标.暂停()
                Case 播放状态.就绪, 播放状态.已暂停, 播放状态.播放结束
                    If 快照.总时长 > TimeSpan.Zero AndAlso 快照.播放位置 >= 快照.总时长 Then
                        目标.跳转(TimeSpan.Zero)
                    End If
                    目标.播放()
            End Select
        Catch ex As 播放器异常
            ' 原生状态会在下一次快照刷新时收敛；连续快捷键不需要弹出提示。
        End Try
    End Sub

    Public Sub 停止()
        If 已释放 Then Return
        Dim 取消源 = Interlocked.Exchange(会话操作取消, Nothing)
        取消源?.Cancel()
        释放当前会话()
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 相对跳转(秒数 As Integer)
        Dim 目标 = 会话
        If 目标 Is Nothing OrElse 正在切换会话 Then Return

        Try
            Dim 快照 = 目标.当前快照
            If Not 可操作(快照.状态) Then Return
            Dim 新位置 = 快照.播放位置 + TimeSpan.FromSeconds(秒数)
            If 新位置 < TimeSpan.Zero Then 新位置 = TimeSpan.Zero
            If 快照.总时长 > TimeSpan.Zero Then 新位置 = 最小时间(新位置, 快照.总时长)
            目标.跳转(新位置)
        Catch ex As 播放器异常
        End Try
    End Sub

    Public Sub 跳转到关键帧(位置 As TimeSpan)
        Dim 目标 = 会话
        If 目标 Is Nothing OrElse 正在切换会话 OrElse 位置 < TimeSpan.Zero Then Return

        Try
            Dim 快照 = 目标.当前快照
            If 可操作(快照.状态) AndAlso 快照.总时长 > TimeSpan.Zero Then
                目标.跳转到关键帧(位置)
            End If
        Catch ex As 播放器异常
        End Try
    End Sub

    Public Sub 设置音量(音量值 As Single)
        当前音量 = Math.Clamp(音量值, 0.0F, 1.0F)
        If 当前音量 > 0 Then 已静音 = False
        应用音量()
    End Sub

    Public Sub 切换静音()
        已静音 = Not 已静音
        应用音量()
    End Sub

    Public Function 切换解码器() As String
        If 已释放 OrElse 正在切换会话 Then Return String.Empty
        Dim 新解码器 = If(当前解码器 = 解码模式.CPU, 解码模式.GPU, 解码模式.CPU)
        If 会话 Is Nothing OrElse String.IsNullOrEmpty(当前文件路径) Then
            当前解码器 = 新解码器
            RaiseEvent 状态已变化(Me, EventArgs.Empty)
            Return 解码器说明(当前解码器)
        End If

        Dim 快照 = 安全读取快照()
        If 快照 Is Nothing Then Return String.Empty
        切换媒体会话(当前文件路径, 新解码器, 快照.播放位置,
            快照.状态 = 播放状态.正在播放, 快照.当前视频流, 快照.当前音频流, True)
        Return 解码器说明(新解码器)
    End Function

    Public Sub 切换HDR模式()
        Dim 目标 = 会话
        Dim 快照 = 安全读取快照()
        If 已释放 OrElse 正在切换会话 OrElse 目标 Is Nothing OrElse 快照 Is Nothing OrElse
            Not 可操作(快照.状态) OrElse Not 快照.是HDR源 Then Return

        Dim 新模式 = CType((CInt(当前色彩输出) + 1) Mod 3, 色彩输出模式)
        Try
            目标.设置色彩模式(新模式, SDR峰值尼特, HDR峰值尼特, SDR纸白尼特)
            当前色彩输出 = 新模式
            RaiseEvent 状态已变化(Me, EventArgs.Empty)
            ' 设置色彩模式会排入原生播放线程；此刻的快照仍可能是切换前的 SDR 状态。
            ' 等待色彩模式变化事件后，再基于新快照给出最终提示，避免把已成功的 HDR 误报为回退。
        Catch ex As 播放器异常
            RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "无法切换 HDR 模式"))
        End Try
    End Sub

    Public Sub 重绑输出窗口()
        If 已释放 OrElse 会话 Is Nothing Then Return
        Try
            会话.设置输出窗口(输出窗口提供器())
        Catch ex As 播放器异常
            ' 视频宿主重建期间可能暂时没有有效句柄，下一次布局变更会再次绑定。
        End Try
    End Sub

    Private Sub 切换媒体会话(路径 As String, 解码器 As 解码模式, 恢复位置 As TimeSpan,
                              恢复播放 As Boolean, 视频流 As Integer, 音频流 As Integer,
                              Optional 保留已加载字幕 As Boolean = False)
        Dim 忽略 = 切换媒体会话Async(路径, 解码器, 恢复位置, 恢复播放, 视频流, 音频流, 保留已加载字幕)
    End Sub

    Private Async Function 切换媒体会话Async(路径 As String, 解码器 As 解码模式, 恢复位置 As TimeSpan,
                                              恢复播放 As Boolean, 视频流 As Integer, 音频流 As Integer,
                                              保留已加载字幕 As Boolean) As Task
        Dim 此次取消 As New CancellationTokenSource()
        Dim 上次取消 = Interlocked.Exchange(会话操作取消, 此次取消)
        上次取消?.Cancel()

        Try
            Await 会话操作锁.WaitAsync(此次取消.Token)
        Catch ex As OperationCanceledException
            If ReferenceEquals(会话操作取消, 此次取消) Then 会话操作取消 = Nothing
            此次取消.Dispose()
            Return
        End Try

        Dim 候选会话 As 播放器会话 = Nothing
        Try
            If 已释放 OrElse 此次取消.IsCancellationRequested Then Return
            正在切换会话 = True
            RaiseEvent 状态已变化(Me, EventArgs.Empty)

            候选会话 = 创建会话(解码器)
            候选会话.设置音量(当前音量, 已静音)
            Await 候选会话.打开Async(路径, 此次取消.Token)
            此次取消.Token.ThrowIfCancellationRequested()

            Dim 初始快照 = 候选会话.当前快照
            Dim 媒体信息 = 候选会话.当前媒体信息
            恢复流选择(候选会话, 媒体信息, 视频流, 音频流)
            If 恢复位置 > TimeSpan.Zero Then
                候选会话.跳转(If(初始快照.总时长 > TimeSpan.Zero, 最小时间(恢复位置, 初始快照.总时长), 恢复位置))
            End If
            Dim 快照 = 候选会话.当前快照

            Dim 保留当前字幕 = 保留已加载字幕 AndAlso
                String.Equals(当前文件路径, 路径, StringComparison.OrdinalIgnoreCase)
            释放当前会话(保留当前字幕)
            会话 = 候选会话
            候选会话 = Nothing
            当前文件路径 = 路径
            当前解码器 = 解码器
            添加会话事件(会话)
            重绑输出窗口()
            If 恢复播放 Then 会话.播放()

            RaiseEvent 媒体已打开(Me, New 播放器媒体事件参数(当前文件路径, 媒体信息, 快照))
            If Not 保留当前字幕 Then
                开始自动加载字幕(当前文件路径)
                开始自动加载弹幕(当前文件路径)
            End If
            RaiseEvent 状态已变化(Me, EventArgs.Empty)
        Catch ex As OperationCanceledException
            ' 新请求或停止操作会主动取消当前打开过程。
        Catch ex As Exception
            If Not 已释放 AndAlso Not 此次取消.IsCancellationRequested Then
                RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "无法播放媒体"))
            End If
        Finally
            候选会话?.释放()
            正在切换会话 = False
            会话操作锁.Release()
            If ReferenceEquals(会话操作取消, 此次取消) Then 会话操作取消 = Nothing
            此次取消.Dispose()
            RaiseEvent 状态已变化(Me, EventArgs.Empty)
        End Try
    End Function

    Private Function 创建会话(解码器 As 解码模式) As 播放器会话
        Return New 播放器会话(New 播放器配置 With {
            .解码器 = 解码器,
            .色彩模式 = 当前色彩输出,
            .SDR峰值尼特 = SDR峰值尼特,
            .HDR峰值尼特 = HDR峰值尼特,
            .SDR纸白尼特 = SDR纸白尼特,
            .输出窗口句柄 = IntPtr.Zero,
            .事件同步上下文 = 事件同步上下文
        })
    End Function

    Private Sub 恢复流选择(目标 As 播放器会话, 信息 As 媒体信息, 视频流 As Integer, 音频流 As Integer)
        If 信息 Is Nothing Then Return
        If 视频流 >= 0 AndAlso 信息.流.Any(Function(x) x.索引 = 视频流 AndAlso x.类型 = "video" AndAlso Not x.是封面图) Then
            目标.选择视频流(视频流)
        End If
        If 音频流 >= 0 AndAlso 信息.流.Any(Function(x) x.索引 = 音频流 AndAlso x.类型 = "audio") Then
            目标.选择音频流(音频流)
        End If
    End Sub

    Private Sub 添加会话事件(目标 As 播放器会话)
        AddHandler 目标.状态变化, AddressOf 会话_状态变化
        AddHandler 目标.打开完成, AddressOf 会话_打开完成
        AddHandler 目标.色彩模式变化, AddressOf 会话_色彩模式变化
        AddHandler 目标.错误, AddressOf 会话_错误
    End Sub

    Private Sub 移除会话事件(目标 As 播放器会话)
        RemoveHandler 目标.状态变化, AddressOf 会话_状态变化
        RemoveHandler 目标.打开完成, AddressOf 会话_打开完成
        RemoveHandler 目标.色彩模式变化, AddressOf 会话_色彩模式变化
        RemoveHandler 目标.错误, AddressOf 会话_错误
    End Sub

    Private Sub 会话_状态变化(sender As Object, e As 播放器事件参数)
        If sender Is 会话 Then RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Private Sub 会话_打开完成(sender As Object, e As 播放器事件参数)
        If sender IsNot 会话 Then Return
        Try
            RaiseEvent 媒体已打开(Me, New 播放器媒体事件参数(当前文件路径, 会话.当前媒体信息, 会话.当前快照))
        Catch ex As 播放器异常
        End Try
    End Sub

    Private Sub 会话_色彩模式变化(sender As Object, e As 播放器事件参数)
        If sender IsNot 会话 Then Return
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
        Dim 快照 = 安全读取快照()
        If 快照 IsNot Nothing AndAlso 快照.是HDR源 Then
            RaiseEvent HDR输出状态已确认(Me, New 播放器HDR状态事件参数(取得HDR模式说明(快照)))
        End If
    End Sub

    Private Sub 会话_错误(sender As Object, e As 播放器事件参数)
        If sender IsNot 会话 OrElse 已释放 Then Return
        RaiseEvent 播放错误(Me, New 播放器错误事件参数(读取事件消息(e.详情JSON), "播放错误"))
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Private Sub 应用音量()
        Try
            会话?.设置音量(当前音量, 已静音)
        Catch ex As 播放器异常
        End Try
    End Sub

    Private Sub 开始自动加载字幕(媒体路径 As String)
        释放当前字幕()
        Dim 本次取消 As New CancellationTokenSource()
        字幕加载取消 = 本次取消
        Dim 忽略 = 自动加载同名字幕Async(媒体路径, 本次取消)
    End Sub

    Private Async Function 自动加载同名字幕Async(媒体路径 As String, 本次取消 As CancellationTokenSource) As Task
        Dim 候选轨道 As 外部字幕轨道 = Nothing
        Try
            候选轨道 = Await 外部字幕自动加载器.尝试加载同名字幕Async(媒体路径, 本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) Then Return
            If 候选轨道 Is Nothing Then Return

            当前外部字幕 = 候选轨道
            候选轨道 = Nothing
            RaiseEvent 外部字幕已加载(Me, New 播放器字幕事件参数(当前外部字幕.路径, 当前外部字幕.格式))
        Catch ex As OperationCanceledException
            ' 新媒体、停止或关闭会取消尚未完成的自动加载。
        Catch
            ' 外部字幕是可选资源，加载失败不影响媒体播放。
        Finally
            候选轨道?.释放()
            If ReferenceEquals(字幕加载取消, 本次取消) Then 字幕加载取消 = Nothing
            本次取消.Dispose()
        End Try
    End Function

    Private Sub 释放当前字幕()
        Dim 取消源 = Interlocked.Exchange(字幕加载取消, Nothing)
        取消源?.Cancel()
        Dim 待释放 = 当前外部字幕
        当前外部字幕 = Nothing
        待释放?.释放()
    End Sub

    Private Sub 开始自动加载弹幕(媒体路径 As String)
        释放当前弹幕()
        Dim 本次取消 As New CancellationTokenSource()
        弹幕加载取消 = 本次取消
        Dim 忽略 = 自动加载同名弹幕Async(媒体路径, 本次取消)
    End Sub

    Private Async Function 自动加载同名弹幕Async(媒体路径 As String, 本次取消 As CancellationTokenSource) As Task
        Try
            Dim 候选资料库 = Await 弹幕自动加载器.尝试加载同名弹幕Async(媒体路径, 本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) OrElse
                候选资料库 Is Nothing Then Return
            当前弹幕资料库 = 候选资料库
            RaiseEvent 外部弹幕已加载(Me, New 播放器弹幕事件参数(
                Path.ChangeExtension(媒体路径, ".xml"), 候选资料库.数量))
        Catch ex As OperationCanceledException
            ' 新媒体、停止或关闭会取消尚未完成的自动加载。
        Catch
            ' 弹幕是可选资源，加载失败不影响媒体播放。
        Finally
            If ReferenceEquals(弹幕加载取消, 本次取消) Then 弹幕加载取消 = Nothing
            本次取消.Dispose()
        End Try
    End Function

    Private Sub 释放当前弹幕()
        Dim 取消源 = Interlocked.Exchange(弹幕加载取消, Nothing)
        取消源?.Cancel()
        当前弹幕资料库 = Nothing
    End Sub

    Private Sub 释放当前会话(Optional 保留已加载字幕 As Boolean = False)
        If Not 保留已加载字幕 Then
            释放当前字幕()
            释放当前弹幕()
        End If
        Dim 待释放 = 会话
        会话 = Nothing
        当前文件路径 = String.Empty
        If 待释放 Is Nothing Then Return

        移除会话事件(待释放)
        Try
            待释放.停止()
        Catch ex As 播放器异常
        Finally
            待释放.释放()
        End Try
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        Dim 取消源 = Interlocked.Exchange(会话操作取消, Nothing)
        取消源?.Cancel()
        释放当前会话()
        GC.SuppressFinalize(Me)
    End Sub

    Public Shared Function 可操作(状态 As 播放状态) As Boolean
        Return 状态 = 播放状态.就绪 OrElse 状态 = 播放状态.正在播放 OrElse
            状态 = 播放状态.已暂停 OrElse 状态 = 播放状态.播放结束
    End Function

    Private Shared Function 解码器说明(解码器 As 解码模式) As String
        Return If(解码器 = 解码模式.GPU, "GPU", "CPU")
    End Function

    Private Function 取得HDR模式说明(快照 As 播放器快照) As String
        Select Case 当前色彩输出
            Case 色彩输出模式.映射到SDR
                Return "HDR 映射到 SDR"
            Case 色彩输出模式.原始HDR按SDR呈现
                Return "原始 HDR 按 SDR 呈现"
            Case 色彩输出模式.峰值映射HDR
                Return If(快照.实际色彩模式 = 色彩输出模式.峰值映射HDR, "1000 nit 真实 HDR 输出", "HDR 目标不可用，已映射到 SDR")
            Case Else
                Return String.Empty
        End Select
    End Function

    Private Shared Function 最小时间(左 As TimeSpan, 右 As TimeSpan) As TimeSpan
        Return If(左 <= 右, 左, 右)
    End Function

    Private Shared Function 读取事件消息(JSON As String) As String
        Try
            Using 文档 = JsonDocument.Parse(JSON)
                Dim 消息 As JsonElement
                If 文档.RootElement.TryGetProperty("message", 消息) Then
                    Dim 文本 = 消息.GetString()
                    If Not String.IsNullOrWhiteSpace(文本) Then Return 文本
                End If
            End Using
        Catch
        End Try
        Return "播放器发生未知错误。"
    End Function
End Class

Public NotInheritable Class 播放器媒体事件参数
    Inherits EventArgs

    Public Sub New(文件路径 As String, 媒体信息 As 媒体信息, 快照 As 播放器快照)
        Me.文件路径 = 文件路径
        Me.媒体信息 = 媒体信息
        Me.快照 = 快照
    End Sub

    Public ReadOnly Property 文件路径 As String
    Public ReadOnly Property 媒体信息 As 媒体信息
    Public ReadOnly Property 快照 As 播放器快照
End Class

Public NotInheritable Class 播放器错误事件参数
    Inherits EventArgs

    Public Sub New(消息 As String, 标题 As String)
        Me.消息 = If(消息, "播放器发生未知错误。")
        Me.标题 = If(标题, "播放错误")
    End Sub

    Public ReadOnly Property 消息 As String
    Public ReadOnly Property 标题 As String
End Class

Public NotInheritable Class 播放器HDR状态事件参数
    Inherits EventArgs

    Public Sub New(说明 As String)
        Me.说明 = 说明
    End Sub

    Public ReadOnly Property 说明 As String
End Class

Public NotInheritable Class 播放器字幕事件参数
    Inherits EventArgs

    Public Sub New(路径 As String, 格式 As 外部字幕格式)
        Me.路径 = 路径
        Me.格式 = 格式
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 格式 As 外部字幕格式
End Class
