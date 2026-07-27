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
