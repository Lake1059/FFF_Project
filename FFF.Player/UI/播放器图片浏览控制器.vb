' 图片模式：与视频模式并列的一种独立形态。
'
' 设计约束（详见 3fp/docs/00-图片模式设计规格.zh.md）：
'   · 只加不改：视频模式的所有既有分支保持原样，本控制器的逻辑全部由 图片模式已启用 门控，
'     关闭时不可达（画面控件在 图片交互已启用=False 时不会把滚轮/拖拽交给本控制器）。
'   · 模式状态显式化：只在"媒体已打开"时判定一次，不靠 duration=0 之类隐式推断。
'   · 输入优先级：光盘 > 360° > 图片 > 视频默认。360° 静态全景图是真实场景，本控制器让位给它。
'
' 结构上对齐 播放器360视角控制器（菜单项 + 模式开关 + Dispose 清理），
' 但键盘走 Form1 的 方向键快捷键已请求（Handled 抢键机制）而不是全局 IMessageFilter，
' 避免与光盘/360 的 IMessageFilter 争抢顺序。

Friend NotInheritable Class 播放器图片浏览控制器
    Implements IDisposable

    Private Const 最小缩放 As Single = 0.1F
    Private Const 最大缩放 As Single = 32.0F
    Private Const 缩放步进 As Single = 1.25F
    Private Const 每刻度像素 As Integer = 120

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 应用视图 As Action(Of Single, Single, Single)
    Private ReadOnly 切换图片 As Action(Of Integer)
    Private ReadOnly 操作提示 As Action(Of String)
    Private ReadOnly 模式菜单项 As LakeUI.ModernContextMenu.ModernMenuItem

    Private 缩放值 As Single = 1.0F
    Private 水平平移值 As Single
    Private 垂直平移值 As Single
    Private 滚轮余量 As Integer
    Private 当前媒体是图片 As Boolean
    Private 已释放 As Boolean

    Friend Sub New(画面控件值 As 播放器画面控件,
                   标题栏菜单 As LakeUI.ModernContextMenu,
                   应用视图值 As Action(Of Single, Single, Single),
                   切换图片值 As Action(Of Integer),
                   操作提示值 As Action(Of String))
        ArgumentNullException.ThrowIfNull(画面控件值)
        ArgumentNullException.ThrowIfNull(标题栏菜单)
        ArgumentNullException.ThrowIfNull(应用视图值)
        ArgumentNullException.ThrowIfNull(切换图片值)
        ArgumentNullException.ThrowIfNull(操作提示值)
        画面控件 = 画面控件值
        应用视图 = 应用视图值
        切换图片 = 切换图片值
        操作提示 = 操作提示值
        模式菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = "图片模式", .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        标题栏菜单.Items.Add(模式菜单项)
        AddHandler 模式菜单项.Click, AddressOf 模式菜单项_Click
        AddHandler 画面控件.图片缩放滚轮, AddressOf 画面控件_图片缩放滚轮
        AddHandler 画面控件.图片平移拖动, AddressOf 画面控件_图片平移拖动
    End Sub

    ''' <summary>模式切换后通知宿主同步界面（隐藏进度条等）。</summary>
    Friend Event 图片模式已变化 As EventHandler

    Friend ReadOnly Property 图片模式已启用 As Boolean
        Get
            Return 模式菜单项.Checked
        End Get
    End Property

    ''' <summary>每次打开媒体判定一次模式；非图片一律退回视频模式。</summary>
    Friend Sub 媒体已打开(信息 As 媒体信息, 快照 As 播放器快照)
        If 已释放 Then Return
        Dim 有画面 = 快照 IsNot Nothing AndAlso 快照.当前视频流 >= 0
        ' 只表示"能不能手动开关"，不作为自动启用的依据（见 自动进入图片模式）。
        当前媒体是图片 = 有画面 AndAlso 信息 IsNot Nothing AndAlso 信息.是静态图片
        Dim 自动启用 = 自动进入图片模式(信息, 快照)
        设置模式(自动启用, False)
        If 自动启用 Then 操作提示("已自动启用图片模式")
    End Sub

    ''' <summary>自动启用图片模式的唯一判定点：静态图，且不让位给 360°（docs/00 §6.3、D8）。</summary>
    ''' <remarks>
    ''' 是360视频 的第三条判据是"宽 == 高×2"的纯启发式 ⇒ 任何 2:1 图片都会命中；两模式同时启用时
    ''' 方向键被 360 的 IMessageFilter 吃掉、滚轮又被本控制器抢走。上游既有测试已钉死"2:1 静态图
    ''' 必须判为 360°"（Program.vb 测试360识别与交互数学），所以只能在这里让位。
    ''' </remarks>
    Friend Shared Function 自动进入图片模式(信息 As 媒体信息, 快照 As 播放器快照) As Boolean
        If 快照 Is Nothing OrElse 快照.当前视频流 < 0 Then Return False
        If 信息 Is Nothing OrElse Not 信息.是静态图片 Then Return False
        Return Not 播放器360视角控制器.是360视频(信息, 快照)
    End Function

    Private Sub 模式菜单项_Click(sender As Object, e As EventArgs)
        If 已释放 Then Return
        If Not 当前媒体是图片 Then
            操作提示("当前不是静态图片")
            Return
        End If
        设置模式(Not 图片模式已启用, True)
    End Sub

    Private Sub 设置模式(启用 As Boolean, 显示提示 As Boolean)
        模式菜单项.Checked = 启用
        画面控件.图片交互已启用 = 启用
        重置视图(False)
        If 显示提示 Then 操作提示(If(启用, "已启用图片模式", "已关闭图片模式"))
        RaiseEvent 图片模式已变化(Me, EventArgs.Empty)
    End Sub

    ''' <summary>方向键：图片模式下 ←/→ 切换上一张/下一张，并抢下这次按键。
    ''' Form1 在既有的 处理方向键快捷键 里先 RaiseEvent 并提供 Handled，
    ''' 于是视频模式那套 ±5 秒跳转在图片模式下根本不会执行（不改动它）。</summary>
    Friend Sub 处理方向键快捷键(sender As Object, e As KeyEventArgs)
        If 已释放 OrElse Not 图片模式已启用 Then Return
        If (e.Modifiers And Keys.Modifiers) <> Keys.None Then Return
        Select Case e.KeyCode And Keys.KeyCode
            Case Keys.Left
                切换图片(-1) : e.Handled = True
            Case Keys.Right
                切换图片(1) : e.Handled = True
        End Select
    End Sub

    Private Sub 画面控件_图片缩放滚轮(sender As Object, e As MouseEventArgs)
        If 已释放 OrElse Not 图片模式已启用 OrElse e.Delta = 0 Then Return
        滚轮余量 += e.Delta
        Dim 刻度 = 滚轮余量 \ 每刻度像素
        If 刻度 = 0 Then Return
        滚轮余量 -= 刻度 * 每刻度像素
        调整缩放(缩放步进 ^ 刻度)
    End Sub

    Private Sub 画面控件_图片平移拖动(sender As Object, e As 播放器图片平移拖动事件参数)
        If 已释放 OrElse Not 图片模式已启用 Then Return
        If 缩放值 <= 1.0F Then Return  ' 未放大时没有可平移的余量
        Dim 宽度 = Math.Max(1, 画面控件.Width)
        Dim 高度 = Math.Max(1, 画面控件.Height)
        ' 对齐内核语义：内核按 offsetX = pan * (zoom - 1) / 2 * 目标宽 平移，
        ' 反解出"拖多少像素等于多少 pan"，平移才跟手（此前用 zoom 当分母，
        ' 且方向相反：鼠标右移画面却向左跑）。
        Dim 水平余量 = 宽度 * (缩放值 - 1.0F) / 2.0F
        Dim 垂直余量 = 高度 * (缩放值 - 1.0F) / 2.0F
        水平平移值 = Math.Clamp(水平平移值 + CSng(e.水平位移) / 水平余量, -1.0F, 1.0F)
        垂直平移值 = Math.Clamp(垂直平移值 + CSng(e.垂直位移) / 垂直余量, -1.0F, 1.0F)
        提交视图()
    End Sub

    Private Sub 调整缩放(倍数 As Single)
        Dim 新缩放 = Math.Clamp(缩放值 * 倍数, 最小缩放, 最大缩放)
        If Math.Abs(新缩放 - 缩放值) < 0.0001F Then Return
        缩放值 = 新缩放
        ' 回到 1:1 以下时没有可平移的余量，直接归位，避免"缩小后画面停在角落"。
        If 缩放值 <= 1.0F Then
            水平平移值 = 0.0F
            垂直平移值 = 0.0F
        End If
        提交视图()
        操作提示($"缩放：{缩放值 * 100.0F:0}%")
    End Sub

    Private Sub 重置视图(显示提示 As Boolean)
        缩放值 = 1.0F
        水平平移值 = 0.0F
        垂直平移值 = 0.0F
        滚轮余量 = 0
        提交视图()
        If 显示提示 Then 操作提示("已恢复适应窗口")
    End Sub

    Private Sub 提交视图()
        If 已释放 Then Return
        Try
            应用视图(缩放值, 水平平移值, 垂直平移值)
        Catch ex As ObjectDisposedException
        Catch ex As 播放器异常
        End Try
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        RemoveHandler 模式菜单项.Click, AddressOf 模式菜单项_Click
        RemoveHandler 画面控件.图片缩放滚轮, AddressOf 画面控件_图片缩放滚轮
        RemoveHandler 画面控件.图片平移拖动, AddressOf 画面控件_图片平移拖动
        画面控件.图片交互已启用 = False
    End Sub
End Class
