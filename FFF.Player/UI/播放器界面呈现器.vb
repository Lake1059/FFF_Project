''' <summary>
''' 集中维护播放器控件的显示状态、进度条交互和文本自适应宽度。
''' 它不持有原生会话，所有播放操作通过事件交还给控制器。
''' </summary>
Friend NotInheritable Class 播放器界面呈现器
    Implements IDisposable

    Private Enum 时间戳精度
        秒
        分钟
        小时
    End Enum

    Private ReadOnly 进度条 As LakeUI.ExcellentTrackBar
    Private ReadOnly 音量条 As LakeUI.ExcellentTrackBar
    Private ReadOnly 播放暂停按钮 As LakeUI.ModernButton
    Private ReadOnly 播放图标 As Image
    Private ReadOnly 暂停图标 As Image
    Private ReadOnly 解码按钮 As LakeUI.ModernButton
    Private ReadOnly HDR按钮 As LakeUI.ModernButton
    Private ReadOnly 视频编码按钮 As LakeUI.ModernButton
    Private ReadOnly 音频编码按钮 As LakeUI.ModernButton
    Private ReadOnly 声道数按钮 As LakeUI.ModernButton
    Private ReadOnly 时间标签 As LakeUI.HtmlColorLabel
    Private ReadOnly 状态栏 As Panel
    Private ReadOnly HDR占位 As Control
    Private ReadOnly 视频编码占位 As Control
    Private ReadOnly 音频编码占位 As Control
    Private ReadOnly 声道数占位 As Control
    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 正在切换提供器 As Func(Of Boolean)
    Private ReadOnly 解码器提供器 As Func(Of 解码模式)
    Private ReadOnly 色彩模式提供器 As Func(Of 色彩输出模式)
    Private ReadOnly 刷新计时器 As LakeUI.PrecisionTimer

    Private 正在更新进度条 As Boolean
    Private 正在拖动进度条 As Boolean
    Private 有媒体快照 As Boolean
    Private 已释放 As Boolean
    Private 显示精确时间戳 As Boolean
    Private 滚轮余量 As Integer

    Friend Sub New(进度条 As LakeUI.ExcellentTrackBar,
                   音量条 As LakeUI.ExcellentTrackBar,
                   播放暂停按钮 As LakeUI.ModernButton,
                   播放图标 As Image,
                   暂停图标 As Image,
                   解码按钮 As LakeUI.ModernButton,
                   HDR按钮 As LakeUI.ModernButton,
                   视频编码按钮 As LakeUI.ModernButton,
                   音频编码按钮 As LakeUI.ModernButton,
                   声道数按钮 As LakeUI.ModernButton,
                   时间标签 As LakeUI.HtmlColorLabel,
                   状态栏 As Panel,
                   HDR占位 As Control,
                   视频编码占位 As Control,
                   音频编码占位 As Control,
                   声道数占位 As Control,
                   画面控件 As 播放器画面控件,
                   快照提供器 As Func(Of 播放器快照),
                   正在切换提供器 As Func(Of Boolean),
                   解码器提供器 As Func(Of 解码模式),
                   色彩模式提供器 As Func(Of 色彩输出模式))
        Me.进度条 = 进度条
        Me.音量条 = 音量条
        Me.播放暂停按钮 = 播放暂停按钮
        Me.播放图标 = 播放图标
        Me.暂停图标 = 暂停图标
        Me.解码按钮 = 解码按钮
        Me.HDR按钮 = HDR按钮
        Me.视频编码按钮 = 视频编码按钮
        Me.音频编码按钮 = 音频编码按钮
        Me.声道数按钮 = 声道数按钮
        Me.时间标签 = 时间标签
        Me.状态栏 = 状态栏
        Me.HDR占位 = HDR占位
        Me.视频编码占位 = 视频编码占位
        Me.音频编码占位 = 音频编码占位
        Me.声道数占位 = 声道数占位
        Me.画面控件 = 画面控件
        Me.快照提供器 = 快照提供器
        Me.正在切换提供器 = 正在切换提供器
        Me.解码器提供器 = 解码器提供器
        Me.色彩模式提供器 = 色彩模式提供器
        刷新计时器 = New LakeUI.PrecisionTimer With {
            .Interval = 100,
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .SynchronizingObject = 进度条
        }

        配置控件()
        时间标签.AutoSize = True
        AddHandler 进度条.MouseDown, AddressOf 进度条_MouseDown
        AddHandler 进度条.MouseUp, AddressOf 进度条_MouseUp
        AddHandler 进度条.ValueChanged, AddressOf 进度条_ValueChanged
        AddHandler 音量条.ValueChanged, AddressOf 音量条_ValueChanged
        AddHandler 画面控件.音量滚轮, AddressOf 画面控件_音量滚轮
        AddHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick

        更新全部自适应宽度()
        更新媒体信息(Nothing, Nothing)
        更新播放按钮(播放状态.空闲)
        更新解码按钮()
        设置时间戳文本(TimeSpan.Zero, TimeSpan.Zero)
    End Sub

    Friend Event 请求跳转到关键帧 As EventHandler(Of 播放器跳转请求事件参数)
    Friend Event 音量已变更 As EventHandler(Of 播放器音量事件参数)
    Friend Event 播放状态已刷新 As EventHandler

    Friend ReadOnly Property 音量百分比 As Integer
        Get
            Return CInt(音量条.Value)
        End Get
    End Property

    Friend Sub 启动()
        If Not 已释放 Then 刷新计时器.Start()
    End Sub

    Friend Sub 刷新()
        If 已释放 Then Return
        更新解码按钮()
        Dim 快照 = 快照提供器()
        If 快照 Is Nothing Then
            If 有媒体快照 Then 清除媒体()
            Return
        End If

        有媒体快照 = True
        更新播放按钮(快照.状态)
        If Not 正在拖动进度条 Then
            正在更新进度条 = True
            Try
                If 快照.总时长 > TimeSpan.Zero Then
                    Dim 最大值 = 快照.总时长.TotalMilliseconds
                    If 进度条.Minimum <> 0 Then 进度条.Minimum = 0
                    If 进度条.Maximum <> 最大值 Then 进度条.Maximum = 最大值
                    Dim 新值 = Math.Clamp(快照.播放位置.TotalMilliseconds, 0, 最大值)
                    If 进度条.Value <> 新值 Then 进度条.Value = 新值
                Else
                    重置媒体进度条()
                End If
            Finally
                正在更新进度条 = False
            End Try
            设置时间戳文本(快照.播放位置, 快照.总时长)
        End If
        刷新HDR按钮(快照)
        RaiseEvent 播放状态已刷新(Me, EventArgs.Empty)
    End Sub

    Friend Sub 更新媒体信息(信息 As 媒体信息, 快照 As 播放器快照)
        Dim 视频流 As 媒体流信息 = Nothing
        Dim 音频流 As 媒体流信息 = Nothing
        If 信息 IsNot Nothing Then
            视频流 = 信息.流.FirstOrDefault(Function(x) x.类型 = "video" AndAlso Not x.是封面图 AndAlso
                (快照 Is Nothing OrElse x.索引 = 快照.当前视频流))
            音频流 = 信息.流.FirstOrDefault(Function(x) x.类型 = "audio" AndAlso
                (快照 Is Nothing OrElse x.索引 = 快照.当前音频流))
        End If

        状态栏.SuspendLayout()
        Try
            设置配对可见(视频编码按钮, 视频编码占位, 视频流 IsNot Nothing)
            设置配对可见(音频编码按钮, 音频编码占位, 音频流 IsNot Nothing)
            设置配对可见(声道数按钮, 声道数占位, 音频流 IsNot Nothing AndAlso 音频流.声道数 > 0)

            If 视频流 IsNot Nothing Then 设置自适应文本(视频编码按钮, 视频流.编码.ToUpperInvariant())
            If 音频流 IsNot Nothing Then
                设置自适应文本(音频编码按钮, 音频流.编码.ToUpperInvariant())
                设置自适应文本(声道数按钮, 格式化声道数(音频流.声道数))
            End If
        Finally
            状态栏.ResumeLayout(True)
        End Try

        刷新HDR按钮(快照)
    End Sub

    Friend Sub 清除媒体()
        有媒体快照 = False
        重置媒体进度条()
        设置时间戳文本(TimeSpan.Zero, TimeSpan.Zero)
        更新媒体信息(Nothing, Nothing)
        更新播放按钮(播放状态.空闲)
        RaiseEvent 播放状态已刷新(Me, EventArgs.Empty)
    End Sub

    Friend Sub 设置精确时间戳(启用 As Boolean)
        If 已释放 Then Return
        显示精确时间戳 = 启用
        Dim 快照 = 快照提供器()
        If 快照 Is Nothing Then
            设置时间戳文本(TimeSpan.Zero, TimeSpan.Zero)
        Else
            设置时间戳文本(快照.播放位置, 快照.总时长)
        End If
    End Sub

    Friend Sub 更新Dpi()
        更新全部自适应宽度()
    End Sub

    Friend Sub 调整音量(增量 As Integer)
        音量条.Value = Math.Clamp(音量条.Value + 增量, 0, 100)
    End Sub

    Private Sub 配置控件()
        进度条.Minimum = 0
        进度条.Maximum = 0
        进度条.SmallChange = 1000
        进度条.LargeChange = 5000
        进度条.Value = 0
        音量条.Minimum = 0
        音量条.Maximum = 100
        音量条.SmallChange = 5
        音量条.LargeChange = 10
        音量条.Value = 100
    End Sub

    Private Sub 刷新计时器_Tick(sender As Object, e As EventArgs)
        刷新()
    End Sub

    Private Sub 进度条_MouseDown(sender As Object, e As MouseEventArgs)
        If e.Button = MouseButtons.Left AndAlso 进度条可调整() Then 正在拖动进度条 = True
    End Sub

    Private Sub 进度条_MouseUp(sender As Object, e As MouseEventArgs)
        If e.Button <> MouseButtons.Left OrElse Not 正在拖动进度条 Then Return
        正在拖动进度条 = False
        请求跳转()
    End Sub

    Private Sub 进度条_ValueChanged(sender As Object, e As EventArgs)
        If 正在更新进度条 OrElse Not 进度条可调整() Then Return
        Dim 快照 = 快照提供器()
        Dim 总时长 = If(快照 Is Nothing, TimeSpan.Zero, 快照.总时长)
        设置时间戳文本(TimeSpan.FromMilliseconds(进度条.Value), 总时长)
        If Not 正在拖动进度条 Then 请求跳转()
    End Sub

    Private Sub 音量条_ValueChanged(sender As Object, e As EventArgs)
        If 已释放 Then Return
        RaiseEvent 音量已变更(Me, New 播放器音量事件参数(CSng(音量条.Value / 100.0F)))
    End Sub

    Private Sub 画面控件_音量滚轮(sender As Object, e As MouseEventArgs)
        If 已释放 OrElse e.Delta = 0 Then Return
        滚轮余量 += e.Delta
        Dim 刻度 = 滚轮余量 \ 120
        If 刻度 = 0 Then Return
        滚轮余量 -= 刻度 * 120
        调整音量(刻度 * 5)
    End Sub

    Private Sub 请求跳转()
        If Not 进度条可调整() Then Return
        RaiseEvent 请求跳转到关键帧(Me,
            New 播放器跳转请求事件参数(TimeSpan.FromMilliseconds(进度条.Value)))
    End Sub

    Private Function 进度条可调整() As Boolean
        If 进度条.Maximum <= 进度条.Minimum OrElse 正在切换提供器() Then Return False
        Dim 快照 = 快照提供器()
        Return 快照 IsNot Nothing AndAlso 快照.总时长 > TimeSpan.Zero AndAlso 播放器控制器.可操作(快照.状态)
    End Function

    Private Sub 更新播放按钮(状态 As 播放状态)
        Dim 图标 = If(状态 = 播放状态.正在播放, 暂停图标, 播放图标)
        If Not ReferenceEquals(播放暂停按钮.BackImage, 图标) Then 播放暂停按钮.BackImage = 图标
        If 播放暂停按钮.Text.Length > 0 Then 播放暂停按钮.Text = String.Empty
    End Sub

    Private Sub 重置媒体进度条()
        正在更新进度条 = True
        Try
            进度条.Minimum = 0
            进度条.Maximum = 0
            进度条.Value = 0
        Finally
            正在更新进度条 = False
            正在拖动进度条 = False
        End Try
    End Sub

    Private Sub 更新解码按钮()
        设置自适应文本(解码按钮, If(解码器提供器() = 解码模式.GPU, "GPU", "CPU"))
    End Sub

    Private Sub 刷新HDR按钮(快照 As 播放器快照)
        Dim 可见 = 快照 IsNot Nothing AndAlso 快照.是HDR源
        设置配对可见(HDR按钮, HDR占位, 可见)
        If Not 可见 Then Return

        Select Case 色彩模式提供器()
            Case 色彩输出模式.映射到SDR
                设置自适应文本(HDR按钮, "SDR")
            Case 色彩输出模式.原始HDR按SDR呈现
                设置自适应文本(HDR按钮, "原始")
            Case 色彩输出模式.峰值映射HDR
                设置自适应文本(HDR按钮, "HDR")
        End Select
    End Sub

    Private Sub 更新全部自适应宽度()
        For Each 按钮 In {解码按钮, HDR按钮, 视频编码按钮, 音频编码按钮, 声道数按钮}
            更新自适应宽度(按钮)
        Next
        更新自适应宽度(时间标签)
    End Sub

    Private Sub 设置自适应文本(控件 As Control, 文本 As String)
        Dim 显示文本 = If(String.IsNullOrWhiteSpace(文本), "?", 文本)
        If 控件.Text = 显示文本 Then Return
        控件.Text = 显示文本
        更新自适应宽度(控件)
    End Sub

    Private Shared Sub 设置配对可见(按钮 As Control, 占位 As Control, 可见 As Boolean)
        按钮.Visible = 可见
        占位.Visible = 可见
    End Sub

    Private Shared Sub 更新自适应宽度(控件 As Control)
        If TypeOf 控件 Is LakeUI.HtmlColorLabel Then
            Dim 标签 = DirectCast(控件, LakeUI.HtmlColorLabel)
            控件.Width = Math.Max(1, 标签.GetPreferredSize(Size.Empty).Width)
            Return
        End If

        Dim 缩放 = CSng(控件.DeviceDpi / 96.0F)
        Dim 文本宽度 = LakeUI.D3D_TextInterop.MeasureWidth(控件.Text, 控件.Font, 缩放)
        Dim 绘制保留宽度 As Integer
        If TypeOf 控件 Is LakeUI.ModernButton Then
            Dim 按钮 = DirectCast(控件, LakeUI.ModernButton)
            绘制保留宽度 = CInt(Math.Ceiling((按钮.BorderRadius * 2.0F + 按钮.BorderSize) * 缩放))
        End If
        控件.Width = Math.Max(1, 文本宽度 + 控件.Padding.Left + 控件.Padding.Right + 绘制保留宽度)
    End Sub

    Private Shared Function 格式化声道数(声道数 As Integer) As String
        Select Case 声道数
            Case 1 : Return "1.0"
            Case 2 : Return "2.0"
            Case 6 : Return "5.1"
            Case 8 : Return "7.1"
            Case Else : Return $"{声道数}ch"
        End Select
    End Function

    Private Sub 设置时间戳文本(当前位置 As TimeSpan, 总时长 As TimeSpan)
        Dim 精度 = 取得时间戳精度(当前位置, 总时长)
        Dim HTML = $"{格式化彩色时长(当前位置, 精度, 显示精确时间戳)}<font color=""#AAB0B9""> / </font>{格式化彩色时长(总时长, 精度)}"
        设置自适应文本(时间标签, HTML)
    End Sub

    Private Shared Function 取得时间戳精度(当前位置 As TimeSpan, 总时长 As TimeSpan) As 时间戳精度
        Dim 最大时长 = If(当前位置 >= 总时长, 当前位置, 总时长)
        If 最大时长 >= TimeSpan.FromHours(1) Then Return 时间戳精度.小时
        If 最大时长 >= TimeSpan.FromMinutes(1) Then Return 时间戳精度.分钟
        Return 时间戳精度.秒
    End Function

    Private Shared Function 格式化彩色时长(时长 As TimeSpan, 精度 As 时间戳精度,
                                             Optional 显示毫秒 As Boolean = False) As String
        If 时长 < TimeSpan.Zero Then 时长 = TimeSpan.Zero
        Dim 小时 = CLng(Math.Floor(时长.TotalHours))
        Dim 分钟 = 时长.Minutes
        Dim 秒 = 时长.Seconds
        Const 小时颜色 = "#C3A2E4"
        Const 分钟颜色 = "#87B7ED"
        Const 秒颜色 = "#83BF9B"
        Const 冒号颜色 = "#E0A667"
        Const 小数颜色 = "#D2C084"
        Dim 小数 = If(显示毫秒,
            $"<font color=""{冒号颜色}"">.</font><font color=""{小数颜色}"">{时长.Milliseconds:000}</font>",
            String.Empty)
        Select Case 精度
            Case 时间戳精度.小时
                Return $"<font color=""{小时颜色}"">{小时:00}</font><font color=""{冒号颜色}"">:</font><font color=""{分钟颜色}"">{分钟:00}</font><font color=""{冒号颜色}"">:</font><font color=""{秒颜色}"">{秒:00}</font>{小数}"
            Case 时间戳精度.分钟
                Return $"<font color=""{分钟颜色}"">{分钟:00}</font><font color=""{冒号颜色}"">:</font><font color=""{秒颜色}"">{秒:00}</font>{小数}"
            Case Else
                Return $"<font color=""{秒颜色}"">{秒:00}</font>{小数}"
        End Select
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        刷新计时器.Stop()
        RemoveHandler 进度条.MouseDown, AddressOf 进度条_MouseDown
        RemoveHandler 进度条.MouseUp, AddressOf 进度条_MouseUp
        RemoveHandler 进度条.ValueChanged, AddressOf 进度条_ValueChanged
        RemoveHandler 音量条.ValueChanged, AddressOf 音量条_ValueChanged
        RemoveHandler 画面控件.音量滚轮, AddressOf 画面控件_音量滚轮
        RemoveHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick
        刷新计时器.Dispose()
    End Sub
End Class

Friend NotInheritable Class 播放器跳转请求事件参数
    Inherits EventArgs

    Friend Sub New(位置 As TimeSpan)
        Me.位置 = 位置
    End Sub

    Friend ReadOnly Property 位置 As TimeSpan
End Class

Friend NotInheritable Class 播放器音量事件参数
    Inherits EventArgs

    Friend Sub New(音量 As Single)
        Me.音量 = 音量
    End Sub

    Friend ReadOnly Property 音量 As Single
End Class
