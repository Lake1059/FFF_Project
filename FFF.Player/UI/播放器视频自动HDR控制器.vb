' NVIDIA 视频处理器私有流扩展自动 HDR（RTX TrueHDR）控制器。
'
' 为什么单独成一个控制器而不是塞进 Form1：与 播放器视频超分辨率控制器 同一套约束——
'   · 只加不改：不碰视频模式既有分支，全部逻辑由自身状态门控。
'   · 能力显式化：内核是否提供这个入口由 支持探测 问出来，不靠"试一次看会不会抛"来猜。
'     旧内核没有这两个导出时，UI 必须写明"需更新 FFF.Native.dll"，而不是让用户面对
'     "勾了但什么都没发生"。
'   · 偏好必须重放到每个新会话：内核的请求是**会话内**状态，新建/切换会话会回到内核默认
'     的 Off。所以媒体打开后必须把用户保存的开关重新登记一次。
'   · UI 不在这里造：本类只留动作与读数，界面元素归 设置 → 视频超分辨率 页
'     （Form设置_视频超分辨率）内的自动 HDR 区，一个概念只留一处入口。
'
' ⚠ 与 VSR 的两点关键差别（文案与行为都跟着差）：
'   ① TrueHDR 有真实的能力探测（VideoProcessorGetStreamExtension 读回 1），
'      所以"生效"比 VSR 的"驱动没拒绝"更强——但**仍然不是画质保证**：
'      生成的 HDR 是驱动算的，强度档位归 NVIDIA App，本程序设不了也读不回来。
'      文案说"已启用"不说"已增强"。
'   ② compare 模式激活时内核直接返回 InvalidState（互斥由内核强制），
'      本控制器的 应用请求 会静默失败；UI 层在 compare 会话里应保持置灰。

Friend NotInheritable Class 播放器视频自动HDR控制器
    Implements IDisposable

    Private ReadOnly 应用请求 As Action(Of Boolean)
    Private ReadOnly 读取状态文本 As Func(Of String)
    Private ReadOnly 支持探测 As Func(Of Boolean)
    Private ReadOnly 操作提示 As Action(Of String)
    Private ReadOnly 保存偏好 As Action(Of Boolean)

    Private 已启用值 As Boolean
    Private 最近状态 As String
    Private 已释放 As Boolean

    Friend Sub New(应用请求值 As Action(Of Boolean),
                   读取状态文本值 As Func(Of String),
                   支持探测值 As Func(Of Boolean),
                   操作提示值 As Action(Of String),
                   已启用初值 As Boolean,
                   保存偏好值 As Action(Of Boolean))
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
    End Sub

    ''' <summary>当前开关值（供宿主在建立新会话后重放偏好，也供设置页回填勾选）。</summary>
    Friend ReadOnly Property 已启用 As Boolean
        Get
            Return 已启用值
        End Get
    End Property

    ''' <summary>内核是否提供这个入口。False 时设置页要把勾灰掉并写明原因。</summary>
    Friend ReadOnly Property 可用 As Boolean
        Get
            Return Not 已释放 AndAlso 安全探测支持()
        End Get
    End Property

    ''' <summary>最近一次读到的内核读数；没读到过就说"未读取"，不伪造一个"未生效"。</summary>
    Friend ReadOnly Property 状态读数 As String
        Get
            If Not 安全探测支持() Then Return "当前内核不支持（需更新 FFF.Native.dll）"
            Return If(最近状态, "未读取（会话未就绪）")
        End Get
    End Property

    ''' <summary>按目标值设定（幂等：与当前一致就不下发，避免无谓地重放请求）。
    ''' 关掉再打开是内核"驱动拒绝"闩的唯一复位路径，所以 False 也必须真的发下去。</summary>
    Friend Sub 设置为(开启 As Boolean)
        If 已释放 Then Return
        If Not 安全探测支持() Then
            操作提示("当前内核不支持自动 HDR")
            Return
        End If
        If 开启 = 已启用值 Then Return
        已启用值 = 开启
        应用请求(已启用值)
        保存偏好(已启用值)
        刷新状态()
        操作提示(If(已启用值, "已请求自动 HDR（SDR 帧将由驱动转换为 HDR10；强度归 NVIDIA App）",
                               "已关闭自动 HDR"))
    End Sub

    Friend Sub 切换()
        If 已释放 Then Return
        设置为(Not 已启用值)
    End Sub

    ''' <summary>先关后开，清掉内核里的"驱动拒绝"闩让下一帧重新探测。</summary>
    Friend Sub 重新探测()
        If 已释放 Then Return
        If Not 安全探测支持() Then
            操作提示("当前内核不支持自动 HDR")
            Return
        End If
        应用请求(False)
        已启用值 = True
        应用请求(True)
        保存偏好(True)
        刷新状态()
        操作提示("已重新请求自动 HDR（清掉驱动拒绝闩，下一帧重新探测）")
    End Sub

    ''' <summary>读取并缓存内核读数。返回的字符串同时给设置页的标签用。</summary>
    Friend Function 刷新状态() As String
        If 已释放 Then Return Nothing
        If Not 安全探测支持() Then Return 状态读数
        最近状态 = 安全读状态()
        Return 状态读数
    End Function

    ''' <summary>媒体打开后：把保存的开关重放到这个新会话（内核每个会话从 Off 起步），
    ''' 然后回报状态。签名匹配 播放器控制器.媒体已打开。</summary>
    Friend Sub 媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 已释放 Then Return
        If Not 安全探测支持() Then Return
        If 已启用值 Then 应用请求(True)
        刷新状态()
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
        已释放 = True
    End Sub
End Class
