Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.Runtime.InteropServices.WindowsRuntime
Imports System.Threading
Imports System.Threading.Tasks
Imports Windows.Foundation
Imports Windows.Media
Imports Windows.Storage.Streams

''' <summary>
''' 系统媒体面板（Windows SMTC）的唯一 WinRT 封装。
'''
''' <para>只使用 <c>SystemMediaTransportControlsInterop.GetForWindow(Form1.Handle)</c>，
''' 不构造 <c>Windows.Media.Playback.MediaPlayer</c>——那会拉起第二套音频引擎，
''' 与 FFF 内核的 WASAPI 会话冲突。3FP 的播放状态、元数据、时间轴由 Form1 推送进来；
''' 系统按钮/seek/倍速由这里回调出去，Form1 负责切回 UI 线程执行。</para>
'''
''' <para><b>线程</b>：WinRT 控件只能在创建它的 UI 线程上访问。本类的
''' <see cref="发布卡片"/> / <see cref="发布状态"/> / <see cref="发布时间轴"/> 可被任意线程调用，
''' 内部统一用构造时传入的 <see cref="SynchronizationContext"/> Post 回 UI 线程；
''' <see cref="初始化(IntPtr)"/>、<see cref="关闭"/>、<see cref="设置按钮可用性"/>、<see cref="清空卡片"/>
''' 必须由 UI 线程调用（Form1 的接线保证）。</para>
'''
''' <para><b>失败策略</b>：SMTC 是“锦上添花”，任何一步失败只记录 Debug 输出并降级，
''' 绝不向播放路径抛异常。初始化失败后 <see cref="已启用"/> 保持 False，不注册卡片。</para>
''' </summary>
Public NotInheritable Class 系统媒体会话
    Implements IDisposable

    Private ReadOnly 同步上下文 As SynchronizationContext
    Private ReadOnly 命令接收器 As Func(Of 系统媒体按钮, Boolean)
    Private ReadOnly 跳转请求接收器 As Func(Of TimeSpan, Boolean)
    Private ReadOnly 倍速请求接收器 As Func(Of Double, Boolean)

    Private 控件 As SystemMediaTransportControls
    Private 已启用 As Boolean
    Private 已关闭 As Boolean
    Private 已释放 As Boolean

    Private 上次播放状态 As 系统媒体播放状态 = 系统媒体播放状态.已关闭
    Private 上次媒体标识 As String
    Private 上次显示类型 As 系统媒体显示类型?
    Private 上次标题 As String
    Private 上次艺术家 As String
    Private 上次专辑 As String
    Private 上次专辑艺术家 As String
    Private 上次轨道号 As UInteger
    Private 上次时间轴 As 系统媒体时间轴?
    Private 上次倍速 As Double = 1.0R

    Private ReadOnly 待发布 As New Queue(Of 系统媒体更新)()
    Private 发布已排队 As Integer
    Private ReadOnly 待执行 As New Queue(Of Action)()
    Private 执行已排队 As Integer

    ''' <summary>
    ''' 创建一个可回收的 SMTC 会话壳；此时不碰 WinRT，真正创建要等 <see cref="初始化(IntPtr)"/>。
    ''' </summary>
    Public Sub New(同步上下文 As SynchronizationContext,
                   命令接收器 As Func(Of 系统媒体按钮, Boolean),
                   跳转请求接收器 As Func(Of TimeSpan, Boolean),
                   倍速请求接收器 As Func(Of Double, Boolean))
        Me.同步上下文 = 同步上下文
        Me.命令接收器 = 命令接收器
        Me.跳转请求接收器 = 跳转请求接收器
        Me.倍速请求接收器 = 倍速请求接收器
    End Sub

    Public ReadOnly Property 已启用面板 As Boolean
        Get
            Return 已启用
        End Get
    End Property

    ''' <summary>
    ''' 惰性创建系统媒体会话并挂事件；必须由 UI 线程、在窗口句柄已创建后调用。
    ''' 返回 False 表示当前环境不支持或创建失败，调用方应保持“无系统媒体卡”并继续播放。
    ''' </summary>
    Public Function 初始化(窗口句柄 As IntPtr) As Boolean
        If 已释放 OrElse 已关闭 Then Return False
        If 控件 IsNot Nothing Then Return 已启用
        If 窗口句柄 = IntPtr.Zero Then Return False

        Try
            控件 = SystemMediaTransportControlsInterop.GetForWindow(窗口句柄)
            If 控件 Is Nothing Then Return False

            ' 先保持不可见：等第一张真正有意义的卡片发布时再 IsEnabled=True，
            ' 避免启动/打开失败时先闪一个空白媒体卡。
            控件.IsEnabled = False
            控件.PlaybackStatus = MediaPlaybackStatus.Closed
            控件.PlaybackRate = 1.0R
            设置所有按钮(False)
            控件.DisplayUpdater.ClearAll()

            AddHandler 控件.ButtonPressed, AddressOf 控件_按钮按下
            AddHandler 控件.PlaybackPositionChangeRequested, AddressOf 控件_位置跳转请求
            AddHandler 控件.PlaybackRateChangeRequested, AddressOf 控件_倍速请求

            已启用 = True
            Return True
        Catch ex As Exception
            Debug.WriteLine("SMTC：初始化失败，降级为无系统媒体卡：" & ex.GetType().Name & " - " & ex.Message)
            尝试清理控件()
            已启用 = False
            Return False
        End Try
    End Function

    ''' <summary>
    ''' 清空卡片并关闭面板，但保留本对象（同一窗口后续打开媒体可再次 <see cref="初始化(IntPtr)"/>）。
    ''' 必须由 UI 线程调用，且在 Form1 句柄销毁之前。
    ''' </summary>
    Public Sub 清空卡片()
        If 控件 Is Nothing Then Return
        ' 清空必须同时丢掉尚未执行的旧发布项；否则“停止/切图片”先把卡片关掉，
        ' 排队的旧卡片随后又把面板点亮，形成无法控制的幽灵卡片。
        SyncLock 待发布
            待发布.Clear()
        End SyncLock
        Try
            控件.PlaybackStatus = MediaPlaybackStatus.Closed
            控件.DisplayUpdater.ClearAll()
            重置时间轴()
            设置所有按钮(False)
            控件.IsEnabled = False
        Catch ex As Exception
            Debug.WriteLine("SMTC：清空卡片失败（已忽略）：" & ex.GetType().Name)
        End Try
        上次播放状态 = 系统媒体播放状态.已关闭
        上次媒体标识 = Nothing
        上次显示类型 = Nothing
        上次标题 = Nothing
        上次艺术家 = Nothing
        上次专辑 = Nothing
        上次专辑艺术家 = Nothing
        上次轨道号 = 0UI
        上次时间轴 = Nothing
    End Sub

    ''' <summary>关闭整个面板并摘除事件。可重复调用；关闭后本对象不再可用。</summary>
    Public Sub 关闭()
        If 已关闭 Then Return
        已关闭 = True
        已启用 = False
        清空卡片()
        If 控件 IsNot Nothing Then
            Try
                RemoveHandler 控件.ButtonPressed, AddressOf 控件_按钮按下
                RemoveHandler 控件.PlaybackPositionChangeRequested, AddressOf 控件_位置跳转请求
                RemoveHandler 控件.PlaybackRateChangeRequested, AddressOf 控件_倍速请求
            Catch ex As Exception
                Debug.WriteLine("SMTC：摘事件失败（已忽略）：" & ex.GetType().Name)
            End Try
        End If
        控件 = Nothing
        SyncLock 待发布
            待发布.Clear()
        End SyncLock
        SyncLock 待执行
            待执行.Clear()
        End SyncLock
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        关闭()
        已释放 = True
    End Sub

    ''' <summary>
    ''' 发布一张完整的媒体卡片。Form1 负责去重（同一媒体标识 + 标题 + 艺术家）；本方法
    ''' 仍会记住字段，避免同一进程内重复 Update。轨道号为 0 时不写 TrackNumber。
    ''' </summary>
    Public Sub 发布卡片(媒体标识 As String, 标题 As String, 艺术家 As String,
                        专辑 As String, 专辑艺术家 As String, 轨道号 As UInteger,
                        显示类型 As 系统媒体显示类型, 倍速 As Double)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.媒体标识 = If(媒体标识, String.Empty)
        项.标题 = If(标题, String.Empty)
        项.艺术家 = If(艺术家, String.Empty)
        项.专辑 = If(专辑, String.Empty)
        项.专辑艺术家 = If(专辑艺术家, String.Empty)
        项.轨道号 = If(轨道号 > 0UI, New UInteger?(轨道号), Nothing)
        项.显示类型 = 显示类型
        项.倍速 = 倍速
        排队发布(项)
    End Sub

    ''' <summary>发布播放状态；传入本地中立枚举，内部转成 WinRT 枚举。</summary>
    Public Sub 发布状态(状态 As 系统媒体播放状态)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.状态 = 状态
        排队发布(项)
    End Sub

    ''' <summary>发布时间轴；只在播放中由 Form1 的 1 Hz UI 计时器调用。</summary>
    Public Sub 发布时间轴(时间轴 As 系统媒体时间轴)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.时间轴 = 时间轴
        排队发布(项)
    End Sub

    ''' <summary>单独发布倍速；用户改倍速时调用，避免等待下一次时间轴 tick。</summary>
    Public Sub 发布倍速(倍速 As Double)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.倍速 = 倍速
        排队发布(项)
    End Sub

    ''' <summary>时间轴与倍速合并发布，减少跨线程队列项。</summary>
    Public Sub 发布时间轴和倍速(时间轴 As 系统媒体时间轴, 倍速 As Double)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.时间轴 = 时间轴
        项.倍速 = 倍速
        排队发布(项)
    End Sub

    ''' <summary>
    ''' 设置系统按钮可用性。由 UI 线程调用；Form1 每次状态/播放列表变化时按快照算一遍。
    ''' 参数语义直接对应用户看到的按钮，内部不再从播放状态猜。
    ''' </summary>
    Public Sub 设置按钮可用性(可播放 As Boolean, 可暂停 As Boolean, 可停止 As Boolean,
                              可跳转 As Boolean, 可下一曲 As Boolean, 可上一曲 As Boolean)
        If 控件 Is Nothing Then Return
        Try
            控件.IsPlayEnabled = 可播放
            控件.IsPauseEnabled = 可暂停
            控件.IsStopEnabled = 可停止
            控件.IsFastForwardEnabled = 可跳转
            控件.IsRewindEnabled = 可跳转
            控件.IsNextEnabled = 可下一曲
            控件.IsPreviousEnabled = 可上一曲
            ' 仅保留音视频两种卡片能力，不让系统面板出现录音/频道按钮。
            控件.IsRecordEnabled = False
            控件.IsChannelUpEnabled = False
            控件.IsChannelDownEnabled = False
        Catch ex As Exception
            Debug.WriteLine("SMTC：设置按钮可用性失败（已忽略）：" & ex.GetType().Name)
        End Try
    End Sub

    ' ---- WinRT 事件：全部回到 UI 线程，绝不直接操作播放器 ----

    ''' <summary>测试钩子：不走真实 WinRT ButtonPressed，直接把按钮投给已注入的
    ''' <see cref="命令接收器"/>。FFF.Player.Tests 借此断言“按钮 → 接收器(=播放器方法)”
    ''' 的分派确实发生，而不是只看能不能建会话。生产路径仍由 控件_按钮按下 触发。</summary>
    Friend Sub 测试用分派按钮(按钮 As 系统媒体按钮)
        Dim 结果 As Boolean
        Try
            If 命令接收器 IsNot Nothing Then
                结果 = 命令接收器(按钮)
            End If
        Catch ex As Exception
            Debug.WriteLine("SMTC：测试分派按钮回调失败（已忽略）：" & ex.GetType().Name)
        End Try
    End Sub

    Private Sub 控件_按钮按下(sender As SystemMediaTransportControls,
                              args As SystemMediaTransportControlsButtonPressedEventArgs)
        Dim 结果 As Boolean
        Try
            If 命令接收器 IsNot Nothing Then
                结果 = 命令接收器(CType(CInt(args.Button), 系统媒体按钮))
            End If
        Catch ex As Exception
            Debug.WriteLine("SMTC：按钮回调失败（已忽略）：" & ex.GetType().Name)
            结果 = False
        End Try
        ' 按钮已被 Form1 受理时，下一次 1 Hz 时间轴会覆盖；这里不强行刷新旧快照。
    End Sub

    Private Sub 控件_位置跳转请求(sender As SystemMediaTransportControls,
                                  args As PlaybackPositionChangeRequestedEventArgs)
        Try
            If 跳转请求接收器 IsNot Nothing Then
                跳转请求接收器(args.RequestedPlaybackPosition)
            End If
        Catch ex As Exception
            Debug.WriteLine("SMTC：seek 回调失败（已忽略）：" & ex.GetType().Name)
        End Try
    End Sub

    Private Sub 控件_倍速请求(sender As SystemMediaTransportControls,
                              args As PlaybackRateChangeRequestedEventArgs)
        Try
            If 倍速请求接收器 IsNot Nothing Then
                倍速请求接收器(args.RequestedPlaybackRate)
            End If
        Catch ex As Exception
            Debug.WriteLine("SMTC：倍速回调失败（已忽略）：" & ex.GetType().Name)
        End Try
    End Sub

    ' ---- 发布队列：所有 WinRT 写操作都在 UI 线程 ----

    Private Sub 排队发布(项 As 系统媒体更新)
        If 已释放 OrElse 已关闭 Then Return
        SyncLock 待发布
            待发布.Enqueue(项)
        End SyncLock
        If Interlocked.Exchange(发布已排队, 1) = 0 Then
            If 同步上下文 IsNot Nothing Then
                Try
                    同步上下文.Post(AddressOf 发布队列回调, Nothing)
                Catch ex As Exception
                    Debug.WriteLine("SMTC：Post 发布队列失败，改为同步发布：" & ex.GetType().Name)
                    发布队列回调(Nothing)
                End Try
            Else
                发布队列回调(Nothing)
            End If
        End If
    End Sub

    Private Sub 发布队列回调(状态 As Object)
        Interlocked.Exchange(发布已排队, 0)
        If 已释放 OrElse 已关闭 Then Return
        Do
            Dim 项 As 系统媒体更新
            SyncLock 待发布
                If 待发布.Count = 0 Then Exit Do
                项 = 待发布.Dequeue()
            End SyncLock
            Try
                应用更新(项)
            Catch ex As Exception
                Debug.WriteLine("SMTC：应用发布项失败（已忽略）：" & ex.GetType().Name)
            End Try
        Loop
    End Sub

    Private Sub 应用更新(项 As 系统媒体更新)
        If 控件 Is Nothing Then Return

        If 项.状态.HasValue Then
            Dim 新状态 = CType(CInt(项.状态.Value), MediaPlaybackStatus)
            If 新状态 <> 上次播放状态 Then
                控件.PlaybackStatus = 新状态
                上次播放状态 = CType(CInt(新状态), 系统媒体播放状态)
            End If
        End If

        If 项.显示类型.HasValue OrElse 项.媒体标识 IsNot Nothing OrElse
           项.标题 IsNot Nothing OrElse 项.艺术家 IsNot Nothing OrElse
           项.专辑 IsNot Nothing OrElse 项.专辑艺术家 IsNot Nothing OrElse
           项.轨道号.HasValue Then
            应用卡片信息(项)
        End If

        If 项.时间轴.HasValue Then
            应用时间轴(项.时间轴.Value)
        End If

        If 项.倍速.HasValue Then
            应用倍速(项.倍速.Value)
        End If
    End Sub

    Private Sub 应用卡片信息(项 As 系统媒体更新)
        Dim 类型 = If(项.显示类型.HasValue,
                      项.显示类型.Value,
                      If(上次显示类型.HasValue, 上次显示类型.Value, 系统媒体显示类型.音乐))
        Dim 类型已变化 = 上次显示类型.HasValue AndAlso 上次显示类型.Value <> 类型
        If 类型已变化 Then
            ' 换类型（音乐 ↔ 视频）时先清旧字段，避免 Music.Title 残留在 Video 卡上。
            控件.DisplayUpdater.ClearAll()
            上次媒体标识 = Nothing
            上次标题 = Nothing
            上次艺术家 = Nothing
            上次专辑 = Nothing
            上次专辑艺术家 = Nothing
            上次轨道号 = 0UI
        End If

        控件.DisplayUpdater.Type = CType(CInt(类型), MediaPlaybackType)
        上次显示类型 = 类型

        If 项.媒体标识 IsNot Nothing AndAlso Not String.Equals(项.媒体标识, 上次媒体标识, StringComparison.Ordinal) Then
            控件.DisplayUpdater.AppMediaId = 项.媒体标识
            上次媒体标识 = 项.媒体标识
        End If

        If 类型 = 系统媒体显示类型.视频 Then
            控件.DisplayUpdater.VideoProperties.Title = If(项.标题, String.Empty)
        Else
            控件.DisplayUpdater.MusicProperties.Title = If(项.标题, String.Empty)
            控件.DisplayUpdater.MusicProperties.Artist = If(项.艺术家, String.Empty)
            控件.DisplayUpdater.MusicProperties.AlbumTitle = If(项.专辑, String.Empty)
            控件.DisplayUpdater.MusicProperties.AlbumArtist = If(项.专辑艺术家, String.Empty)
            If 项.轨道号.HasValue AndAlso 项.轨道号.Value > 0UI Then
                控件.DisplayUpdater.MusicProperties.TrackNumber = 项.轨道号.Value
            End If
        End If

        控件.DisplayUpdater.Update()
        控件.IsEnabled = True

        上次标题 = 项.标题
        上次艺术家 = 项.艺术家
        上次专辑 = 项.专辑
        上次专辑艺术家 = 项.专辑艺术家
        上次轨道号 = If(项.轨道号.HasValue, 项.轨道号.Value, 0UI)
    End Sub

    Private Sub 应用时间轴(值 As 系统媒体时间轴)
        Dim 属性 As New SystemMediaTransportControlsTimelineProperties()
        属性.StartTime = 值.StartTime
        属性.EndTime = 值.EndTime
        属性.Position = 值.Position
        属性.MinSeekTime = 值.MinSeekTime
        属性.MaxSeekTime = 值.MaxSeekTime
        控件.UpdateTimelineProperties(属性)
        上次时间轴 = 值
    End Sub

    Private Sub 应用倍速(倍速 As Double)
        If Not Double.IsFinite(倍速) OrElse 倍速 <= 0R Then Return
        If 倍速 = 上次倍速 Then Return
        控件.PlaybackRate = 倍速
        上次倍速 = 倍速
    End Sub

    Private Sub 重置时间轴()
        If 控件 Is Nothing Then Return
        Dim 属性 As New SystemMediaTransportControlsTimelineProperties()
        属性.StartTime = TimeSpan.Zero
        属性.EndTime = TimeSpan.Zero
        属性.Position = TimeSpan.Zero
        属性.MinSeekTime = TimeSpan.Zero
        属性.MaxSeekTime = TimeSpan.Zero
        控件.UpdateTimelineProperties(属性)
        上次时间轴 = Nothing
    End Sub

    Private Sub 设置所有按钮(值 As Boolean)
        If 控件 Is Nothing Then Return
        控件.IsPlayEnabled = 值
        控件.IsPauseEnabled = 值
        控件.IsStopEnabled = 值
        控件.IsFastForwardEnabled = 值
        控件.IsRewindEnabled = 值
        控件.IsNextEnabled = 值
        控件.IsPreviousEnabled = 值
    End Sub

    Private Sub 尝试清理控件()
        控件 = Nothing
    End Sub

    ''' <summary>内部发布项；字段为 Nothing 表示本次不改。</summary>
    Private Structure 系统媒体更新
        Public 状态 As 系统媒体播放状态?
        Public 媒体标识 As String
        Public 标题 As String
        Public 艺术家 As String
        Public 专辑 As String
        Public 专辑艺术家 As String
        Public 轨道号 As UInteger?
        Public 显示类型 As 系统媒体显示类型?
        Public 时间轴 As 系统媒体时间轴?
        Public 倍速 As Double?
    End Structure

End Class

''' <summary>时间轴发布值；全部用 TimeSpan，避免秒/毫秒单位在宿主与 WinRT 之间漂移。</summary>
Public Structure 系统媒体时间轴
    Public StartTime As TimeSpan
    Public EndTime As TimeSpan
    Public Position As TimeSpan
    Public MinSeekTime As TimeSpan
    Public MaxSeekTime As TimeSpan
End Structure
