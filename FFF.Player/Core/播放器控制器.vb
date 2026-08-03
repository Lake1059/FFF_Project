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
    Private Const HDR峰值尼特 As Single = 0.0F
    Private Const SDR纸白尼特 As Single = 203.0F

    Private ReadOnly 输出窗口提供器 As Func(Of IntPtr)
    Private ReadOnly 事件同步上下文 As SynchronizationContext
    Private ReadOnly 会话操作锁 As New SemaphoreSlim(1, 1)

    Private 会话 As 播放器会话
    Private 会话操作取消 As CancellationTokenSource
    Private 字幕加载取消 As CancellationTokenSource
    Private 弹幕加载取消 As CancellationTokenSource
    Private 歌词加载取消 As CancellationTokenSource
    Private 当前字幕轨道 As 外部字幕轨道
    Private 已导入外部字幕 As 外部字幕轨道
    Private 外部字幕候选快照 As 外部字幕候选() = Array.Empty(Of 外部字幕候选)()
    Private 当前内嵌字幕 As 外部字幕轨道
    Private 当前字幕来源索引 As Integer = -2
    Private 当前弹幕资料库 As 弹幕资料库
    Private 当前歌词资料 As LRC歌词资料
    Private 当前媒体是纯音频 As Boolean
    Private 当前音乐包含封面 As Boolean
    Private 当前文件路径 As String = String.Empty
    Private 当前解码器 As 解码模式 = 解码模式.CPU
    Private 当前色彩输出 As 色彩输出模式 = 色彩输出模式.映射到SDR
    Private HDR色彩输出偏好 As 色彩输出模式 = 色彩输出模式.映射到SDR
    Private 当前WASAPI模式 As WASAPI共享模式 = WASAPI共享模式.共享
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
    Public Event 操作提示 As EventHandler(Of 播放器操作提示事件参数)
    Public Event HDR输出状态已确认 As EventHandler(Of 播放器HDR状态事件参数)
    Public Event 外部字幕已加载 As EventHandler(Of 播放器字幕事件参数)
    Public Event 字幕选择已变化 As EventHandler
    Public Event 外部弹幕已加载 As EventHandler(Of 播放器弹幕事件参数)
    Public Event 外部歌词已加载 As EventHandler(Of 播放器歌词事件参数)

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

    Public ReadOnly Property WASAPI模式 As WASAPI共享模式
        Get
            Return 当前WASAPI模式
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
            Return Volatile.Read(当前字幕轨道)
        End Get
    End Property

    Public ReadOnly Property 已导入字幕 As 外部字幕轨道
        Get
            Return Volatile.Read(已导入外部字幕)
        End Get
    End Property

    Public ReadOnly Property 可用外部字幕 As IReadOnlyList(Of 外部字幕候选)
        Get
            Return Volatile.Read(外部字幕候选快照)
        End Get
    End Property

    ''' <summary>-2 表示关闭，-1 表示外部字幕，非负数表示内嵌字幕流索引。</summary>
    Public ReadOnly Property 当前字幕流索引 As Integer
        Get
            Return Volatile.Read(当前字幕来源索引)
        End Get
    End Property

    Public ReadOnly Property 当前媒体路径 As String
        Get
            Return 当前文件路径
        End Get
    End Property

    Public ReadOnly Property 当前弹幕 As 弹幕资料库
        Get
            Return Volatile.Read(当前弹幕资料库)
        End Get
    End Property

    Public ReadOnly Property 当前歌词 As LRC歌词资料
        Get
            Return Volatile.Read(当前歌词资料)
        End Get
    End Property

    Public ReadOnly Property 当前音乐有封面 As Boolean
        Get
            Return Volatile.Read(当前音乐包含封面)
        End Get
    End Property

    Friend Sub 提交定时文字图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                          序号 As ULong, 目标帧率 As Single)
        Dim 目标 = 会话
        If 已释放 OrElse 目标 Is Nothing Then Return
        Try
            目标.设置定时文字图层(画布大小, 命令, 序号, 目标帧率)
        Catch ex As ObjectDisposedException
        Catch ex As 播放器异常
        End Try
    End Sub

    Friend Sub 提交弹幕图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                        序号 As ULong, 目标帧率 As Single)
        Dim 目标 = 会话
        If 已释放 OrElse 目标 Is Nothing Then Return
        Try
            目标.设置弹幕图层(画布大小, 命令, 序号, 目标帧率)
        Catch ex As ObjectDisposedException
        Catch ex As 播放器异常
        End Try
    End Sub

    Friend Sub 提交播放器信息图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                              序号 As ULong, 目标帧率 As Single)
        Dim 目标 = 会话
        If 已释放 OrElse 目标 Is Nothing Then Return
        Try
            目标.设置播放器信息图层(画布大小, 命令, 序号, 目标帧率)
        Catch ex As ObjectDisposedException
        Catch ex As 播放器异常
        End Try
    End Sub

    Friend Function 提交歌词图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                            序号 As ULong, 目标帧率 As Single, 呈现设置 As 歌词呈现设置) As Boolean
        Dim 目标 = 会话
        If 已释放 OrElse 目标 Is Nothing Then Return False
        Try
            目标.设置歌词图层(画布大小, 命令, 序号, 目标帧率, 呈现设置)
            Return True
        Catch ex As ObjectDisposedException
        Catch ex As 播放器异常
        End Try
        Return False
    End Function

    Friend Function 读取定时文字状态() As 定时文字状态
        Try
            Return 会话?.当前定时文字状态
        Catch ex As ObjectDisposedException
            Return Nothing
        Catch ex As 播放器异常
            Return Nothing
        End Try
    End Function

    Friend Function 读取弹幕状态() As 定时文字状态
        Try
            Return 会话?.当前弹幕状态
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

    Friend Function 读取音频峰值() As Single()
        Try
            Return If(会话?.读取音频峰值(), Array.Empty(Of Single)())
        Catch ex As ObjectDisposedException
            Return Array.Empty(Of Single)()
        Catch ex As 播放器异常
            Return Array.Empty(Of Single)()
        End Try
    End Function

    Public Function 安全读取媒体信息() As 媒体信息
        Try
            Return 会话?.当前媒体信息
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

    ''' <summary>
    ''' 在不重建媒体会话、不改变播放位置的前提下替换外部字幕。新文件先在
    ''' 后台完整解析，成功后才交换轨道；加载失败期间旧字幕始终保持可用。
    ''' </summary>
    Public Sub 替换字幕(路径 As String)
        If 已释放 OrElse String.IsNullOrWhiteSpace(路径) Then Return
        If Not 是否有媒体 Then
            RaiseEvent 播放错误(Me, New 播放器错误事件参数("请先播放媒体，再加载外部字幕。", "无法加载字幕"))
            Return
        End If
        If Not File.Exists(路径) OrElse Not 外部字幕自动加载器.是支持的字幕文件(路径) Then
            RaiseEvent 播放错误(Me, New 播放器错误事件参数("仅可加载存在的 SRT、ASS、SSA 或 SUP 字幕文件。", "无法加载字幕"))
            Return
        End If
        Dim 本次取消 As New CancellationTokenSource()
        Dim 上次取消 = Interlocked.Exchange(字幕加载取消, 本次取消)
        上次取消?.Cancel()
        Dim 媒体路径 = 当前文件路径
        Dim 忽略 = 替换字幕Async(Path.GetFullPath(路径), 媒体路径, 本次取消)
    End Sub

    Private Async Function 替换字幕Async(字幕路径 As String, 媒体路径 As String,
                                       本次取消 As CancellationTokenSource) As Task
        Dim 候选轨道 As 外部字幕轨道 = Nothing
        Try
            候选轨道 = Await 外部字幕自动加载器.加载字幕Async(字幕路径, 媒体路径, 本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not ReferenceEquals(字幕加载取消, 本次取消) OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) Then Return
            ' Interlocked documents the publication contract for test hosts that
            ' do not provide a UI SynchronizationContext. In the application the
            ' continuation and renderer timer are additionally serialized by UI.
            添加外部字幕候选(候选轨道.路径, 候选轨道.格式)
            发布外部字幕(候选轨道)
            候选轨道 = Nothing
            RaiseEvent 外部字幕已加载(Me,
                New 播放器字幕事件参数(已导入外部字幕.路径, 已导入外部字幕.格式))
        Catch ex As OperationCanceledException
            ' A newer manual choice, media replacement or shutdown superseded it.
        Catch ex As Exception
            If Not 已释放 AndAlso Not 本次取消.IsCancellationRequested Then
                RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "无法加载字幕"))
            End If
        Finally
            候选轨道?.释放()
            If ReferenceEquals(字幕加载取消, 本次取消) Then 字幕加载取消 = Nothing
            本次取消.Dispose()
        End Try
    End Function

    Public Sub 关闭字幕()
        If 已释放 Then Return
        取消字幕加载()
        Dim 待释放内嵌 = Interlocked.Exchange(当前内嵌字幕, Nothing)
        Interlocked.Exchange(当前字幕轨道, Nothing)
        Volatile.Write(当前字幕来源索引, -2)
        待释放内嵌?.释放()
        RaiseEvent 字幕选择已变化(Me, EventArgs.Empty)
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 选择外部字幕()
        If 已释放 Then Return
        Dim 外部 = Volatile.Read(已导入外部字幕)
        If 外部 Is Nothing Then Return
        取消字幕加载()
        Dim 待释放内嵌 = Interlocked.Exchange(当前内嵌字幕, Nothing)
        Interlocked.Exchange(当前字幕轨道, 外部)
        Volatile.Write(当前字幕来源索引, -1)
        待释放内嵌?.释放()
        RaiseEvent 字幕选择已变化(Me, EventArgs.Empty)
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 选择外部字幕(路径 As String)
        If 已释放 OrElse String.IsNullOrWhiteSpace(路径) Then Return
        Dim 完整路径 = Path.GetFullPath(路径)
        Dim 已加载 = Volatile.Read(已导入外部字幕)
        If 已加载 IsNot Nothing AndAlso
            String.Equals(已加载.路径, 完整路径, StringComparison.OrdinalIgnoreCase) Then
            选择外部字幕()
        Else
            替换字幕(完整路径)
        End If
    End Sub

    Public Sub 选择内嵌字幕(流索引 As Integer)
        If 已释放 OrElse 正在切换会话 OrElse 流索引 < 0 Then Return
        Dim 信息 = 安全读取媒体信息()
        Dim 字幕流 = 信息?.流.FirstOrDefault(
            Function(x) x.索引 = 流索引 AndAlso String.Equals(x.类型, "subtitle", StringComparison.OrdinalIgnoreCase))
        If 字幕流 Is Nothing OrElse String.IsNullOrWhiteSpace(当前文件路径) Then Return
        If 当前字幕流索引 = 流索引 AndAlso Volatile.Read(当前内嵌字幕) IsNot Nothing Then Return

        Dim 本次取消 As New CancellationTokenSource()
        Dim 上次取消 = Interlocked.Exchange(字幕加载取消, 本次取消)
        上次取消?.Cancel()
        Dim 媒体路径 = 当前文件路径
        Dim 忽略 = 选择内嵌字幕Async(媒体路径, 字幕流, 本次取消)
    End Sub

    Private Async Function 选择内嵌字幕Async(媒体路径 As String, 字幕流 As 媒体流信息,
                                          本次取消 As CancellationTokenSource) As Task
        Dim 候选轨道 As 外部字幕轨道 = Nothing
        Try
            候选轨道 = Await Task.Run(
                Function() 外部字幕自动加载器.加载内嵌字幕(媒体路径, 字幕流, 本次取消.Token),
                本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not ReferenceEquals(字幕加载取消, 本次取消) OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) Then Return

            Dim 待释放 = Interlocked.Exchange(当前内嵌字幕, 候选轨道)
            Interlocked.Exchange(当前字幕轨道, 候选轨道)
            Volatile.Write(当前字幕来源索引, 字幕流.索引)
            候选轨道 = Nothing
            待释放?.释放()
            RaiseEvent 字幕选择已变化(Me, EventArgs.Empty)
            RaiseEvent 状态已变化(Me, EventArgs.Empty)
        Catch ex As OperationCanceledException
        Catch ex As Exception
            If Not 已释放 AndAlso Not 本次取消.IsCancellationRequested Then
                RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "无法加载内嵌字幕"))
            End If
        Finally
            候选轨道?.释放()
            If ReferenceEquals(字幕加载取消, 本次取消) Then 字幕加载取消 = Nothing
            本次取消.Dispose()
        End Try
    End Function

    ''' <summary>
    ''' 在不重建媒体会话、不改变播放位置的前提下替换 XML 弹幕。候选文件必须先在
    ''' 后台完整解析；解析失败或请求被后续操作取代时，当前弹幕资料库保持不变。
    ''' </summary>
    Public Sub 替换弹幕(路径 As String)
        If 已释放 OrElse String.IsNullOrWhiteSpace(路径) Then Return
        If Not 是否有媒体 Then
            RaiseEvent 播放错误(Me, New 播放器错误事件参数("请先播放媒体，再加载外部弹幕。", "无法加载弹幕"))
            Return
        End If
        If Not File.Exists(路径) OrElse Not 弹幕自动加载器.是支持的弹幕文件(路径) Then
            RaiseEvent 播放错误(Me, New 播放器错误事件参数("仅可加载存在的 B 站 XML 弹幕文件。", "无法加载弹幕"))
            Return
        End If
        Dim 本次取消 As New CancellationTokenSource()
        Dim 上次取消 = Interlocked.Exchange(弹幕加载取消, 本次取消)
        上次取消?.Cancel()
        Dim 媒体路径 = 当前文件路径
        Dim 忽略 = 替换弹幕Async(Path.GetFullPath(路径), 媒体路径, 本次取消)
    End Sub

    Private Async Function 替换弹幕Async(弹幕路径 As String, 媒体路径 As String,
                                      本次取消 As CancellationTokenSource) As Task
        Try
            Dim 候选资料库 = Await 弹幕自动加载器.加载弹幕Async(弹幕路径, 本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not ReferenceEquals(弹幕加载取消, 本次取消) OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) Then Return
            ' 原子发布只能发生在完整解析和媒体身份复核之后。不得预先清空旧资料库，
            ' 否则大文件解析、损坏 XML 或连续拖入会让正在显示的弹幕瞬间消失。
            Interlocked.Exchange(当前弹幕资料库, 候选资料库)
            RaiseEvent 外部弹幕已加载(Me, New 播放器弹幕事件参数(弹幕路径, 候选资料库.数量))
        Catch ex As OperationCanceledException
            ' 更新的手动选择、媒体替换或关闭操作已经取代本次请求。
        Catch ex As Exception
            If Not 已释放 AndAlso Not 本次取消.IsCancellationRequested Then
                RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "无法加载弹幕"))
            End If
        Finally
            If ReferenceEquals(弹幕加载取消, 本次取消) Then 弹幕加载取消 = Nothing
            本次取消.Dispose()
        End Try
    End Function

    Public Sub 替换歌词(路径 As String)
        If 已释放 OrElse String.IsNullOrWhiteSpace(路径) Then Return
        If Not 是否有媒体 Then
            RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(
                "请先播放纯音频媒体，再加载 LRC 外挂歌词。", True, "无法加载歌词"))
            Return
        End If
        If Not Volatile.Read(当前媒体是纯音频) Then
            RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(
                "LRC 外挂歌词仅支持纯音频媒体。", True, "不支持此操作"))
            Return
        End If
        If Not File.Exists(路径) OrElse Not LRC歌词自动加载器.是支持的歌词文件(路径) Then
            RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(
                "仅可加载存在的 LRC 歌词文件。", True, "无法加载歌词"))
            Return
        End If
        Dim 本次取消 As New CancellationTokenSource()
        Dim 上次取消 = Interlocked.Exchange(歌词加载取消, 本次取消)
        上次取消?.Cancel()
        Dim 媒体路径 = 当前文件路径
        Dim 忽略 = 替换歌词Async(Path.GetFullPath(路径), 媒体路径, 本次取消)
    End Sub

    Private Async Function 替换歌词Async(歌词路径 As String, 媒体路径 As String,
                                      本次取消 As CancellationTokenSource) As Task
        Try
            Dim candidate = Await LRC歌词自动加载器.加载歌词Async(歌词路径, 本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not ReferenceEquals(歌词加载取消, 本次取消) OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) Then Return
            Interlocked.Exchange(当前歌词资料, candidate)
            RaiseEvent 外部歌词已加载(Me, New 播放器歌词事件参数(candidate.路径, candidate.条目.Count))
        Catch ex As OperationCanceledException
        Catch ex As NotSupportedException
            If Not 已释放 AndAlso Not 本次取消.IsCancellationRequested Then
                RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(
                    ex.Message, True, "不支持此歌词"))
            End If
        Catch ex As Exception
            If Not 已释放 AndAlso Not 本次取消.IsCancellationRequested Then
                RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(
                    ex.Message, True, "无法加载歌词"))
            End If
        Finally
            If ReferenceEquals(歌词加载取消, 本次取消) Then 歌词加载取消 = Nothing
            本次取消.Dispose()
        End Try
    End Function

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

    Public Sub 跳转到位置(位置 As TimeSpan)
        Dim 目标 = 会话
        If 目标 Is Nothing OrElse 正在切换会话 OrElse 位置 < TimeSpan.Zero Then Return

        Try
            Dim 快照 = 目标.当前快照
            If 可操作(快照.状态) AndAlso 快照.总时长 > TimeSpan.Zero Then
                目标.跳转(最小时间(位置, 快照.总时长))
            End If
        Catch ex As 播放器异常
        End Try
    End Sub

    Public Sub 逐帧(方向 As Integer)
        If 方向 <> -1 AndAlso 方向 <> 1 Then Throw New ArgumentOutOfRangeException(NameOf(方向))
        Dim 目标 = 会话
        If 目标 Is Nothing OrElse 正在切换会话 Then Return

        Try
            Dim 快照 = 目标.当前快照
            If 可操作(快照.状态) AndAlso 快照.当前视频流 >= 0 Then
                If 方向 < 0 Then 目标.上一帧() Else 目标.下一帧()
            End If
        Catch ex As 播放器异常
        End Try
    End Sub

    Public Sub 跳转到相邻关键帧(方向 As Integer)
        If 方向 <> -1 AndAlso 方向 <> 1 Then Throw New ArgumentOutOfRangeException(NameOf(方向))
        Dim 目标 = 会话
        If 目标 Is Nothing OrElse 正在切换会话 Then Return

        Try
            Dim 快照 = 目标.当前快照
            If 可操作(快照.状态) AndAlso 快照.当前视频流 >= 0 Then
                If 方向 < 0 Then 目标.上一关键帧() Else 目标.下一关键帧()
            End If
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

    Public Sub 选择视频流(流索引 As Integer)
        Dim 目标 = 会话
        If 已释放 OrElse 正在切换会话 OrElse 目标 Is Nothing Then Return
        Try
            Dim 信息 = 目标.当前媒体信息
            Dim 快照 = 目标.当前快照
            If Not 可操作(快照.状态) OrElse 快照.当前视频流 = 流索引 OrElse
                信息 Is Nothing OrElse Not 信息.流.Any(
                    Function(x) x.索引 = 流索引 AndAlso x.类型 = "video" AndAlso Not x.是封面图) Then Return
            目标.选择视频流(流索引)
        Catch ex As 播放器异常
            RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "无法切换视频流"))
        End Try
    End Sub

    Public Sub 选择音频流(流索引 As Integer)
        Dim 目标 = 会话
        If 已释放 OrElse 正在切换会话 OrElse 目标 Is Nothing Then Return
        Try
            Dim 信息 = 目标.当前媒体信息
            Dim 快照 = 目标.当前快照
            If Not 可操作(快照.状态) OrElse 快照.当前音频流 = 流索引 OrElse
                信息 Is Nothing OrElse Not 信息.流.Any(
                    Function(x) x.索引 = 流索引 AndAlso x.类型 = "audio") Then Return
            目标.选择音频流(流索引)
        Catch ex As 播放器异常
            RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "无法切换音频流"))
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
            HDR色彩输出偏好 = 新模式
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

    ''' <summary>切换端点共享/独占模式。原生层仅重建音频渲染器，保留当前媒体和流选择。</summary>
    Public Sub 切换WASAPI模式()
        Dim 目标 = 会话
        Dim 快照 = 安全读取快照()
        If 已释放 OrElse 正在切换会话 OrElse 目标 Is Nothing OrElse 快照 Is Nothing OrElse
            Not 可操作(快照.状态) Then Return
        Dim 新模式 = If(当前WASAPI模式 = WASAPI共享模式.共享, WASAPI共享模式.独占, WASAPI共享模式.共享)
        Try
            目标.设置WASAPI独占模式(新模式 = WASAPI共享模式.独占)
            ' 这是用户的目标模式。先同步记录，设备事件会在成功或回退后
            ' 再校正实际模式，也避免紧接着打开文件时仍按旧共享状态处理。
            当前WASAPI模式 = 新模式
            RaiseEvent 状态已变化(Me, EventArgs.Empty)
        Catch ex As 播放器异常
            RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "无法切换 WASAPI 模式"))
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
        Dim 原会话 = 会话
        Dim 保留WASAPI模式 = 当前WASAPI模式
        Dim 已临时释放独占 = False
        Dim 打开异常 As Exception = Nothing
        Try
            Try
                If 已释放 OrElse 此次取消.IsCancellationRequested Then Return
                正在切换会话 = True
                RaiseEvent 状态已变化(Me, EventArgs.Empty)

                ' Windows 不允许另一个客户端在当前端点仍被旧会话独占时初始化。
                ' 先让旧会话回到共享以保留其媒体作为失败回退；候选成功后释放
                ' 旧会话，再把新会话切回用户选择的独占模式。
                If 保留WASAPI模式 = WASAPI共享模式.独占 AndAlso 原会话 IsNot Nothing Then
                    Await 设置会话WASAPI模式Async(原会话, False, 此次取消.Token)
                    已临时释放独占 = True
                    当前WASAPI模式 = 保留WASAPI模式
                End If

            ' HDR 输出策略属于播放器偏好。新片源沿用该策略；如果新片源是
            ' SDR，会由播放器会话按源类型安全地收敛回 SDR 映射。
            Dim 候选色彩输出 = If(
                String.Equals(当前文件路径, 路径, StringComparison.OrdinalIgnoreCase),
                当前色彩输出, HDR色彩输出偏好)
            候选会话 = 创建会话(解码器, 候选色彩输出)
            候选会话.设置音量(当前音量, 已静音)
            Await 候选会话.打开Async(路径, 此次取消.Token)
            此次取消.Token.ThrowIfCancellationRequested()
            Dim 初始快照 = 候选会话.当前快照
            Dim 媒体信息 = 候选会话.当前媒体信息
            Dim 候选是纯音频 = 媒体信息.流.Any(
                Function(x) String.Equals(x.类型, "audio", StringComparison.OrdinalIgnoreCase)) AndAlso
                Not 媒体信息.流.Any(
                    Function(x) String.Equals(x.类型, "video", StringComparison.OrdinalIgnoreCase) AndAlso
                                Not x.是封面图)
            Dim 候选包含封面 = 候选是纯音频 AndAlso 媒体信息.流.Any(
                Function(x) String.Equals(x.类型, "video", StringComparison.OrdinalIgnoreCase) AndAlso x.是封面图)
            恢复流选择(候选会话, 媒体信息, 视频流, 音频流)
            If 恢复位置 > TimeSpan.Zero Then
                候选会话.跳转(If(初始快照.总时长 > TimeSpan.Zero, 最小时间(恢复位置, 初始快照.总时长), 恢复位置))
            End If
            Dim 快照 = 候选会话.当前快照

            Dim 保留当前字幕 = 保留已加载字幕 AndAlso
                String.Equals(当前文件路径, 路径, StringComparison.OrdinalIgnoreCase)
            释放当前会话(保留当前字幕)
            已临时释放独占 = False
            会话 = 候选会话
            候选会话 = Nothing
            当前文件路径 = 路径
            Volatile.Write(当前媒体是纯音频, 候选是纯音频)
            Volatile.Write(当前音乐包含封面, 候选包含封面)
            当前解码器 = 快照.解码器
            当前色彩输出 = 快照.请求色彩模式
            添加会话事件(会话)
            If 保留WASAPI模式 = WASAPI共享模式.独占 Then
                Try
                    Await 设置会话WASAPI模式Async(会话, True, 此次取消.Token)
                Catch ex As 播放器异常
                    ' 原生层已经恢复共享模式并通过会话错误事件报告原因；
                    ' 新媒体仍可继续按共享模式播放。
                Catch ex As TimeoutException
                    ' 模式确认超时不应阻止已经打开的新媒体继续播放。
                    RaiseEvent 播放错误(Me,
                        New 播放器错误事件参数(ex.Message, "无法确认 WASAPI 独占模式"))
                End Try
            End If
            重绑输出窗口()
            If 恢复播放 Then 会话.播放()

            ' “媒体已打开”是可操作边界。事件处理器可能立即切换色彩、音轨或跳转，
            ' 因而不能等到 Finally 才清除切换标记，否则首个操作会被静默忽略。
            正在切换会话 = False
            RaiseEvent 媒体已打开(Me,
                New 播放器媒体事件参数(当前文件路径, 媒体信息, 快照, 保留当前字幕))
            If Not 保留当前字幕 Then
                开始自动加载字幕(当前文件路径)
                开始自动加载弹幕(当前文件路径)
                开始自动加载歌词(当前文件路径)
            End If
            RaiseEvent 状态已变化(Me, EventArgs.Empty)
            Catch ex As OperationCanceledException
                ' 新请求或停止操作会主动取消当前打开过程。
            Catch ex As Exception
                打开异常 = ex
            End Try
            If 已临时释放独占 Then Await 尝试恢复独占Async(原会话)
            If 打开异常 IsNot Nothing AndAlso Not 已释放 AndAlso
                Not 此次取消.IsCancellationRequested Then
                RaiseEvent 播放错误(Me, New 播放器错误事件参数(打开异常.Message, "无法播放媒体"))
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

    Private Async Function 设置会话WASAPI模式Async(目标 As 播放器会话, 独占 As Boolean,
                                                   取消标记 As CancellationToken) As Task
        Dim 完成源 As New TaskCompletionSource(Of Boolean)(TaskCreationOptions.RunContinuationsAsynchronously)
        Dim 设备处理 As EventHandler(Of 播放器事件参数) =
            Sub(sender, e)
                Try
                    Using 文档 = JsonDocument.Parse(e.详情JSON)
                        Dim 模式值 As JsonElement
                        If 文档.RootElement.TryGetProperty("exclusive", 模式值) AndAlso
                            模式值.GetBoolean() = 独占 Then 完成源.TrySetResult(True)
                        Dim 回退值 As JsonElement
                        If 独占 AndAlso 文档.RootElement.TryGetProperty("exclusiveFallback", 回退值) AndAlso
                            回退值.GetBoolean() Then
                            Dim 原因值 As JsonElement
                            Dim 原因 = If(文档.RootElement.TryGetProperty("reason", 原因值) AndAlso
                                        原因值.ValueKind = JsonValueKind.String,
                                        原因值.GetString(), "音频端点拒绝了独占模式，已继续使用共享模式。")
                            完成源.TrySetException(New 播放器异常(-1, 原因))
                        End If
                    End Using
                Catch
                End Try
            End Sub
        Dim 错误处理 As EventHandler(Of 播放器事件参数) =
            Sub(sender, e)
                If e.详情JSON.Contains("audio-exclusive-mode", StringComparison.Ordinal) Then
                    完成源.TrySetException(New 播放器异常(-1, 读取事件消息(e.详情JSON)))
                End If
            End Sub
        AddHandler 目标.设备变化, 设备处理
        AddHandler 目标.错误, 错误处理
        Using 超时源 = CancellationTokenSource.CreateLinkedTokenSource(取消标记)
            超时源.CancelAfter(TimeSpan.FromSeconds(10))
            Using 取消注册 = 超时源.Token.Register(Sub() 完成源.TrySetCanceled(超时源.Token))
                Try
                    目标.设置WASAPI独占模式(独占)
                    Await 完成源.Task
                Catch ex As OperationCanceledException When Not 取消标记.IsCancellationRequested
                    Throw New TimeoutException("等待 WASAPI 模式切换超时。", ex)
                Finally
                    RemoveHandler 目标.设备变化, 设备处理
                    RemoveHandler 目标.错误, 错误处理
                End Try
            End Using
        End Using
    End Function

    Private Async Function 尝试恢复独占Async(目标 As 播放器会话) As Task
        If 已释放 OrElse 目标 Is Nothing OrElse Not ReferenceEquals(会话, 目标) Then Return
        Try
            Await 设置会话WASAPI模式Async(目标, True, CancellationToken.None)
        Catch
            ' 保留原媒体优先；设备拒绝恢复独占时由原生错误事件报告并留在共享。
        End Try
    End Function

    Private Function 创建会话(解码器 As 解码模式, 色彩模式 As 色彩输出模式) As 播放器会话
        Return New 播放器会话(New 播放器配置 With {
            .解码器 = 解码器,
            .色彩模式 = 色彩模式,
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
        AddHandler 目标.操作完成, AddressOf 会话_操作完成
        AddHandler 目标.色彩模式变化, AddressOf 会话_色彩模式变化
        AddHandler 目标.设备变化, AddressOf 会话_设备变化
        AddHandler 目标.错误, AddressOf 会话_错误
    End Sub

    Private Sub 移除会话事件(目标 As 播放器会话)
        RemoveHandler 目标.状态变化, AddressOf 会话_状态变化
        RemoveHandler 目标.打开完成, AddressOf 会话_打开完成
        RemoveHandler 目标.操作完成, AddressOf 会话_操作完成
        RemoveHandler 目标.色彩模式变化, AddressOf 会话_色彩模式变化
        RemoveHandler 目标.设备变化, AddressOf 会话_设备变化
        RemoveHandler 目标.错误, AddressOf 会话_错误
    End Sub

    Private Sub 会话_状态变化(sender As Object, e As 播放器事件参数)
        If sender Is 会话 Then RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Private Sub 会话_打开完成(sender As Object, e As 播放器事件参数)
        If sender IsNot 会话 Then Return
        Try
            RaiseEvent 媒体已打开(Me,
                New 播放器媒体事件参数(当前文件路径, 会话.当前媒体信息, 会话.当前快照, False))
        Catch ex As 播放器异常
        End Try
    End Sub

    Private Sub 会话_操作完成(sender As Object, e As 播放器事件参数)
        If sender Is 会话 Then RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Private Sub 会话_色彩模式变化(sender As Object, e As 播放器事件参数)
        If sender IsNot 会话 Then Return
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
        Dim 快照 = 安全读取快照()
        If 快照 IsNot Nothing AndAlso 快照.是HDR源 Then
            RaiseEvent HDR输出状态已确认(Me, New 播放器HDR状态事件参数(取得HDR模式说明(快照)))
        End If
    End Sub

    Private Sub 会话_设备变化(sender As Object, e As 播放器事件参数)
        If sender IsNot 会话 Then Return
        Dim 快照 = 安全读取快照()
        If 快照 IsNot Nothing Then 当前解码器 = 快照.解码器
        Dim 解码回退 = False
        Dim 回退原因 As String = String.Empty
        Dim 独占回退 = False
        Dim 独占回退原因 As String = String.Empty
        Dim 音频不可用 = False
        Dim 音频不可用原因 As String = String.Empty
        Try
            Using 文档 = JsonDocument.Parse(e.详情JSON)
                Dim 独占 As JsonElement
                If 文档.RootElement.TryGetProperty("exclusive", 独占) Then
                    当前WASAPI模式 = If(独占.GetBoolean(), WASAPI共享模式.独占, WASAPI共享模式.共享)
                End If
                Dim 回退 As JsonElement
                If 文档.RootElement.TryGetProperty("fallback", 回退) Then
                    解码回退 = 回退.GetBoolean()
                ElseIf 文档.RootElement.TryGetProperty("hardwareFallback", 回退) Then
                    解码回退 = 回退.GetBoolean()
                End If
                Dim 原因 As JsonElement
                If 解码回退 AndAlso 文档.RootElement.TryGetProperty("reason", 原因) AndAlso
                    原因.ValueKind = JsonValueKind.String Then 回退原因 = 原因.GetString()
                Dim 独占回退值 As JsonElement
                If 文档.RootElement.TryGetProperty("exclusiveFallback", 独占回退值) Then
                    独占回退 = 独占回退值.GetBoolean()
                End If
                If 独占回退 AndAlso 文档.RootElement.TryGetProperty("reason", 原因) AndAlso
                    原因.ValueKind = JsonValueKind.String Then 独占回退原因 = 原因.GetString()
                Dim 音频不可用值 As JsonElement
                If 文档.RootElement.TryGetProperty("audioUnavailable", 音频不可用值) Then
                    音频不可用 = 音频不可用值.GetBoolean()
                End If
                If 音频不可用 AndAlso 文档.RootElement.TryGetProperty("reason", 原因) AndAlso
                    原因.ValueKind = JsonValueKind.String Then 音频不可用原因 = 原因.GetString()
            End Using
        Catch
        End Try
        If 解码回退 Then
            Dim 说明 = "GPU 解码已回退到 CPU"
            If Not String.IsNullOrWhiteSpace(回退原因) Then 说明 &= $"：{回退原因.Trim()}"
            RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(说明))
        End If
        If 独占回退 Then
            Dim 说明 = "音频设备当前无法使用独占模式，已切换到 WASAPI 共享模式继续播放。"
            If Not String.IsNullOrWhiteSpace(独占回退原因) Then 说明 &= $"{vbCrLf}{vbCrLf}{独占回退原因.Trim()}"
            RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(
                说明, True, "WASAPI 独占模式不可用"))
        End If
        If 音频不可用 Then
            Dim 说明 = "音频设备正被其他应用独占。媒体已按 WASAPI 共享模式继续播放，但设备释放前暂时无声。"
            If Not String.IsNullOrWhiteSpace(音频不可用原因) Then 说明 &= $"{vbCrLf}{vbCrLf}{音频不可用原因.Trim()}"
            RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(
                说明, True, "音频设备暂不可用"))
        End If
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
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
            Dim 候选字幕 = Await 外部字幕自动加载器.扫描同名字幕Async(媒体路径, 本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not ReferenceEquals(字幕加载取消, 本次取消) OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) Then Return
            发布外部字幕候选(候选字幕)
            If 候选字幕.Count = 0 Then Return

            候选轨道 = Await 外部字幕自动加载器.尝试加载候选字幕Async(
                候选字幕, 媒体路径, 本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not ReferenceEquals(字幕加载取消, 本次取消) OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) Then Return
            If 候选轨道 Is Nothing Then Return

            发布外部字幕(候选轨道)
            候选轨道 = Nothing
            RaiseEvent 外部字幕已加载(Me,
                New 播放器字幕事件参数(已导入外部字幕.路径, 已导入外部字幕.格式))
        Catch ex As OperationCanceledException
            ' 新媒体、停止或关闭会取消尚未完成的自动加载。
        Catch ex As NotSupportedException
            If Not 已释放 AndAlso Not 本次取消.IsCancellationRequested AndAlso
                String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) Then
                RaiseEvent 播放错误(Me, New 播放器错误事件参数(ex.Message, "字幕不受支持"))
            End If
        Catch
            ' 外部字幕是可选资源，加载失败不影响媒体播放。
        Finally
            候选轨道?.释放()
            If ReferenceEquals(字幕加载取消, 本次取消) Then 字幕加载取消 = Nothing
            本次取消.Dispose()
        End Try
    End Function

    Private Sub 发布外部字幕(轨道 As 外部字幕轨道)
        ArgumentNullException.ThrowIfNull(轨道)
        Dim 旧外部 = Interlocked.Exchange(已导入外部字幕, 轨道)
        Dim 旧内嵌 = Interlocked.Exchange(当前内嵌字幕, Nothing)
        Interlocked.Exchange(当前字幕轨道, 轨道)
        Volatile.Write(当前字幕来源索引, -1)
        旧内嵌?.释放()
        旧外部?.释放()
        RaiseEvent 字幕选择已变化(Me, EventArgs.Empty)
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Private Sub 发布外部字幕候选(候选字幕 As IEnumerable(Of 外部字幕候选))
        ArgumentNullException.ThrowIfNull(候选字幕)
        Volatile.Write(外部字幕候选快照, 候选字幕.ToArray())
        RaiseEvent 字幕选择已变化(Me, EventArgs.Empty)
        RaiseEvent 状态已变化(Me, EventArgs.Empty)
    End Sub

    Private Sub 添加外部字幕候选(路径 As String, 格式 As 外部字幕格式)
        Dim 完整路径 = Path.GetFullPath(路径)
        Dim 当前候选 = Volatile.Read(外部字幕候选快照)
        If 当前候选.Any(Function(x) String.Equals(x.路径, 完整路径, StringComparison.OrdinalIgnoreCase)) Then Return
        Dim 新候选(当前候选.Length) As 外部字幕候选
        Array.Copy(当前候选, 新候选, 当前候选.Length)
        新候选(新候选.Length - 1) = New 外部字幕候选(完整路径, 格式)
        Volatile.Write(外部字幕候选快照, 新候选)
    End Sub

    Private Sub 取消字幕加载()
        Dim 取消源 = Interlocked.Exchange(字幕加载取消, Nothing)
        取消源?.Cancel()
    End Sub

    Private Sub 释放当前字幕()
        取消字幕加载()
        Dim 当前 = Interlocked.Exchange(当前字幕轨道, Nothing)
        Dim 外部 = Interlocked.Exchange(已导入外部字幕, Nothing)
        Volatile.Write(外部字幕候选快照, Array.Empty(Of 外部字幕候选)())
        Dim 内嵌 = Interlocked.Exchange(当前内嵌字幕, Nothing)
        Volatile.Write(当前字幕来源索引, -2)
        当前?.释放()
        外部?.释放()
        内嵌?.释放()
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
                Not ReferenceEquals(弹幕加载取消, 本次取消) OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) OrElse
                候选资料库 Is Nothing Then Return
            Interlocked.Exchange(当前弹幕资料库, 候选资料库)
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
        Interlocked.Exchange(当前弹幕资料库, Nothing)
    End Sub

    Private Sub 开始自动加载歌词(媒体路径 As String)
        释放当前歌词()
        If Not Volatile.Read(当前媒体是纯音频) Then Return
        Dim 本次取消 As New CancellationTokenSource()
        歌词加载取消 = 本次取消
        Dim 忽略 = 自动加载同名歌词Async(媒体路径, 本次取消)
    End Sub

    Private Async Function 自动加载同名歌词Async(媒体路径 As String,
                                            本次取消 As CancellationTokenSource) As Task
        Try
            Dim candidate = Await LRC歌词自动加载器.尝试加载同名歌词Async(媒体路径, 本次取消.Token)
            If 本次取消.IsCancellationRequested OrElse 已释放 OrElse
                Not ReferenceEquals(歌词加载取消, 本次取消) OrElse
                Not String.Equals(当前文件路径, 媒体路径, StringComparison.OrdinalIgnoreCase) OrElse
                candidate Is Nothing Then Return
            Interlocked.Exchange(当前歌词资料, candidate)
            RaiseEvent 外部歌词已加载(Me, New 播放器歌词事件参数(candidate.路径, candidate.条目.Count))
        Catch ex As OperationCanceledException
        Catch ex As NotSupportedException
            If Not 已释放 AndAlso Not 本次取消.IsCancellationRequested Then
                RaiseEvent 操作提示(Me, New 播放器操作提示事件参数(
                    ex.Message, True, "不支持此歌词"))
            End If
        Catch
            ' 同名歌词是可选资源；无法读取时不影响音频播放。
        Finally
            If ReferenceEquals(歌词加载取消, 本次取消) Then 歌词加载取消 = Nothing
            本次取消.Dispose()
        End Try
    End Function

    Private Sub 释放当前歌词()
        Dim 取消源 = Interlocked.Exchange(歌词加载取消, Nothing)
        取消源?.Cancel()
        Interlocked.Exchange(当前歌词资料, Nothing)
    End Sub

    Private Sub 释放当前会话(Optional 保留已加载字幕 As Boolean = False)
        If Not 保留已加载字幕 Then
            释放当前字幕()
            释放当前弹幕()
            释放当前歌词()
        End If
        Dim 待释放 = 会话
        会话 = Nothing
        当前文件路径 = String.Empty
        Volatile.Write(当前媒体是纯音频, False)
        Volatile.Write(当前音乐包含封面, False)
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
                Return If(快照.实际色彩模式 = 色彩输出模式.峰值映射HDR,
                    $"{HDR规格文本(快照)} 真实高亮",
                    "HDR 目标不可用，已映射到 SDR")
            Case Else
                Return String.Empty
        End Select
    End Function

    Private Shared Function HDR规格文本(快照 As 播放器快照) As String
        Select Case 快照.HDR规格
            Case HDR格式.HDR10Plus : Return "HDR10+"
            Case HDR格式.HLG : Return "HLG"
            Case HDR格式.杜比视界 : Return "Dolby Vision 基础层→HDR10"
            Case HDR格式.HDRVivid : Return "HDR Vivid"
            Case Else : Return "HDR10"
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

    Public Sub New(文件路径 As String, 媒体信息 As 媒体信息, 快照 As 播放器快照,
                   保留剪辑区间 As Boolean)
        Me.文件路径 = 文件路径
        Me.媒体信息 = 媒体信息
        Me.快照 = 快照
        Me.保留剪辑区间 = 保留剪辑区间
    End Sub

    Public ReadOnly Property 文件路径 As String
    Public ReadOnly Property 媒体信息 As 媒体信息
    Public ReadOnly Property 快照 As 播放器快照
    Public ReadOnly Property 保留剪辑区间 As Boolean
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

Public NotInheritable Class 播放器操作提示事件参数
    Inherits EventArgs

    Public Sub New(说明 As String, Optional 弹出提示 As Boolean = False,
                   Optional 标题 As String = "播放提示")
        Me.说明 = If(说明, String.Empty)
        Me.弹出提示 = 弹出提示
        Me.标题 = If(标题, "播放提示")
    End Sub

    Public ReadOnly Property 说明 As String
    Public ReadOnly Property 弹出提示 As Boolean
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

Public NotInheritable Class 播放器歌词事件参数
    Inherits EventArgs

    Public Sub New(路径值 As String, 条目数值 As Integer)
        路径 = If(路径值, String.Empty)
        条目数 = Math.Max(0, 条目数值)
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 条目数 As Integer
End Class
