Imports System.Diagnostics

Friend NotInheritable Class 剪辑区间模式变化事件参数
    Inherits EventArgs

    Friend Sub New(已启用值 As Boolean)
        已启用 = 已启用值
    End Sub

    Friend ReadOnly Property 已启用 As Boolean
End Class

''' <summary>集中管理剪辑区间的状态、控件行为、快捷键和 3FUI 传递。</summary>
Friend NotInheritable Class 播放器剪辑区间控制器
    Private Const 剪辑快速跳转秒数 As Integer = 10

    Private ReadOnly 播放控制器 As 播放器控制器
    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 模式按钮 As LakeUI.ModernButton
    Private ReadOnly 操作容器 As Control
    Private ReadOnly 按钮容器 As Panel
    Private ReadOnly 传递按钮 As LakeUI.ModernButton
    Private 当前模式已启用 As Boolean
    Private 已有媒体快照 As Boolean

    Friend Sub New(播放控制器值 As 播放器控制器,
                   画面控件值 As 播放器画面控件,
                   模式按钮值 As LakeUI.ModernButton,
                   操作容器值 As Control,
                   进度条容器 As Control,
                   按钮容器值 As Panel,
                   传递按钮值 As LakeUI.ModernButton)
        ArgumentNullException.ThrowIfNull(播放控制器值)
        ArgumentNullException.ThrowIfNull(画面控件值)
        ArgumentNullException.ThrowIfNull(模式按钮值)
        ArgumentNullException.ThrowIfNull(操作容器值)
        ArgumentNullException.ThrowIfNull(进度条容器)
        ArgumentNullException.ThrowIfNull(按钮容器值)
        ArgumentNullException.ThrowIfNull(传递按钮值)
        播放控制器 = 播放控制器值
        画面控件 = 画面控件值
        模式按钮 = 模式按钮值
        操作容器 = 操作容器值
        按钮容器 = 按钮容器值
        传递按钮 = 传递按钮值
        模式按钮.TabStop = False
        进度条 = New 剪辑区间进度条控件 With {.Dock = DockStyle.Fill}
        进度条容器.Controls.Add(进度条)
        校正按钮宽度()
    End Sub

    Friend Event 模式已变化 As EventHandler(Of 剪辑区间模式变化事件参数)

    Friend ReadOnly Property 进度条 As 剪辑区间进度条控件

    Friend ReadOnly Property 模式已启用 As Boolean
        Get
            Return 当前模式已启用
        End Get
    End Property

    Friend Sub 切换模式(sender As Object, e As EventArgs)
        当前模式已启用 = Not 当前模式已启用
        操作容器.Visible = 当前模式已启用
        模式按钮.BackColor1 = If(当前模式已启用,
            Color.FromArgb(40, 220, 220, 220), Color.Transparent)
        RaiseEvent 模式已变化(Me, New 剪辑区间模式变化事件参数(当前模式已启用))
        If 画面控件.CanFocus Then 画面控件.Focus()
    End Sub

    Friend Sub 按钮容器大小已变化(sender As Object, e As EventArgs)
        校正按钮宽度()
    End Sub

    Friend Sub 进度条请求跳转(sender As Object, e As 播放器跳转请求事件参数)
        播放控制器.跳转到位置(e.位置)
    End Sub

    Friend Sub 后退到关键帧(sender As Object, e As EventArgs)
        播放控制器.跳转到相邻关键帧(-1)
    End Sub

    Friend Sub 前进到关键帧(sender As Object, e As EventArgs)
        播放控制器.跳转到相邻关键帧(1)
    End Sub

    Friend Sub 后退一帧(sender As Object, e As EventArgs)
        播放控制器.逐帧(-1)
    End Sub

    Friend Sub 前进一帧(sender As Object, e As EventArgs)
        播放控制器.逐帧(1)
    End Sub

    Friend Sub 设为入点(sender As Object, e As EventArgs)
        Dim 时间 = 当前剪辑时间()
        If 时间.HasValue Then 进度条.设为入点(时间.Value)
    End Sub

    Friend Sub 设为出点(sender As Object, e As EventArgs)
        Dim 时间 = 当前剪辑时间()
        If 时间.HasValue Then 进度条.设为出点(时间.Value)
    End Sub

    Friend Sub 去入点(sender As Object, e As EventArgs)
        If 进度条.入点.HasValue Then 播放控制器.跳转到位置(进度条.入点.Value)
    End Sub

    Friend Sub 去出点(sender As Object, e As EventArgs)
        If 进度条.出点.HasValue Then 播放控制器.跳转到位置(进度条.出点.Value)
    End Sub

    Friend Sub 媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If Not e.保留剪辑区间 Then 清除媒体()
    End Sub

    Friend Sub 播放状态已刷新(sender As Object, e As EventArgs)
        Dim 快照 = 播放控制器.安全读取快照()
        If 快照 Is Nothing Then
            If 已有媒体快照 Then 清除媒体()
            Return
        End If
        已有媒体快照 = True
        进度条.更新播放状态(快照.播放位置, 快照.总时长)
    End Sub

    Friend Sub 停止已点击(sender As Object, e As EventArgs)
        清除媒体()
    End Sub

    Friend Sub 清除媒体()
        已有媒体快照 = False
        进度条.清除媒体()
    End Sub

    Friend Sub 处理方向键快捷键(sender As Object, e As KeyEventArgs)
        If Not 当前模式已启用 Then Return
        Dim 按键 = e.KeyData And Keys.KeyCode
        If 按键 <> Keys.Left AndAlso 按键 <> Keys.Right Then Return
        Dim 方向 = If(按键 = Keys.Left, -1, 1)
        Select Case e.KeyData And Keys.Modifiers
            Case Keys.None
                播放控制器.逐帧(方向)
            Case Keys.Control
                播放控制器.跳转到相邻关键帧(方向)
            Case Keys.Alt
                播放控制器.相对跳转(方向 * 剪辑快速跳转秒数)
            Case Else
                Return
        End Select
        e.Handled = True
    End Sub

    Friend Sub 传给3FUI(sender As Object, e As EventArgs)
        If Not 进度条.入点.HasValue AndAlso Not 进度条.出点.HasValue Then
            LakeUI.ExFloatingTip(传递按钮, "没有可发送的入点或出点")
            Return
        End If
        If 进度条.入点.HasValue AndAlso 进度条.出点.HasValue AndAlso
           进度条.出点.Value < 进度条.入点.Value Then
            LakeUI.ExFloatingTip(传递按钮, "出点不能早于入点")
            Return
        End If

        Try
            Dim 已找到3FUI As Boolean
            Dim 文件路径 = 查找3FUI路径(已找到3FUI)
            If Not 已找到3FUI Then
                LakeUI.ExFloatingTip(传递按钮, "请先启动 3FUI")
                Return
            End If
            If String.IsNullOrWhiteSpace(文件路径) Then
                LakeUI.ExFloatingTip(传递按钮, "无法取得 3FUI 路径")
                Return
            End If

            Dim 启动信息 As New ProcessStartInfo With {
                .FileName = 文件路径,
                .UseShellExecute = False,
                .CreateNoWindow = True}
            If 进度条.入点.HasValue Then
                启动信息.ArgumentList.Add("-3fuiVideoHelperInPointTime")
                启动信息.ArgumentList.Add(剪辑区间进度条控件.格式化时长(进度条.入点.Value))
            End If
            If 进度条.出点.HasValue Then
                启动信息.ArgumentList.Add("-3fuiVideoHelperOutPointTime")
                启动信息.ArgumentList.Add(剪辑区间进度条控件.格式化时长(进度条.出点.Value))
            End If
            Using 已启动 = Process.Start(启动信息)
            End Using
            LakeUI.ExFloatingTip(传递按钮, "剪辑区间已发送")
        Catch ex As Exception
            LakeUI.ExFloatingTip(传递按钮, $"发送失败：{ex.Message}")
        End Try
    End Sub

    Private Function 当前剪辑时间() As TimeSpan?
        Dim 快照 = 播放控制器.安全读取快照()
        If 快照 Is Nothing OrElse 快照.总时长 <= TimeSpan.Zero Then Return Nothing
        Return 快照.播放位置
    End Function

    Private Sub 校正按钮宽度()
        If 按钮容器.ClientSize.Width <= 0 Then Return
        Dim 按钮 = 按钮容器.Controls.OfType(Of LakeUI.ModernButton)().ToArray()
        If 按钮.Length = 0 Then Return
        Dim 分割宽度 = 按钮容器.Controls.OfType(Of LakeUI.JustEmptyControl)().Sum(Function(x) x.Width)
        Dim 可用宽度 = Math.Max(按钮.Length, 按钮容器.ClientSize.Width - 分割宽度)
        Dim 统一宽度 = Math.Max(1, 可用宽度 \ 按钮.Length)
        按钮容器.SuspendLayout()
        Try
            按钮容器.Padding = Padding.Empty
            For Each 当前按钮 In 按钮
                If 当前按钮 IsNot 传递按钮 Then 当前按钮.Width = 统一宽度
            Next
        Finally
            按钮容器.ResumeLayout(True)
        End Try
    End Sub

    Private Shared Function 查找3FUI路径(ByRef 已找到 As Boolean) As String
        已找到 = False
        For Each 候选进程 In Process.GetProcesses()
            Try
                If Not 候选进程.ProcessName.Contains("FFmpegFreeUI", StringComparison.OrdinalIgnoreCase) Then Continue For
                已找到 = True
                Dim 路径 = 候选进程.MainModule?.FileName
                If Not String.IsNullOrWhiteSpace(路径) Then Return 路径
            Catch ex As Exception When TypeOf ex Is InvalidOperationException OrElse
                                             TypeOf ex Is ComponentModel.Win32Exception OrElse
                                             TypeOf ex Is NotSupportedException
                ' 枚举期间进程可能退出或拒绝访问；继续查找其他 3FUI 实例。
            Finally
                候选进程.Dispose()
            End Try
        Next
        Return Nothing
    End Function
End Class
