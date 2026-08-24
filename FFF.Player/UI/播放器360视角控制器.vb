Imports System.Diagnostics

Friend NotInheritable Class 播放器360视角控制器
    Implements IDisposable, IMessageFilter

    Private Const WM_KEYDOWN As Integer = &H100
    Private Const WM_KEYUP As Integer = &H101
    Private Const WM_SYSKEYDOWN As Integer = &H104
    Private Const WM_SYSKEYUP As Integer = &H105
    Private Const 键盘转速 As Single = 90.0F
    Private Const 视场角调整刻度 As Single = 1.0F
    Private Const 鼠标每像素角度 As Single = 0.18F
    Private Const 最大垂直角度 As Single = 89.0F
    Private Const 最小视场角 As Single = 30.0F
    Private Const 最大视场角 As Single = 90.0F
    ' A short critically-damped spring adds a little acceleration/deceleration
    ' weight without bringing back the long 160 ms camera lag that looked like
    ' ghosting during fast turns.
    Private Const 阻尼时间秒 As Single = 0.25F
    ' Native presentation is paced to the active monitor (up to 120 Hz).
    ' Sample input at that ceiling so a 120 Hz display is not quantized by a
    ' 16 ms WinForms timer; native coalescing keeps slower displays bounded.
    Private Const 输入刷新间隔毫秒 As Integer = 8

    Private Shared ReadOnly 全景元数据键 As String() = {
        "projection", "projection_type", "spherical", "spherical_video", "stitching_software"
    }

    Private ReadOnly 宿主窗口 As Form
    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 应用视角 As Action(Of Boolean, Single, Single, Single)
    Private ReadOnly 操作提示 As Action(Of String)
    Private ReadOnly 模式菜单项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private ReadOnly 按键刷新计时器 As Timer
    Private ReadOnly 计时器 As Stopwatch = Stopwatch.StartNew()
    Private ReadOnly 已按下方向键 As New HashSet(Of Keys)
    Private 视场角滚轮余量 As Integer
    Private 有可用视频 As Boolean
    Private 当前媒体是图片 As Boolean
    Private 上次刷新秒数 As Double
    Private 目标水平角度 As Single
    Private 目标垂直角度 As Single
    Private 实际水平角度 As Single
    Private 实际垂直角度 As Single
    Private 水平角速度 As Single
    Private 垂直角速度 As Single
    Private 视场角 As Single = 最大视场角
    Private 已释放 As Boolean

    Friend Sub New(宿主窗口值 As Form, 画面控件值 As 播放器画面控件,
                   标题栏菜单 As LakeUI.ModernContextMenu,
                   应用视角值 As Action(Of Boolean, Single, Single, Single),
                   操作提示值 As Action(Of String))
        ArgumentNullException.ThrowIfNull(宿主窗口值)
        ArgumentNullException.ThrowIfNull(画面控件值)
        ArgumentNullException.ThrowIfNull(标题栏菜单)
        ArgumentNullException.ThrowIfNull(应用视角值)
        ArgumentNullException.ThrowIfNull(操作提示值)
        宿主窗口 = 宿主窗口值
        画面控件 = 画面控件值
        应用视角 = 应用视角值
        操作提示 = 操作提示值
        模式菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = "360° 视频模式", .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        标题栏菜单.Items.Add(模式菜单项)
        按键刷新计时器 = New Timer With {.Interval = 输入刷新间隔毫秒}
        AddHandler 模式菜单项.Click, AddressOf 模式菜单项_Click
        AddHandler 按键刷新计时器.Tick, AddressOf 按键刷新计时器_Tick
        AddHandler 画面控件.全景视角拖动, AddressOf 画面控件_全景视角拖动
        AddHandler 画面控件.全景视场角滚轮, AddressOf 画面控件_全景视场角滚轮
        Application.AddMessageFilter(Me)
    End Sub

    Friend ReadOnly Property 模式已启用 As Boolean
        Get
            Return 模式菜单项.Checked
        End Get
    End Property

    Friend Sub 媒体已打开(文件路径 As String, 信息 As 媒体信息, 快照 As 播放器快照)
        If 已释放 Then Return
        已按下方向键.Clear()
        视场角滚轮余量 = 0
        按键刷新计时器.Stop()
        当前媒体是图片 = 信息 IsNot Nothing AndAlso 信息.是静态图片
        Dim 有视频流 = 快照 IsNot Nothing AndAlso 快照.当前视频流 >= 0
        Dim 自动识别 = 有视频流 AndAlso 是360视频(信息, 快照)
        ' 静态图片也可以是 360°全景图；普通图片仍然不能手动进入该模式。
        有可用视频 = 有视频流 AndAlso (Not 当前媒体是图片 OrElse 自动识别)
        设置模式(自动识别, False)
        If 自动识别 Then 操作提示("已自动启用 360° 视频模式")
    End Sub

    Friend Shared Function 是360视频(信息 As 媒体信息, 快照 As 播放器快照) As Boolean
        If 信息 Is Nothing OrElse 快照 Is Nothing OrElse 快照.当前视频流 < 0 Then Return False
        Dim 视频流 = 信息.流.FirstOrDefault(Function(流) 流.索引 = 快照.当前视频流 AndAlso
                                                String.Equals(流.类型, "video", StringComparison.OrdinalIgnoreCase) AndAlso
                                                Not 流.是封面图)
        If 视频流 Is Nothing Then Return False
        If If(视频流.投影, String.Empty).Contains("equirectangular", StringComparison.OrdinalIgnoreCase) Then Return True
        If 包含360元数据(信息.元数据) OrElse 包含360元数据(视频流.元数据) Then Return True
        Return 视频流.宽度 > 0 AndAlso 视频流.高度 > 0 AndAlso
            CLng(视频流.宽度) = CLng(视频流.高度) * 2L
    End Function

    Private Shared Function 包含360元数据(元数据 As IDictionary(Of String, String)) As Boolean
        If 元数据 Is Nothing Then Return False
        For Each 项目 In 元数据
            Dim 键 = If(项目.Key, String.Empty).Trim()
            Dim 值 = If(项目.Value, String.Empty).Trim()
            If 全景元数据键.Any(Function(候选) String.Equals(候选, 键, StringComparison.OrdinalIgnoreCase)) AndAlso
                (值.Contains("equirectangular", StringComparison.OrdinalIgnoreCase) OrElse
                 值.Contains("spherical", StringComparison.OrdinalIgnoreCase) OrElse
                 值.Contains("360", StringComparison.OrdinalIgnoreCase) OrElse
                 String.Equals(值, "1", StringComparison.OrdinalIgnoreCase) OrElse
                 String.Equals(值, "true", StringComparison.OrdinalIgnoreCase)) Then Return True
        Next
        Return False
    End Function

    Private Sub 模式菜单项_Click(sender As Object, e As EventArgs)
        If 已释放 Then Return
        If Not 有可用视频 Then
            操作提示(If(当前媒体是图片, "360° 模式不适用于图片", "当前没有可用的视频画面"))
            Return
        End If
        设置模式(Not 模式已启用, True)
    End Sub

    Private Sub 设置模式(启用 As Boolean, 显示提示 As Boolean)
        模式菜单项.Checked = 启用
        画面控件.全景交互已启用 = 启用
        已按下方向键.Clear()
        视场角滚轮余量 = 0
        If 启用 Then
            视场角 = Math.Clamp(设置.实例对象.视角360视场角, 最小视场角, 最大视场角)
            重置视角(False, True)
            上次刷新秒数 = 计时器.Elapsed.TotalSeconds
            按键刷新计时器.Start()
        Else
            按键刷新计时器.Stop()
            重置视角(False, True)
        End If
        If 显示提示 Then 操作提示(If(启用, "已启用 360°视频模式", "已关闭 360°视频模式"))
    End Sub

    Private Sub 重置视角(显示提示 As Boolean, Optional 立即应用 As Boolean = False)
        目标水平角度 = 0
        目标垂直角度 = 0
        If 立即应用 Then
            实际水平角度 = 0
            实际垂直角度 = 0
            水平角速度 = 0
            垂直角速度 = 0
            提交视角()
        ElseIf 模式已启用 AndAlso Not 按键刷新计时器.Enabled Then
            上次刷新秒数 = 计时器.Elapsed.TotalSeconds
            按键刷新计时器.Start()
        End If
        If 显示提示 Then 操作提示("360°视角已恢复正前方")
    End Sub

    Public Function PreFilterMessage(ByRef m As Message) As Boolean Implements IMessageFilter.PreFilterMessage
        If 已释放 OrElse Not 模式已启用 OrElse Form.ActiveForm IsNot 宿主窗口 Then Return False
        If m.Msg <> WM_KEYDOWN AndAlso m.Msg <> WM_SYSKEYDOWN AndAlso
            m.Msg <> WM_KEYUP AndAlso m.Msg <> WM_SYSKEYUP Then Return False
        Dim 按键 = CType(m.WParam.ToInt32() And &HFFFF, Keys) And Keys.KeyCode
        If 按键 = Keys.Home Then
            If m.Msg = WM_KEYDOWN OrElse m.Msg = WM_SYSKEYDOWN Then 重置视角(True)
            Return True
        End If
        Dim 方向键 = 规范化方向键(按键)
        If 方向键 = Keys.None Then Return False
        Dim 是按下 = m.Msg = WM_KEYDOWN OrElse m.Msg = WM_SYSKEYDOWN
        If Not 是按下 Then
            已按下方向键.Remove(方向键)
            Return True
        End If

        Dim 修饰键 = Control.ModifierKeys And Keys.Modifiers
        If 修饰键 = Keys.Control Then
            已按下方向键.Remove(方向键)
            调整视场角(计算视场角调整(方向键, 修饰键))
        ElseIf 修饰键 = Keys.None Then
            已按下方向键.Add(方向键)
            If Not 按键刷新计时器.Enabled Then
                上次刷新秒数 = 计时器.Elapsed.TotalSeconds
                按键刷新计时器.Start()
            End If
        Else
            Return False
        End If
        Return True
    End Function

    Private Shared Function 规范化方向键(按键 As Keys) As Keys
        Select Case 按键
            Case Keys.W, Keys.Up, Keys.S, Keys.Down, Keys.A, Keys.Left, Keys.D, Keys.Right
                Return 按键
            Case Else : Return Keys.None
        End Select
    End Function

    Friend Shared Function 计算视场角调整(按键 As Keys, 修饰键 As Keys) As Single
        If 修饰键 <> Keys.Control Then Return 0.0F
        Select Case 按键
            Case Keys.A, Keys.Left, Keys.S, Keys.Down : Return -视场角调整刻度
            Case Keys.D, Keys.Right, Keys.W, Keys.Up : Return 视场角调整刻度
            Case Else : Return 0.0F
        End Select
    End Function

    Private Sub 调整视场角(调整量 As Single)
        视场角 = Math.Clamp(视场角 + 调整量,
                            最小视场角, 最大视场角)
        设置.实例对象.视角360视场角 = 视场角
        提交视角()
        操作提示($"视场角：{视场角:0}°")
    End Sub

    Private Sub 画面控件_全景视场角滚轮(sender As Object, e As MouseEventArgs)
        If 已释放 OrElse Not 模式已启用 OrElse e.Delta = 0 OrElse
            (Control.ModifierKeys And Keys.Control) <> Keys.Control Then Return
        视场角滚轮余量 += e.Delta
        Dim 刻度 = 视场角滚轮余量 \ 120
        If 刻度 = 0 Then Return
        视场角滚轮余量 -= 刻度 * 120
        调整视场角(-刻度)
    End Sub

    Private Sub 按键刷新计时器_Tick(sender As Object, e As EventArgs)
        Dim 当前秒数 = 计时器.Elapsed.TotalSeconds
        Dim 间隔 = CSng(Math.Clamp(当前秒数 - 上次刷新秒数, 0.0R, 0.05R))
        上次刷新秒数 = 当前秒数
        Dim 方向 = 计算方向向量(已按下方向键)
        If Not 方向.IsEmpty Then
            目标水平角度 += 方向.X * 键盘转速 * 间隔
            目标垂直角度 = Math.Clamp(目标垂直角度 + 方向.Y * 键盘转速 * 间隔,
                                    -最大垂直角度, 最大垂直角度)
        End If
        Dim 水平差 = MathF.IEEERemainder(目标水平角度 - 实际水平角度, 360.0F)
        Dim 连续水平目标 = 实际水平角度 + 水平差
        实际水平角度 = 平滑阻尼(实际水平角度, 连续水平目标, 水平角速度, 间隔)
        实际垂直角度 = 平滑阻尼(实际垂直角度, 目标垂直角度, 垂直角速度, 间隔)
        提交视角()
        If 已按下方向键.Count = 0 AndAlso
            Math.Abs(MathF.IEEERemainder(目标水平角度 - 实际水平角度, 360.0F)) < 0.005F AndAlso
            Math.Abs(目标垂直角度 - 实际垂直角度) < 0.005F AndAlso
            Math.Abs(水平角速度) < 0.01F AndAlso Math.Abs(垂直角速度) < 0.01F Then
            实际水平角度 = 目标水平角度
            实际垂直角度 = 目标垂直角度
            水平角速度 = 0
            垂直角速度 = 0
            提交视角()
            按键刷新计时器.Stop()
        End If
    End Sub

    Private Sub 画面控件_全景视角拖动(sender As Object, e As 播放器360视角拖动事件参数)
        If 已释放 OrElse Not 模式已启用 Then Return
        目标水平角度 -= e.水平位移 * 鼠标每像素角度
        目标垂直角度 = Math.Clamp(目标垂直角度 + e.垂直位移 * 鼠标每像素角度,
                                -最大垂直角度, 最大垂直角度)
        If Not 按键刷新计时器.Enabled Then
            上次刷新秒数 = 计时器.Elapsed.TotalSeconds
            按键刷新计时器.Start()
        End If
    End Sub

    Private Sub 提交视角()
        If Math.Abs(实际水平角度) >= 360.0F Then
            实际水平角度 = MathF.IEEERemainder(实际水平角度, 360.0F)
            目标水平角度 = 实际水平角度 +
                MathF.IEEERemainder(目标水平角度 - 实际水平角度, 360.0F)
        End If
        应用视角(模式已启用, 实际水平角度, 实际垂直角度, 视场角)
    End Sub

    Friend Shared Function 计算方向向量(按键 As ISet(Of Keys)) As PointF
        Dim 水平 = If(按键.Contains(Keys.D) OrElse 按键.Contains(Keys.Right), 1.0F, 0.0F) -
            If(按键.Contains(Keys.A) OrElse 按键.Contains(Keys.Left), 1.0F, 0.0F)
        Dim 垂直 = If(按键.Contains(Keys.W) OrElse 按键.Contains(Keys.Up), 1.0F, 0.0F) -
            If(按键.Contains(Keys.S) OrElse 按键.Contains(Keys.Down), 1.0F, 0.0F)
        Dim 长度 = MathF.Sqrt(水平 * 水平 + 垂直 * 垂直)
        Return If(长度 > 0, New PointF(水平 / 长度, 垂直 / 长度), PointF.Empty)
    End Function

    Friend Shared Function 平滑阻尼(当前值 As Single, 目标值 As Single,
                                  ByRef 当前速度 As Single, 间隔秒 As Single) As Single
        If 间隔秒 <= 0 Then Return 当前值
        Dim 角频率 = 2.0F / 阻尼时间秒
        Dim x = 角频率 * 间隔秒
        Dim 衰减 = 1.0F / (1.0F + x + 0.48F * x * x + 0.235F * x * x * x)
        Dim 差值 = 当前值 - 目标值
        Dim 临时速度 = (当前速度 + 角频率 * 差值) * 间隔秒
        当前速度 = (当前速度 - 角频率 * 临时速度) * 衰减
        Return 目标值 + (差值 + 临时速度) * 衰减
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        Application.RemoveMessageFilter(Me)
        按键刷新计时器.Stop()
        RemoveHandler 模式菜单项.Click, AddressOf 模式菜单项_Click
        RemoveHandler 按键刷新计时器.Tick, AddressOf 按键刷新计时器_Tick
        RemoveHandler 画面控件.全景视角拖动, AddressOf 画面控件_全景视角拖动
        RemoveHandler 画面控件.全景视场角滚轮, AddressOf 画面控件_全景视场角滚轮
        按键刷新计时器.Dispose()
        画面控件.全景交互已启用 = False
    End Sub
End Class
