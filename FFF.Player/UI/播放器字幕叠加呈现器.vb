Imports System.Runtime.InteropServices
Imports System.Threading

''' <summary>将 SRT、ASS/SSA、SUP 和内嵌字幕转换为独立 GPU 图层。</summary>
Friend NotInheritable Class 播放器字幕叠加呈现器
    Implements IDisposable

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 字幕提供器 As Func(Of 外部字幕轨道)
    Private ReadOnly 提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single)
    Private ReadOnly SRT绘制项 As New List(Of SRT字幕绘制项)()
    Private ReadOnly SRT行测量 As New List(Of 字幕行测量结果)()
    Private ReadOnly SRT测量缓存 As New Dictionary(Of 字幕行测量键, 字幕行测量结果)()
    Private ReadOnly SUP绘制项 As New List(Of SUP字幕绘制项)(1)
    Private ReadOnly 图层命令 As New List(Of 定时文字命令)()
    Private ReadOnly 命令对象池 As New List(Of 定时文字命令)(128)
    Private ReadOnly 刷新计时器 As LakeUI.PrecisionTimer
    Private Const 当前目标帧率 As Integer = 60
    Private 图层序号 As ULong
    Private 上次图层签名 As ULong
    Private 命令对象使用数 As Integer
    Private 缓存客户区宽度 As Integer
    Private 缓存客户区高度 As Integer
    Private 缓存DPI位元 As Integer
    Private 图层签名有效标志 As Integer
    Private ReadOnly 生命周期锁 As New Object()
    Private ReadOnly 刷新空闲 As New ManualResetEventSlim(True)
    Private 活动刷新数 As Integer
    Private 已释放标志 As Integer

    ' ── 空闲降频（2026-11-01）──────────────────────────────────────────────
    ' 三台图层泵各占一条 PrecisionTimer 调度线程，而调度线程的设计是在每个刻度的
    ' 最后 0.5 ms 用 SpinWait 忙等精磨（PrecisionTimer.vb:509-531）。16 ms 间隔下
    ' 每台约 3% 单核，消融矩阵实测三台合计 51 ms/s 常驻 CPU（地板 66 → 15 ms/s）。
    ' 没有字幕轨道时这份忙等毫无产出。
    '
    ' 判据取「有没有轨道」而不是「当前时刻有没有字幕行」：后者只能靠 tick 自己发现，
    ' 降频会让字幕晚 200 ms 出现。轨道到达/卸载后由 tick 自发现并切换间隔；
    ' 最坏等一个空闲拍（200 ms）恢复 60 Hz。使图层失效 不改间隔（单写者原则）。
    '
    ' 只调频率、不改提交条件：提交当前帧() 内部已有图层签名去重，无字幕时那一拍
    ' 是几次整数运算。不自己记「屏幕上是不是空的」，以免复制它的职责后漏掉状态迁移。
    Private Const 工作间隔毫秒 As Integer = 16
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
                   提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single))
        ArgumentNullException.ThrowIfNull(画面控件)
        ArgumentNullException.ThrowIfNull(快照提供器)
        ArgumentNullException.ThrowIfNull(字幕提供器)
        ArgumentNullException.ThrowIfNull(提交图层)
        Me.画面控件 = 画面控件
        Me.快照提供器 = 快照提供器
        Me.字幕提供器 = 字幕提供器
        Me.提交图层 = 提交图层
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

    Private Sub 刷新计时器_Tick(sender As Object, e As EventArgs)
        SyncLock 生命周期锁
            If 已释放标志 <> 0 Then Return
            活动刷新数 += 1
            刷新空闲.Reset()
        End SyncLock
        Try
            Dim 快照 = 快照提供器()
            Dim 客户区大小 = New Size(Volatile.Read(缓存客户区宽度), Volatile.Read(缓存客户区高度))
            If 快照 Is Nothing OrElse 快照.视频宽度 = 0 OrElse 快照.视频高度 = 0 OrElse
                客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then
                ' 没有画面就谈不上图层：与旧行为一样直接返回，只把调度降下来。
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
            设置间隔(If(字幕 Is Nothing, 空闲间隔毫秒, 工作间隔毫秒))
            Dim DPI = BitConverter.Int32BitsToSingle(Volatile.Read(缓存DPI位元))
            提交当前帧(客户区大小, 快照.视频宽度, 快照.视频高度,
                     快照.播放位置, 字幕, DPI, 快照.时间轴代次)
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

    Friend Function 生成命令(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                         播放位置 As TimeSpan, 字幕 As 外部字幕轨道,
                         Optional DPI As Single = 96.0F,
                         Optional 时间轴代次 As ULong = 0) As IReadOnlyList(Of 定时文字命令)
        图层命令.Clear()
        命令对象使用数 = 0
        If Volatile.Read(已释放标志) <> 0 OrElse 视频宽度 = 0 OrElse 视频高度 = 0 OrElse
            客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then Return 图层命令
        If Not Single.IsFinite(DPI) OrElse DPI <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(DPI))
        ' WinForms 的 ClientSize 已经是物理像素；先换回 DIP，再交给字幕/弹幕共用的
        ' 视频显示区域换算，避免高 DPI 下重复放大。
        Dim DPI缩放 = DPI / 96.0F
        Dim 区域 = 视频显示区域.计算(客户区大小.Width / DPI缩放, 客户区大小.Height / DPI缩放, DPI,
                                      CInt(视频宽度), CInt(视频高度))
        If 字幕 IsNot Nothing AndAlso 字幕.尝试进入使用() Then
            Try
                Select Case 字幕.格式
                    Case 外部字幕格式.SRT
                        生成SRT命令(字幕.SRT生成器, 播放位置, 区域)
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
        Return 图层命令
    End Function

    Friend Sub 提交当前帧(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                       播放位置 As TimeSpan, 字幕 As 外部字幕轨道,
                       Optional DPI As Single = 96.0F,
                       Optional 时间轴代次 As ULong = 0)
        Try
            Dim commands = 生成命令(客户区大小, 视频宽度, 视频高度, 播放位置, 字幕,
                DPI, 时间轴代次)
            Dim signature = 计算图层签名(客户区大小, commands)
            If Volatile.Read(图层签名有效标志) <> 0 AndAlso signature = 上次图层签名 Then Return
            上次图层签名 = signature
            Volatile.Write(图层签名有效标志, 1)
            图层序号 += 1UL
            提交图层(客户区大小, commands, 图层序号, CSng(当前目标帧率))
        Catch
            ' 定时文字是可选图层；异常不能影响视频呈现。
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
            混合签名(hash, CULng(AscW(character)))
        Next
        混合签名(hash, &HFFFFUL)
    End Sub

    Private Shared Sub 混合签名(ByRef hash As ULong, value As ULong)
        hash = Numerics.BitOperations.RotateLeft(hash, 7) Xor value
    End Sub

    Private Sub 生成SRT命令(生成器 As SRT字幕帧生成器, 时间 As TimeSpan, 区域 As 视频显示区域)
        If 生成器 Is Nothing Then Return
        SRT绘制项.Clear()
        生成器.生成帧(时间, 区域, SRT绘制项, 缓存客户区宽度, 缓存客户区高度)
        Dim 顶部字幕Y = 区域.Y像素
        For Each 项 In SRT绘制项
            SRT行测量.Clear()
            For Each 行 In 项.行
                SRT行测量.Add(测量字幕行(行, 区域.宽度像素))
            Next
            If SRT行测量.Count = 0 Then Continue For
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

    Private Sub 添加位图命令(像素BGRA As Byte(), 位图宽度 As Integer, 位图高度 As Integer,
                           行跨度 As Integer, 区域 As RectangleF, 内容标识 As ULong,
                           Optional HDR高亮 As Boolean = False)
        Dim command = 取得复用命令()
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
