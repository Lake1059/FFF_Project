Friend NotInheritable Class 播放器全屏交互控制器
    Implements IDisposable

    Private Const 面板隐藏延迟毫秒 As Long = 1000
    Private Const 鼠标隐藏延迟毫秒 As Long = 3000

    Private ReadOnly 宿主窗口 As Form
    Private ReadOnly 画面控件 As Control
    Private ReadOnly 播放控制面板 As Control
    Private ReadOnly 剪辑区间面板 As Control
    Private ReadOnly 获取剪辑区间模式 As Func(Of Boolean)

    Private 面板隐藏计时器 As LakeUI.PrecisionTimer
    Private 鼠标隐藏计时器 As LakeUI.PrecisionTimer
    Private 当前为全屏 As Boolean
    Private 上次鼠标活动时刻 As Long
    Private 鼠标已由本控制器隐藏 As Boolean
    Private 已释放 As Boolean

    Friend Sub New(宿主窗口值 As Form, 画面控件值 As Control,
                   播放控制面板值 As Control, 剪辑区间面板值 As Control,
                   获取剪辑区间模式值 As Func(Of Boolean))
        ArgumentNullException.ThrowIfNull(宿主窗口值)
        ArgumentNullException.ThrowIfNull(画面控件值)
        ArgumentNullException.ThrowIfNull(播放控制面板值)
        ArgumentNullException.ThrowIfNull(剪辑区间面板值)
        ArgumentNullException.ThrowIfNull(获取剪辑区间模式值)

        宿主窗口 = 宿主窗口值
        画面控件 = 画面控件值
        播放控制面板 = 播放控制面板值
        剪辑区间面板 = 剪辑区间面板值
        获取剪辑区间模式 = 获取剪辑区间模式值
        订阅鼠标事件(宿主窗口)
        AddHandler 宿主窗口.Activated, AddressOf 宿主窗口_Activated
        AddHandler 宿主窗口.Deactivate, AddressOf 宿主窗口_Deactivate
    End Sub

    Friend Sub 设置全屏状态(已进入全屏 As Boolean)
        If 已释放 Then Return
        当前为全屏 = 已进入全屏
        取消面板隐藏()
        取消鼠标隐藏()
        上次鼠标活动时刻 = Environment.TickCount64

        If Not 当前为全屏 Then
            显示鼠标()
            播放控制面板.Visible = True
            剪辑区间面板.Visible = 获取剪辑区间模式()
            Return
        End If

        更新全屏面板(Cursor.Position, True)
        更新全屏鼠标(Cursor.Position, True)
    End Sub

    Friend Sub 剪辑区间模式已变化()
        If 已释放 Then Return
        取消面板隐藏()
        If Not 当前为全屏 Then
            播放控制面板.Visible = True
            剪辑区间面板.Visible = 获取剪辑区间模式()
            Return
        End If
        更新全屏面板(Cursor.Position, True)
    End Sub

    Private Sub 控件_MouseMove(sender As Object, e As MouseEventArgs)
        If 已释放 OrElse Not 当前为全屏 Then Return
        Dim 鼠标屏幕位置 = DirectCast(sender, Control).PointToScreen(e.Location)
        更新全屏面板(鼠标屏幕位置, False)
        更新全屏鼠标(鼠标屏幕位置, True)
    End Sub

    Private Sub 控件_MouseLeave(sender As Object, e As EventArgs)
        If 已释放 OrElse Not 当前为全屏 Then Return
        Dim 鼠标屏幕位置 = Cursor.Position
        更新全屏面板(鼠标屏幕位置, False)
        更新全屏鼠标(鼠标屏幕位置, False)
    End Sub

    Private Sub 宿主窗口_Activated(sender As Object, e As EventArgs)
        If 已释放 OrElse Not 当前为全屏 Then Return
        上次鼠标活动时刻 = Environment.TickCount64
        更新全屏鼠标(Cursor.Position, False)
    End Sub

    Private Sub 宿主窗口_Deactivate(sender As Object, e As EventArgs)
        取消鼠标隐藏()
        显示鼠标()
    End Sub

    Private Sub 更新全屏面板(鼠标屏幕位置 As Point, 立即应用 As Boolean)
        If 获取剪辑区间模式() Then
            取消面板隐藏()
            播放控制面板.Visible = True
            剪辑区间面板.Visible = True
            Return
        End If

        剪辑区间面板.Visible = False
        Dim 客户区鼠标位置 = 宿主窗口.PointToClient(鼠标屏幕位置)
        Dim 位于客户区 = 宿主窗口.ClientRectangle.Contains(客户区鼠标位置)
        Dim 面板区顶部 = Math.Max(0, 宿主窗口.ClientSize.Height - 播放控制面板.Height)
        If 位于客户区 AndAlso 客户区鼠标位置.Y >= 面板区顶部 Then
            取消面板隐藏()
            播放控制面板.Visible = True
        ElseIf 立即应用 Then
            取消面板隐藏()
            播放控制面板.Visible = False
        ElseIf 播放控制面板.Visible AndAlso 面板隐藏计时器 Is Nothing Then
            启动面板隐藏计时器()
        End If
    End Sub

    Private Sub 更新全屏鼠标(鼠标屏幕位置 As Point, 记录活动 As Boolean)
        If 记录活动 Then
            上次鼠标活动时刻 = Environment.TickCount64
            显示鼠标()
        End If

        If Form.ActiveForm IsNot 宿主窗口 OrElse Not 鼠标位于画面(鼠标屏幕位置) Then
            取消鼠标隐藏()
            显示鼠标()
        ElseIf 鼠标隐藏计时器 Is Nothing Then
            启动鼠标隐藏计时器(CInt(鼠标隐藏延迟毫秒))
        End If
    End Sub

    Private Sub 启动面板隐藏计时器()
        面板隐藏计时器 = 创建单次计时器(CInt(面板隐藏延迟毫秒), AddressOf 面板隐藏计时器_Tick)
        面板隐藏计时器.Start()
    End Sub

    Private Sub 面板隐藏计时器_Tick(sender As Object, e As EventArgs)
        Dim 已到期计时器 = DirectCast(sender, LakeUI.PrecisionTimer)
        If ReferenceEquals(面板隐藏计时器, 已到期计时器) Then 面板隐藏计时器 = Nothing
        RemoveHandler 已到期计时器.Tick, AddressOf 面板隐藏计时器_Tick
        异步释放计时器(已到期计时器)
        If 已释放 OrElse Not 当前为全屏 OrElse 获取剪辑区间模式() Then Return

        Dim 客户区鼠标位置 = 宿主窗口.PointToClient(Cursor.Position)
        Dim 面板区顶部 = Math.Max(0, 宿主窗口.ClientSize.Height - 播放控制面板.Height)
        If Not 宿主窗口.ClientRectangle.Contains(客户区鼠标位置) OrElse
           客户区鼠标位置.Y < 面板区顶部 Then 播放控制面板.Visible = False
    End Sub

    Private Sub 启动鼠标隐藏计时器(延迟毫秒 As Integer)
        鼠标隐藏计时器 = 创建单次计时器(延迟毫秒, AddressOf 鼠标隐藏计时器_Tick)
        鼠标隐藏计时器.Start()
    End Sub

    Private Sub 鼠标隐藏计时器_Tick(sender As Object, e As EventArgs)
        Dim 已到期计时器 = DirectCast(sender, LakeUI.PrecisionTimer)
        If ReferenceEquals(鼠标隐藏计时器, 已到期计时器) Then 鼠标隐藏计时器 = Nothing
        RemoveHandler 已到期计时器.Tick, AddressOf 鼠标隐藏计时器_Tick
        异步释放计时器(已到期计时器)
        If 已释放 OrElse Not 当前为全屏 OrElse Form.ActiveForm IsNot 宿主窗口 OrElse
           Not 鼠标位于画面(Cursor.Position) Then
            显示鼠标()
            Return
        End If

        Dim 剩余延迟 = 鼠标隐藏延迟毫秒 - (Environment.TickCount64 - 上次鼠标活动时刻)
        If 剩余延迟 > 0 Then
            启动鼠标隐藏计时器(CInt(Math.Min(Integer.MaxValue, 剩余延迟)))
            Return
        End If

        If Not 鼠标已由本控制器隐藏 Then
            Cursor.Hide()
            鼠标已由本控制器隐藏 = True
        End If
    End Sub

    Private Function 鼠标位于画面(鼠标屏幕位置 As Point) As Boolean
        Return 画面控件.Visible AndAlso
            画面控件.RectangleToScreen(画面控件.ClientRectangle).Contains(鼠标屏幕位置)
    End Function

    Private Function 创建单次计时器(延迟毫秒 As Integer, 处理器 As EventHandler) As LakeUI.PrecisionTimer
        Dim 计时器 As New LakeUI.PrecisionTimer With {
            .Interval = Math.Max(1, 延迟毫秒),
            .AutoReset = False,
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .WorkerThreadCount = 1,
            .SynchronizingObject = 宿主窗口
        }
        AddHandler 计时器.Tick, 处理器
        Return 计时器
    End Function

    Private Sub 取消面板隐藏()
        Dim 计时器 = 面板隐藏计时器
        面板隐藏计时器 = Nothing
        If 计时器 Is Nothing Then Return
        RemoveHandler 计时器.Tick, AddressOf 面板隐藏计时器_Tick
        计时器.Dispose()
    End Sub

    Private Sub 取消鼠标隐藏()
        Dim 计时器 = 鼠标隐藏计时器
        鼠标隐藏计时器 = Nothing
        If 计时器 Is Nothing Then Return
        RemoveHandler 计时器.Tick, AddressOf 鼠标隐藏计时器_Tick
        计时器.Dispose()
    End Sub

    Private Shared Sub 异步释放计时器(计时器 As LakeUI.PrecisionTimer)
        Threading.ThreadPool.QueueUserWorkItem(Sub() 计时器.Dispose())
    End Sub

    Private Sub 显示鼠标()
        If Not 鼠标已由本控制器隐藏 Then Return
        Cursor.Show()
        鼠标已由本控制器隐藏 = False
    End Sub

    Private Sub 订阅鼠标事件(控件 As Control)
        AddHandler 控件.MouseMove, AddressOf 控件_MouseMove
        AddHandler 控件.MouseLeave, AddressOf 控件_MouseLeave
        AddHandler 控件.ControlAdded, AddressOf 控件_ControlAdded
        AddHandler 控件.ControlRemoved, AddressOf 控件_ControlRemoved
        For Each 子控件 As Control In 控件.Controls
            订阅鼠标事件(子控件)
        Next
    End Sub

    Private Sub 取消订阅鼠标事件(控件 As Control)
        RemoveHandler 控件.MouseMove, AddressOf 控件_MouseMove
        RemoveHandler 控件.MouseLeave, AddressOf 控件_MouseLeave
        RemoveHandler 控件.ControlAdded, AddressOf 控件_ControlAdded
        RemoveHandler 控件.ControlRemoved, AddressOf 控件_ControlRemoved
        For Each 子控件 As Control In 控件.Controls
            取消订阅鼠标事件(子控件)
        Next
    End Sub

    Private Sub 控件_ControlAdded(sender As Object, e As ControlEventArgs)
        订阅鼠标事件(e.Control)
    End Sub

    Private Sub 控件_ControlRemoved(sender As Object, e As ControlEventArgs)
        取消订阅鼠标事件(e.Control)
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        RemoveHandler 宿主窗口.Activated, AddressOf 宿主窗口_Activated
        RemoveHandler 宿主窗口.Deactivate, AddressOf 宿主窗口_Deactivate
        取消订阅鼠标事件(宿主窗口)
        取消面板隐藏()
        取消鼠标隐藏()
        显示鼠标()
    End Sub
End Class
