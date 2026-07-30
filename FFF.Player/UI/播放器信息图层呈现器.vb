Imports System.Diagnostics
Imports System.IO

''' <summary>
''' 在独立的最顶层 GPU 槽位中呈现播放器信息。调试信息固定在左上角，
''' 短时操作反馈固定在左下角；两者不参与字幕或弹幕的生命周期。
''' </summary>
Friend NotInheritable Class 播放器信息图层呈现器
    Implements IDisposable

    Private NotInheritable Class 操作消息
        Friend Sub New(文本值 As String, 颜色值 As UInteger, 到期时钟值 As Long, 操作键值 As String)
            文本 = 文本值 : 颜色 = 颜色值 : 到期时钟 = 到期时钟值 : 操作键 = 操作键值
        End Sub
        Friend ReadOnly 文本 As String
        Friend ReadOnly 颜色 As UInteger
        Friend ReadOnly 到期时钟 As Long
        Friend ReadOnly 操作键 As String
    End Class

    Private NotInheritable Class 文本段
        Friend Sub New(文本值 As String, 颜色值 As UInteger)
            文本 = If(文本值, String.Empty) : 颜色 = 颜色值
        End Sub
        Friend Property 文本 As String
        Friend ReadOnly 颜色 As UInteger
    End Class

    Private NotInheritable Class 信息行
        Friend Sub New(空隙前值 As Single, ParamArray 文本段值 As 文本段())
            空隙前 = 空隙前值 : 文本段 = 文本段值
        End Sub
        Friend ReadOnly 空隙前 As Single
        Friend ReadOnly 文本段 As 文本段()
    End Class

    Private Shared ReadOnly 全屏遮罩像素 As Byte() = {0, 0, 0, 160}
    Private Shared ReadOnly 消息背景像素 As Byte() = {0, 0, 0, 160}
    Private Const 标签颜色 As UInteger = &HFFE8E8E8UI
    Private Const 次要颜色 As UInteger = &HFFB8B8B8UI
    Private Const 青色 As UInteger = &HFF55E7EAUI
    Private Const 黄色 As UInteger = &HFFF0D35DUI
    Private Const 绿色 As UInteger = &HFF69DF8BUI
    Private Const 品红 As UInteger = &HFFFF62B0UI
    Private Const 蓝色 As UInteger = &HFF75A7FFUI
    Private Const 紫色 As UInteger = &HFFC58CFFUI
    Private Const 橙色 As UInteger = &HFFFFA85AUI

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 获取快照 As Func(Of 播放器快照)
    Private ReadOnly 获取媒体信息 As Func(Of 媒体信息)
    Private ReadOnly 获取媒体路径 As Func(Of String)
    Private ReadOnly 获取字幕 As Func(Of 外部字幕轨道)
    Private ReadOnly 获取弹幕 As Func(Of 弹幕资料库)
    Private ReadOnly 获取字幕状态 As Func(Of 定时文字状态)
    Private ReadOnly 获取弹幕状态 As Func(Of 定时文字状态)
    Private ReadOnly 获取WASAPI模式 As Func(Of WASAPI共享模式)
    Private ReadOnly 提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single)
    Private ReadOnly 刷新定时器 As LakeUI.PrecisionTimer
    Private ReadOnly 普通字体 As New Font("Microsoft YaHei UI", 11.0F, FontStyle.Regular, GraphicsUnit.Point)
    Private ReadOnly 操作消息列表 As New List(Of 操作消息)()
    Private ReadOnly 图层命令 As New List(Of 定时文字命令)(32)

    Private 调试可见 As Boolean
    Private 图层序号 As ULong
    Private 上次图层签名 As ULong
    Private 图层签名有效 As Boolean
    Private 上次视频帧次数 As ULong
    Private 上次总丢帧数 As ULong
    Private 上次帧率采样时钟 As Long
    Private 最近实际帧率 As Double
    Private 最近实时丢帧数 As ULong
    Private 已释放 As Boolean

    Friend Sub New(画面 As 播放器画面控件,
                   快照提供器 As Func(Of 播放器快照),
                   媒体信息提供器 As Func(Of 媒体信息),
                   媒体路径提供器 As Func(Of String),
                   字幕提供器 As Func(Of 外部字幕轨道),
                   弹幕提供器 As Func(Of 弹幕资料库),
                   字幕状态提供器 As Func(Of 定时文字状态),
                   弹幕状态提供器 As Func(Of 定时文字状态),
                   WASAPI模式提供器 As Func(Of WASAPI共享模式),
                   图层提交器 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single))
        ArgumentNullException.ThrowIfNull(画面)
        ArgumentNullException.ThrowIfNull(快照提供器)
        ArgumentNullException.ThrowIfNull(媒体信息提供器)
        ArgumentNullException.ThrowIfNull(媒体路径提供器)
        ArgumentNullException.ThrowIfNull(图层提交器)
        画面控件 = 画面
        获取快照 = 快照提供器 : 获取媒体信息 = 媒体信息提供器 : 获取媒体路径 = 媒体路径提供器
        获取字幕 = 字幕提供器 : 获取弹幕 = 弹幕提供器
        获取字幕状态 = 字幕状态提供器 : 获取弹幕状态 = 弹幕状态提供器
        获取WASAPI模式 = WASAPI模式提供器
        提交图层 = 图层提交器
        刷新定时器 = New LakeUI.PrecisionTimer With {.Interval = 200}
        AddHandler 刷新定时器.Tick, AddressOf 刷新定时器_Tick
        AddHandler 画面控件.ClientSizeChanged, AddressOf 画面大小已变化
    End Sub

    Friend Function 切换调试信息() As Boolean
        If 已释放 Then Return False
        调试可见 = Not 调试可见
        If 调试可见 OrElse 操作消息列表.Count > 0 Then 刷新定时器.Start()
        提交当前内容()
        If Not 调试可见 AndAlso 操作消息列表.Count = 0 Then 刷新定时器.Stop()
        Return 调试可见
    End Function

    Friend Sub 显示操作信息(文本 As String, Optional 颜色 As UInteger = 黄色,
                          Optional 操作键 As String = Nothing)
        If 已释放 OrElse String.IsNullOrWhiteSpace(文本) Then Return
        文本 = 文本.Trim()
        操作键 = If(String.IsNullOrWhiteSpace(操作键), Nothing, 操作键.Trim())
        Dim 到期 = Stopwatch.GetTimestamp() + CLng(3.6R * Stopwatch.Frequency)
        Dim 旧索引 = 操作消息列表.FindIndex(
            Function(x) If(操作键 IsNot Nothing,
                String.Equals(x.操作键, 操作键, StringComparison.Ordinal),
                x.操作键 Is Nothing AndAlso x.颜色 = 颜色 AndAlso
                    String.Equals(x.文本, 文本, StringComparison.Ordinal)))
        If 旧索引 >= 0 Then 操作消息列表.RemoveAt(旧索引)
        操作消息列表.Add(New 操作消息(文本, 颜色, 到期, 操作键))
        While 操作消息列表.Count > 3
            操作消息列表.RemoveAt(0)
        End While
        刷新定时器.Start()
        提交当前内容()
    End Sub

    Friend Sub 使内容失效()
        If 已释放 Then Return
        图层签名有效 = False
        If 调试可见 OrElse 操作消息列表.Count > 0 Then 提交当前内容()
    End Sub

    Private Sub 刷新定时器_Tick(sender As Object, e As EventArgs)
        If 已释放 Then Return
        Dim 当前时钟 = Stopwatch.GetTimestamp()
        操作消息列表.RemoveAll(Function(x) x.到期时钟 <= 当前时钟)
        提交当前内容()
        If Not 调试可见 AndAlso 操作消息列表.Count = 0 Then 刷新定时器.Stop()
    End Sub

    Private Sub 画面大小已变化(sender As Object, e As EventArgs)
        使内容失效()
    End Sub

    Private Sub 提交当前内容()
        Dim 画布 = 画面控件.ClientSize
        If 画布.Width <= 0 OrElse 画布.Height <= 0 Then Return
        图层命令.Clear()
        Using 图形 = 画面控件.CreateGraphics()
            Dim 快照 = 安全获取(获取快照)
            If 调试可见 Then
                更新实际帧率(快照)
                构建调试信息(图形, 画布, 快照)
            End If
            构建操作消息(图形, 画布)
        End Using

        Dim 签名 = 计算图层签名(画布, 图层命令)
        If 图层签名有效 AndAlso 签名 = 上次图层签名 Then Return
        上次图层签名 = 签名 : 图层签名有效 = True : 图层序号 += 1UL
        提交图层(画布, 图层命令, 图层序号, 10.0F)
    End Sub

    Private Sub 构建调试信息(图形 As Graphics, 画布 As Size, 快照 As 播放器快照)
        图层命令.Add(定时文字命令.创建位图(全屏遮罩像素, 1, 1, 4,
            New RectangleF(0, 0, 画布.Width, 画布.Height), 1UL))
        Dim DPI缩放 = Math.Max(0.5F, 画面控件.DeviceDpi / 96.0F)
        Dim 边距 = 16.0F * DPI缩放
        Dim 行高 = Math.Max(18.0F * DPI缩放, 普通字体.GetHeight(图形) + 3.0F * DPI缩放)
        Dim 信息宽度 = Math.Max(1.0F, 画布.Width - 边距 * 2.0F)
        Dim 信息 = 安全获取(获取媒体信息)
        Dim 媒体路径 = 安全获取(获取媒体路径)
        Dim 行 = 创建调试行(信息, 快照, 媒体路径)
        Dim y = 边距
        For Each 当前行 In 行
            y += 当前行.空隙前 * DPI缩放
            If y + 行高 > 画布.Height - 边距 Then Exit For
            添加左对齐行(图形, 当前行.文本段, y, 行高, 信息宽度, 边距,
                      画面控件.DeviceDpi)
            y += 行高
        Next
    End Sub

    Private Function 创建调试行(信息 As 媒体信息, 快照 As 播放器快照, 媒体路径 As String) As List(Of 信息行)
        Dim 结果 As New List(Of 信息行)()
        If 信息 Is Nothing OrElse 快照 Is Nothing Then
            结果.Add(New 信息行(0, New 文本段("尚未打开媒体", 次要颜色)))
            Return 结果
        End If

        Dim 视频 = 查找流(信息, 快照, "video")
        Dim 音频 = 查找流(信息, 快照, "audio")
        Dim 字幕 = 安全获取(获取字幕), 弹幕 = 安全获取(获取弹幕)
        Dim 字幕状态 = 安全获取(获取字幕状态), 弹幕状态 = 安全获取(获取弹幕状态)
        Dim 文件名 = 安全文件名(媒体路径)
        Dim 百分比 = If(快照.总时长 > TimeSpan.Zero,
                    Math.Clamp(快照.播放位置.TotalMilliseconds / 快照.总时长.TotalMilliseconds * 100.0R, 0, 100), 0)
        添加配对行如果有值(结果, "文件名：", 文件名, 黄色)
        Dim 时间戳 = 格式化时间(快照.播放位置)
        If 快照.总时长 > TimeSpan.Zero Then
            时间戳 &= $" / {格式化时间(快照.总时长)} ({百分比:0}%)"
        End If
        结果.Add(配对行("时间戳：", 时间戳, 青色))

        If 视频 IsNot Nothing Then
            Dim 解码器 = If(快照.解码器 = 解码模式.GPU, "GPU",
                         If(快照.解码器 = 解码模式.CPU, "CPU", String.Empty))
            Dim 编码 = If(String.IsNullOrWhiteSpace(视频.编码), String.Empty,
                        视频.编码.ToUpperInvariant())
            Dim 视频概要 = If(String.IsNullOrEmpty(编码), 解码器,
                           If(String.IsNullOrEmpty(解码器), 编码, $"{编码} - {解码器}"))
            添加配对行如果有值(结果, "视频：", 视频概要, 品红, 8)
            添加配对行如果有值(结果, "输入：", 视频输入(视频, 快照.视频实时比特率), 紫色)
            添加配对行如果有值(结果, "色彩：", 视频色彩(视频), 蓝色)
            添加配对行如果有值(结果, "输出：", 视频输出(快照, 画面控件.ClientSize), 绿色)
            添加配对行如果有值(结果, "渲染：", 视频渲染(快照), 黄色)
        End If

        If 音频 IsNot Nothing Then
            Dim WASAPI = 安全获取(获取WASAPI模式)
            Dim 编码 = If(String.IsNullOrWhiteSpace(音频.编码), String.Empty,
                        音频.编码.ToUpperInvariant())
            Dim 音频概要 = If(String.IsNullOrEmpty(编码), $"WASAPI {WASAPI}",
                           $"{编码} - WASAPI {WASAPI}")
            添加配对行如果有值(结果, "音频：", 音频概要, 品红, 8)
            添加配对行如果有值(结果, "输入：", 音频输入(音频, 快照.音频实时比特率), 紫色)
            添加配对行如果有值(结果, "输出：", 音频输出(音频, 快照), 绿色)
        End If

        Dim 字幕文本 = If(字幕 Is Nothing, "未加载", 合并字段(
            字幕.格式.ToString().ToUpperInvariant(),
            $"总数量 {字幕条目数(字幕)}",
            If(字幕状态 Is Nothing, String.Empty, $"当前正在渲染 {字幕状态.命令数:N0}")))
        Dim 弹幕文本 = If(弹幕 Is Nothing, "未加载", 合并字段(
            "哔哩哔哩 XML", $"总数量 {弹幕.数量:N0}",
            If(弹幕状态 Is Nothing, String.Empty, $"当前正在渲染 {弹幕状态.命令数:N0}")))
        结果.Add(配对行("字幕：", 字幕文本, 青色, 8))
        结果.Add(配对行("弹幕：", 弹幕文本, 橙色))
        Return 结果
    End Function

    Friend Function 读取调试文本行(信息 As 媒体信息, 快照 As 播放器快照,
                                媒体路径 As String) As IReadOnlyList(Of String)
        Return 创建调试行(信息, 快照, 媒体路径).
            Select(Function(行) String.Concat(行.文本段.Select(Function(段) 段.文本))).ToArray()
    End Function

    Private Sub 构建操作消息(图形 As Graphics, 画布 As Size)
        If 操作消息列表.Count = 0 Then Return
        Dim DPI缩放 = Math.Max(0.5F, 画面控件.DeviceDpi / 96.0F)
        Dim 边距 = 16.0F * DPI缩放
        Dim 水平内边距 = 12.0F * DPI缩放, 垂直内边距 = 7.0F * DPI缩放
        Dim 行高 = Math.Max(18.0F * DPI缩放, 普通字体.GetHeight(图形) + 2.0F * DPI缩放)
        Dim 背景宽度 = Math.Max(1.0F, 画布.Width - 边距 * 2.0F)
        Dim 最大文本宽度 = Math.Max(1.0F, 背景宽度 - 水平内边距 * 2.0F)
        Dim y = 画布.Height - 边距
        For index = 操作消息列表.Count - 1 To 0 Step -1
            Dim 消息 = 操作消息列表(index)
            Dim 文本 = 拟合文本(图形, 消息.文本, 普通字体, 最大文本宽度)
            Dim 文本宽度 = 测量文本(图形, 文本, 普通字体)
            Dim 背景高度 = 行高 + 垂直内边距 * 2.0F
            y -= 背景高度
            图层命令.Add(定时文字命令.创建位图(消息背景像素, 1, 1, 4,
                New RectangleF(边距, y, 背景宽度, 背景高度), 2UL))
            图层命令.Add(创建文字命令(文本, 普通字体, 消息.颜色,
                New RectangleF(边距 + 水平内边距, y + 垂直内边距,
                               Math.Max(1.0F, 文本宽度 + 2.0F), 行高), 画面控件.DeviceDpi))
            y -= 6.0F * DPI缩放
        Next
    End Sub

    Private Sub 添加左对齐行(图形 As Graphics, 段 As 文本段(), y As Single, 行高 As Single,
                         最大宽度 As Single, 左边界 As Single, DPI As Integer)
        Dim 宽度(段.Length - 1) As Single
        Dim 总宽度 As Single
        For index = 0 To 段.Length - 1
            宽度(index) = 测量文本(图形, 段(index).文本, 普通字体)
            总宽度 += 宽度(index)
        Next
        If 总宽度 > 最大宽度 AndAlso 段.Length > 0 Then
            Dim 最后 = 段.Length - 1
            Dim 其他宽度 = 总宽度 - 宽度(最后)
            段(最后).文本 = 拟合文本(图形, 段(最后).文本, 普通字体, Math.Max(20.0F, 最大宽度 - 其他宽度))
            宽度(最后) = 测量文本(图形, 段(最后).文本, 普通字体)
        End If
        Dim x = 左边界
        For index = 0 To 段.Length - 1
            If String.IsNullOrEmpty(段(index).文本) Then Continue For
            图层命令.Add(创建文字命令(段(index).文本, 普通字体, 段(index).颜色,
                New RectangleF(x, y, Math.Max(1.0F, 宽度(index) + 2.0F), 行高), DPI))
            x += 宽度(index)
        Next
    End Sub

    Private Shared Function 创建文字命令(文本 As String, 字体 As Font, 颜色 As UInteger,
                                      区域 As RectangleF, DPI As Integer) As 定时文字命令
        Dim 字号像素 = CSng(字体.SizeInPoints * Math.Max(1, DPI) / 72.0F)
        Return 定时文字命令.创建文字(文本, 字体.FontFamily.Name, 字号像素, 区域, 颜色,
            &HFF000000UI, 0, 定时文字对齐.靠前, 定时文字对齐.靠前, 定时文字样式.无)
    End Function

    Private Shared Function 配对行(标签 As String, 值 As String, 值颜色 As UInteger,
                               Optional 空隙前 As Single = 0) As 信息行
        Return New 信息行(空隙前, New 文本段(标签, 标签颜色), New 文本段(值, 值颜色))
    End Function

    Private Shared Sub 添加配对行如果有值(结果 As List(Of 信息行), 标签 As String,
                                      值 As String, 值颜色 As UInteger,
                                      Optional 空隙前 As Single = 0)
        If String.IsNullOrWhiteSpace(值) Then Return
        结果.Add(配对行(标签, 值, 值颜色, 空隙前))
    End Sub

    Private Shared Function 合并字段(ParamArray 字段 As String()) As String
        Return String.Join("   ", 字段.Where(Function(x) Not String.IsNullOrWhiteSpace(x)))
    End Function

    Private Shared Function 查找流(信息 As 媒体信息, 快照 As 播放器快照, 类型 As String) As 媒体流信息
        If 信息 Is Nothing Then Return Nothing
        Dim 当前索引 = If(类型 = "video", 快照.当前视频流, 快照.当前音频流)
        Return 信息.流.FirstOrDefault(Function(x) x.类型.Equals(类型, StringComparison.OrdinalIgnoreCase) AndAlso
            (类型 <> "video" OrElse Not x.是封面图) AndAlso x.索引 = 当前索引)
    End Function

    Private Sub 更新实际帧率(快照 As 播放器快照)
        Dim 当前时钟 = Stopwatch.GetTimestamp()
        If 快照 Is Nothing OrElse 快照.状态 <> 播放状态.正在播放 Then
            上次视频帧次数 = If(快照 Is Nothing, 0UL, 快照.已呈现视频帧数)
            上次总丢帧数 = If(快照 Is Nothing, 0UL, 计算总丢帧数(快照))
            上次帧率采样时钟 = 当前时钟 : 最近实际帧率 = 0 : 最近实时丢帧数 = 0
            Return
        End If
        If 上次帧率采样时钟 = 0 Then
            上次视频帧次数 = 快照.已呈现视频帧数
            上次总丢帧数 = 计算总丢帧数(快照)
            上次帧率采样时钟 = 当前时钟
            Return
        End If
        Dim 已过秒数 = CDbl(当前时钟 - 上次帧率采样时钟) / Stopwatch.Frequency
        If 已过秒数 < 0.85R Then Return
        最近实际帧率 = If(快照.已呈现视频帧数 >= 上次视频帧次数,
            (快照.已呈现视频帧数 - 上次视频帧次数) / 已过秒数, 0)
        Dim 总丢帧 = 计算总丢帧数(快照)
        最近实时丢帧数 = If(总丢帧 >= 上次总丢帧数, 总丢帧 - 上次总丢帧数, 0UL)
        上次视频帧次数 = 快照.已呈现视频帧数
        上次总丢帧数 = 总丢帧
        上次帧率采样时钟 = 当前时钟
    End Sub

    Private Shared Function 视频输入(流 As 媒体流信息, 实时比特率 As ULong) As String
        Return 合并字段(
            If(String.IsNullOrWhiteSpace(流.像素格式), String.Empty,
               $"格式 {流.像素格式.ToUpperInvariant()}"),
            If(流.宽度 > 0 AndAlso 流.高度 > 0, $"分辨率 {流.宽度}x{流.高度}", String.Empty),
            If(流.平均帧率 > 0, $"帧率 {流.平均帧率:0.###}fps", String.Empty),
            If(实时比特率 > 0, $"实时码率 {格式化比特率(实时比特率)}", String.Empty))
    End Function

    Private Shared Function 视频色彩(流 As 媒体流信息) As String
        Dim 采样 = 格式化色度抽样(流.色度抽样)
        Dim 矩阵 = 色彩空间(流.色彩空间)
        Dim 色域 = 色彩原色(流.色彩原色)
        Dim 传输 = 色彩传递(流.色彩传递)
        Dim 范围 = 色彩范围(流.色彩范围)
        Return 合并字段(
            If(String.IsNullOrEmpty(采样), String.Empty, $"采样 {采样}"),
            If(String.IsNullOrEmpty(矩阵), String.Empty, $"颜色矩阵 {矩阵}"),
            If(String.IsNullOrEmpty(色域), String.Empty, $"色域 {色域}"),
            If(String.IsNullOrEmpty(传输), String.Empty, $"传输特性 {传输}"),
            If(String.IsNullOrEmpty(范围), String.Empty, $"范围 {范围}"))
    End Function

    Private Shared Function 视频输出(快照 As 播放器快照, 输出大小 As Size) As String
        Dim 格式 = 视频输出格式(快照.视频输出位深度)
        Return 合并字段(
            If(String.IsNullOrEmpty(格式), String.Empty, $"格式 {格式}"),
            If(输出大小.Width > 0 AndAlso 输出大小.Height > 0,
               $"分辨率 {输出大小.Width}x{输出大小.Height}", String.Empty),
            $"色彩模式 {色彩模式文本(快照.实际色彩模式, 快照.是HDR源)}")
    End Function

    Private Function 视频渲染(快照 As 播放器快照) As String
        Return 合并字段(
            If(最近实际帧率 > 0, $"帧率 {最近实际帧率:0.00}fps", String.Empty),
            $"缓冲池 {快照.视频队列帧数}帧",
            $"实时丢帧 {最近实时丢帧数:N0}",
            $"总丢帧 {计算总丢帧数(快照):N0}")
    End Function

    Private Shared Function 计算总丢帧数(快照 As 播放器快照) As ULong
        If ULong.MaxValue - 快照.已丢弃视频帧数 < 快照.已合并视频帧数 Then Return ULong.MaxValue
        Return 快照.已丢弃视频帧数 + 快照.已合并视频帧数
    End Function

    Private Shared Function 音频输入(流 As 媒体流信息, 实时比特率 As ULong) As String
        Dim 位深 = If(流.原始采样位数 > 0, 流.原始采样位数, 流.编码采样位数)
        Return 合并字段(
            If(流.采样率 > 0, $"采样 {流.采样率}Hz", String.Empty),
            If(位深 > 0, $"位深 {位深}bit", String.Empty),
            If(流.声道数 > 0, $"声道数 {流.声道数}", String.Empty),
            If(实时比特率 > 0, $"实时码率 {格式化比特率(实时比特率)}", String.Empty))
    End Function

    Private Shared Function 音频输出(流 As 媒体流信息, 快照 As 播放器快照) As String
        Dim 采样率 = If(流.输出采样率 > 0, 流.输出采样率, 流.采样率)
        Dim 声道 = If(流.输出声道数 > 0, 流.输出声道数, 流.声道数)
        Dim 位深 = If(流.输出有效采样位数 > 0, 流.输出有效采样位数, 流.输出采样位数)
        Dim 格式 = If(流.输出浮点, "FLOAT PCM", "PCM")
        Dim 有输出格式 = 流.输出浮点 OrElse 流.输出采样位数 > 0 OrElse
                       流.输出有效采样位数 > 0 OrElse 流.输出采样率 > 0 OrElse
                       流.输出声道数 > 0 OrElse 采样率 > 0 OrElse 声道 > 0
        Return 合并字段(
            If(有输出格式, $"{格式}", String.Empty),
            If(采样率 > 0, $"采样 {采样率}Hz", String.Empty),
            If(位深 > 0, $"位深 {位深}bit", String.Empty),
            If(声道 > 0, $"声道数 {声道}", String.Empty),
            $"实时延迟 {快照.音频缓冲时长.TotalMilliseconds:0}ms")
    End Function

    Private Shared Function 字幕条目数(字幕 As 外部字幕轨道) As String
        Return If(字幕.条目数 >= 0, 字幕.条目数.ToString("N0"), "按需解码")
    End Function

    Private Shared Function 格式化比特率(值 As Long) As String
        If 值 <= 0 Then Return String.Empty
        If 值 >= 1_000_000 Then Return $"{值 / 1_000_000.0R:0.##} Mbps"
        Return $"{值 / 1000.0R:0.##} kbps"
    End Function

    Private Shared Function 格式化比特率(值 As ULong) As String
        If 值 = 0 Then Return String.Empty
        If 值 >= 1_000_000UL Then Return $"{值 / 1_000_000.0R:0.##} Mbps"
        Return $"{值 / 1000.0R:0.##} kbps"
    End Function

    Private Shared Function 格式化时间(值 As TimeSpan) As String
        If 值 < TimeSpan.Zero Then 值 = TimeSpan.Zero
        Return $"{CInt(Math.Floor(值.TotalHours)):00}:{值.Minutes:00}:{值.Seconds:00}"
    End Function

    Private Shared Function 色彩模式文本(模式 As 色彩输出模式, 是HDR源 As Boolean) As String
        Select Case 模式
            Case 色彩输出模式.峰值映射HDR : Return "真实 HDR 高亮"
            Case 色彩输出模式.原始HDR按SDR呈现 : Return "原始 HDR 灰"
            Case Else : Return If(是HDR源, "映射 SDR", "SDR")
        End Select
    End Function

    Private Shared Function 色彩范围(值 As Integer) As String
        Return If(值 = 1, "Limited", If(值 = 2, "Full", String.Empty))
    End Function

    Private Shared Function 色彩原色(值 As Integer) As String
        Select Case 值
            Case 1 : Return "BT.709"
            Case 9 : Return "BT.2020"
            Case 12 : Return "P3-D65"
            Case Else : Return String.Empty
        End Select
    End Function

    Private Shared Function 色彩传递(值 As Integer) As String
        Select Case 值
            Case 1 : Return "BT.709"
            Case 13 : Return "sRGB"
            Case 16 : Return "PQ"
            Case 18 : Return "HLG"
            Case Else : Return String.Empty
        End Select
    End Function

    Private Shared Function 色彩空间(值 As Integer) As String
        Select Case 值
            Case 1 : Return "BT.709"
            Case 5, 6 : Return "BT.601"
            Case 9, 10 : Return "BT.2020"
            Case Else : Return String.Empty
        End Select
    End Function

    Private Shared Function 格式化色度抽样(值 As String) As String
        If String.IsNullOrWhiteSpace(值) Then Return String.Empty
        Return 值.Replace(":"c, String.Empty).Replace("-"c, String.Empty).Replace(" "c, String.Empty)
    End Function

    Private Shared Function 视频输出格式(位深 As Integer) As String
        Select Case 位深
            Case 8 : Return "BGRA8 (8bit)"
            Case 10 : Return "RGB10A2 (10bit)"
            Case Else : Return String.Empty
        End Select
    End Function

    Private Shared Function 测量文本(图形 As Graphics, 文本 As String, 字体 As Font) As Single
        If String.IsNullOrEmpty(文本) Then Return 0
        Using 格式 = StringFormat.GenericTypographic.Clone()
            格式.FormatFlags = 格式.FormatFlags Or StringFormatFlags.MeasureTrailingSpaces Or StringFormatFlags.NoWrap
            Return 图形.MeasureString(文本, 字体, Integer.MaxValue, 格式).Width
        End Using
    End Function

    Private Shared Function 拟合文本(图形 As Graphics, 文本 As String, 字体 As Font, 最大宽度 As Single) As String
        If String.IsNullOrEmpty(文本) OrElse 测量文本(图形, 文本, 字体) <= 最大宽度 Then Return 文本
        Const 省略号 = "..."
        Dim 低 = 0, 高 = 文本.Length
        While 低 < 高
            Dim 中 = (低 + 高 + 1) \ 2
            If 测量文本(图形, String.Concat(文本.AsSpan(0, 中), 省略号), 字体) <= 最大宽度 Then
                低 = 中
            Else
                高 = 中 - 1
            End If
        End While
        Return String.Concat(文本.AsSpan(0, 低), 省略号)
    End Function

    Private Shared Function 安全获取(Of T)(提供器 As Func(Of T)) As T
        If 提供器 Is Nothing Then Return Nothing
        Try
            Return 提供器()
        Catch
            Return Nothing
        End Try
    End Function

    Private Shared Function 安全文件名(路径 As String) As String
        If String.IsNullOrWhiteSpace(路径) Then Return String.Empty
        Try
            Return Path.GetFileName(路径)
        Catch
            Return 路径
        End Try
    End Function

    Private Shared Function 计算图层签名(画布 As Size, 命令 As IReadOnlyList(Of 定时文字命令)) As ULong
        Dim 哈希 As ULong = &HCBF29CE484222325UL
        混合(哈希, CULng(CUInt(画布.Width))) : 混合(哈希, CULng(CUInt(画布.Height)))
        混合(哈希, CULng(命令.Count))
        For Each 项 In 命令
            混合(哈希, If(项.是位图, 1UL, 0UL))
            混合(哈希, BitConverter.SingleToUInt32Bits(项.X))
            混合(哈希, BitConverter.SingleToUInt32Bits(项.Y))
            混合(哈希, BitConverter.SingleToUInt32Bits(项.宽度))
            混合(哈希, BitConverter.SingleToUInt32Bits(项.高度))
            混合(哈希, 项.前景色ARGB) : 混合(哈希, CULng(项.样式))
            For Each 字符 In 项.文本
                混合(哈希, CULng(AscW(字符) And &HFFFF&))
            Next
        Next
        Return 哈希
    End Function

    Private Shared Sub 混合(ByRef 哈希 As ULong, 值 As ULong)
        哈希 = Numerics.BitOperations.RotateLeft(哈希, 7) Xor 值
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        刷新定时器.Stop()
        RemoveHandler 刷新定时器.Tick, AddressOf 刷新定时器_Tick
        RemoveHandler 画面控件.ClientSizeChanged, AddressOf 画面大小已变化
        操作消息列表.Clear() : 图层命令.Clear() : 图层序号 += 1UL
        Try
            Dim 画布 = New Size(Math.Max(1, 画面控件.ClientSize.Width), Math.Max(1, 画面控件.ClientSize.Height))
            提交图层(画布, Array.Empty(Of 定时文字命令)(), 图层序号, 10.0F)
        Catch
        End Try
        刷新定时器.Dispose() : 普通字体.Dispose()
        GC.SuppressFinalize(Me)
    End Sub
End Class
