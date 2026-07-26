''' <summary>
''' 把字幕和弹幕等定时内容转换为同一个 GPU 命令图层。
''' 图层刷新率可由弹幕接入方提高，播放器只保留尚未处理的最新一帧命令。
''' </summary>
Friend NotInheritable Class 播放器定时文字图层呈现器
    Implements IDisposable

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 字幕提供器 As Func(Of 外部字幕轨道)
    Private ReadOnly 提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong)
    Private ReadOnly SRT绘制项 As New List(Of SRT字幕绘制项)()
    Private ReadOnly ASS绘制项 As New List(Of ASS字幕绘制项)()
    Private ReadOnly SUP绘制项 As New List(Of SUP字幕绘制项)(1)
    Private ReadOnly 图层命令 As New List(Of 定时文字命令)()
    Private ReadOnly 刷新计时器 As New System.Windows.Forms.Timer()
    Private 当前目标帧率 As Integer = 10
    Private 图层序号 As ULong
    Private 上次图层签名 As ULong
    Private 上次图层签名有效 As Boolean
    Private 已释放 As Boolean

    Friend Sub New(画面控件 As 播放器画面控件, 快照提供器 As Func(Of 播放器快照),
                   字幕提供器 As Func(Of 外部字幕轨道),
                   提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong))
        ArgumentNullException.ThrowIfNull(画面控件)
        ArgumentNullException.ThrowIfNull(快照提供器)
        ArgumentNullException.ThrowIfNull(字幕提供器)
        ArgumentNullException.ThrowIfNull(提交图层)
        Me.画面控件 = 画面控件
        Me.快照提供器 = 快照提供器
        Me.字幕提供器 = 字幕提供器
        Me.提交图层 = 提交图层
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
            更新刷新间隔()
        End Set
    End Property

    Private Sub 刷新计时器_Tick(sender As Object, e As EventArgs)
        If 已释放 Then Return
        Dim 快照 = 快照提供器()
        If 快照 Is Nothing OrElse 快照.视频宽度 = 0 OrElse 快照.视频高度 = 0 OrElse
            画面控件.ClientSize.Width <= 0 OrElse 画面控件.ClientSize.Height <= 0 Then Return
        提交当前帧(画面控件.ClientSize, 快照.视频宽度, 快照.视频高度,
                 快照.播放位置, 字幕提供器())
    End Sub

    Private Sub 更新刷新间隔()
        刷新计时器.Interval = Math.Max(1, CInt(Math.Round(1000.0R / 当前目标帧率)))
    End Sub

    Friend Function 生成命令(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                         播放位置 As TimeSpan, 字幕 As 外部字幕轨道) As IReadOnlyList(Of 定时文字命令)
        图层命令.Clear()
        If 已释放 OrElse 视频宽度 = 0 OrElse 视频高度 = 0 OrElse
            客户区大小.Width <= 0 OrElse 客户区大小.Height <= 0 Then Return 图层命令.ToArray()
        Dim 区域 = 视频显示区域.计算(客户区大小.Width, 客户区大小.Height, 96.0F,
                                      CInt(视频宽度), CInt(视频高度))
        If 字幕 IsNot Nothing Then
            Select Case 字幕.格式
                Case 外部字幕格式.SRT
                    生成SRT命令(字幕.SRT生成器, 播放位置, 区域)
                Case 外部字幕格式.ASS, 外部字幕格式.SSA
                    生成ASS命令(字幕.ASS生成器, 播放位置, 区域)
                Case 外部字幕格式.SUP
                    生成SUP命令(字幕.SUP生成器, 播放位置, 区域)
            End Select
        End If
        RaiseEvent 绘制扩展定时文字(Me,
            New 定时文字图层绘制事件参数(图层命令, 播放位置, 区域))
        Return 图层命令.ToArray()
    End Function

    Friend Sub 提交当前帧(客户区大小 As Size, 视频宽度 As UInteger, 视频高度 As UInteger,
                       播放位置 As TimeSpan, 字幕 As 外部字幕轨道)
        Try
            Dim commands = 生成命令(客户区大小, 视频宽度, 视频高度, 播放位置, 字幕)
            Dim signature = 计算图层签名(客户区大小, commands)
            If 上次图层签名有效 AndAlso signature = 上次图层签名 Then Return
            上次图层签名 = signature
            上次图层签名有效 = True
            图层序号 += 1UL
            提交图层(客户区大小, commands, 图层序号)
        Catch
            ' 定时文字是可选图层；异常不能影响视频呈现。
        End Try
    End Sub

    Friend Sub 使图层失效()
        上次图层签名有效 = False
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
                混合文本签名(hash, item.文本)
                混合文本签名(hash, item.字体)
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
                图层命令.Add(定时文字命令.创建文字(行.文本, 行.字体, 行.字号像素,
                    New RectangleF(区域.X像素, y, 区域.宽度像素, 行高 + 4.0F),
                    行.颜色ARGB, 行.描边颜色ARGB, 行.描边宽度像素,
                    定时文字对齐.居中, 定时文字对齐.靠前))
                y += 行高 + 项.行间距像素
            Next
        Next
    End Sub

    Private Sub 生成ASS命令(生成器 As ASS字幕帧生成器, 时间 As TimeSpan, 区域 As 视频显示区域)
        If 生成器 Is Nothing Then Return
        ASS绘制项.Clear()
        生成器.生成帧(时间, 区域, ASS绘制项)
        For Each 项 In ASS绘制项
            Dim 文本 = String.Concat(项.提示.片段.Select(Function(x) x.文本))
            If String.IsNullOrWhiteSpace(文本) Then Continue For
            Dim 样式 = 项.基础样式
            Dim 左边距 = If(项.提示.左边距 > 0, 项.提示.左边距, 样式.左边距) * 项.脚本到像素水平缩放
            Dim 右边距 = If(项.提示.右边距 > 0, 项.提示.右边距, 样式.右边距) * 项.脚本到像素水平缩放
            Dim 垂直边距 = If(项.提示.垂直边距 > 0, 项.提示.垂直边距, 样式.垂直边距) * 项.脚本到像素垂直缩放
            Dim 文本区域 = New RectangleF(区域.X像素 + 左边距, 区域.Y像素 + 垂直边距,
                Math.Max(1.0F, 区域.宽度像素 - 左边距 - 右边距),
                Math.Max(1.0F, 区域.高度像素 - 垂直边距 * 2.0F))
            Dim flags = 定时文字样式.无
            If 样式.粗体 Then flags = flags Or 定时文字样式.粗体
            If 样式.斜体 Then flags = flags Or 定时文字样式.斜体
            If 样式.下划线 Then flags = flags Or 定时文字样式.下划线
            If 样式.删除线 Then flags = flags Or 定时文字样式.删除线
            Dim 描边宽度 = Math.Max(0.0F, 样式.描边宽度 *
                (项.脚本到像素水平缩放 + 项.脚本到像素垂直缩放) * 0.5F)
            图层命令.Add(定时文字命令.创建文字(文本, 样式.字体,
                Math.Max(1.0F, 样式.字号 * 项.脚本到像素垂直缩放), 文本区域,
                样式.主颜色ARGB, 样式.描边颜色ARGB, 描边宽度,
                获取ASS水平对齐(样式.对齐方式), 获取ASS垂直对齐(样式.对齐方式), flags))
        Next
    End Sub

    Private Sub 生成SUP命令(生成器 As SUP字幕帧生成器, 时间 As TimeSpan, 区域 As 视频显示区域)
        If 生成器 Is Nothing Then Return
        SUP绘制项.Clear()
        生成器.生成帧(时间, 区域, SUP绘制项)
        For Each 项 In SUP绘制项
            Dim 事件 = 项.事件
            If 事件 Is Nothing OrElse 事件.像素BGRA Is Nothing OrElse 事件.宽度 <= 0 OrElse
                事件.高度 <= 0 OrElse 事件.行跨度 < 事件.宽度 * 4 Then Continue For
            图层命令.Add(定时文字命令.创建位图(事件.像素BGRA, 事件.宽度, 事件.高度,
                事件.行跨度, New RectangleF(项.X像素, 项.Y像素, 项.宽度像素, 项.高度像素),
                CULng(Math.Max(0, 事件.序号))))
        Next
    End Sub

    Private Shared Function 获取ASS水平对齐(对齐方式 As Integer) As 定时文字对齐
        Select Case 对齐方式
            Case 1, 4, 7 : Return 定时文字对齐.靠前
            Case 3, 6, 9 : Return 定时文字对齐.靠后
            Case Else : Return 定时文字对齐.居中
        End Select
    End Function

    Private Shared Function 获取ASS垂直对齐(对齐方式 As Integer) As 定时文字对齐
        Select Case 对齐方式
            Case 7, 8, 9 : Return 定时文字对齐.靠前
            Case 4, 5, 6 : Return 定时文字对齐.居中
            Case Else : Return 定时文字对齐.靠后
        End Select
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        刷新计时器.Stop()
        RemoveHandler 刷新计时器.Tick, AddressOf 刷新计时器_Tick
        刷新计时器.Dispose()
        Try
            图层序号 += 1UL
            提交图层(New Size(Math.Max(1, 画面控件.ClientSize.Width),
                              Math.Max(1, 画面控件.ClientSize.Height)),
                   Array.Empty(Of 定时文字命令)(), 图层序号)
        Catch
        End Try
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
