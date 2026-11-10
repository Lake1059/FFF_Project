''' <summary>
''' 集中维护播放器控件的显示状态、进度条交互和文本自适应宽度。
''' 它不持有原生会话，所有播放操作通过事件交还给控制器。
''' </summary>
Friend NotInheritable Class 播放器界面呈现器
    Implements IDisposable

    Private Const 图片时间戳文本 As String = "图片"

    ' 与信息图层"双耳：HRTF 空间音频"那一行同色，同一件事在状态栏与信息层里
    ' 必须长得一样，否则用户会以为它们是两件事。
    Private Shared ReadOnly 双耳高亮色 As Color = Color.FromArgb(CByte(255), CByte(&H55), CByte(&HE7), CByte(&HEA))

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
    Private ReadOnly 双耳按钮 As LakeUI.ModernButton
    Private ReadOnly 时间标签 As LakeUI.HtmlColorLabel
    Private ReadOnly 状态栏 As LakeUI.ModernPanel
    Private ReadOnly HDR占位 As Control
    Private ReadOnly 视频编码占位 As Control
    Private ReadOnly 音频编码占位 As Control
    Private ReadOnly 声道数占位 As Control
    Private ReadOnly 双耳占位 As Control
    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 双耳状态提供器 As Func(Of 双耳状态)
    Private ReadOnly 正在切换提供器 As Func(Of Boolean)
    Private ReadOnly 解码器提供器 As Func(Of 解码模式)
    Private ReadOnly 刷新计时器 As LakeUI.PrecisionTimer

    Private 正在更新进度条 As Boolean
    Private 正在拖动进度条 As Boolean
    Private 有媒体快照 As Boolean
    Private 当前媒体信息 As 媒体信息
    Private 已显示视频流索引 As Integer = -1
    Private 已显示音频流索引 As Integer = -1
    Private 已释放 As Boolean
    Private 显示精确时间戳 As Boolean
    Private 滚轮余量 As Integer
    Private 未知时长已知上限毫秒 As Double

    ' 未知总时长时保留可拖动的初始量程；已读取范围超过它后继续扩展。
    Private Const 未知时长虚拟上限毫秒 As Double = 60000

    Private 上次时间戳测量文本 As String
    Private 图片模式已启用 As Boolean

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
                   双耳按钮 As LakeUI.ModernButton,
                   时间标签 As LakeUI.HtmlColorLabel,
                   状态栏 As LakeUI.ModernPanel,
                   HDR占位 As Control,
                   视频编码占位 As Control,
                   音频编码占位 As Control,
                   声道数占位 As Control,
                   双耳占位 As Control,
                   画面控件 As 播放器画面控件,
                   快照提供器 As Func(Of 播放器快照),
                   双耳状态提供器 As Func(Of 双耳状态),
                   正在切换提供器 As Func(Of Boolean),
                   解码器提供器 As Func(Of 解码模式))
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
        Me.双耳按钮 = 双耳按钮
        Me.时间标签 = 时间标签
        Me.状态栏 = 状态栏
        Me.HDR占位 = HDR占位
        Me.视频编码占位 = 视频编码占位
        Me.音频编码占位 = 音频编码占位
        Me.声道数占位 = 声道数占位
        Me.双耳占位 = 双耳占位
        Me.画面控件 = 画面控件
        Me.快照提供器 = 快照提供器
        Me.双耳状态提供器 = 双耳状态提供器
        Me.正在切换提供器 = 正在切换提供器
        Me.解码器提供器 = 解码器提供器
        刷新计时器 = New LakeUI.PrecisionTimer With {
            .Interval = 100,
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .SynchronizingObject = 进度条
        }

        配置控件()
        时间标签.AutoSize = False
        AddHandler 进度条.MouseDown, AddressOf 进度条_MouseDown
        AddHandler 进度条.MouseUp, AddressOf 进度条_MouseUp
        AddHandler 进度条.ValueChanged, AddressOf 进度条_ValueChanged
        AddHandler 音量条.ValueChanged, AddressOf 音量条_ValueChanged
        AddHandler 时间标签.MouseDoubleClick, AddressOf 时间标签_MouseDoubleClick
        AddHandler 时间标签.MouseClick, AddressOf 时间标签_MouseClick
        AddHandler 画面控件.音量滚轮, AddressOf 画面控件_音量滚轮
        AddHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick

        更新全部自适应宽度()
        更新媒体信息(Nothing, Nothing)
        更新播放按钮(播放状态.空闲)
        更新解码按钮()
        ' 初始就要藏起来：否则状态栏上会先闪一个还没接上会话的 "HRTF"。
        刷新双耳按钮()
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

    Friend Sub 设置音量百分比(百分比 As Integer)
        If 已释放 Then Return
        音量条.Value = Math.Clamp(百分比, 0, 100)
    End Sub

    Friend Sub 启动()
        If Not 已释放 Then 刷新计时器.Start()
    End Sub

    Friend Sub 刷新()
        If 已释放 Then Return
        更新解码按钮()
        ' 双耳徽章挂在会话上而不是快照上：设备一换、插槽一装，它就该变，不必等
        ' 下一帧解码。会话缺席时提供器给"不可用"，徽章自己隐藏。
        刷新双耳按钮()
        Dim 快照 = 快照提供器()
        If 快照 Is Nothing Then
            If 有媒体快照 Then 清除媒体()
            Return
        End If

        有媒体快照 = True
        ' 切流由原生工作线程异步完成，按实际快照刷新，不能在发出切流请求时提前更新。
        If 当前媒体信息 IsNot Nothing AndAlso
            (快照.当前视频流 <> 已显示视频流索引 OrElse 快照.当前音频流 <> 已显示音频流索引) Then
            更新媒体信息(当前媒体信息, 快照)
        End If
        更新播放按钮(快照.状态)
        ' 图片模式不计算时间轴，时间戳位置显示媒体类型。
        If 图片模式已启用 Then
            设置时间戳文本(TimeSpan.Zero, TimeSpan.Zero)
            刷新HDR按钮(快照)
            RaiseEvent 播放状态已刷新(Me, EventArgs.Empty)
            Return
        End If
        If Not 正在拖动进度条 Then
            正在更新进度条 = True
            Try
                If 快照.已知总时长 AndAlso 快照.总时长 > TimeSpan.Zero Then
                    未知时长已知上限毫秒 = 0
                    Dim 最大值 = 快照.总时长.TotalMilliseconds
                    If 进度条.Minimum <> 0 Then 进度条.Minimum = 0
                    If 进度条.Maximum <> 最大值 Then 进度条.Maximum = 最大值
                    Dim 新值 = Math.Clamp(快照.播放位置.TotalMilliseconds, 0, 最大值)
                    If 进度条.Value <> 新值 Then 进度条.Value = 新值
                Else
                    Dim 当前位置 = Math.Max(0, 快照.播放位置.TotalMilliseconds)
                    未知时长已知上限毫秒 = Math.Max(未知时长已知上限毫秒, Math.Max(当前位置, 快照.已读取范围.TotalMilliseconds))
                    If 进度条.Minimum <> 0 Then 进度条.Minimum = 0
                    ' 未知时长模式以虚拟上限维持非退化量程（见 未知时长虚拟上限毫秒 注释），
                    ' 保证起步阶段点击不落 0；已探明范围超过虚拟上限后量程跟随增长。
                    Dim 量程上限 = Math.Max(未知时长已知上限毫秒, 未知时长虚拟上限毫秒)
                    If 进度条.Maximum <> 量程上限 Then
                        进度条.Maximum = 量程上限
                    End If
                    ' 显示值仍 clamp 在已见位置（未知时长已知上限毫秒）之内，不超前于实际播放范围。
                    Dim 新值 = Math.Min(当前位置, 未知时长已知上限毫秒)
                    If 进度条.Value <> 新值 Then 进度条.Value = 新值
                End If
            Finally
                正在更新进度条 = False
            End Try
            设置时间戳文本(快照.播放位置, If(快照.已知总时长, 快照.总时长, TimeSpan.Zero))
        End If
        刷新HDR按钮(快照)
        RaiseEvent 播放状态已刷新(Me, EventArgs.Empty)
    End Sub

    Friend Sub 更新媒体信息(信息 As 媒体信息, 快照 As 播放器快照)
        当前媒体信息 = 信息
        更新章节标记(信息)
        已显示视频流索引 = If(快照 Is Nothing, -1, 快照.当前视频流)
        已显示音频流索引 = If(快照 Is Nothing, -1, 快照.当前音频流)
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

            If 视频流 IsNot Nothing Then 设置自适应文本(视频编码按钮, 格式化视频编码(视频流.编码))
            If 音频流 IsNot Nothing Then
                设置自适应文本(音频编码按钮, 音频流.编码.ToUpperInvariant())
                设置自适应文本(声道数按钮, 格式化声道数(音频流.声道数))
            End If
        Finally
            状态栏.ResumeLayout(True)
        End Try

        刷新HDR按钮(快照)
    End Sub

    Friend Sub 媒体已打开(保留当前范围 As Boolean)
        If Not 保留当前范围 Then 未知时长已知上限毫秒 = 0
    End Sub

    ''' <summary>静态图片模式下隐藏进度条，时间戳位置显示“图片”。</summary>
    Friend Sub 设置图片模式(启用 As Boolean)
        If 已释放 OrElse 图片模式已启用 = 启用 Then Return
        图片模式已启用 = 启用
        进度条.Visible = Not 启用
        设置时间戳文本(TimeSpan.Zero, TimeSpan.Zero)
        If Not 启用 Then 刷新()
    End Sub

    Friend Sub 清除媒体()
        有媒体快照 = False
        图片模式已启用 = False
        进度条.Visible = True
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
            设置时间戳文本(快照.播放位置, If(快照.已知总时长, 快照.总时长, TimeSpan.Zero))
        End If
    End Sub

    Friend Sub 更新Dpi()
        更新全部自适应宽度()
    End Sub

    Friend Sub 更新字体()
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
        If e.Button <> MouseButtons.Left OrElse Not 进度条可调整() Then Return
        Dim scale As Single = CSng(进度条.DeviceDpi / 96.0)
        Dim width = 10.0F * scale
        Dim height = 8.0F * scale
        Dim distance = 2.0F * scale
        ' 章节标记命中按量程（Maximum-Minimum）比例换算；未知时长文件通常无章节标记，
        ' 循环自然跳过，无需为虚拟量程做特殊处理。
        For index = 进度条.ChapterMarkers.Count - 1 To 0 Step -1
            Dim marker = 进度条.ChapterMarkers(index)
            If Double.IsNaN(marker.Position) OrElse marker.Position < 进度条.Minimum OrElse marker.Position > 进度条.Maximum Then Continue For
            Dim x = CSng(进度条.Padding.Left + (marker.Position - 进度条.Minimum) / (进度条.Maximum - 进度条.Minimum) * (进度条.ClientSize.Width - 进度条.Padding.Horizontal))
            Dim rect As New RectangleF(x - width / 2.0F, distance, width, height)
            If Not rect.Contains(e.Location) Then Continue For
            正在拖动进度条 = False
            正在更新进度条 = True
            Try
                进度条.Value = marker.Position
            Finally
                正在更新进度条 = False
            End Try
            RaiseEvent 请求跳转到关键帧(Me,
                New 播放器跳转请求事件参数(TimeSpan.FromMilliseconds(进度条.Value)))
            Return
        Next
        正在拖动进度条 = True
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

    Private Sub 时间标签_MouseDoubleClick(sender As Object, e As MouseEventArgs)
        If e.Button = MouseButtons.Left Then 复制时间戳(False)
    End Sub

    Private Sub 时间标签_MouseClick(sender As Object, e As MouseEventArgs)
        If e.Button = MouseButtons.Right Then 复制时间戳(True)
    End Sub

    Private Sub 复制时间戳(复制总时长 As Boolean)
        If 已释放 OrElse 图片模式已启用 OrElse Not 有媒体快照 OrElse 正在切换提供器() Then Return
        Dim 快照 = 快照提供器()
        If 快照 Is Nothing OrElse Not 播放器控制器.可操作(快照.状态) Then Return
        If 复制总时长 Then
            If Not 快照.已知总时长 OrElse 快照.总时长 <= TimeSpan.Zero Then Return
        ElseIf 快照.播放位置 < TimeSpan.Zero Then
            Return
        End If

        ' 复制标签实际显示的精度（包括剪辑模式的毫秒），不带颜色标记。
        Dim 时间文本 = System.Text.RegularExpressions.Regex.Replace(时间标签.Text, "<[^>]*>", String.Empty).Split("/"c)
        If 时间文本.Length <> 2 Then Return
        Dim 文本 = 时间文本(If(复制总时长, 1, 0)).Trim()
        If Not System.Text.RegularExpressions.Regex.IsMatch(文本, "^\d+(?::\d{2}){0,2}(?:\.\d{3})?$") Then Return
        Try
            Clipboard.SetText(文本)
        Catch ex As System.Runtime.InteropServices.ExternalException
            ' 剪贴板被占用时不显示成功提示。
            Return
        End Try
        LakeUI.ExFloatingTip(时间标签, If(复制总时长, "已复制总时间戳", "已复制当前时间戳"))
    End Sub

    Private Sub 音量条_ValueChanged(sender As Object, e As EventArgs)
        If 已释放 Then Return
        RaiseEvent 音量已变更(Me, New 播放器音量事件参数(CSng(音量条.Value / 100.0F)))
    End Sub

    Private Sub 画面控件_音量滚轮(sender As Object, e As MouseEventArgs)
        If 已释放 OrElse e.Delta = 0 OrElse
            (Control.ModifierKeys And Keys.Control) = Keys.Control Then Return
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
        If 正在切换提供器() Then Return False
        Dim 快照 = 快照提供器()
        If 快照 Is Nothing Then Return False
        If Not 播放器控制器.可操作(快照.状态) Then Return False
        If 快照.已知总时长 AndAlso 快照.总时长 > TimeSpan.Zero Then
            ' 已知时长模式：保持原 Maximum>Minimum 量程门槛，行为不变（R2.2 回归不变式）。
            Return 进度条.Maximum > 进度条.Minimum
        End If
        ' 未知时长模式（R1.1/R1.2）：起步阶段 Maximum=Minimum=0，原量程门槛会拦下全部
        ' 点击，此处放宽为可点击，点击换算基准由刷新播放状态维护的虚拟上限量程保证不落 0。
        ' 静态图片/loop-aware 图像解容器的时长同样被有意置 0，必须维持进度条禁用（R1.5）。
        Return Not 图片模式已启用
    End Function

    Private Sub 更新播放按钮(状态 As 播放状态)
        Dim 图标 = If(状态 = 播放状态.正在播放, 暂停图标, 播放图标)
        If Not ReferenceEquals(播放暂停按钮.BackImage, 图标) Then 播放暂停按钮.BackImage = 图标
        If 播放暂停按钮.Text.Length > 0 Then 播放暂停按钮.Text = String.Empty
    End Sub

    Private Sub 重置媒体进度条()
        正在更新进度条 = True
        Try
            未知时长已知上限毫秒 = 0
            进度条.ClearChapterMarkers()
            进度条.Minimum = 0
            进度条.Maximum = 0
            进度条.Value = 0
        Finally
            正在更新进度条 = False
            正在拖动进度条 = False
        End Try
    End Sub

    Private Sub 更新章节标记(信息 As 媒体信息)
        进度条.ClearChapterMarkers()
        If 信息 Is Nothing OrElse 信息.章节 Is Nothing Then Return
        For Each 章节 In 信息.章节
            Dim 位置 = TimeSpan.FromTicks(章节.开始时间100纳秒).TotalMilliseconds
            If 位置 < 0 OrElse (信息.时长 > TimeSpan.Zero AndAlso 位置 > 信息.时长.TotalMilliseconds) Then Continue For
            Dim 标题 = If(String.IsNullOrWhiteSpace(章节.标题), "未命名章节", 章节.标题)
            进度条.AddChapterMarker(位置, 标题, LakeUI.ExcellentTrackBar.LabelSideEnum.TopOrLeft)
        Next
    End Sub
    Private Sub 更新解码按钮()
        设置自适应文本(解码按钮, If(解码器提供器() = 解码模式.GPU, "GPU", "CPU"))
    End Sub

    ''' <summary>双耳徽章：可用才出现（模块已授权且端点是 2 声道），文本写"现在
    ''' 听到的是什么"。同一行里唯一的操作是"点一下换一种"，所以文本必须是结果
    ''' 而不是意愿——用户点完立刻能看到它反过来。</summary>
    Private Sub 刷新双耳按钮()
        Dim 状态 = 双耳状态提供器()
        设置配对可见(双耳按钮, 双耳占位, 状态.可用)
        If Not 状态.可用 Then Return
        设置自适应文本(双耳按钮, If(状态.期望双耳, "HRTF", "立体声"))
        双耳按钮.ForeColor = If(状态.期望双耳, 双耳高亮色, Color.Silver)
    End Sub

    Private Sub 刷新HDR按钮(快照 As 播放器快照)
        Dim 可见 = 快照 IsNot Nothing AndAlso 快照.是HDR源
        设置配对可见(HDR按钮, HDR占位, 可见)
        If Not 可见 Then Return

        ' 徽章描述"现在看到的是什么"：优先用实际色彩模式。SDR 屏上请求真实HDR
        ' 会被内核降级成映射SDR，此时显示请求值会写出与实际不符的 "HDR"。
        Select Case 快照.实际色彩模式
            Case 色彩输出模式.映射到SDR
                设置自适应文本(HDR按钮, "SDR")
            Case 色彩输出模式.原始HDR按SDR呈现
                设置自适应文本(HDR按钮, "原始")
            Case 色彩输出模式.峰值映射HDR
                设置自适应文本(HDR按钮, "HDR")
        End Select
    End Sub

    Private Sub 更新全部自适应宽度()
        For Each 按钮 In {解码按钮, HDR按钮, 视频编码按钮, 音频编码按钮, 声道数按钮, 双耳按钮}
            更新自适应宽度(按钮)
        Next
        更新时间戳自适应宽度(创建时间戳测量文本(时间标签.Text), True)
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

    Private Sub 更新时间戳自适应宽度(测量文本 As String, Optional 强制 As Boolean = False)
        ' 字体、DPI 或定时刷新也必须按当前模式测量，不能沿用时间字符占位。
        If 图片模式已启用 Then 测量文本 = 图片时间戳文本
        If Not 强制 AndAlso String.Equals(上次时间戳测量文本, 测量文本, StringComparison.Ordinal) Then Return
        时间标签.Width = Math.Max(1, 时间标签.GetPreferredSizeForText(测量文本, Size.Empty).Width)
        上次时间戳测量文本 = 测量文本
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

    Private Shared Function 格式化视频编码(编码 As String) As String
        Select Case If(编码, String.Empty).Trim().ToLowerInvariant()
            Case "h264", "avc", "avc1"
                Return "AVC"
            Case Else
                Return If(String.IsNullOrWhiteSpace(编码), String.Empty, 编码.Trim().ToUpperInvariant())
        End Select
    End Function

    Private Sub 设置时间戳文本(当前位置 As TimeSpan, 总时长 As TimeSpan)
        If 图片模式已启用 Then
            If 时间标签.Text <> 图片时间戳文本 Then 时间标签.Text = 图片时间戳文本
            更新时间戳自适应宽度(图片时间戳文本)
            Return
        End If
        Dim 精度 = 取得时间戳精度(当前位置, 总时长)
        Dim 总时长文本 = If(总时长 > TimeSpan.Zero OrElse Not 有媒体快照,
            格式化彩色时长(总时长, 精度), 格式化未知时长(精度))
        Dim HTML = $"{格式化彩色时长(当前位置, 精度, 显示精确时间戳)}<font color=""#AAB0B9""> / </font>{总时长文本}"
        If 时间标签.Text <> HTML Then 时间标签.Text = HTML
        更新时间戳自适应宽度(创建时间戳测量文本(HTML))
    End Sub

    Private Shared Function 创建时间戳测量文本(HTML As String) As String
        If String.IsNullOrEmpty(HTML) Then Return If(HTML, String.Empty)
        Dim 字符 = HTML.ToCharArray()
        Dim 在标签内 As Boolean
        For 索引 As Integer = 0 To 字符.Length - 1
            Select Case 字符(索引)
                Case "<"c
                    在标签内 = True
                Case ">"c
                    在标签内 = False
                Case Else
                    If Not 在标签内 AndAlso 字符(索引) >= "0"c AndAlso 字符(索引) <= "9"c Then
                        字符(索引) = "0"c
                    End If
            End Select
        Next
        Return New String(字符)
    End Function

    Private Shared Function 格式化未知时长(精度 As 时间戳精度) As String
        Const 未知颜色 = "#AAB0B9"
        Select Case 精度
            Case 时间戳精度.小时
                Return $"<font color=""{未知颜色}"">--:--:--</font>"
            Case 时间戳精度.分钟
                Return $"<font color=""{未知颜色}"">--:--</font>"
            Case Else
                Return $"<font color=""{未知颜色}"">--</font>"
        End Select
    End Function

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
        RemoveHandler 时间标签.MouseDoubleClick, AddressOf 时间标签_MouseDoubleClick
        RemoveHandler 时间标签.MouseClick, AddressOf 时间标签_MouseClick
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
