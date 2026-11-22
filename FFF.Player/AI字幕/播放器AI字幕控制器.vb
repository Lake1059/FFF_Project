''' <summary>管理 AI 字幕包、音频读取和推理进程，向字幕图层提供按媒体时间排队的结果。</summary>
Friend NotInheritable Class 播放器AI字幕控制器
    Implements IDisposable

    Private ReadOnly 句柄提供器 As Func(Of 播放器原生句柄)
    Private ReadOnly 媒体信息提供器 As Func(Of 媒体信息)
    Private ReadOnly 操作提示 As Action(Of String)
    Private ReadOnly 保存停用偏好 As Action(Of Boolean)
    Private ReadOnly 打开设置页 As Action
    ' 宿主侧派生状态（「纯音频也要驱动字幕图层」那个标志）跟着启用态走。
    ' 控制器不方便直接认识呈现器，交给宿主回调 —— 菜单与设置页两条入口才收敛到一处。
    Private ReadOnly 同步宿主 As Action

    ' 状态机与管道都是本控制器私有：Form1 只经由 取当前条目 看见字幕文本。
    Private ReadOnly 状态机 As New Ai字幕状态机()
    Private 管道 As Ai字幕管道 = Nothing
    ' 当前生效的扩展包；Nothing = 没装包或包不可用（此时一切静默关闭）。
    Private 包 As Ai字幕包配置 = Nothing
    Private 停用 As Boolean
    Private 已报告跳过 As Boolean
    Private 已释放 As Boolean
    Private 已装Tap As 播放器原生句柄
    Private 已启动后端 As String = Ai字幕推理档.后端自动
    Private 已启动语言 As String = Ai字幕语言.自动
    Private 已启动模型档 As String = String.Empty
    Private 已启动线程 As Integer = -1
    Private 已启用翻译 As Boolean
    Private 已启动窗口 As Double
    Private 已启动步长 As Double

    Public Sub New(句柄提供器值 As Func(Of 播放器原生句柄),
                   媒体信息提供器值 As Func(Of 媒体信息),
                   操作提示值 As Action(Of String),
                   保存停用偏好值 As Action(Of Boolean),
                   打开设置页值 As Action,
                   同步宿主值 As Action)
        ArgumentNullException.ThrowIfNull(句柄提供器值)
        ArgumentNullException.ThrowIfNull(媒体信息提供器值)
        ArgumentNullException.ThrowIfNull(操作提示值)
        ArgumentNullException.ThrowIfNull(保存停用偏好值)
        ArgumentNullException.ThrowIfNull(打开设置页值)
        ArgumentNullException.ThrowIfNull(同步宿主值)
        ' 字段与参数同名，必须 Me. 限定：否则是参数自我赋值，字段恒为 Nothing。
        句柄提供器 = 句柄提供器值
        媒体信息提供器 = 媒体信息提供器值
        操作提示 = 操作提示值
        保存停用偏好 = 保存停用偏好值
        打开设置页 = 打开设置页值
        同步宿主 = 同步宿主值
        停用 = 设置.实例对象.Ai字幕已停用

    End Sub

    ''' <summary>设置变化后重新读包：装了有效包就起，包没了/坏了就停。
    ''' Form1 的 应用Ai字幕设置 与启动初始化都走这里。</summary>
    Friend Sub 应用设置()
        If 已释放 Then Return
        Dim 原先启用 = 启用
        停用 = 设置.实例对象.Ai字幕已停用
        Dim 错误 As String = String.Empty
        Dim 新包 = Ai字幕包.载入(设置.实例对象.Ai字幕包目录, 错误)
        Dim 已更换包 = Not String.Equals(包?.包目录, 新包?.包目录, StringComparison.OrdinalIgnoreCase)
        包 = 新包
        If 新包 Is Nothing Then
            停止()
            同步宿主()
            Return
        End If
        已报告跳过 = False
        Dim 设置对象 = 设置.实例对象
        Dim 新后端 = 生效后端(包)
        Dim 新语言 = Ai字幕语言.规整(设置对象.Ai字幕语言)
        ' 管道还活着时 启动() 会直接返回"已在运行"，所以换设备（或换包、换字幕语言）
        ' 必须显式先停：否则用户改了设置，跑着的还是旧二进制/旧后端/旧语言提示。
        Dim 新模型档 = If(设置对象.Ai字幕模型档, String.Empty).Trim()
        Dim 新线程 = Math.Max(0, 设置对象.Ai字幕线程数)
        ' 判据只剩四个：档（决定跑在哪）、语言、模型档、线程数。序号 / 层数 /
        ' 精确设备这些中间量已经不存在了。
        Dim 新双语 = Ai字幕双语.规整(设置对象.Ai字幕双语)
        If 新包.引擎 <> Ai字幕包.引擎Llama Then 新双语 = Ai字幕双语.关闭
        If 管道 IsNot Nothing AndAlso (已更换包 OrElse 已启动后端 <> 新后端 OrElse
                                       已启动语言 <> 新语言 OrElse
                                       已启动模型档 <> 新模型档 OrElse
                                       已启动线程 <> 新线程 OrElse
                                       已启动窗口 <> 设置对象.Ai字幕窗口秒覆盖 OrElse
                                       已启动步长 <> 设置对象.Ai字幕步长秒覆盖 OrElse
                                       已启用翻译 <> Ai字幕双语.已启用(新双语)) Then
            停止()
        End If
        ' 驻留偏移是纯显示侧（状态机守卫判定用）：不触发重启，这里同步即可，
        ' 播放中改设置也立刻见效。字幕"何时出现"由推理窗口决定，偏移只调驻留。
        状态机.时间偏移100纳秒 = CLng(设置对象.Ai字幕字幕偏移秒 * 10_000_000L)
        If 停用 Then
            停止()
        Else
            If Not 原先启用 Then 清除普通字幕?.Invoke()
            尝试启动(Nothing)
        End If
        ' 同步字幕图层是否需要在纯音频模式下呈现。
        同步宿主()
    End Sub

    ''' <summary>这台机器上真正要用的推理后端：设备 + 后端两个下拉解析成一个标识
    ''' （设备只过滤候选表，不参与传参）。auto 就是让 worker 自己按能力试 —— 本体
    ''' 不再叠一层「包推荐后端」的推断，那层推断正是 2026-10-27 清掉的账。</summary>
    Private Shared Function 生效后端(包 As Ai字幕包配置) As String
        If 包 Is Nothing Then Return Ai字幕推理档.后端自动
        Return Ai字幕推理档.解析(设置.实例对象.Ai字幕运行设备, 设置.实例对象.Ai字幕推理后端,
                                  包.引擎)
    End Function

    ''' <summary>AI 字幕是否在工作（包有效且未停用）。</summary>
    Friend ReadOnly Property 启用 As Boolean
        Get
            Return 包 IsNot Nothing AndAlso Not 停用 AndAlso Not 已释放
        End Get
    End Property

    ''' <summary>此刻应显示的 AI 字幕文本；未启用/无有效条目返回空串。
    ''' 只在 PrecisionTimer 线程（字幕图层绘制事件）调用。</summary>
    Friend Function 取当前条目(播放位置 As TimeSpan, 时间轴代次 As ULong) As (主 As String, 副 As String, 推理开始 As Long)
        If 已释放 OrElse Not 启用 Then Return (String.Empty, String.Empty, 0L)
        Dim 推理开始 As Long = 0
        Dim 文本 = 状态机.取当前两行(播放位置, 时间轴代次, 推理开始)
        If 文本.副 <> "" AndAlso Ai字幕双语.规整(设置.实例对象.Ai字幕双语) = Ai字幕双语.中文在下 Then
            Return (文本.副, 文本.主, 推理开始)
        End If
        Return (文本.主, 文本.副, 推理开始)
    End Function

    Friend Function 性能图层文本(延迟毫秒 As Double) As String
        Return If(管道?.性能文本(延迟毫秒), String.Empty)
    End Function

    ''' <summary>worker 是否在运行（诊断/测试用）。</summary>
    Friend ReadOnly Property Worker运行中 As Boolean
        Get
            Return 管道 IsNot Nothing AndAlso 管道.运行中
        End Get
    End Property

    ''' <summary>打开媒体后调用：装 tap 并确保 worker 在跑。
    ''' 签名匹配 <c>播放器控制器.媒体已打开</c>，可直接挂到该事件上。</summary>
    Friend Sub 媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 已释放 OrElse Not 启用 Then Return
        尝试启动(e)
    End Sub

    ''' <summary>停 worker 并清字幕显示。停止播放/关窗体/切文件前调用；
    ''' 幂等，重复调用安全。</summary>
    Friend Sub 停止()
        Dim 句柄 = 已装Tap
        已装Tap = Nothing
        Try
            If 句柄 IsNot Nothing AndAlso Not 句柄.IsClosed AndAlso Not 句柄.IsInvalid Then
                播放器原生接口.停用音频Tap(句柄)
            End If
        Catch ex As ObjectDisposedException
        End Try
        Dim 管道本地 = 管道
        管道 = Nothing
        管道本地?.Dispose()
        Try
            状态机.重置()
        Catch
        End Try
    End Sub

    ' ---------------- 内部 ----------------

    Private Sub 尝试启动(e As 播放器媒体事件参数)
        If 已释放 OrElse Not 启用 OrElse 包 Is Nothing Then Return
        Try
            If Not 播放器原生接口.支持音频Tap Then
                If Not 已报告跳过 Then
                    已报告跳过 = True
                    操作提示("AI 字幕：当前内核不支持，功能已跳过")
                End If
                Return
            End If

            Dim 句柄 = 句柄提供器()
            If 句柄 Is Nothing OrElse 句柄.IsInvalid Then
                ' 没有会话（尚未打开媒体）：等 媒体已打开 再来。
                Return
            End If

            ' 有音轨才值得拉一个 1GB 模型起来（图片/无音轨视频直接跳过）。
            Dim 信息 = If(e IsNot Nothing, e.媒体信息, 媒体信息提供器())
            If 信息 Is Nothing OrElse Not 信息.流.Any(Function(x) x.类型 = "audio") Then
                If 管道 IsNot Nothing Then 停止()
                Return
            End If

            If Not ReferenceEquals(已装Tap, 句柄) AndAlso 已装Tap IsNot Nothing Then 停止()
            If e Is Nothing AndAlso ReferenceEquals(已装Tap, 句柄) AndAlso Worker运行中 Then Return

            Dim 设置对象 = 设置.实例对象
            Dim 窗口 = If(设置对象.Ai字幕窗口秒覆盖 > 0, 设置对象.Ai字幕窗口秒覆盖, 包.窗口秒)
            ' 给跨窗接句、整句翻译和释放结果预留媒体时间。内核仍按正常播放时钟限制读头。
            Dim 预读毫秒 = CUInt(Math.Clamp(窗口 * 1000 + 3000, 22000, 24000))
            Dim 结果 = 播放器原生接口.启用音频Tap(句柄, 8UI * 1024UI * 1024UI, 预读毫秒)
            If 结果 <> 原生播放器结果.成功 Then Return
            已装Tap = 句柄
            If 管道 Is Nothing Then
                管道 = New Ai字幕管道(句柄提供器, 状态机, 操作提示)
                操作提示("AI 字幕：模型加载中")
            End If
            状态机.重置()
            ' 生效后端 已经把"用户没选 → 跟随包清单 device → 还是空则 auto"处理完。
            Dim 后端 = 生效后端(包)
            Dim 语言 = Ai字幕语言.规整(设置对象.Ai字幕语言)
            Dim 新模型档 = If(设置对象.Ai字幕模型档, String.Empty).Trim()
            Dim 新双语 = If(包.引擎 = Ai字幕包.引擎Llama,
                Ai字幕双语.规整(设置对象.Ai字幕双语), Ai字幕双语.关闭)
            If Not 管道.启动(包, 设置对象.Ai字幕窗口秒覆盖, 设置对象.Ai字幕步长秒覆盖,
                             后端, 语言, 新模型档, 设置对象.Ai字幕线程数, 新双语) Then
                停止()
                操作提示("AI 字幕：无法启动识别进程，请检查扩展包中的 Python 运行时")
            Else
                已启动后端 = 后端
                已启动语言 = 语言
                已启动模型档 = 新模型档
                已启动线程 = Math.Max(0, 设置对象.Ai字幕线程数)
                已启用翻译 = Ai字幕双语.已启用(新双语)
                已启动窗口 = 设置对象.Ai字幕窗口秒覆盖
                已启动步长 = 设置对象.Ai字幕步长秒覆盖
            End If
        Catch ex As Exception
            操作提示("AI 字幕：启动失败。" & ex.Message)
        End Try
    End Sub

    ''' <summary>通知播放控制器 AI 字幕已被选中。</summary>
    Friend Property 清除普通字幕 As Action

    ''' <summary>是否装了一个可用的扩展包。字幕列表用它决定这一项摆不摆。</summary>
    Friend ReadOnly Property 有可用包 As Boolean
        Get
            Return 包 IsNot Nothing AndAlso Not 已释放
        End Get
    End Property

    ''' <summary>选择 AI 字幕并启动推理。与关闭、内嵌、外挂互斥。</summary>
    Friend Sub 作为字幕源启用()
        If 已释放 Then Return
        If 包 Is Nothing Then
            ' 没装包：这一项点了也没意义，直接把人送到设置页去选包。
            操作提示("AI 字幕：请先在设置里选择扩展包")
            请求打开设置页()
            Return
        End If
        If 启用 Then
            ' 用户可用再次选择取消一条尚未加载完毕的普通字幕。
            清除普通字幕?.Invoke()
            Return
        End If
        停用 = False
        保存停用偏好(False)
        清除普通字幕?.Invoke()
        尝试启动(Nothing)
        同步宿主()
        操作提示("已启用 AI 实时字幕")
    End Sub

    ''' <summary>普通字幕接管时把 AI 字幕停掉（偏好也落盘，下次打开不会抢回来）。
    ''' 已停用时空转，供字幕选择的每条路径无脑调用。</summary>
    Friend Sub 作为字幕源停用()
        If 已释放 OrElse 停用 Then Return
        停用 = True
        保存停用偏好(True)
        停止()
        同步宿主()
    End Sub

    Friend Sub 请求打开设置页()
        If 已释放 Then Return
        Try
            打开设置页()
        Catch
            ' 打开设置失败不影响播放，安静吞掉。
        End Try
    End Sub

    Friend ReadOnly Property 当前包配置 As Ai字幕包配置
        Get
            Return 包
        End Get
    End Property

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        停止()
        Try
            状态机.Dispose()
        Catch
        End Try
        GC.SuppressFinalize(Me)
    End Sub
End Class
