''' <summary>
''' 管理视频宿主窗口的重绑节流和首次 16:9 容器比例校正。
''' </summary>
Friend NotInheritable Class 播放器窗口布局控制器
    Implements IDisposable

    Private ReadOnly 窗体 As Form
    Private ReadOnly 视频容器 As Control
    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 重绑输出窗口 As Action
    Private ReadOnly 重绑计时器 As LakeUI.PrecisionTimer

    Private 已校正启动视频比例 As Boolean
    Private 已释放 As Boolean

    Friend Sub New(窗体 As Form, 视频容器 As Control, 画面控件 As 播放器画面控件, 重绑输出窗口 As Action)
        ArgumentNullException.ThrowIfNull(窗体)
        ArgumentNullException.ThrowIfNull(视频容器)
        ArgumentNullException.ThrowIfNull(画面控件)
        ArgumentNullException.ThrowIfNull(重绑输出窗口)
        Me.窗体 = 窗体
        Me.视频容器 = 视频容器
        Me.画面控件 = 画面控件
        Me.重绑输出窗口 = 重绑输出窗口
        重绑计时器 = New LakeUI.PrecisionTimer With {
            .Interval = 180,
            .AutoReset = False,
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .SynchronizingObject = 窗体
        }

        AddHandler 画面控件.输出窗口创建, AddressOf 输出窗口已创建
        AddHandler 画面控件.SizeChanged, AddressOf 请求重绑
        AddHandler 窗体.LocationChanged, AddressOf 请求重绑
        AddHandler 重绑计时器.Tick, AddressOf 重绑计时器_Tick
    End Sub

    Friend Sub 校正初始视频比例()
        If 已释放 OrElse 已校正启动视频比例 OrElse 窗体.IsDisposed OrElse Not 窗体.IsHandleCreated Then Return
        Dim 容器宽度 = 视频容器.ClientSize.Width
        If 容器宽度 <= 0 OrElse 视频容器.Height <= 0 Then Return

        Dim 目标容器高度 = CInt(Math.Round(容器宽度 * 9.0R / 16.0R))
        ' 使用已完成停靠布局的实际边界计算：不可见的剪辑区间面板不会占用视频容器上下空间。
        ' 视频容器的 Top / Bottom 同时涵盖 ThisIsYourWindow 注入的标题栏、绘制边框与所有可见工具栏。
        Dim 客户区非视频高度 = 视频容器.Top + (窗体.ClientSize.Height - 视频容器.Bottom)
        Dim 非客户区边框高度 = Math.Max(0, 窗体.Height - 窗体.ClientSize.Height)
        Dim 目标窗体高度 = 目标容器高度 + 客户区非视频高度 + 非客户区边框高度

        窗体.Size = New Size(窗体.Width, Math.Max(窗体.MinimumSize.Height, 目标窗体高度))
        已校正启动视频比例 = True
    End Sub

    Friend Sub 应用初始画面尺寸(目标画面大小 As Size)
        If 已释放 OrElse 已校正启动视频比例 OrElse 窗体.IsDisposed OrElse Not 窗体.IsHandleCreated OrElse
            目标画面大小.Width <= 0 OrElse 目标画面大小.Height <= 0 Then Return
        应用画面尺寸(目标画面大小)
        已校正启动视频比例 = True
    End Sub

    Friend Sub 调整画面尺寸(目标画面大小 As Size)
        If 已释放 OrElse 窗体.IsDisposed OrElse Not 窗体.IsHandleCreated OrElse
            目标画面大小.Width <= 0 OrElse 目标画面大小.Height <= 0 Then Return
        If 窗体.WindowState <> FormWindowState.Normal Then 窗体.WindowState = FormWindowState.Normal
        应用画面尺寸(目标画面大小)
    End Sub

    Private Sub 应用画面尺寸(目标画面大小 As Size)
        窗体.PerformLayout()
        Dim 目标画面设备大小 = 按DPI缩放画面尺寸(目标画面大小, 窗体.DeviceDpi)
        Dim 客户区非视频宽度 = Math.Max(0, 窗体.ClientSize.Width - 视频容器.ClientSize.Width)
        Dim 客户区非视频高度 = Math.Max(0, 窗体.ClientSize.Height - 视频容器.ClientSize.Height)
        Dim 非客户区宽度 = Math.Max(0, 窗体.Width - 窗体.ClientSize.Width)
        Dim 非客户区高度 = Math.Max(0, 窗体.Height - 窗体.ClientSize.Height)
        Dim 目标宽度 = 目标画面设备大小.Width + 客户区非视频宽度 + 非客户区宽度
        Dim 目标高度 = 目标画面设备大小.Height + 客户区非视频高度 + 非客户区高度
        Dim workingArea = Screen.FromControl(窗体).WorkingArea
        Dim 最终大小 = New Size(Math.Clamp(目标宽度, 窗体.MinimumSize.Width, workingArea.Width),
                               Math.Clamp(目标高度, 窗体.MinimumSize.Height, workingArea.Height))
        窗体.StartPosition = FormStartPosition.Manual
        窗体.Bounds = 计算工作区居中边界(最终大小, workingArea)
    End Sub

    Friend Shared Function 按DPI缩放画面尺寸(逻辑画面大小 As Size, DPI As Integer) As Size
        If 逻辑画面大小.Width <= 0 OrElse 逻辑画面大小.Height <= 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(逻辑画面大小))
        End If
        If DPI <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(DPI))

        Const 基准DPI As Double = 96.0R
        Return New Size(
            CInt(Math.Round(逻辑画面大小.Width * DPI / 基准DPI, MidpointRounding.AwayFromZero)),
            CInt(Math.Round(逻辑画面大小.Height * DPI / 基准DPI, MidpointRounding.AwayFromZero)))
    End Function

    Friend Shared Function 计算工作区居中边界(窗口大小 As Size, 工作区 As Rectangle) As Rectangle
        If 窗口大小.Width <= 0 OrElse 窗口大小.Height <= 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(窗口大小))
        End If
        If 工作区.Width <= 0 OrElse 工作区.Height <= 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(工作区))
        End If

        Dim x = 工作区.Left + (工作区.Width - 窗口大小.Width) \ 2
        Dim y = 工作区.Top + (工作区.Height - 窗口大小.Height) \ 2
        Return New Rectangle(New Point(x, y), 窗口大小)
    End Function

    Private Sub 输出窗口已创建(sender As Object, e As EventArgs)
        If Not 已释放 Then 重绑输出窗口()
    End Sub

    Private Sub 请求重绑(sender As Object, e As EventArgs)
        If 已释放 Then Return
        重绑计时器.Stop()
        重绑计时器.Start()
    End Sub

    Private Sub 重绑计时器_Tick(sender As Object, e As EventArgs)
        If 已释放 OrElse Not 画面控件.IsHandleCreated Then Return
        重绑输出窗口()
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        重绑计时器.Stop()
        RemoveHandler 画面控件.输出窗口创建, AddressOf 输出窗口已创建
        RemoveHandler 画面控件.SizeChanged, AddressOf 请求重绑
        RemoveHandler 窗体.LocationChanged, AddressOf 请求重绑
        RemoveHandler 重绑计时器.Tick, AddressOf 重绑计时器_Tick
        重绑计时器.Dispose()
    End Sub
End Class
