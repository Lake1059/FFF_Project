Imports System.Diagnostics
Imports System.Runtime.InteropServices
Imports System.Threading

''' <summary>
''' 弹幕图层呈现器。把弹幕资料库按播放时钟转成一层 GPU 命令（内核 timed-text 图层槽位 1）。
'''
''' 与字幕分开是有原因的，不是单纯的分类整理：字幕是静态、低频、内容数秒一变，
''' 弹幕是每帧都在动、同屏上百条、寿命只有几秒。共用一个呈现器时，为静态内容设计的
''' 三件事对弹幕全是负收益——图层签名去重（弹幕 x 每帧变，永远不命中，却仍要为上百条
''' 命令做全文本哈希）、按显示器刷新率定的固定节拍、以及"量不足就丢整个 tick"的降频
''' 策略。拆开之后两边各用自己的节拍与策略。
'''
''' 驱动方式：**下限 60 Hz 保底，其余跟随片源**。
'''   · 播放中每个泵拍都提交一次；泵间隔按实测片源帧周期取 clamp(帧周期, 8, 16) ms：
'''     ≤60 fps 的片源一律 16 ms（62.5 Hz，即 60 Hz 保底），120 fps 片源 ≈8 ms（≈120 Hz），
'''     更快的行为 8 ms 护栏（125 Hz）——8 ms 在 180 px/s 下每拍位移约 1.4 px，再快收益递减。
'''     位置按连续媒体时钟计算，提交多密、滚动步进就多细；内核侧的合成节奏另见
'''     FrameRateBoundHz（可见图层的目标帧率那一半，不被媒体帧率上限吞掉，也不超过面板）。
'''   · 暂停/停止 ⇒ 零工作（不取数、不测量、不提交），图层保持上一帧内容。
'''   · seek 拖动（时间轴代次变化）、窗口几何变化、配置变化 ⇒ 立即强制提交一次。
'''
''' 命令与文字对象由呈现器复用；调用方只能在同步提交期间读取命令，不能跨帧保留引用。
''' </summary>
Friend NotInheritable Class 播放器弹幕图层呈现器
    Implements IDisposable

    ' ── 驱动节拍 ────────────────────────────────────────────────────────────
    ' 初始/最高泵间隔 16 ms（62.5 Hz）：≤60 fps 片源一律按它走，这就是 60 Hz 保底。
    Private Const 泵间隔毫秒 As Integer = 16
    ' 最小泵间隔 8 ms（125 Hz）：高帧率片源跟随到这里为止，再快就只烧定时器。
    Private Const 最小间隔毫秒 As Integer = 8

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 宿主窗体 As Form
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 弹幕提供器 As Func(Of 弹幕资料库)
    Private ReadOnly 提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single)

    Private 弹幕配置 As 弹幕显示配置
    Private 当前弹幕资料库 As 弹幕资料库
    Private 当前弹幕调度器 As 弹幕调度器
    Private ReadOnly 弹幕绘制项 As New List(Of 弹幕绘制项)(100)
    ' 文本宽度的缓存做在 默认弹幕文本测量器 里（见 弹幕调度器.vb），不在本类包装一层：
    ' 调度器会对测量器做 TryCast(测量器, 默认弹幕文本测量器)，命中才走"带字体样式"的
    ' 重载。外层包装会让 TryCast 落空、丢掉粗体/斜体，所以缓存必须做进那个类型内部。

    Private ReadOnly 图层命令 As New List(Of 定时文字命令)()
    Private ReadOnly 命令对象池 As New List(Of 定时文字命令)(128)
    Private 命令对象使用数 As Integer
    Private 图层序号 As ULong

    Private 缓存客户区宽度 As Integer
    Private 缓存客户区高度 As Integer
    Private 缓存DPI位元 As Integer

    ' ── 强制/重建标记 ───────────────────────────────────────────────────────
    Private 上一时间轴代次 As ULong
    Private 时间轴代次有效 As Boolean
    Private 强制提交标志 As Integer

    ' ── 片源帧周期估计（决定泵间隔：跟随片源，下限 60 Hz）──────────────────
    Private 采样起始时间戳 As Long
    Private 采样起始解码帧数 As ULong
    Private 上次正在播放 As Boolean

    ' ── 埋点（只读，供浮层/采样器读取）─────────────────────────────────────
    Private 提交次数 As Long
    Private 跳过次数 As Long

    Private ReadOnly 生命周期锁 As New Object()
    Private ReadOnly 配置锁 As New Object()
    Private ReadOnly 刷新空闲 As New ManualResetEventSlim(True)
    Private 活动刷新数 As Integer
    Private 已释放标志 As Integer
    Private ReadOnly 刷新计时器 As LakeUI.PrecisionTimer

    ''' <summary>最近一次实际使用的提交间隔（毫秒）。跟随视频时它≈片源帧周期。</summary>
    Friend ReadOnly Property 当前间隔毫秒 As Integer
        Get
            Return 刷新计时器.Interval
        End Get
    End Property

    ''' <summary>已提交次数（用于验证"跟随视频"：暂停时应停止增长）。</summary>
    Friend ReadOnly Property 已提交次数 As Long
        Get
            Return Interlocked.Read(提交次数)
        End Get
    End Property

    ''' <summary>因"未在播放且无强制/重建"而跳过的次数。</summary>
    Friend ReadOnly Property 已跳过次数 As Long
        Get
            Return Interlocked.Read(跳过次数)
        End Get
    End Property

    ''' <summary>文本测量未命中缓存的次数（即真正做了 DirectWrite 测量的次数）。</summary>
    Friend ReadOnly Property 已测量次数 As Long
        Get
            Return 默认弹幕文本测量器.缓存未命中次数
        End Get
    End Property

    Friend ReadOnly Property 已命中缓存次数 As Long
        Get
            Return 默认弹幕文本测量器.缓存命中次数
        End Get
    End Property

    ''' <summary>被定时器丢弃的 tick 数。每拍工作量是固定的，这个数应为 0；
    ''' 若它持续增长，说明单次生成仍然超过一个间隔，需要继续优化而不是继续丢帧。</summary>
    Friend ReadOnly Property 已丢弃节拍数 As Long
        Get
            Return 刷新计时器.MissedTickCount
        End Get
    End Property

    Friend Sub New(画面控件 As 播放器画面控件,
                   快照提供器 As Func(Of 播放器快照),
                   提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single),
                   弹幕提供器 As Func(Of 弹幕资料库),
                   Optional 弹幕配置 As 弹幕显示配置 = Nothing)
        ArgumentNullException.ThrowIfNull(画面控件)
        ArgumentNullException.ThrowIfNull(快照提供器)
        ArgumentNullException.ThrowIfNull(提交图层)
        ArgumentNullException.ThrowIfNull(弹幕提供器)
        Me.画面控件 = 画面控件
        宿主窗体 = 画面控件.FindForm()
        Me.快照提供器 = 快照提供器
        Me.提交图层 = 提交图层
        Me.弹幕提供器 = 弹幕提供器
        Me.弹幕配置 = If(弹幕配置, New 弹幕显示配置())
        Me.弹幕配置.验证()
        ' 溢出策略仍是 Drop：偶发一次超时就丢弃，靠下一拍补上，比堆积一批过期的
        ' 弹幕命令更合适。丢弃数由 已丢弃节拍数 暴露出来，不会静默发生。
        刷新计时器 = New LakeUI.PrecisionTimer With {
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .Interval = 泵间隔毫秒,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop
        }
        更新画面快照()
        AddHandler 画面控件.ClientSizeChanged, AddressOf 画面几何已变化
        AddHandler 画面控件.DpiChangedAfterParent, AddressOf 画面几何已变化
        AddHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick
        Volatile.Write(强制提交标志, 1)
        刷新计时器.Start()
    End Sub

    ' ── 驱动 ────────────────────────────────────────────────────────────────

    Private Sub 刷新计时器_Tick(sender As Object, e As EventArgs)
        SyncLock 生命周期锁
            If 已释放标志 <> 0 Then Return
            活动刷新数 += 1
            刷新空闲.Reset()
        End SyncLock
        Try
            Dim 快照 = 快照提供器()
            If 快照 Is Nothing OrElse 快照.视频宽度 = 0 OrElse 快照.视频高度 = 0 Then Return
            Dim 客户区大小 = New Size(Volatile.Read(缓存客户区宽度), Volatile.Read(缓存客户区高度))
            If 客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then Return
            Dim DPI = BitConverter.Int32BitsToSingle(Volatile.Read(缓存DPI位元))
            If Not Single.IsFinite(DPI) OrElse DPI <= 0 Then DPI = 96.0F

            ' 1) 片源帧周期估计：决定泵间隔（跟随片源，下限 60 Hz）。只在播放中更新。
            Dim 正在播放 = 快照.状态 = 播放状态.正在播放
            ' 暂停会把墙钟拉长而解码帧不涨，恢复后那个窗口必须重开；否则一次暂停
            ' 会把帧周期估成"总时长/几帧"，泵被错误地放慢到 16 ms。
            If 正在播放 AndAlso Not 上次正在播放 Then 采样起始时间戳 = 0L
            上次正在播放 = 正在播放
            刷新帧率估计(快照.已解码视频帧数, 正在播放)

            ' 2) seek 拖动：时间轴代次只在 seek 时自增，此时调度器必须整体重建。
            Dim 时间轴代次 = 快照.时间轴代次
            Dim 需要重建 = (Not 时间轴代次有效) OrElse 时间轴代次 <> 上一时间轴代次
            上一时间轴代次 = 时间轴代次
            时间轴代次有效 = True

            ' 3) 播放中每拍必提交（60 Hz 保底；高帧率片源泵已按片源缩短）；未播放时只有强制/重建才动。
            Dim 需要强制 = Volatile.Read(强制提交标志) <> 0
            If Not (正在播放 OrElse 需要重建 OrElse 需要强制) Then
                Interlocked.Increment(跳过次数)
                Return
            End If

            Volatile.Write(强制提交标志, 0)
            提交当前帧(客户区大小, 快照.视频宽度, 快照.视频高度, 快照.播放位置,
                     DPI, 时间轴代次, 需要重建)
        Finally
            SyncLock 生命周期锁
                活动刷新数 -= 1
                If 活动刷新数 = 0 Then 刷新空闲.Set()
            End SyncLock
        End Try
    End Sub

    ''' <summary>用"解码帧数/时间"估算片源帧周期，泵间隔取 clamp(周期, 8, 16) ms：
    ''' 60 fps 及以下 ⇒ 16 ms（60 Hz 保底），120 fps ⇒ ≈8 ms（跟随片源），更快 ⇒ 8 ms 护栏。
    ''' 250 ms 窗口且至少 1 帧才更新，避免片头/抖动把泵带偏。
    ''' 换媒体时解码计数会归零，负增量就是那个信号：重开窗口，否则要等新片
    ''' 的解码数追过旧片的起点，120 fps 跟随会长时间停在 16 ms。</summary>
    Private Sub 刷新帧率估计(解码帧数 As ULong, 正在播放 As Boolean)
        If Not 正在播放 Then Return
        Dim 现在 = Stopwatch.GetTimestamp()
        If 采样起始时间戳 = 0L Then
            采样起始时间戳 = 现在
            采样起始解码帧数 = 解码帧数
            Return
        End If
        Dim 经过毫秒 = CDbl(现在 - 采样起始时间戳) * 1000.0R / CDbl(Stopwatch.Frequency)
        If 经过毫秒 < 250.0R Then Return
        Dim 新增帧 = CLng(解码帧数) - CLng(采样起始解码帧数)
        If 新增帧 < 0 Then
            ' 计数器归零（换媒体）。这里必须把起点拉到当前值，不能只 Return：
            ' 旧的起点可能比新片的计数器大几个数量级。
            采样起始时间戳 = 现在
            采样起始解码帧数 = 解码帧数
            Return
        End If
        If 新增帧 = 0 Then Return
        Dim 帧周期 = 经过毫秒 / CDbl(新增帧)
        采样起始时间戳 = 现在
        采样起始解码帧数 = 解码帧数
        If Double.IsFinite(帧周期) AndAlso 帧周期 > 0.0R Then
            Dim 目标 = CInt(Math.Round(Math.Clamp(帧周期, CDbl(最小间隔毫秒), CDbl(泵间隔毫秒)),
                                     MidpointRounding.AwayFromZero))
            If 目标 <> 刷新计时器.Interval Then 刷新计时器.Interval = 目标
        End If
    End Sub

    ' ── 命令生成与提交 ──────────────────────────────────────────────────────

    Private Sub 提交当前帧(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                       播放位置 As TimeSpan, DPI As Single,
                       时间轴代次 As ULong, 重建调度器 As Boolean)
        Try
            SyncLock 配置锁
                If 重建调度器 Then 当前弹幕调度器?.重置()
                Dim commands = 生成命令(客户区大小, 视频宽度, 视频高度, 播放位置, DPI)
                图层序号 += 1UL
                提交图层(客户区大小, commands, 图层序号, 弹幕目标帧率())
            End SyncLock
            Interlocked.Increment(提交次数)
        Catch
            ' 弹幕是可选图层；异常不能影响视频呈现。
        End Try
    End Sub

    ''' <summary>交给内核的"目标帧率"：1000 / 当前泵间隔。16 ms ⇒ 62.5，8 ms ⇒ 125；
    ''' 内核用它抬升 overlay 合成节奏（FrameRateBoundHz 的图层那一半），
    ''' 不被媒体帧率上限吞掉，也不会超过实际面板刷新率。</summary>
    Private Function 弹幕目标帧率() As Single
        Dim 间隔 = 刷新计时器.Interval
        If 间隔 <= 0 Then Return 60.0F
        Return CSng(1000.0R / CDbl(间隔))
    End Function

    Private Function 生成命令(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                          播放位置 As TimeSpan, DPI As Single) As IReadOnlyList(Of 定时文字命令)
        图层命令.Clear()
        命令对象使用数 = 0
        If Volatile.Read(已释放标志) <> 0 OrElse 视频宽度 = 0 OrElse 视频高度 = 0 OrElse
            客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then Return 图层命令
        Dim DPI缩放 = DPI / 96.0F
        Dim 区域 = 视频显示区域.计算(客户区大小.Width / DPI缩放, 客户区大小.Height / DPI缩放, DPI,
                                     CInt(视频宽度), CInt(视频高度))
        ' 弹幕横向穿过整个窗口（不是视频框），字号/行高/上下边距仍按视频高度缩放。
        Dim 弹幕缩放 = 区域.高度像素 / 弹幕配置.基准视频高度
        Select Case 弹幕配置.尺寸缩放方式
            Case 1 : 弹幕缩放 = 客户区大小.Width / 1920.0F
            Case 2 : 弹幕缩放 = 客户区大小.Height / 弹幕配置.基准视频高度
            Case 3 : 弹幕缩放 = 1.0F
        End Select
        Dim 弹幕区域 As New 视频显示区域(0.0F, 区域.Y像素, 客户区大小.Width,
                                   区域.高度像素, 弹幕缩放, 区域.DPI)
        生成弹幕命令(弹幕提供器?.Invoke(), 播放位置, 弹幕区域)
        Return 图层命令
    End Function

    Private Sub 生成弹幕命令(资料库 As 弹幕资料库, 时间 As TimeSpan, 区域 As 视频显示区域)
        If 资料库 Is Nothing Then
            当前弹幕资料库 = Nothing
            当前弹幕调度器 = Nothing
            Return
        End If
        If Not ReferenceEquals(当前弹幕资料库, 资料库) Then
            当前弹幕资料库 = 资料库
            ' 测量器用默认的即可 —— 宽度缓存已经做在它内部（Shared，跨实例复用）。
            当前弹幕调度器 = New 弹幕调度器(资料库, 弹幕配置)
        End If
        弹幕绘制项.Clear()
        当前弹幕调度器.生成帧(时间, 区域, 弹幕绘制项)
        For Each 项 In 弹幕绘制项
            If String.IsNullOrWhiteSpace(项.项目.文本) Then Continue For
            添加文字命令(项.项目.文本, 项.字体, 项.字号像素,
                New RectangleF(项.X像素, 项.Y像素, Math.Max(1.0F, 项.宽度像素), Math.Max(1.0F, 项.高度像素)),
                项.颜色ARGB, 弹幕配置.描边颜色ARGB,
                弹幕配置.描边宽度 * 项.字号像素 / 弹幕配置.字号,
                定时文字对齐.靠前, 定时文字对齐.靠前,
                样式:=CType(弹幕配置.字体样式, 定时文字样式) Or 定时文字样式.软阴影,
                阴影色ARGB:=弹幕配置.阴影颜色ARGB,
                阴影X偏移:=弹幕配置.阴影深度 * 项.字号像素 / 弹幕配置.字号,
                阴影Y偏移:=弹幕配置.阴影深度 * 项.字号像素 / 弹幕配置.字号)
        Next
    End Sub

    ' ── 对外操作 ────────────────────────────────────────────────────────────

    ''' <summary>下一次节拍强制提交一次（设置/几何变化后调用）。</summary>
    Friend Sub 使图层失效()
        Volatile.Write(强制提交标志, 1)
    End Sub

    Friend Sub 应用弹幕设置(value As 弹幕显示配置)
        ArgumentNullException.ThrowIfNull(value)
        value.验证()
        SyncLock 配置锁
            弹幕配置 = value
            当前弹幕调度器?.应用配置(value)
        End SyncLock
        使图层失效()
    End Sub

    Private Sub 画面几何已变化(sender As Object, e As EventArgs)
        更新画面快照()
        使图层失效()
    End Sub

    Private Sub 更新画面快照()
        Volatile.Write(缓存客户区宽度, 画面控件.ClientSize.Width)
        Volatile.Write(缓存客户区高度, 画面控件.ClientSize.Height)
        Volatile.Write(缓存DPI位元, BitConverter.SingleToInt32Bits(画面控件.DeviceDpi))
    End Sub

    ' ── 命令复用 ────────────────────────────────────────────────────────────

    Private Function 取得复用命令() As 定时文字命令
        Dim result As 定时文字命令
        If 命令对象使用数 < 命令对象池.Count Then
            result = 命令对象池(命令对象使用数)
        Else
            result = New 定时文字命令()
            命令对象池.Add(result)
        End If
        命令对象使用数 += 1
        Return result
    End Function

    Private Sub 添加文字命令(文本 As String, 字体 As String, 字号 As Single,
                          区域 As RectangleF, 前景色ARGB As UInteger,
                          描边色ARGB As UInteger, 描边宽度 As Single,
                          水平对齐 As 定时文字对齐, 垂直对齐 As 定时文字对齐,
                          Optional 样式 As 定时文字样式 = 定时文字样式.无,
                          Optional 内容标识 As ULong = 0,
                          Optional 阴影色ARGB As UInteger = 0,
                          Optional 阴影X偏移 As Single = 0,
                          Optional 阴影Y偏移 As Single = 0)
        Dim command = 取得复用命令()
        command.设置文字(文本, 字体, 字号, 区域, 前景色ARGB, 描边色ARGB, 描边宽度,
                     水平对齐, 垂直对齐, 样式, 内容标识,
                     阴影色ARGB, 阴影X偏移, 阴影Y偏移)
        图层命令.Add(command)
    End Sub

    ' ── 释放 ────────────────────────────────────────────────────────────────

    Public Sub 释放() Implements IDisposable.Dispose
        SyncLock 生命周期锁
            If 已释放标志 <> 0 Then Return
            Volatile.Write(已释放标志, 1)
        End SyncLock
        刷新计时器.Stop()
        RemoveHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick
        ' PrecisionTimer 在 UI 线程调用 Stop 时异步回收 worker。这里等待已经进入的
        ' 后台 Tick 退出，保证清空图层之后旧命令不可能再次提交。
        刷新空闲.Wait()
        刷新计时器.Dispose()
        RemoveHandler 画面控件.ClientSizeChanged, AddressOf 画面几何已变化
        RemoveHandler 画面控件.DpiChangedAfterParent, AddressOf 画面几何已变化
        Try
            图层序号 += 1UL
            提交图层(New Size(Math.Max(1, 画面控件.ClientSize.Width),
                              Math.Max(1, 画面控件.ClientSize.Height)),
                   Array.Empty(Of 定时文字命令)(), 图层序号, 60.0F)
        Catch
        End Try
        刷新空闲.Dispose()
    End Sub
End Class
