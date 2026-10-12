Imports System.Diagnostics

''' <summary>自动识别静态图片，处理阻尼缩放、平移和方向键切图；输入让位于光盘及 360° 视角。</summary>
Friend NotInheritable Class 播放器图片浏览控制器
    Implements IDisposable

    Private Const 滚轮缩放步进 As Double = 1.10
    Private Const 按键缩放步进 As Double = 1.01
    Private Const 每刻度像素 As Integer = 120
    Private Const 缩放阻尼毫秒 As Integer = 300
    Private Const 平移阻尼毫秒 As Integer = 150
    Private Const 缩放提示间隔秒 As Double = 0.1

    ' 与 LakeUI.PixelPictureZoomMotion 相同的临界阻尼解析解，连续输入保留速度。
    Private NotInheritable Class 阻尼运动
        Friend 值 As Double
        Friend 目标 As Double
        Friend 速度 As Double

        Friend Sub 重置(初值 As Double)
            值 = 初值
            目标 = 初值
            速度 = 0
        End Sub

        Friend Function 推进(间隔秒 As Double, 阻尼毫秒 As Integer) As Boolean
            Dim 角频率 = 8.0 / (阻尼毫秒 / 1000.0)
            Dim 差值 = 值 - 目标
            Dim 系数 = 速度 + 角频率 * 差值
            Dim 衰减 = Math.Exp(-角频率 * 间隔秒)
            值 = 目标 + (差值 + 系数 * 间隔秒) * 衰减
            速度 = (速度 - 角频率 * 系数 * 间隔秒) * 衰减
            If Math.Abs(值 - 目标) < 0.0000001 AndAlso Math.Abs(速度) < 0.000001 Then
                重置(目标)
                Return False
            End If
            Return True
        End Function
    End Class

    Friend Delegate Function 锚定缩放委托(倍数 As Single, 锚点水平 As Single,
                                      锚点垂直 As Single, ByRef 结果缩放 As Single) As Boolean
    Friend Delegate Function 读取缩放委托(ByRef 当前倍率 As Single, ByRef 最小倍率 As Single,
                                      ByRef 最大倍率 As Single) As Boolean

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 应用视图 As Action(Of Single, Single, Single)
    Private ReadOnly 锚定缩放 As 锚定缩放委托
    Private ReadOnly 平移视图 As Func(Of Single, Single, Boolean)
    Private ReadOnly 读取缩放 As 读取缩放委托
    Private ReadOnly 切换图片 As Action(Of Integer)
    Private ReadOnly 操作提示 As Action(Of String)
    Private ReadOnly 当前旋转 As Func(Of UInteger)
    Private ReadOnly 动画计时器 As LakeUI.PrecisionTimer
    Private ReadOnly 缩放运动 As New 阻尼运动()
    Private ReadOnly 水平运动 As New 阻尼运动()
    Private ReadOnly 垂直运动 As New 阻尼运动()

    Private 实际倍率 As Single
    Private 最小倍率 As Single
    Private 最大倍率 As Single
    Private 已提交水平 As Double
    Private 已提交垂直 As Double
    Private 锚点水平 As Single = 0.5F
    Private 锚点垂直 As Single = 0.5F
    Private 上次动画时刻 As Long
    Private 视图尺寸 As Size
    Private 视图旋转 As UInteger
    Private 视图已初始化 As Boolean
    Private 滚轮余量 As Integer
    Private 上次缩放提示 As String
    Private 缩放提示累计秒 As Double = 缩放提示间隔秒
    Private 图片模式已启用值 As Boolean
    Private 已释放 As Boolean

    Friend Sub New(画面控件值 As 播放器画面控件,
                   应用视图值 As Action(Of Single, Single, Single),
                   切换图片值 As Action(Of Integer), 操作提示值 As Action(Of String),
                   锚定缩放值 As 锚定缩放委托,
                   平移视图值 As Func(Of Single, Single, Boolean),
                   当前旋转值 As Func(Of UInteger), 读取缩放值 As 读取缩放委托)
        ArgumentNullException.ThrowIfNull(画面控件值)
        ArgumentNullException.ThrowIfNull(应用视图值)
        ArgumentNullException.ThrowIfNull(切换图片值)
        ArgumentNullException.ThrowIfNull(操作提示值)
        ArgumentNullException.ThrowIfNull(锚定缩放值)
        ArgumentNullException.ThrowIfNull(平移视图值)
        ArgumentNullException.ThrowIfNull(当前旋转值)
        ArgumentNullException.ThrowIfNull(读取缩放值)
        画面控件 = 画面控件值
        应用视图 = 应用视图值
        切换图片 = 切换图片值
        操作提示 = 操作提示值
        锚定缩放 = 锚定缩放值
        平移视图 = 平移视图值
        当前旋转 = 当前旋转值
        读取缩放 = 读取缩放值
        动画计时器 = New LakeUI.PrecisionTimer With {
            .Interval = 8,
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .SynchronizingObject = 画面控件
        }
        AddHandler 动画计时器.Tick, AddressOf 动画计时器_Tick
        AddHandler 画面控件.图片缩放滚轮, AddressOf 画面控件_图片缩放滚轮
        AddHandler 画面控件.图片平移拖动, AddressOf 画面控件_图片平移拖动
    End Sub

    Friend Event 图片模式已变化 As EventHandler

    Friend ReadOnly Property 图片模式已启用 As Boolean
        Get
            Return 图片模式已启用值
        End Get
    End Property

    Friend Sub 媒体已打开(信息 As 媒体信息, 快照 As 播放器快照)
        If 已释放 Then Return
        设置模式(自动进入图片模式(信息, 快照))
        If 图片模式已启用值 Then 操作提示("已自动启用图片模式")
    End Sub

    Friend Shared Function 自动进入图片模式(信息 As 媒体信息, 快照 As 播放器快照) As Boolean
        If 快照 Is Nothing OrElse 快照.当前视频流 < 0 Then Return False
        If 信息 Is Nothing OrElse Not 信息.是静态图片 Then Return False
        Return Not 播放器360视角控制器.是360视频(信息, 快照)
    End Function

    Private Sub 设置模式(启用 As Boolean)
        停止动画()
        滚轮余量 = 0
        上次缩放提示 = Nothing
        缩放提示累计秒 = 缩放提示间隔秒
        图片模式已启用值 = 启用
        画面控件.图片交互已启用 = 启用
        Try
            应用视图(1.0F, 0.0F, 0.0F)
        Catch ex As ObjectDisposedException
        Catch ex As 播放器异常
        End Try
        RaiseEvent 图片模式已变化(Me, EventArgs.Empty)
    End Sub

    Friend Sub 关闭图片模式()
        If 已释放 Then Return
        设置模式(False)
    End Sub

    Friend Sub 处理方向键快捷键(sender As Object, e As KeyEventArgs)
        If 已释放 OrElse Not 图片模式已启用 Then Return
        If e.Modifiers <> Keys.None Then Return
        Select Case e.KeyCode
            Case Keys.Left
                切换图片(-1)
                e.Handled = True
            Case Keys.Right
                切换图片(1)
                e.Handled = True
        End Select
    End Sub

    Friend Function 处理缩放快捷键(keyData As Keys) As Boolean
        If 已释放 OrElse Not 图片模式已启用 Then Return False
        Dim 按键 = keyData And Keys.KeyCode
        Dim 修饰 = keyData And Keys.Modifiers
        If 修饰 <> Keys.None AndAlso Not (修饰 = Keys.Shift AndAlso 按键 = Keys.Oemplus) Then Return False
        Select Case 按键
            Case Keys.Oemplus, Keys.Add
                调整缩放(按键缩放步进, 0.5F, 0.5F)
            Case Keys.OemMinus, Keys.Subtract
                调整缩放(1.0 / 按键缩放步进, 0.5F, 0.5F)
            Case Else
                Return False
        End Select
        Return True
    End Function

    Private Sub 画面控件_图片缩放滚轮(sender As Object, e As MouseEventArgs)
        If 已释放 OrElse Not 图片模式已启用 OrElse e.Delta = 0 Then Return
        滚轮余量 += e.Delta
        Dim 刻度 = 滚轮余量 \ 每刻度像素
        If 刻度 = 0 Then Return
        滚轮余量 -= 刻度 * 每刻度像素
        调整缩放(Math.Pow(滚轮缩放步进, 刻度),
            Math.Clamp(CSng(e.X) / Math.Max(1, 画面控件.Width), 0.0F, 1.0F),
            Math.Clamp(CSng(e.Y) / Math.Max(1, 画面控件.Height), 0.0F, 1.0F))
    End Sub

    Private Sub 调整缩放(倍数 As Double, 水平锚点 As Single, 垂直锚点 As Single)
        If Not 准备视图() Then Return
        推进至当前时刻()
        锚点水平 = 水平锚点
        锚点垂直 = 垂直锚点
        Dim 目标倍率 = Math.Clamp(Math.Exp(缩放运动.目标) * 倍数, CDbl(最小倍率), CDbl(最大倍率))
        缩放运动.目标 = Math.Log(目标倍率)
        启动动画()
    End Sub

    Private Sub 画面控件_图片平移拖动(sender As Object, e As 播放器图片平移拖动事件参数)
        If 已释放 OrElse Not 图片模式已启用 OrElse Not 准备视图() Then Return
        推进至当前时刻()
        水平运动.目标 += e.水平位移
        垂直运动.目标 += e.垂直位移
        启动动画()
    End Sub

    Private Function 准备视图() As Boolean
        Dim 当前 As Single
        Dim 最小 As Single
        Dim 最大 As Single
        If Not 读取缩放(当前, 最小, 最大) OrElse Not Single.IsFinite(当前) OrElse
            Not Single.IsFinite(最小) OrElse Not Single.IsFinite(最大) OrElse
            当前 <= 0 OrElse 最小 <= 0 OrElse 最大 < 最小 Then
            停止动画()
            Return False
        End If
        Dim 旋转 = 当前旋转()
        If Not 视图已初始化 OrElse 视图尺寸 <> 画面控件.ClientSize OrElse 视图旋转 <> 旋转 OrElse
            Math.Abs(当前 - 实际倍率) > Math.Max(0.00001F, 当前 * 0.00001F) Then
            停止动画()
            实际倍率 = 当前
            缩放运动.重置(Math.Log(当前))
            视图尺寸 = 画面控件.ClientSize
            视图旋转 = 旋转
            视图已初始化 = True
        End If
        最小倍率 = 最小
        最大倍率 = 最大
        Return True
    End Function

    Private Sub 启动动画()
        If 动画计时器.IsRunning Then Return
        上次动画时刻 = Stopwatch.GetTimestamp()
        动画计时器.Start()
    End Sub

    Private Sub 动画计时器_Tick(sender As Object, e As EventArgs)
        If 已释放 OrElse Not 图片模式已启用 Then
            停止动画()
            Return
        End If
        Try
            推进至当前时刻()
        Catch ex As ObjectDisposedException
            停止动画()
        Catch ex As 播放器异常
            停止动画()
        End Try
    End Sub

    Private Sub 推进至当前时刻()
        If Not 动画计时器.IsRunning Then Return
        Dim 现在 = Stopwatch.GetTimestamp()
        Dim 间隔 = Math.Max(0, (现在 - 上次动画时刻) / CDbl(Stopwatch.Frequency))
        上次动画时刻 = 现在
        推进动画(间隔)
    End Sub

    Private Sub 推进动画(间隔秒 As Double)
        If Not 准备视图() Then Return
        Dim 缩放未结束 = 缩放运动.推进(间隔秒, 缩放阻尼毫秒)
        Dim 新倍率 = CSng(Math.Clamp(Math.Exp(缩放运动.值), CDbl(最小倍率), CDbl(最大倍率)))
        If 新倍率 <> 实际倍率 Then
            Dim 结果倍率 As Single
            If Not 锚定缩放(新倍率 / 实际倍率, 锚点水平, 锚点垂直, 结果倍率) Then
                停止动画()
                Return
            End If
            实际倍率 = 结果倍率
        End If
        Dim 水平未结束 = 水平运动.推进(间隔秒, 平移阻尼毫秒)
        Dim 垂直未结束 = 垂直运动.推进(间隔秒, 平移阻尼毫秒)
        Dim 水平增量 = CSng(水平运动.值 - 已提交水平)
        Dim 垂直增量 = CSng(垂直运动.值 - 已提交垂直)
        If 水平增量 <> 0 OrElse 垂直增量 <> 0 Then
            If Not 平移视图(水平增量, 垂直增量) Then
                停止动画()
                Return
            End If
            已提交水平 = 水平运动.值
            已提交垂直 = 垂直运动.值
        End If
        Dim 提示 = $"缩放：{实际倍率 * 100.0F:0.##}%"
        缩放提示累计秒 += 间隔秒
        ' 合并动画中间值，缩放结束时立即补上最终倍率。
        If 提示 <> 上次缩放提示 AndAlso
            (缩放提示累计秒 >= 缩放提示间隔秒 OrElse Not 缩放未结束) Then
            操作提示(提示)
            上次缩放提示 = 提示
            缩放提示累计秒 = 0
        End If
        If Not 缩放未结束 AndAlso Not 水平未结束 AndAlso Not 垂直未结束 Then
            动画计时器.Stop()
            水平运动.重置(0)
            垂直运动.重置(0)
            已提交水平 = 0
            已提交垂直 = 0
        End If
    End Sub

    Private Sub 停止动画()
        动画计时器.Stop()
        视图已初始化 = False
        水平运动.重置(0)
        垂直运动.重置(0)
        已提交水平 = 0
        已提交垂直 = 0
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        停止动画()
        RemoveHandler 动画计时器.Tick, AddressOf 动画计时器_Tick
        动画计时器.Dispose()
        RemoveHandler 画面控件.图片缩放滚轮, AddressOf 画面控件_图片缩放滚轮
        RemoveHandler 画面控件.图片平移拖动, AddressOf 画面控件_图片平移拖动
        画面控件.图片交互已启用 = False
    End Sub
End Class
