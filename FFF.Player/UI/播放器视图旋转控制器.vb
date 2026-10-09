' 视图旋转控制器：把「手动旋转」做成菜单项 + 快捷键，并管理"自动转正"开关。
'
' 设计约束（对齐 播放器图片浏览控制器 / 播放器360视角控制器）：
'   · 只加不改：不改视频模式既有分支；本控制器的逻辑全部由自身状态门控。
'   · 模式状态显式化：自动转正开关是显式字段，不靠"旋转值为 0"之类隐式推断
'     （源本身可能就是 0，那时"关掉自动转正"与"本来是正的"不可区分）。
'   · 自动转正由**内核在打开时**套用。本控制器只负责：
'       1) 在打开媒体后，若用户关了自动转正，把内核已套用的旋转显式归零；
'       2) 提供手动旋转（菜单 + R / Shift+R）；
'       3) 让"复位"回到源方向。
'
' ⚠ 为什么归零要放在"打开之后"而不是"打开之前"：
'   内核在 DoOpen 里读源旋转，早于宿主任何回调；打开之前归零会被内核随后覆盖。

Friend NotInheritable Class 播放器视图旋转控制器
    Implements IDisposable

    Private ReadOnly 应用旋转 As Action(Of UInteger)
    Private ReadOnly 取得旋转 As Func(Of UInteger)
    Private ReadOnly 操作提示 As Action(Of String)
    Private ReadOnly 保存自动转正偏好 As Action(Of Boolean)
    Private ReadOnly 顺时针菜单项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private ReadOnly 逆时针菜单项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private ReadOnly 复位菜单项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private ReadOnly 自动转正菜单项 As LakeUI.ModernContextMenu.ModernMenuItem

    Private 自动转正已启用值 As Boolean = True
    Private 源旋转值 As UInteger
    Private 已释放 As Boolean

    Friend Sub New(标题栏菜单 As LakeUI.ModernContextMenu,
                   应用旋转值 As Action(Of UInteger),
                   取得旋转值 As Func(Of UInteger),
                   操作提示值 As Action(Of String),
                   自动转正初值 As Boolean,
                   保存自动转正 As Action(Of Boolean))
        ArgumentNullException.ThrowIfNull(标题栏菜单)
        ArgumentNullException.ThrowIfNull(应用旋转值)
        ArgumentNullException.ThrowIfNull(取得旋转值)
        ArgumentNullException.ThrowIfNull(操作提示值)
        ArgumentNullException.ThrowIfNull(保存自动转正)
        应用旋转 = 应用旋转值
        取得旋转 = 取得旋转值
        操作提示 = 操作提示值
        保存自动转正偏好 = 保存自动转正
        自动转正已启用值 = 自动转正初值

        ' 菜单顺序：顺时针 → 逆时针 → 复位 → 自动转正（分隔线由宿主菜单统一处理，这里只加项）。
        顺时针菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = "顺时针旋转 90°", .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        逆时针菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = "逆时针旋转 90°", .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        复位菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = "复位方向", .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        自动转正菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = "按源方向自动转正", .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        自动转正菜单项.Checked = 自动转正已启用值

        标题栏菜单.Items.Add(顺时针菜单项)
        标题栏菜单.Items.Add(逆时针菜单项)
        标题栏菜单.Items.Add(复位菜单项)
        标题栏菜单.Items.Add(自动转正菜单项)
        AddHandler 顺时针菜单项.Click, AddressOf 顺时针_Click
        AddHandler 逆时针菜单项.Click, AddressOf 逆时针_Click
        AddHandler 复位菜单项.Click, AddressOf 复位_Click
        AddHandler 自动转正菜单项.Click, AddressOf 自动转正_Click
    End Sub

    ''' <summary>自动转正是否启用（默认启用）。关闭会把当前旋转归零。</summary>
    Friend Property 自动转正已启用 As Boolean
        Get
            Return 自动转正已启用值
        End Get
        Set(值 As Boolean)
            If 已释放 Then Return
            自动转正已启用值 = 值
            自动转正菜单项.Checked = 值
        End Set
    End Property

    ''' <summary>顺时针 / 逆时针 90°（R / Shift+R）。</summary>
    Friend Sub 旋转(顺时针 As Boolean)
        If 已释放 Then Return
        Dim 当前 = 安全取旋转()
        Dim 下一个 = If(顺时针, (当前 + 1UI) Mod 4UI, (当前 + 3UI) Mod 4UI)
        应用旋转(下一个)
        报告方向(下一个)
    End Sub

    ''' <summary>回到源方向（内核打开时套用的那个）。自动转正关闭时则回到 0。</summary>
    Friend Sub 复位()
        If 已释放 Then Return
        Dim 目标 = If(自动转正已启用值, 源旋转值, 0UI)
        应用旋转(目标)
        操作提示(If(目标 = 0UI, "已复原方向", $"已回到源方向 {CInt(目标) * 90}°"))
    End Sub

    ''' <summary>打开媒体后调用：内核已套用源旋转，若用户关了自动转正就显式归零。
    ''' 签名匹配 <c>播放器控制器.媒体已打开</c>（EventHandler(Of 播放器媒体事件参数)），
    ''' 这样能直接挂到该事件上，不需要中间适配层。</summary>
    Friend Sub 媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 已释放 Then Return
        源旋转值 = 安全取旋转()
        If Not 自动转正已启用值 AndAlso 源旋转值 <> 0UI Then
            应用旋转(0UI)
            操作提示("已按设置保持原始方向")
        End If
    End Sub

    Private Function 安全取旋转() As UInteger
        Try
            Return 取得旋转()
        Catch ex As ObjectDisposedException
            Return 0UI
        Catch ex As 播放器异常
            Return 0UI
        End Try
    End Function

    Private Sub 报告方向(四分一转 As UInteger)
        操作提示(If(四分一转 = 0UI, "已复原方向", $"已旋转 {CInt(四分一转) * 90}°"))
    End Sub

    Private Sub 顺时针_Click(sender As Object, e As EventArgs)
        旋转(True)
    End Sub

    Private Sub 逆时针_Click(sender As Object, e As EventArgs)
        旋转(False)
    End Sub

    Private Sub 复位_Click(sender As Object, e As EventArgs)
        复位()
    End Sub

    Private Sub 自动转正_Click(sender As Object, e As EventArgs)
        If 已释放 Then Return
        自动转正已启用 = Not 自动转正已启用值
        ' 落盘：这是用户显式表达的偏好，必须跨会话保留。
        保存自动转正偏好(自动转正已启用值)
        ' 打开设置：立即回到源方向（把内核套用的值再套一次，幂等）。
        ' 关闭设置：立即归零，让用户看到"原始方向"。
        If 自动转正已启用值 Then
            应用旋转(源旋转值)
            操作提示("已启用按源方向自动转正")
        Else
            应用旋转(0UI)
            操作提示("已关闭自动转正（保持原始方向）")
        End If
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        RemoveHandler 顺时针菜单项.Click, AddressOf 顺时针_Click
        RemoveHandler 逆时针菜单项.Click, AddressOf 逆时针_Click
        RemoveHandler 复位菜单项.Click, AddressOf 复位_Click
        RemoveHandler 自动转正菜单项.Click, AddressOf 自动转正_Click
    End Sub
End Class
