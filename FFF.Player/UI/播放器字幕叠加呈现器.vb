Imports System.Threading

Friend Enum 定时文字图层内容
    合并
    仅字幕
    仅弹幕
End Enum

''' <summary>
''' 把一种定时内容转换为独立 GPU 命令图层。产品路径分别创建字幕和弹幕实例；
''' 合并模式仅保留给生成器单元测试和兼容调用。
''' 图层命令和文字对象由呈现器复用；调用方只能在同步提交期间读取命令，不能跨帧保留引用。
''' </summary>
Friend NotInheritable Class 播放器定时文字图层呈现器
    Implements IDisposable

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 字幕提供器 As Func(Of 外部字幕轨道)
    Private ReadOnly 提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single)
    Private ReadOnly 弹幕提供器 As Func(Of 弹幕资料库)
    Private ReadOnly 弹幕配置 As 弹幕显示配置
    Private ReadOnly 图层内容 As 定时文字图层内容
    Private ReadOnly SRT绘制项 As New List(Of SRT字幕绘制项)()
    Private ReadOnly SUP绘制项 As New List(Of SUP字幕绘制项)(1)
    Private ReadOnly 弹幕绘制项 As New List(Of 弹幕绘制项)(100)
    Private ReadOnly 图层命令 As New List(Of 定时文字命令)()
    Private ReadOnly 命令对象池 As New List(Of 定时文字命令)(128)
    Private ReadOnly 刷新计时器 As LakeUI.PrecisionTimer
    Private 当前目标帧率 As Integer = 60
    Private 当前弹幕资料库 As 弹幕资料库
    Private 当前弹幕调度器 As 弹幕调度器
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

    Friend Sub New(画面控件 As 播放器画面控件, 快照提供器 As Func(Of 播放器快照),
                   字幕提供器 As Func(Of 外部字幕轨道),
                   提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single),
                   Optional 弹幕提供器 As Func(Of 弹幕资料库) = Nothing,
                   Optional 弹幕配置 As 弹幕显示配置 = Nothing,
                   Optional 图层内容 As 定时文字图层内容 = 定时文字图层内容.合并)
        ArgumentNullException.ThrowIfNull(画面控件)
        ArgumentNullException.ThrowIfNull(快照提供器)
        ArgumentNullException.ThrowIfNull(字幕提供器)
        ArgumentNullException.ThrowIfNull(提交图层)
        Me.画面控件 = 画面控件
        Me.快照提供器 = 快照提供器
        Me.字幕提供器 = 字幕提供器
        Me.提交图层 = 提交图层
        Me.弹幕提供器 = 弹幕提供器
        Me.弹幕配置 = If(弹幕配置, New 弹幕显示配置())
        Me.图层内容 = 图层内容
        Me.弹幕配置.验证()
        If 图层内容 = 定时文字图层内容.仅弹幕 Then
            当前目标帧率 = CInt(Math.Round(Me.弹幕配置.目标帧率, MidpointRounding.AwayFromZero))
        End If
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

    Friend Event 绘制扩展定时文字 As EventHandler(Of 定时文字图层绘制事件参数)

    Friend Property 目标帧率 As Integer
        Get
            Return 当前目标帧率
        End Get
        Set(value As Integer)
            If value < 1 OrElse value > 240 Then Throw New ArgumentOutOfRangeException(NameOf(value))
            If 当前目标帧率 = value Then Return
            当前目标帧率 = value
            If 图层内容 <> 定时文字图层内容.仅字幕 Then
                ' 单一公开属性同时定义唤醒频率和媒体时间量化频率；否则未来
                ' 120 Hz 选项会每两个 Tick 生成一次相同的 60 Hz 位置。
                弹幕配置.目标帧率 = value
            End If
            更新刷新间隔()
        End Set
    End Property

    Private Sub 刷新计时器_Tick(sender As Object, e As EventArgs)
        SyncLock 生命周期锁
            If 已释放标志 <> 0 Then Return
            活动刷新数 += 1
            刷新空闲.Reset()
        End SyncLock
        Try
            Dim 快照 = 快照提供器()
            Dim 客户区大小 = New Size(Volatile.Read(缓存客户区宽度), Volatile.Read(缓存客户区高度))
            Dim DPI = BitConverter.Int32BitsToSingle(Volatile.Read(缓存DPI位元))
            If 快照 Is Nothing OrElse 快照.视频宽度 = 0 OrElse 快照.视频高度 = 0 OrElse
                客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then Return
            Dim 字幕 = If(图层内容 = 定时文字图层内容.仅弹幕, Nothing, 字幕提供器())
            提交当前帧(客户区大小, 快照.视频宽度, 快照.视频高度,
                     快照.播放位置, 字幕, DPI)
        Finally
            SyncLock 生命周期锁
                活动刷新数 -= 1
                If 活动刷新数 = 0 Then 刷新空闲.Set()
            End SyncLock
        End Try
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
        ' 弹幕帧率属于此实例，不依赖视频或字幕刷新率。整数毫秒只负责唤醒；
        ' 位置始终由连续媒体时钟按目标帧率量化，因此 90/120/144 Hz 可独立开放。
        刷新计时器.Interval = Math.Max(1, CInt(Math.Round(
            1000.0R / 当前目标帧率, MidpointRounding.AwayFromZero)))
    End Sub

    Friend Function 生成命令(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                         播放位置 As TimeSpan, 字幕 As 外部字幕轨道,
                         Optional DPI As Single = 96.0F) As IReadOnlyList(Of 定时文字命令)
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
        If 图层内容 <> 定时文字图层内容.仅弹幕 AndAlso 字幕 IsNot Nothing AndAlso 字幕.尝试进入使用() Then
            Try
                Select Case 字幕.格式
                    Case 外部字幕格式.SRT
                        生成SRT命令(字幕.SRT生成器, 播放位置, 区域)
                    Case 外部字幕格式.ASS, 外部字幕格式.SSA
                        生成ASS特效命令(字幕.ASS特效生成器, 播放位置, 区域)
                    Case 外部字幕格式.SUP
                        生成SUP命令(字幕.SUP生成器, 播放位置, 区域)
                End Select
            Finally
                字幕.离开使用()
            End Try
        End If
        If 图层内容 <> 定时文字图层内容.仅字幕 Then
            生成弹幕命令(弹幕提供器?.Invoke(), 播放位置, 区域)
        End If
        If 图层内容 <> 定时文字图层内容.仅弹幕 Then
            RaiseEvent 绘制扩展定时文字(Me,
                New 定时文字图层绘制事件参数(图层命令, 播放位置, 区域))
        End If
        Return 图层命令
    End Function

    Friend Sub 提交当前帧(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                       播放位置 As TimeSpan, 字幕 As 外部字幕轨道,
                       Optional DPI As Single = 96.0F)
        Try
            Dim commands = 生成命令(客户区大小, 视频宽度, 视频高度, 播放位置, 字幕, DPI)
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
        生成器.生成帧(时间, 区域, SRT绘制项)
        For Each 项 In SRT绘制项
            Dim 总高度 = 项.行.Sum(Function(行) 行.字号像素 * 1.24F)
            If 项.行.Count > 1 Then 总高度 += 项.行间距像素 * (项.行.Count - 1)
            Dim y = 项.Y底部像素 - 总高度
            For Each 行 In 项.行
                Dim 行高 = 行.字号像素 * 1.24F
                添加文字命令(行.文本, 行.字体, 行.字号像素,
                    New RectangleF(区域.X像素, y, 区域.宽度像素, 行高 + 4.0F),
                    行.颜色ARGB, 行.描边颜色ARGB, 行.描边宽度像素,
                    定时文字对齐.居中, 定时文字对齐.靠前)
                y += 行高 + 项.行间距像素
            Next
        Next
    End Sub

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
                CULng(Math.Max(0, 事件.序号)))
        Next
    End Sub

    Private Sub 生成弹幕命令(资料库 As 弹幕资料库, 时间 As TimeSpan, 区域 As 视频显示区域)
        If 资料库 Is Nothing Then
            当前弹幕资料库 = Nothing
            当前弹幕调度器 = Nothing
            Return
        End If
        If Not ReferenceEquals(当前弹幕资料库, 资料库) Then
            当前弹幕资料库 = 资料库
            当前弹幕调度器 = New 弹幕调度器(资料库, 弹幕配置)
        End If
        弹幕绘制项.Clear()
        当前弹幕调度器.生成帧(时间, 区域, 弹幕绘制项)
        For Each 项 In 弹幕绘制项
            If String.IsNullOrWhiteSpace(项.项目.文本) Then Continue For
            添加文字命令(项.项目.文本, 项.字体, 项.字号像素,
                New RectangleF(项.X像素, 项.Y像素, Math.Max(1.0F, 项.宽度像素), Math.Max(1.0F, 项.高度像素)),
                项.颜色ARGB, &H80000000UI, Math.Max(0.5F, 项.字号像素 / 32.0F),
                定时文字对齐.靠前, 定时文字对齐.靠前)
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
                          Optional 内容标识 As ULong = 0)
        Dim command = 取得复用命令()
        command.设置文字(文本, 字体, 字号, 区域, 前景色ARGB, 描边色ARGB, 描边宽度,
                     水平对齐, 垂直对齐, 样式, 内容标识)
        图层命令.Add(command)
    End Sub

    Private Sub 添加位图命令(像素BGRA As Byte(), 位图宽度 As Integer, 位图高度 As Integer,
                          行跨度 As Integer, 区域 As RectangleF, 内容标识 As ULong)
        Dim command = 取得复用命令()
        command.设置位图(像素BGRA, 位图宽度, 位图高度, 行跨度, 区域, 内容标识)
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
                   Array.Empty(Of 定时文字命令)(), 图层序号, CSng(当前目标帧率))
        Catch
        End Try
        刷新空闲.Dispose()
    End Sub
End Class

Friend NotInheritable Class 定时文字图层绘制事件参数
    Inherits EventArgs

    Friend Sub New(命令 As ICollection(Of 定时文字命令), 播放位置 As TimeSpan, 视频区域 As 视频显示区域)
        Me.命令 = 命令
        Me.播放位置 = 播放位置
        Me.视频区域 = 视频区域
    End Sub

    Friend ReadOnly Property 命令 As ICollection(Of 定时文字命令)
    Friend ReadOnly Property 播放位置 As TimeSpan
    Friend ReadOnly Property 视频区域 As 视频显示区域
End Class
