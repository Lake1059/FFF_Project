' RTX 视频超分辨率（VSR）开关控制器。
'
' 为什么单独成一个控制器而不是塞进 Form1：与 播放器视图旋转控制器 同一套约束——
'   · 只加不改：不碰视频模式既有分支，全部逻辑由自身状态门控。
'   · 能力显式化：内核是否提供这个入口由 支持探测 问出来，不靠"试一次看会不会抛"来猜。
'     旧内核没有这两个导出时，菜单项置灰并写明原因，而不是让用户面对
'     "勾了但什么都没发生"。
'   · 偏好必须重放到每个新会话：内核的 VSR 请求是**会话内**状态，新建/切换会话会回到
'     内核默认的 Off。所以媒体打开后必须把用户保存的开关重新登记一次，
'     否则"我明明开着"会在下一次打开后静默失效。
'
' ⚠ 文案里那句"驱动已接受不等于画面已被增强"不是谦虚，是实测结论：
'   这条路径上没有任何能力查询可用（实测 VideoProcessorGetStreamExtension 连对一个
'   伪造的 GUID 都返回成功），内核能报告的只有"驱动没拒绝 + 这次是否用了"。
'   画质档位（1-4/Auto）与总开关归 NVIDIA App / 控制面板，本程序既设不了也读不到。

Friend NotInheritable Class 播放器视频超分辨率控制器
    Implements IDisposable

    Private ReadOnly 应用请求 As Action(Of Boolean)
    Private ReadOnly 读取状态文本 As Func(Of String)
    Private ReadOnly 支持探测 As Func(Of Boolean)
    Private ReadOnly 操作提示 As Action(Of String)
    Private ReadOnly 保存偏好 As Action(Of Boolean)
    Private ReadOnly 开关菜单项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private ReadOnly 状态菜单项 As LakeUI.ModernContextMenu.ModernMenuItem

    Private 已启用值 As Boolean
    Private 已释放 As Boolean

    Friend Sub New(标题栏菜单 As LakeUI.ModernContextMenu,
                   应用请求值 As Action(Of Boolean),
                   读取状态文本值 As Func(Of String),
                   支持探测值 As Func(Of Boolean),
                   操作提示值 As Action(Of String),
                   已启用初值 As Boolean,
                   保存偏好值 As Action(Of Boolean))
        ArgumentNullException.ThrowIfNull(标题栏菜单)
        ArgumentNullException.ThrowIfNull(应用请求值)
        ArgumentNullException.ThrowIfNull(读取状态文本值)
        ArgumentNullException.ThrowIfNull(支持探测值)
        ArgumentNullException.ThrowIfNull(操作提示值)
        ArgumentNullException.ThrowIfNull(保存偏好值)
        应用请求 = 应用请求值
        读取状态文本 = 读取状态文本值
        支持探测 = 支持探测值
        操作提示 = 操作提示值
        保存偏好 = 保存偏好值
        已启用值 = 已启用初值

        开关菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = "RTX 视频超分辨率", .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        ' 状态行用 IsDescription：ModernMenuItem 没有 Enabled，而描述行天生不可点，
        ' 正好用来摊开内核的原因码，避免用户拿"勾没勾"去推断"有没有生效"。
        状态菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = "RTX 视频超分辨率：未读取", .IsDescription = True,
            .ToggleCheckOnClick = False, .CloseOnClick = False
        }

        标题栏菜单.Items.Add(开关菜单项)
        标题栏菜单.Items.Add(状态菜单项)
        AddHandler 开关菜单项.Click, AddressOf 开关_Click

        应用能力状态()
    End Sub

    ''' <summary>把内核能力反映到菜单上。ModernMenuItem 不支持禁用，所以"不支持"用
    ''' 灰字 + 文字说明表达，点击则只回报原因——不能留一个看起来能开、开了什么都没发生的勾。</summary>
    Private Sub 应用能力状态()
        Dim 可用 = 安全探测支持()
        If 可用 Then
            开关菜单项.Text = "RTX 视频超分辨率"
            开关菜单项.ForeColor = System.Drawing.Color.Empty
        Else
            开关菜单项.Text = "RTX 视频超分辨率（当前内核不支持）"
            开关菜单项.ForeColor = System.Drawing.Color.Gray
            状态菜单项.Text = "RTX 视频超分辨率：需更新 FFF.Native.dll"
        End If
        开关菜单项.Checked = 可用 AndAlso 已启用值
    End Sub

    ''' <summary>当前开关值（供宿主在建立新会话后重放偏好）。</summary>
    Friend ReadOnly Property 已启用 As Boolean
        Get
            Return 已启用值
        End Get
    End Property

    ''' <summary>切换开关：登记请求 → 落盘 → 立刻回报内核的判断结果。
    ''' 关掉再打开就是内核"驱动拒绝"闩的唯一复位路径，所以这里必须真的把 False 也发下去。</summary>
    Friend Sub 切换()
        If 已释放 Then Return
        If Not 安全探测支持() Then
            操作提示("当前内核不支持 RTX 视频超分辨率")
            Return
        End If
        已启用值 = Not 已启用值
        开关菜单项.Checked = 已启用值
        应用请求(已启用值)
        保存偏好(已启用值)
        刷新状态()
        操作提示(If(已启用值, "已请求 RTX 视频超分辨率（是否用上由内核逐帧判定）",
                                "已关闭 RTX 视频超分辨率"))
    End Sub

    ''' <summary>读取并显示内核的状态文本。读不到就写"未读取"，不伪造一个"未生效"。</summary>
    Friend Sub 刷新状态()
        If 已释放 Then Return
        If Not 安全探测支持() Then
            应用能力状态()
            Return
        End If
        Dim 文本 = 安全读状态()
        状态菜单项.Text = "RTX 视频超分辨率：" &
            If(文本, "未读取（会话未就绪）")
    End Sub

    ''' <summary>媒体打开后：把保存的开关重放到这个新会话（内核每个会话从 Off 起步），
    ''' 然后回报状态。签名匹配 播放器控制器.媒体已打开。</summary>
    Friend Sub 媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 已释放 Then Return
        If Not 安全探测支持() Then
            应用能力状态()
            Return
        End If
        If 已启用值 Then 应用请求(True)
        刷新状态()
    End Sub

    Private Sub 开关_Click(sender As Object, e As EventArgs)
        切换()
    End Sub

    Private Function 安全探测支持() As Boolean
        Try
            Return 支持探测()
        Catch ex As ObjectDisposedException
            Return False
        Catch ex As Exception
            Return False
        End Try
    End Function

    Private Function 安全读状态() As String
        Try
            Return 读取状态文本()
        Catch ex As ObjectDisposedException
            Return Nothing
        Catch ex As Exception
            Return Nothing
        End Try
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        RemoveHandler 开关菜单项.Click, AddressOf 开关_Click
    End Sub
End Class
