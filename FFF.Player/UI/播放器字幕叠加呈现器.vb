Imports System.Runtime.InteropServices
Imports System.Threading

''' <summary>将 SRT、ASS/SSA、SUP 和内嵌字幕转换为独立 GPU 图层。</summary>
Friend NotInheritable Class 播放器字幕叠加呈现器
    Implements IDisposable

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 字幕提供器 As Func(Of 外部字幕轨道)
    Private ReadOnly 提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single)
    ' AI 实时字幕（docs/ai-subtitles/）：可选注入点。Nothing = 未接入，本类行为与从前一致。
    ' 签名取（播放位置, 时间轴代次）→ 文本：控制器自己管 worker 与状态机，呈现器只问
    ' 「这一帧该显示哪句」，不持有任何 ASR 类型，也不参与它的启停。
    Private ReadOnly AI字幕提供器 As Func(Of TimeSpan, ULong, (主 As String, 副 As String, 推理开始 As Long))
    Private AI字幕样式 As SRT字幕样式 = 设置.实例对象.创建SRT字幕样式()
    Private ReadOnly AI字幕绘制行 As New List(Of SRT字幕绘制行)(4)
    Private ReadOnly SRT绘制项 As New List(Of SRT字幕绘制项)()
    Private ReadOnly SRT行测量 As New List(Of 字幕行测量结果)()
    Private ReadOnly SRT测量缓存 As New Dictionary(Of 字幕行测量键, 字幕行测量结果)()
    Private ReadOnly SUP绘制项 As New List(Of SUP字幕绘制项)(1)
    Private ReadOnly 图层命令 As New List(Of 定时文字命令)()
    Private ReadOnly 命令对象池 As New 定时文字命令池(128)
    Private ReadOnly 刷新计时器 As LakeUI.PrecisionTimer
    Private Const 当前目标帧率 As Integer = 60
    Private 图层序号 As ULong
    Private ReadOnly 读取字幕呈现 As Func(Of (序号 As ULong, 计时 As Long))
    Private 本次推理开始 As Long
    Private 已计时结果 As Long
    Private 待呈现结果 As Long
    Private 待呈现图层 As ULong
    Private 上次字幕代次 As ULong
    Private 上次推理呈现毫秒 As Double = -1

    Friend ReadOnly Property AI字幕呈现毫秒 As Double
        Get
            Return Threading.Volatile.Read(上次推理呈现毫秒)
        End Get
    End Property
    Private 上次图层签名 As ULong
    Private 缓存客户区宽度 As Integer
    Private 缓存客户区高度 As Integer
    Private 缓存DPI位元 As Integer
    Private 图层签名有效标志 As Integer
    Private AI字幕选中标志 As Integer
    Private 待清理AI字幕缓存 As Integer
    Private ReadOnly 生命周期锁 As New Object()
    Private ReadOnly 刷新空闲 As New ManualResetEventSlim(True)
    Private 活动刷新数 As Integer
    Private 已释放标志 As Integer
    ' 上一拍是否画了 AI 字幕。有短句在屏上时按帧检查结束时间。
    Private 上一拍有AI字幕标志 As Integer

    ' ── 空闲降频（2026-11-01）──────────────────────────────────────────────
    ' 三台图层泵各占一条 PrecisionTimer 调度线程，而调度线程的设计是在每个刻度的
    ' 最后 0.5 ms 用 SpinWait 忙等精磨（PrecisionTimer.vb:509-531）。16 ms 间隔下
    ' 每台约 3% 单核，消融矩阵实测三台合计 51 ms/s 常驻 CPU（地板 66 → 15 ms/s）。
    ' 没有字幕轨道时这份忙等毫无产出。
    '
    ' 普通轨按帧检查。AI 是当前源时，短句之间保留较短间隔，以便短句开始后及时提交。
    ' 使图层失效不改间隔，仍由 tick 线程负责切换。
    '
    ' 只调频率、不改提交条件：提交当前帧() 内部已有图层签名去重，无字幕时那一拍
    ' 是几次整数运算。不自己记「屏幕上是不是空的」，以免复制它的职责后漏掉状态迁移。
    Private Const 工作间隔毫秒 As Integer = 16
    Private Const AI字幕间隙间隔毫秒 As Integer = 40
    Private Const 空闲间隔毫秒 As Integer = 200
    Private 当前间隔毫秒 As Integer = 工作间隔毫秒

    Private Structure 字幕行测量键
        Implements IEquatable(Of 字幕行测量键)

        Public Sub New(文本值 As String, 字体值 As String, 字号值 As Single, 样式值 As FontStyle,
                       宽度值 As Single, 描边值 As Single, 阴影值 As Single, 阴影有效值 As Boolean)
            文本 = 文本值
            字体 = 字体值
            字号位元 = BitConverter.SingleToInt32Bits(字号值)
            样式 = CInt(样式值)
            宽度位元 = BitConverter.SingleToInt32Bits(宽度值)
            描边位元 = BitConverter.SingleToInt32Bits(描边值)
            阴影位元 = BitConverter.SingleToInt32Bits(阴影值)
            阴影有效 = 阴影有效值
        End Sub

        Private ReadOnly 文本 As String
        Private ReadOnly 字体 As String
        Private ReadOnly 字号位元 As Integer
        Private ReadOnly 样式 As Integer
        Private ReadOnly 宽度位元 As Integer
        Private ReadOnly 描边位元 As Integer
        Private ReadOnly 阴影位元 As Integer
        Private ReadOnly 阴影有效 As Boolean

        Public Overloads Function Equals(other As 字幕行测量键) As Boolean Implements IEquatable(Of 字幕行测量键).Equals
            Return String.Equals(文本, other.文本, StringComparison.Ordinal) AndAlso
                String.Equals(字体, other.字体, StringComparison.Ordinal) AndAlso
                字号位元 = other.字号位元 AndAlso 样式 = other.样式 AndAlso
                宽度位元 = other.宽度位元 AndAlso 描边位元 = other.描边位元 AndAlso
                阴影位元 = other.阴影位元 AndAlso 阴影有效 = other.阴影有效
        End Function

        Public Overrides Function Equals(obj As Object) As Boolean
            Return TypeOf obj Is 字幕行测量键 AndAlso Equals(DirectCast(obj, 字幕行测量键))
        End Function

        Public Overrides Function GetHashCode() As Integer
            Dim hash = HashCode.Combine(文本, 字体, 字号位元, 样式, 宽度位元, 描边位元, 阴影位元)
            Return HashCode.Combine(hash, 阴影有效)
        End Function
    End Structure

    Private Structure 字幕行测量结果
        Public Sub New(布局高度值 As Single, 可见底部值 As Single)
            布局高度 = 布局高度值
            可见底部 = 可见底部值
        End Sub
        Public ReadOnly 布局高度 As Single
        Public ReadOnly 可见底部 As Single
    End Structure

    Friend Sub New(画面控件 As 播放器画面控件, 快照提供器 As Func(Of 播放器快照),
                   字幕提供器 As Func(Of 外部字幕轨道),
                   提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single),
                   Optional AI字幕提供器 As Func(Of TimeSpan, ULong, (主 As String, 副 As String, 推理开始 As Long)) = Nothing,
                   Optional 读取字幕呈现 As Func(Of (序号 As ULong, 计时 As Long)) = Nothing)
        ArgumentNullException.ThrowIfNull(画面控件)
        ArgumentNullException.ThrowIfNull(快照提供器)
        ArgumentNullException.ThrowIfNull(字幕提供器)
        ArgumentNullException.ThrowIfNull(提交图层)
        Me.画面控件 = 画面控件
        Me.快照提供器 = 快照提供器
        Me.字幕提供器 = 字幕提供器
        Me.提交图层 = 提交图层
        Me.AI字幕提供器 = AI字幕提供器
        Me.读取字幕呈现 = 读取字幕呈现
        刷新计时器 = New LakeUI.PrecisionTimer With {
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop
        }
        ' 定时泵不依赖 UI 消息队列。窗口几何只由 UI 事件发布为数值快照；
        ' 后台 Tick 不访问 Control 成员，因此拖动窗口或界面刷新不会把 60 Hz
        ' 弹幕退化成 30–40 Hz。
        更新画面快照()
        AddHandler 画面控件.ClientSizeChanged, AddressOf 画面几何已变化
        AddHandler 画面控件.DpiChangedAfterParent, AddressOf 画面几何已变化
        AddHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick
        更新刷新间隔()
        刷新计时器.Start()
    End Sub

    ''' <summary>AI 是选中的字幕源时才取推理结果，纯音频也驱动图层。</summary>
    Friend Property AI字幕已选中 As Boolean
        Get
            Return Volatile.Read(AI字幕选中标志) <> 0
        End Get
        Set(value As Boolean)
            Dim 旧值 = Volatile.Read(AI字幕选中标志) <> 0
            If 旧值 = value Then Return
            Volatile.Write(AI字幕选中标志, If(value, 1, 0))
            If Not value Then Volatile.Write(待清理AI字幕缓存, 1)
            使图层失效()
        End Set
    End Property

    Private Sub 刷新计时器_Tick(sender As Object, e As EventArgs)
        SyncLock 生命周期锁
            If 已释放标志 <> 0 Then Return
            活动刷新数 += 1
            刷新空闲.Reset()
        End SyncLock
        Try
            If Interlocked.Exchange(待清理AI字幕缓存, 0) <> 0 Then
                AI字幕换行缓存.Clear()
                AI字幕绘制行.Clear()
                SRT测量缓存.Clear()
                图层命令.Clear()
                命令对象池.清空()
                Volatile.Write(上一拍有AI字幕标志, 0)
            End If
            Dim 快照 = 快照提供器()
            Dim 客户区大小 = New Size(Volatile.Read(缓存客户区宽度), Volatile.Read(缓存客户区高度))
            If 快照 Is Nothing OrElse 客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then
                ' 没有画面就谈不上图层：与旧行为一样直接返回，只把调度降下来。
                设置间隔(空闲间隔毫秒)
                Return
            End If
            ' 纯音频：没有画面就没有"画面区域"，但 AI 字幕仍然要上屏（音乐/播客场景）。
            ' 只有选了 AI 字幕才走这条；否则纯音频不画字幕。
            If (快照.视频宽度 = 0 OrElse 快照.视频高度 = 0) AndAlso Not AI字幕已选中 Then
                ' 关掉回退开关的那一刻，屏上可能正挂着纯音频路径提交的最后一帧
                ' （AI 字幕或此前开过回退时的外部字幕）。这里不显式清一次，
                ' 后续每拍都在这条早退里 return，再没有一拍会提交空图层 ——
                ' 残影一直挂到停止或换媒体（P1，2026-11-02 审查定案）。
                ' 本拍之外没有内容可画：只清一次（签名有效标志归零后不再重发）。
                If Volatile.Read(图层签名有效标志) <> 0 Then
                    提交空图层()
                End If
                设置间隔(空闲间隔毫秒)
                Return
            End If
            ' 有画面 ⇒ 一律照常提交，**不在这里判断"有没有内容要画"**。
            ' 为什么这样而不是自己维护一个「已清空」标志：提交当前帧() 内部已有
            ' 图层签名去重（signature = 画布大小 + commands，相同即早退），
            ' 无字幕时 commands 为空、签名恒定 ⇒ 那一拍只是几次整数运算。
            ' 自己再记一份"屏幕上是不是空的"等于复制它的职责，一旦漏了某条
            ' 状态迁移（加载→卸载、seek、改设置…）就会把残影永久留在屏幕上。
            ' 所以这里只调**频率**，把"要不要真画"的判断留给已有的去重机制。
            Dim 字幕 = 字幕提供器()
            Dim AI当前选中 = AI字幕已选中 AndAlso 快照.状态 <> 播放状态.播放结束
            设置间隔(If(字幕 IsNot Nothing OrElse Volatile.Read(上一拍有AI字幕标志) <> 0,
                        工作间隔毫秒, If(AI当前选中, AI字幕间隙间隔毫秒, 空闲间隔毫秒)))
            Dim DPI = BitConverter.Int32BitsToSingle(Volatile.Read(缓存DPI位元))
            提交当前帧(客户区大小, 快照.视频宽度, 快照.视频高度,
                     快照.播放位置, 字幕, DPI, 快照.时间轴代次,
                     显示AI字幕:=AI当前选中)
        Finally
            SyncLock 生命周期锁
                活动刷新数 -= 1
                If 活动刷新数 = 0 Then 刷新空闲.Set()
            End SyncLock
        End Try
    End Sub

    ''' <summary>
    ''' 改调度间隔。PrecisionTimer.Interval 的 setter 只写一个字段、调度线程每刻度重读
    ''' （PrecisionTimer.vb:497），因此从后台 Tick 里改是安全的，且下一刻度即生效。
    ''' 相同值直接跳过，避免每拍都写。
    ''' </summary>
    Private Sub 设置间隔(毫秒 As Integer)
        If 当前间隔毫秒 = 毫秒 Then Return
        当前间隔毫秒 = 毫秒
        刷新计时器.Interval = 毫秒
    End Sub

    Private Sub 画面几何已变化(sender As Object, e As EventArgs)
        更新画面快照()
    End Sub

    Private Sub 更新画面快照()
        Volatile.Write(缓存客户区宽度, 画面控件.ClientSize.Width)
        Volatile.Write(缓存客户区高度, 画面控件.ClientSize.Height)
        Volatile.Write(缓存DPI位元, BitConverter.SingleToInt32Bits(画面控件.DeviceDpi))
    End Sub

    Private Sub 更新刷新间隔()
        ' 60 Hz 若四舍五入为 17 ms，上限只有 58.82 Hz，并会周期性错过一帧。
        ' 计时器略快唤醒，最终原生呈现器仍按目标帧率合并提交。
        刷新计时器.Interval = 16
    End Sub

    Friend Sub 应用SRT字幕样式(样式 As SRT字幕样式)
        Volatile.Write(AI字幕样式, 样式)
        使图层失效()
    End Sub

    Friend Function 生成命令(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                         播放位置 As TimeSpan, 字幕 As 外部字幕轨道,
                         Optional DPI As Single = 96.0F,
                         Optional 时间轴代次 As ULong = 0,
                         Optional 显示AI字幕 As Boolean = True) As IReadOnlyList(Of 定时文字命令)
        图层命令.Clear()
        命令对象池.开始帧()
        Dim 无视频 = 视频宽度 = 0 OrElse 视频高度 = 0
        If Volatile.Read(已释放标志) <> 0 OrElse (无视频 AndAlso Not AI字幕已选中) OrElse
            客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then Return 图层命令
        If Not Single.IsFinite(DPI) OrElse DPI <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(DPI))
        ' WinForms 的 ClientSize 已经是物理像素；先换回 DIP，再交给字幕/弹幕共用的
        ' 视频显示区域换算，避免高 DPI 下重复放大。
        Dim DPI缩放 = DPI / 96.0F
        Dim 区域 As 视频显示区域
        If 无视频 Then
            ' 纯音频：没有画面可让位，整个客户区就是"视频区域"，AI 字幕照它定底边。
            ' 计算() 对 0 尺寸抛异常，所以这条分支必须绕开它，而不是塞一个假的 1×1。
            ' ⚠ 字段名是「像素」就是物理像素：ClientSize 已经是物理像素，这里**不能**
            '   再除 DPI缩放。此前除了一次但从没乘回来，下游（基准字号 = 高度像素/22、
            '   底边 = Y像素 + 高度像素）全当物理像素用 ⇒ 150%/200% 缩放下字号只有
            '   应有大小的 2/3、1/2，且贴在窗口中间（P1，2026-11-02 审查定案）。
            区域 = New 视频显示区域(0.0F, 0.0F, CSng(客户区大小.Width), CSng(客户区大小.Height),
                                      CSng(客户区大小.Height) / 1080.0F, DPI)
        Else
            区域 = 视频显示区域.计算(客户区大小.Width / DPI缩放, 客户区大小.Height / DPI缩放, DPI,
                                      CInt(视频宽度), CInt(视频高度))
        End If
        ' 纯音频回退（无视频轨）**不画外部字幕**：属性注释对用户承诺的就是这一条
        ' （"既有的纯音频行为一字不改"）。此前这里原样下传字幕轨，装了 AI 包后
        ' 带同名 srt/ass 的音频凭空多出外部字幕层（P1，2026-11-02 审查定案）。
        If Not 无视频 AndAlso 字幕 IsNot Nothing AndAlso 字幕.尝试进入使用() Then
            Try
                Select Case 字幕.格式
                    Case 外部字幕格式.SRT
                        生成SRT命令(字幕.SRT生成器, 播放位置, 区域, 客户区大小)
                    Case 外部字幕格式.ASS, 外部字幕格式.SSA
                        生成ASS特效命令(字幕.ASS特效生成器, 播放位置, 区域)
                    Case 外部字幕格式.SUP
                        生成SUP命令(字幕.SUP生成器, 播放位置, 区域)
                    Case 外部字幕格式.MKS
                        ' MKS 是字幕容器：位图轨由 SUP 生成器、文本轨由 ASS 特效
                        ' 生成器承载，与 外部字幕自动加载器.分派字幕轨 的选型一致。
                        If 字幕.SUP生成器 IsNot Nothing Then
                            生成SUP命令(字幕.SUP生成器, 播放位置, 区域)
                        Else
                            生成ASS特效命令(字幕.ASS特效生成器, 播放位置, 区域)
                        End If
                End Select
            Finally
                字幕.离开使用()
            End Try
        End If
        If 显示AI字幕 AndAlso 字幕 Is Nothing AndAlso AI字幕提供器 IsNot Nothing Then
            追加AI字幕命令(播放位置, 时间轴代次, 区域, 客户区大小)
        Else
            Volatile.Write(上一拍有AI字幕标志, 0)
        End If
        Return 图层命令
    End Function

    ' 状态机负责字幕时序。实时文本直接转成 SRT 绘制项，与外挂 SRT 共用样式、测量和摆放。
    Private Sub 追加AI字幕命令(播放位置 As TimeSpan, 时间轴代次 As ULong, 区域 As 视频显示区域,
                           画布大小 As Size)
        Volatile.Write(上一拍有AI字幕标志, 0)
        Try
            Dim 两行 = AI字幕提供器(播放位置, 时间轴代次)
            If String.IsNullOrWhiteSpace(两行.主) Then Return
            If 区域.宽度像素 <= 1 OrElse 区域.高度像素 <= 1 Then Return

            Dim 样式 = Volatile.Read(AI字幕样式)
            Dim 缩放 = 样式.计算缩放(区域, 画布大小.Width, 画布大小.Height)
            Dim 有下行 = Not String.IsNullOrWhiteSpace(两行.副)
            AI字幕绘制行.Clear()
            追加AI折行(样式.格式化行(两行.主, SRT字幕解析器.检测语言(两行.主), 0, 缩放),
                      区域.宽度像素, If(有下行, 2, AI字幕最大行数))
            If 有下行 Then
                追加AI折行(样式.格式化行(两行.副, SRT字幕解析器.检测语言(两行.副), 1, 缩放),
                          区域.宽度像素, 2)
            End If
            Dim 底边 = 样式.计算底部位置(区域, 画布大小.Height, 缩放)
            ' 外部字幕或位图已占住底部时，实时字幕叠到该项上方。
            For Each 命令 In 图层命令
                If 命令.Y > 0.0F Then 底边 = Math.Min(底边, 命令.Y)
            Next
            Dim 项 As New SRT字幕绘制项(Nothing, AI字幕绘制行, 0.0F, 底边, 样式.行间距 * 缩放,
                                       垂直对齐值:=定时文字对齐.靠后)
            Dim 顶部Y = 区域.Y像素
            Dim 原命令数 = 图层命令.Count
            追加SRT绘制项(项, 区域, 顶部Y)
            If 图层命令.Count > 原命令数 Then 本次推理开始 = 两行.推理开始
            Volatile.Write(上一拍有AI字幕标志, 1)
        Catch ex As Exception
            ' 实时图层的单帧失败不得中断视频和外部字幕。
            Debug.WriteLine(ex)
        End Try
    End Sub

    Private Sub 追加AI折行(行 As SRT字幕绘制行, 最大宽度 As Single, 最大行数 As Integer)
        Dim 描边余量 = If((行.描边颜色ARGB >> 24) <> 0UI, 行.描边宽度像素 * 2.0F, 0.0F)
        Dim 折行 = 生成AI字幕行(行.文本, 行.字体, 行.字号像素, 行.字体样式,
                              Math.Max(1.0F, 最大宽度 - 描边余量), 最大行数)
        Dim 缩放 = 折行.字号 / 行.字号像素
        For Each 文本 In 折行.行
            AI字幕绘制行.Add(New SRT字幕绘制行(文本, 行.字体, 折行.字号, 行.颜色ARGB,
                                            行.描边颜色ARGB, 行.描边宽度像素 * 缩放,
                                            行.阴影颜色ARGB, 行.阴影偏移像素 * 缩放, 行.字体样式))
        Next
    End Sub

    Private Const AI字幕最大行数 As Integer = 3
    Private Const AI字幕换行缓存上限 As Integer = 512
    Private Const AI字幕最小缩放 As Single = 0.62F
    Private ReadOnly AI字幕换行缓存 As New Dictionary(
        Of (文本 As String, 字体 As String, 字号位元 As Integer, 样式 As FontStyle, 最大宽度 As Integer), String())

    ''' <summary>把整句折成至多 AI字幕最大行数 行；任一档字号放不下就逐级缩小
    ''' （1.0 → 0.62），到下限仍超出则截尾加省略号。返回最终行数组与采用的字号。</summary>
    Private Function 生成AI字幕行(文本 As String, 字体 As String, 基准字号 As Single, 样式 As FontStyle,
                              最大宽度 As Single, 最大行数 As Integer) As (行 As String(), 字号 As Single)
        If String.IsNullOrWhiteSpace(文本) Then Return (Array.Empty(Of String)(), 基准字号)
        If 最大行数 < 1 Then 最大行数 = 1
        Dim 行 As String() = {文本}
        For Each 缩放 In {1.0F, 0.88F, 0.78F, 0.7F, AI字幕最小缩放}
            Dim 字号 = 基准字号 * 缩放
            行 = 折行AI字幕文本(文本, 字体, 字号, 样式, 最大宽度)
            If 行.Length <= 最大行数 Then Return (行, 字号)
        Next
        Dim 字号下限 = 基准字号 * AI字幕最小缩放
        Dim 截断(最大行数 - 1) As String
        Array.Copy(行, 截断, 最大行数)
        ' 尾行换成"折行后能放下的最长前缀"——折行函数保证首行不超宽。
        截断(最大行数 - 1) = 折行AI字幕文本(
            行(最大行数 - 1) & "…", 字体, 字号下限, 样式, 最大宽度)(0)
        Return (截断, 字号下限)
    End Function

    ''' <summary>按字素二分找每行能放下的最长前缀，西文优先在空格处断行
    '''（策略与 播放器歌词呈现器.按字素换行 一致），宽度用
    ''' FFF3FP_MeasureTimedTextWidth 实测，按（文本,字体,字号,宽度）缓存。
    ''' 本方法与绘制同在字幕呈现的刷新线程串行调用，缓存无需加锁
    '''（SRT测量缓存 同款约定）。</summary>
    Private Function 折行AI字幕文本(文本 As String, 字体 As String, 字号 As Single, 样式 As FontStyle,
                                最大宽度 As Single) As String()
        Dim 宽度上限 = Math.Max(1, CInt(Math.Floor(最大宽度)))
        Dim 键 = (文本, 字体, BitConverter.SingleToInt32Bits(字号), 样式, 宽度上限)
        Dim 已折 As String() = Nothing
        If AI字幕换行缓存.TryGetValue(键, 已折) Then Return 已折

        Dim 实测宽度 As Single
        If 测量AI字幕宽度(文本, 字体, 字号, 样式, 实测宽度) AndAlso 实测宽度 <= 宽度上限 Then
            已折 = {文本}
        Else
            Dim starts = System.Globalization.StringInfo.ParseCombiningCharacters(文本)
            Dim 结果 As New List(Of String)()
            Dim 起点 = 0
            While 起点 < starts.Length
                While 起点 < starts.Length AndAlso Char.IsWhiteSpace(文本, starts(起点))
                    起点 += 1
                End While
                If 起点 >= starts.Length Then Exit While

                Dim 低 = 起点 + 1
                Dim 高 = starts.Length
                Dim 适配终点 = 低
                While 低 <= 高
                    Dim 中 = 低 + ((高 - 低) \ 2)
                    If 测量AI字幕宽度(取得字素前缀(文本, starts, 起点, 中), 字体, 字号, 样式, 实测宽度) AndAlso
                        实测宽度 <= 宽度上限 Then
                        适配终点 = 中
                        低 = 中 + 1
                    Else
                        高 = 中 - 1
                    End If
                End While

                Dim 断点 = 适配终点
                If 适配终点 < starts.Length Then
                    For 索引 = 适配终点 - 1 To 起点 + 1 Step -1
                        If Char.IsWhiteSpace(文本, starts(索引)) Then
                            断点 = 索引
                            Exit For
                        End If
                    Next
                End If
                Dim 行 = 取得字素前缀(文本, starts, 起点, 断点).TrimEnd()
                If 行.Length > 0 Then 结果.Add(行)
                起点 = Math.Max(断点, 起点 + 1)
            End While
            已折 = If(结果.Count = 0, {文本}, 结果.ToArray())
        End If
        If AI字幕换行缓存.Count >= AI字幕换行缓存上限 Then AI字幕换行缓存.Clear()
        AI字幕换行缓存(键) = 已折
        Return 已折
    End Function

    Private Shared Function 取得字素前缀(文本 As String, starts As Integer(),
                                    起点 As Integer, 终点 As Integer) As String
        Dim 起始索引 = starts(起点)
        Dim 结束索引 = If(终点 < starts.Length, starts(终点), 文本.Length)
        Return 文本.Substring(起始索引, 结束索引 - 起始索引)
    End Function

    Private Shared Function 测量AI字幕宽度(文本 As String, 字体 As String, 字号 As Single, 样式 As FontStyle,
                                      ByRef 宽度 As Single) As Boolean
        宽度 = 0
        Try
            Return 播放器原生接口.FFF3FP_MeasureTimedTextWidth(
                文本, 字体, 字号, CType(样式, 原生定时文字标志), 宽度) = 原生播放器结果.成功 AndAlso
                Single.IsFinite(宽度) AndAlso 宽度 >= 0
        Catch
            Return False
        End Try
    End Function

    Friend Sub 提交当前帧(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                       播放位置 As TimeSpan, 字幕 As 外部字幕轨道,
                       Optional DPI As Single = 96.0F,
                       Optional 时间轴代次 As ULong = 0,
                       Optional 显示AI字幕 As Boolean = True)
        Try
            If 上次字幕代次 <> 时间轴代次 Then
                上次字幕代次 = 时间轴代次
                待呈现结果 = 0
                Threading.Volatile.Write(上次推理呈现毫秒, -1)
            End If
            If 待呈现结果 <> 0 Then
                Dim 呈现 = 读取字幕呈现()
                If 呈现.序号 = 待呈现图层 AndAlso 呈现.计时 >= 待呈现结果 Then
                    Threading.Volatile.Write(上次推理呈现毫秒,
                        Stopwatch.GetElapsedTime(待呈现结果, 呈现.计时).TotalMilliseconds)
                    已计时结果 = 待呈现结果
                    待呈现结果 = 0
                End If
            End If
            本次推理开始 = 0
            Dim commands = 生成命令(客户区大小, 视频宽度, 视频高度, 播放位置, 字幕,
                DPI, 时间轴代次, 显示AI字幕)
            Dim signature = 计算图层签名(客户区大小, commands)
            Dim 测本次 = 本次推理开始 <> 0 AndAlso 本次推理开始 <> 已计时结果 AndAlso 本次推理开始 <> 待呈现结果
            If Volatile.Read(图层签名有效标志) <> 0 AndAlso signature = 上次图层签名 AndAlso Not 测本次 Then Return
            上次图层签名 = signature
            Volatile.Write(图层签名有效标志, 1)
            图层序号 += 1UL
            If 读取字幕呈现 IsNot Nothing AndAlso 测本次 Then
                待呈现结果 = 本次推理开始
                待呈现图层 = 图层序号
            ElseIf 待呈现结果 <> 0 Then
                If 本次推理开始 = 待呈现结果 Then
                    待呈现图层 = 图层序号
                Else
                    待呈现结果 = 0
                End If
            End If
            提交图层(客户区大小, commands, 图层序号, CSng(当前目标帧率))
        Catch
            ' 定时文字是可选图层；异常不能影响视频呈现。
        End Try
    End Sub

    ''' <summary>提交一帧空图层把屏上内容擦掉（幂等：已空的签名相同即早退）。
    ''' 只用于「本来有内容、现在不该有」的过渡：关掉纯音频回退、释放。
    ''' 尺寸取 UI 线程发布的几何快照，不碰 Control 成员（本方法可能在 tick 线程）。</summary>
    Private Sub 提交空图层()
        Try
            Dim 宽 = Volatile.Read(缓存客户区宽度)
            Dim 高 = Volatile.Read(缓存客户区高度)
            If 宽 <= 0 OrElse 高 <= 0 Then Return
            Dim 尺寸 = New Size(宽, 高)
            Dim 空命令 = Array.Empty(Of 定时文字命令)()
            Dim signature = 计算图层签名(尺寸, 空命令)
            If Volatile.Read(图层签名有效标志) <> 0 AndAlso signature = 上次图层签名 Then Return
            上次图层签名 = signature
            Volatile.Write(图层签名有效标志, 1)
            图层序号 += 1UL
            提交图层(尺寸, 空命令, 图层序号, CSng(当前目标帧率))
        Catch
            ' 图层清理失败无妨：下一次提交或释放路径会再试。
        End Try
    End Sub

    Friend Sub 使图层失效()
        Volatile.Write(图层签名有效标志, 0)
        ' 注意：**这里不碰计时器间隔**。间隔只由 tick 线程写（单写者原则）。
        ' 从 UI 线程写会与 tick 里的写交错，可能让「守卫字段」与真实 Interval 脱钩，
        ' 结果是泵在有内容时永久停在空闲档（推演：UI 读到旧值→写守卫字段=16，
        ' tick 随后写 Interval=200 ⇒ 守卫说 16、硬件是 200，下一拍守卫相等即早退，
        ' 再也不会修正）。而且它想省的延迟本来也省不掉：调度线程当前这一拍已按旧
        ' 间隔武装（PrecisionTimer.vb:508-524），改字段不会缩短在途的 wait。
        ' 轨道到达后最坏等一个空闲拍（200 ms）恢复 60 Hz，只影响首行字幕出场。
    End Sub

    Private Shared Function 计算图层签名(画布大小 As Size,
                                      commands As IReadOnlyList(Of 定时文字命令)) As ULong
        Dim hash As ULong = &HCBF29CE484222325UL
        混合签名(hash, CULng(CUInt(画布大小.Width)))
        混合签名(hash, CULng(CUInt(画布大小.Height)))
        混合签名(hash, CULng(commands.Count))
        For Each item In commands
            混合签名(hash, If(item.是位图, 1UL, 0UL))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.X))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.Y))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.宽度))
            混合签名(hash, BitConverter.SingleToUInt32Bits(item.高度))
            If item.是位图 Then
                混合签名(hash, item.内容标识)
                混合签名(hash, CULng(item.位图宽度))
                混合签名(hash, CULng(item.位图高度))
                混合签名(hash, CULng(CLng(Runtime.CompilerServices.RuntimeHelpers.GetHashCode(item.位图像素BGRA)) And &HFFFFFFFFL))
            Else
                If item.内容标识 <> 0 Then
                    混合签名(hash, item.内容标识)
                Else
                    混合文本签名(hash, item.文本)
                    混合文本签名(hash, item.字体)
                End If
                混合签名(hash, BitConverter.SingleToUInt32Bits(item.字号))
                混合签名(hash, BitConverter.SingleToUInt32Bits(item.描边宽度))
                混合签名(hash, item.前景色ARGB)
                混合签名(hash, item.描边色ARGB)
                混合签名(hash, item.阴影色ARGB)
                混合签名(hash, BitConverter.SingleToUInt32Bits(item.阴影X偏移))
                混合签名(hash, BitConverter.SingleToUInt32Bits(item.阴影Y偏移))
                混合签名(hash, CULng(item.样式))
                混合签名(hash, CULng(item.水平对齐))
                混合签名(hash, CULng(item.垂直对齐))
            End If
        Next
        Return hash
    End Function

    Private Shared Sub 混合文本签名(ByRef hash As ULong, value As String)
        If value Is Nothing Then
            混合签名(hash, 0UL)
            Return
        End If
        For Each character In value
            混合签名(hash, CULng(AscW(character) And &HFFFF))
        Next
        混合签名(hash, &HFFFFUL)
    End Sub

    Private Shared Sub 混合签名(ByRef hash As ULong, value As ULong)
        hash = Numerics.BitOperations.RotateLeft(hash, 7) Xor value
    End Sub

    Private Sub 生成SRT命令(生成器 As SRT字幕帧生成器, 时间 As TimeSpan, 区域 As 视频显示区域,
                         画布大小 As Size)
        If 生成器 Is Nothing Then Return
        SRT绘制项.Clear()
        生成器.生成帧(时间, 区域, SRT绘制项, 画布大小.Width, 画布大小.Height)
        Dim 顶部字幕Y = 区域.Y像素
        For Each 项 In SRT绘制项
            追加SRT绘制项(项, 区域, 顶部字幕Y)
        Next
    End Sub

    Private Sub 追加SRT绘制项(项 As SRT字幕绘制项, 区域 As 视频显示区域, ByRef 顶部字幕Y As Single)
        SRT行测量.Clear()
        For Each 行 In 项.行
            SRT行测量.Add(测量字幕行(行, 区域.宽度像素))
        Next
        If SRT行测量.Count = 0 Then Return
        Dim 总高度 = SRT行测量.Sum(Function(测量) 测量.布局高度)
        If 项.行.Count > 1 Then 总高度 += 项.行间距像素 * (项.行.Count - 1)
        ' 底部边距针对最终可见像素，不是字号经验系数。这里的布局高度、字形
        ' overhang、描边和阴影均来自渲染所调用的同一个 DirectWrite 布局函数。
        Dim 最后一行测量 = SRT行测量(SRT行测量.Count - 1)
        总高度 += Math.Max(最后一行测量.可见底部 - 最后一行测量.布局高度, 0.0F)
        Dim y As Single
        Dim 命令X = 区域.X像素
        Dim 命令宽度 = 区域.宽度像素
        Dim 命令水平对齐 = 定时文字对齐.居中
        Dim 命令垂直对齐 = 定时文字对齐.靠前
        Dim 顶部对齐 = Not 项.使用定位 AndAlso 项.垂直对齐 = 定时文字对齐.靠前
        If 项.使用定位 Then
            Select Case 项.水平对齐
                Case 定时文字对齐.靠前
                    命令X = 项.X中心像素
                Case 定时文字对齐.居中
                    命令X = 项.X中心像素 - 区域.宽度像素 * 0.5F
                Case Else
                    命令X = 项.X中心像素 - 区域.宽度像素
            End Select
            命令水平对齐 = 项.水平对齐
            命令垂直对齐 = 项.垂直对齐
            Select Case 命令垂直对齐
                Case 定时文字对齐.靠前
                    y = 项.Y底部像素
                Case 定时文字对齐.居中
                    y = 项.Y底部像素 - 总高度 * 0.5F
                Case 定时文字对齐.靠后
                    y = 项.Y底部像素 - 总高度
            End Select
        Else
            If 顶部对齐 Then
                y = Math.Max(顶部字幕Y, 项.Y底部像素)
                顶部字幕Y = y + 总高度 + Math.Max(8.0F, 项.行间距像素)
            Else
                y = 项.Y底部像素 - 总高度
            End If
        End If
        For 行索引 = 0 To 项.行.Count - 1
            Dim 行 = 项.行(行索引)
            Dim 行高 = SRT行测量(行索引).布局高度
            添加文字命令(行.文本, 行.字体, 行.字号像素,
                New RectangleF(命令X, y, 命令宽度, 行高),
                行.颜色ARGB, 行.描边颜色ARGB, 行.描边宽度像素,
                命令水平对齐, 命令垂直对齐,
                阴影色ARGB:=行.阴影颜色ARGB,
                样式:=CType(行.字体样式, 定时文字样式),
                阴影X偏移:=行.阴影偏移像素, 阴影Y偏移:=行.阴影偏移像素)
            y += 行高 + 项.行间距像素
        Next
    End Sub

    Private Function 测量字幕行(行 As SRT字幕绘制行, 最大宽度 As Single) As 字幕行测量结果
        Dim 有描边 = 行.描边宽度像素 > 0 AndAlso (行.描边颜色ARGB >> 24) <> 0UI
        Dim 有阴影 = (行.阴影颜色ARGB >> 24) <> 0UI
        Dim 实际描边 = If(有描边, 行.描边宽度像素, 0.0F)
        Dim 限定宽度 = Math.Max(最大宽度, 1.0F)
        Dim 键 As New 字幕行测量键(行.文本, 行.字体, 行.字号像素, 行.字体样式,
                              限定宽度, 实际描边, 行.阴影偏移像素, 有阴影)
        Dim 已有 As 字幕行测量结果
        If SRT测量缓存.TryGetValue(键, 已有) Then Return 已有

        Dim 原生 As New 原生定时文字测量 With {
            .大小 = CUInt(Marshal.SizeOf(Of 原生定时文字测量)()), .版本 = 20UI
        }
        Dim 结果 = 播放器原生接口.FFF3FP_MeasureTimedText(
            行.文本, 行.字体, 行.字号像素, CType(行.字体样式, 原生定时文字标志),
            限定宽度, 实际描边, 行.阴影偏移像素, 行.阴影偏移像素,
            If(有阴影, 1UI, 0UI), 原生)
        If 结果 <> 原生播放器结果.成功 OrElse Not Single.IsFinite(原生.布局高度) OrElse
            原生.布局高度 <= 0 OrElse Not Single.IsFinite(原生.可见底部) Then
            Throw New InvalidOperationException($"DirectWrite 无法测量字幕行（{结果}）。")
        End If
        已有 = New 字幕行测量结果(原生.布局高度, 原生.可见底部)
        If SRT测量缓存.Count >= 1024 Then SRT测量缓存.Clear()
        SRT测量缓存.Add(键, 已有)
        Return 已有
    End Function

    Private Sub 生成ASS特效命令(生成器 As ASS特效字幕帧生成器, 时间 As TimeSpan,
                            区域 As 视频显示区域)
        If 生成器 Is Nothing OrElse 区域.宽度像素 <= 0 OrElse 区域.高度像素 <= 0 Then Return
        ' libass 的画布应匹配最终显示区域，而不是源视频分辨率。4K 视频在
        ' 1080p 窗口播放时继续生成 4K 字幕，只会增加滤镜、扫描和跨边界复制成本，
        ' 最终仍会被 GPU 缩回 1080p。
        Dim 画布宽度 = Math.Max(1, CInt(Math.Round(区域.宽度像素, MidpointRounding.AwayFromZero)))
        Dim 画布高度 = Math.Max(1, CInt(Math.Round(区域.高度像素, MidpointRounding.AwayFromZero)))
        Dim 帧 = 生成器.生成帧(时间, 画布宽度, 画布高度)
        If 帧 Is Nothing OrElse 帧.像素BGRA.Length = 0 OrElse 帧.宽度 <= 0 OrElse 帧.高度 <= 0 OrElse
            帧.画布宽度 <= 0 OrElse 帧.画布高度 <= 0 Then Return
        Dim 水平缩放 = 区域.宽度像素 / 帧.画布宽度
        Dim 垂直缩放 = 区域.高度像素 / 帧.画布高度
        添加位图命令(帧.像素BGRA, 帧.宽度, 帧.高度, 帧.行跨度,
            New RectangleF(区域.X像素 + 帧.X * 水平缩放, 区域.Y像素 + 帧.Y * 垂直缩放,
                           帧.宽度 * 水平缩放, 帧.高度 * 垂直缩放), 帧.内容标识)
    End Sub

    Private Sub 生成SUP命令(生成器 As SUP字幕帧生成器, 时间 As TimeSpan, 区域 As 视频显示区域)
        If 生成器 Is Nothing Then Return
        SUP绘制项.Clear()
        生成器.生成帧(时间, 区域, SUP绘制项)
        For Each 项 In SUP绘制项
            Dim 事件 = 项.事件
            If 事件 Is Nothing OrElse 事件.像素BGRA Is Nothing OrElse 事件.宽度 <= 0 OrElse
                事件.高度 <= 0 OrElse 事件.行跨度 < 事件.宽度 * 4 Then Continue For
            添加位图命令(事件.像素BGRA, 事件.宽度, 事件.高度,
                事件.行跨度, New RectangleF(项.X像素, 项.Y像素, 项.宽度像素, 项.高度像素),
                CULng(Math.Max(0, 事件.序号)), HDR高亮:=True)
        Next
    End Sub

    Private Sub 添加文字命令(文本 As String, 字体 As String, 字号 As Single,
                          区域 As RectangleF, 前景色ARGB As UInteger,
                          描边色ARGB As UInteger, 描边宽度 As Single,
                          水平对齐 As 定时文字对齐, 垂直对齐 As 定时文字对齐,
                          Optional 样式 As 定时文字样式 = 定时文字样式.无,
                          Optional 内容标识 As ULong = 0,
                          Optional 阴影色ARGB As UInteger = 0,
                          Optional 阴影X偏移 As Single = 0,
                          Optional 阴影Y偏移 As Single = 0)
        Dim command = 命令对象池.取得()
        command.设置文字(文本, 字体, 字号, 区域, 前景色ARGB, 描边色ARGB, 描边宽度,
                     水平对齐, 垂直对齐, 样式, 内容标识,
                     阴影色ARGB, 阴影X偏移, 阴影Y偏移)
        图层命令.Add(command)
    End Sub

    Private Sub 添加位图命令(像素BGRA As Byte(), 位图宽度 As Integer, 位图高度 As Integer,
                           行跨度 As Integer, 区域 As RectangleF, 内容标识 As ULong,
                           Optional HDR高亮 As Boolean = False)
        Dim command = 命令对象池.取得()
        command.设置位图(像素BGRA, 位图宽度, 位图高度, 行跨度, 区域, 内容标识, HDR高亮)
        图层命令.Add(command)
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        SyncLock 生命周期锁
            If 已释放标志 <> 0 Then Return
            Volatile.Write(已释放标志, 1)
        End SyncLock
        刷新计时器.Stop()
        RemoveHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick
        ' PrecisionTimer 在 UI 线程调用 Stop 时异步回收 worker。这里等待已经
        ' 进入的后台 Tick 退出，保证清空图层之后旧命令不可能再次提交。
        刷新空闲.Wait()
        刷新计时器.Dispose()
        RemoveHandler 画面控件.ClientSizeChanged, AddressOf 画面几何已变化
        RemoveHandler 画面控件.DpiChangedAfterParent, AddressOf 画面几何已变化
        Try
            图层序号 += 1UL
            提交图层(New Size(Math.Max(1, 画面控件.ClientSize.Width),
                               Math.Max(1, 画面控件.ClientSize.Height)),
                   Array.Empty(Of 定时文字命令)(), 图层序号,
                   CSng(当前目标帧率))
        Catch
        End Try
        刷新空闲.Dispose()
    End Sub
End Class

