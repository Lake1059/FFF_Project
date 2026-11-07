Imports System.Diagnostics
Imports System.IO
Imports System.Globalization

''' <summary>
''' 在独立的最顶层 GPU 槽位中呈现播放器信息。调试信息固定在左上角，
''' 短时操作反馈固定在左下角；两者不参与字幕或弹幕的生命周期。
''' </summary>
Friend NotInheritable Class 播放器信息图层呈现器
    Implements IDisposable

    Private NotInheritable Class 操作消息
        Friend Sub New(文本值 As String, 颜色值 As UInteger, 到期时钟值 As Long, 操作键值 As String,
                       文本提供器值 As Func(Of String))
            文本 = 文本值 : 颜色 = 颜色值 : 到期时钟 = 到期时钟值 : 操作键 = 操作键值
            文本提供器 = 文本提供器值
        End Sub
        Friend ReadOnly 文本 As String
        Friend ReadOnly 颜色 As UInteger
        Friend ReadOnly 到期时钟 As Long
        Friend ReadOnly 操作键 As String
        Friend ReadOnly 文本提供器 As Func(Of String)
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
    ' 告警色：比橙色更偏红，与所有"普通信息"色（青/黄/绿/品红/蓝/紫/橙）都能区分，
    ' 避免用户把异常当成又一条常规读数。
    Private Const 告警色 As UInteger = &HFFFF6B6BUI
    Private Const 默认信息字体 As String = "Microsoft YaHei UI"
    Private Const 信息字号 As Single = 11.0F
    Private Const 文字阴影颜色 As UInteger = &H90000000UI
    Private Const 文字阴影偏移DIP As Single = 2.0F

    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 获取快照 As Func(Of 播放器快照)
    Private ReadOnly 获取媒体信息 As Func(Of 媒体信息)
    Private ReadOnly 获取媒体路径 As Func(Of String)
    Private ReadOnly 获取字幕 As Func(Of 外部字幕轨道)
    Private ReadOnly 获取弹幕 As Func(Of 弹幕资料库)
    Private ReadOnly 获取字幕状态 As Func(Of 定时文字状态)
    Private ReadOnly 获取弹幕状态 As Func(Of 定时文字状态)
    Private ReadOnly 获取WASAPI模式 As Func(Of WASAPI共享模式)
    Private ReadOnly 获取内容描述 As Func(Of String)
    ' 模块侧逐帧峰值（DLL 解锁后经裸导出回注；0 = 无）。
    Private ReadOnly 获取外部峰值 As Func(Of Single)
    Private ReadOnly 获取合成文本 As Func(Of String)
    Private ReadOnly 获取超分状态 As Func(Of String)
    Private ReadOnly 获取自动HDR状态 As Func(Of String)
    Private ReadOnly 获取直通文本 As Func(Of String)
    ' AI 实时字幕是否正在跑（worker 进程活着）。字幕行用它区分"真的没有字幕"
    ' 与"没有外挂字幕但 AI 在实时转写"——后者显示"未加载"是口径错误。
    Private ReadOnly 获取AI字幕活动 As Func(Of Boolean)
    Private ReadOnly 提交图层 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single)
    Private ReadOnly 刷新定时器 As LakeUI.PrecisionTimer
    Private 普通字体 As New Font(默认信息字体, 信息字号, FontStyle.Regular, GraphicsUnit.Point)
    Private 普通字体名称 As String = 默认信息字体
    Private 临时字号偏移磅值 As Single
    Private ReadOnly 原生宽度缓存 As New Dictionary(Of String, Single)(StringComparer.Ordinal)
    Private ReadOnly 文本测量格式 As StringFormat
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
    Private 上次播放位置 As TimeSpan
    Private 上次时间轴代次 As ULong
    Private 最近媒体速度 As Double = 1
    ' HDR 动态段的粘滞状态：动态元数据是逐帧口径（无 SEI 的帧会回落），
    ' 直接显示会横跳；本流内一旦见过就保持显示，换片/换流时重置。
    Private 粘滞动态媒体键 As String = String.Empty
    Private 粘滞动态已见过 As Boolean
    ' HDR 行快照派生段的"开流取一次"锁存：源峰值/显示目标/回退位/规格回退枚举
    ' 全是逐帧口径，实时显示就是横跳本身。打开后的第一次有效读数取一次，
    ' 整轮冻结；键含请求色彩模式 —— 用户主动换档是合法变化，必须重新可见。
    Private 粘滞HDR键 As String = String.Empty
    Private 粘滞HDR已取 As Boolean
    Private 粘滞HDR规格枚举 As HDR格式 = HDR格式.SDR
    Private 粘滞源峰值 As UInteger
    ' 外部模块的逐帧峰值：与源峰值的“开流取一次”不同，这一项点名要动态显示——
    ' 启用位粘滞（见过一帧就固定走逐帧口径，无 L1 的帧保持末值不回跳），
    ' 数值随 UI 刷新取泵最新发布；换片/换流时随动态键重置。
    Private 粘滞外部峰值已见过 As Boolean
    Private 粘滞外部峰值 As UInteger
    Private 粘滞目标有效 As Boolean
    Private 粘滞目标尼特 As UInteger
    Private 粘滞回退 As Boolean
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
                   内容描述提供器 As Func(Of String),
                   外部峰值提供器 As Func(Of Single),
                   图层提交器 As Action(Of Size, IReadOnlyList(Of 定时文字命令), ULong, Single),
                   Optional 超分状态提供器 As Func(Of String) = Nothing,
                   Optional 自动HDR状态提供器 As Func(Of String) = Nothing,
                   Optional 合成文本提供器 As Func(Of String) = Nothing,
                   Optional AI字幕活动提供器 As Func(Of Boolean) = Nothing,
                   Optional 直通文本提供器 As Func(Of String) = Nothing)
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
        获取内容描述 = 内容描述提供器
        获取外部峰值 = 外部峰值提供器
        获取合成文本 = 合成文本提供器
        获取超分状态 = 超分状态提供器
        获取自动HDR状态 = 自动HDR状态提供器
        获取直通文本 = 直通文本提供器
        获取AI字幕活动 = AI字幕活动提供器
        提交图层 = 图层提交器
        文本测量格式 = DirectCast(StringFormat.GenericTypographic.Clone(), StringFormat)
        文本测量格式.FormatFlags = 文本测量格式.FormatFlags Or
            StringFormatFlags.MeasureTrailingSpaces Or StringFormatFlags.NoWrap
        刷新定时器 = New LakeUI.PrecisionTimer With {.Interval = 200, .SynchronizingObject = 画面控件}
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

    Friend ReadOnly Property 调试信息可见 As Boolean
        Get
            Return 调试可见 AndAlso Not 已释放
        End Get
    End Property

    Friend Sub 调整临时字号(滚轮格数 As Integer)
        If Not 调试信息可见 OrElse 滚轮格数 = 0 Then Return
        Dim 新偏移 = Math.Clamp(临时字号偏移磅值 + 滚轮格数 * 1.0F,
                                8.0F - 信息字号, 36.0F - 信息字号)
        If 新偏移 = 临时字号偏移磅值 Then Return
        临时字号偏移磅值 = 新偏移
        重建普通字体()
        原生宽度缓存.Clear()
        使内容失效()
    End Sub

    Friend Sub 应用全局字体(fontName As String)
        If 已释放 Then Return
        Dim 新字体名称 = 规范信息字体名称(fontName)
        Dim 新字体 = 创建信息字体(新字体名称, 普通字体, 信息字号 + 临时字号偏移磅值)
        If String.Equals(普通字体名称, 新字体名称, StringComparison.CurrentCultureIgnoreCase) AndAlso
           String.Equals(普通字体.Name, 新字体.Name,
                         StringComparison.CurrentCultureIgnoreCase) Then
            新字体.Dispose()
            Return
        End If

        Dim 旧字体 = 普通字体
        普通字体 = 新字体
        普通字体名称 = 新字体名称
        原生宽度缓存.Clear()
        旧字体.Dispose()
        使内容失效()
    End Sub

    Private Sub 重建普通字体()
        Dim 新字体 = 创建信息字体(普通字体名称, 普通字体, 信息字号 + 临时字号偏移磅值)
        Dim 旧字体 = 普通字体
        普通字体 = 新字体
        旧字体.Dispose()
    End Sub

    Friend Sub 显示操作信息(文本 As String, Optional 颜色 As UInteger = 黄色,
                          Optional 操作键 As String = Nothing,
                          Optional 文本提供器 As Func(Of String) = Nothing)
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
        操作消息列表.Add(New 操作消息(文本, 颜色, 到期, 操作键, 文本提供器))
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
        Dim 结果 As New List(Of 信息行)(14)
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
        If 快照.已知总时长 AndAlso 快照.总时长 > TimeSpan.Zero Then
            时间戳 &= $" / {格式化时间(快照.总时长)} ({百分比:0}%)"
        End If
        结果.Add(配对行("时间戳：", 时间戳, 青色))
        添加配对行如果有值(结果, "文件异常：", 文件异常(快照), 告警色)

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
            ' 内容分类与合成状态都由外部模块的注入泵产出（解锁后才非空）；宿主只显示
            ' 不解释，"空值自然不出现"。用户裁定（2026-11-02）：外部增强等级不再单独成行，
            ' 注入态由模块供给 "P…" 文本，本体只拼 "HDR " & 文本 = "HDR P8.1 …"（规格词
            ' 整体替换，HDR10 等基础词不显示），信息图层里没有独立的「内容：」行。
            ' 合成段同源：显示模块快照文本自己的措辞（"已请求"≠"已合成"，措辞由模块负责）。
            Dim 内容描述 = 安全获取(获取内容描述)
            Dim 合成文本 = 安全获取(获取合成文本)
            ' 粘滞窗口按 媒体路径|视频流 划界：跨过边界一律从头累计。
            Dim 粘滞键 = $"{媒体路径}|{快照.当前视频流}"
            If 粘滞键 <> 粘滞动态媒体键 Then
                粘滞动态媒体键 = 粘滞键
                粘滞动态已见过 = False
                粘滞外部峰值已见过 = False
            End If
            If 快照.动态HDR元数据有效 Then 粘滞动态已见过 = True
            ' 外部逐帧峰值：>0 即启用粘滞并跟随刷新（换片/换流时随键重置；
            ' 0 是“本帧无 L1/未解锁”，保持末值，不把读数回跳到零）。
            ' -1 是“通路没在跑”（泵已停）：末值已经冻结，粘滞位必须作废，
            ' 否则泵死之后这一行还挂着“逐帧”两字，把静态旧读数冒充成实时。
            Dim 外部峰值 = 安全获取(获取外部峰值)
            ' 上界防护放在取数之后、转换之前：安全获取 只管"读"这一步，转换在外面。
            ' NaN 本身是安全的（NaN 的比较恒为 False，不进分支），但 +Infinity 会穿过
            ' >0.5F 后在 CUInt 抛出 OverflowException，直接冲到图层绘制线程上。
            ' 100000 尼特远超任何真实读数量级，越界的按"无数据"处理（保持末值）。
            If 外部峰值 < 0F Then
                粘滞外部峰值已见过 = False
            ElseIf 外部峰值 > 0.5F AndAlso 外部峰值 < 100000F Then
                粘滞外部峰值已见过 = True
                粘滞外部峰值 = CUInt(Math.Round(外部峰值))
            End If
            ' HDR 行"开流取一次"锁存：换片/换流/用户换档跨过键界才重取，
            ' 其余时刻不再读快照的逐帧派生段 —— 显示层从此没有逐帧输入。
            Dim HDR键 = $"{媒体路径}|{快照.当前视频流}|{快照.请求色彩模式}"
            If HDR键 <> 粘滞HDR键 Then
                粘滞HDR键 = HDR键
                粘滞HDR已取 = False
            End If
            If Not 粘滞HDR已取 AndAlso (快照.是HDR源 OrElse 视频.是HDR) Then
                粘滞HDR已取 = True
                粘滞HDR规格枚举 = 快照.HDR规格
                粘滞源峰值 = 快照.源峰值尼特
                粘滞目标有效 = 快照.实际色彩模式 = 色彩输出模式.峰值映射HDR AndAlso
                    快照.HDR有效目标峰值尼特 > 0
                粘滞目标尼特 = 快照.HDR有效目标峰值尼特
                粘滞回退 = 快照.HDR回退有效
            End If
            ' 显示目标的迟到补取：显示器能力（DXGI 查询）与峰值映射档的落地都晚于
            ' 开流，开流一刻常读到无效或兜底读数 ⇒ 只补目标段：快照一旦报出有效
            ' 目标且与已锁存值不同，就一次性升级（取到后照旧冻结；目标不是逐帧量，
            ' 内核侧只在真实显示/配置变化时改写它，不存在横跳输入）。
            If 快照.实际色彩模式 = 色彩输出模式.峰值映射HDR AndAlso
                快照.HDR有效目标峰值尼特 > 0 AndAlso
                (Not 粘滞目标有效 OrElse 粘滞目标尼特 <> 快照.HDR有效目标峰值尼特) Then
                粘滞目标有效 = True
                粘滞目标尼特 = 快照.HDR有效目标峰值尼特
            End If
            添加配对行如果有值(结果, "HDR：", 视频HDR(视频, 快照, 内容描述, 合成文本), 橙色)

            添加配对行如果有值(结果, "输出：", 视频输出(快照, 画面控件.ClientSize), 绿色)
            添加配对行如果有值(结果, "渲染：", 视频渲染(快照), 黄色)
            ' RTX 视频超分辨率与 RTX 自动 HDR（TrueHDR）各一行。这两行存在的理由相同：
            ' 内核逐帧跳过时**完全静默**（软解源没有 D3D11_BIND_DECODER、窗口没放大、
            ' 输出不是 10 位 HDR 契约……），用户能看到的只有"勾了但没效果"。
            ' 文本由 播放器会话 侧组好：判词（已激活/已启用 · 未生效）+ 一句人话，
            ' 这里只按判词着色——判词取自内核的 生效 字段，不是用户的勾选态。
            Dim 超分行 = 安全获取(获取超分状态)
            If 超分行 IsNot Nothing Then
                添加配对行如果有值(结果, "超分辨率：", 超分行, 判词颜色(超分行))
            End If
            Dim 自动HDR行 = 安全获取(获取自动HDR状态)
            If 自动HDR行 IsNot Nothing Then
                添加配对行如果有值(结果, "自动 HDR：", 自动HDR行, 判词颜色(自动HDR行))
            End If
        End If

        If 音频 IsNot Nothing Then
            Dim WASAPI = 安全获取(获取WASAPI模式)
            Dim 编码 = If(String.IsNullOrWhiteSpace(音频.编码), String.Empty,
                        音频.编码.ToUpperInvariant())
            Dim 音频概要 = If(String.IsNullOrEmpty(编码), $"WASAPI {WASAPI}",
                           $"{编码} - WASAPI {WASAPI}")
            添加配对行如果有值(结果, "音频：", 音频概要, 品红, 8)
            ' IAMF 由 AOM 参考解码器接管时，容器里那条音轨的编码名（如 opus/flac）
            ' 并不能说明实际声场 —— 同一素材在旧路径下会显示为 stereo。这一行把
            ' 真实声道布局摆出来，用户才能确认沉浸声是否真的生效。
            添加配对行如果有值(结果, "声场：", 快照.IAMF描述, 青色)
            ' 接缝生效才出现这一行：外部渲染器拥有床（HRTF 双耳）。缺席 / 旧件 / 未解锁
            ' 一律不出现——那是现网的矩阵折叠，不是一条需要向用户解释的降级。
            ' 用户口径只写身份："床/拥有床"是混音行话（channel-based 的静态层），
            ' 谁在渲染是实现者视角的诊断，对用户是噪音。
            If 快照.音频双耳渲染生效 Then
                添加配对行如果有值(结果, "双耳：", "HRTF 空间音频", 青色)
            End If
            ' 源码直通通道拥有音轨时才出现这一行：显示名由模块注入、宿主原样打印
            ' 不解释（格式身份是模块的语义，宿主只搬运）。缺席/未接管
            ' 不出现——那是现网解码路径，不是需要向用户解释的降级。
            Dim 直通行 = 安全获取(获取直通文本)
            If 直通行 IsNot Nothing Then
                添加配对行如果有值(结果, "直通：", 直通行, 青色)
            End If
            添加配对行如果有值(结果, "输入：", 音频输入(音频, 快照.音频实时比特率), 紫色)
            添加配对行如果有值(结果, "输出：", 音频输出(音频, 快照), 绿色)
        End If
        添加配对行如果有值(结果, "音频故障：", 音频健康度(快照), 告警色)

        ' AI 实时字幕与外挂字幕是两条独立轨道：外挂为空不代表"没有字幕"——
        ' worker 在跑就是 AI 在实时转写，此时显示"未加载"是错误口径（用户按
        ' Tab 想确认字幕状态，看到的应当是"已加载"级别的答复）。两种情况都
        ' 覆盖：纯 AI（外挂为空）整行改口；外挂 + AI 并存时在外挂信息后追加
        ' AI 标记，两种来源一眼可分。
        Dim AI活动 = 安全获取(获取AI字幕活动)
        Dim 字幕文本 = If(字幕 Is Nothing,
            If(AI活动, "已加载 AI实时识别", "未加载"),
            合并字段(
                字幕.格式.ToString().ToUpperInvariant(),
                $"总数量 {字幕条目数(字幕)}",
                If(字幕状态 Is Nothing, String.Empty, $"正在渲染 {字幕状态.命令数}"),
                $"延迟 {图层延迟(字幕状态)}",
                If(AI活动, "AI实时识别", String.Empty)))
        Dim 弹幕文本 = If(弹幕 Is Nothing, "未加载", 合并字段(
            "哔哩哔哩 XML", $"总数量 {弹幕.数量}",
            If(弹幕状态 Is Nothing, String.Empty, $"正在渲染 {弹幕状态.命令数}"),
            $"延迟 {图层延迟(弹幕状态)}"))
        结果.Add(配对行("字幕：", 字幕文本, 青色, 8))
        结果.Add(配对行("弹幕：", 弹幕文本, 橙色))
        Return 结果
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
            Dim 当前文本 = If(消息.文本提供器 Is Nothing, 消息.文本, 安全获取(消息.文本提供器))
            If String.IsNullOrWhiteSpace(当前文本) Then Continue For
            Dim 文本 = 拟合文本(图形, 当前文本, 普通字体, 最大文本宽度)
            Dim 文本宽度 = 测量文本(图形, 文本, 普通字体)
            Dim 背景高度 = 行高 + 垂直内边距 * 2.0F
            ' Short operation messages keep a compact background; long text is
            ' fitted first and may grow to the existing full-width limit.
            Dim 当前背景宽度 = Math.Min(背景宽度,
                Math.Max(1.0F, 文本宽度 + 水平内边距 * 2.0F))
            y -= 背景高度
            图层命令.Add(定时文字命令.创建位图(消息背景像素, 1, 1, 4,
                New RectangleF(边距, y, 当前背景宽度, 背景高度), 2UL))
            图层命令.Add(创建文字命令(文本, 普通字体名称, 普通字体, 消息.颜色,
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
            图层命令.Add(创建文字命令(段(index).文本, 普通字体名称, 普通字体, 段(index).颜色,
                New RectangleF(x, y, Math.Max(1.0F, 宽度(index) + 2.0F), 行高), DPI))
            x += 宽度(index)
        Next
    End Sub

    Private Shared Function 创建文字命令(文本 As String, 字体名称 As String, 字体 As Font, 颜色 As UInteger,
                                      区域 As RectangleF, DPI As Integer) As 定时文字命令
        Dim 字号像素 = CSng(字体.SizeInPoints * Math.Max(1, DPI) / 72.0F)
        Dim 阴影偏移 = 文字阴影偏移DIP * Math.Max(1, DPI) / 96.0F
        Return 定时文字命令.创建文字(文本, 规范信息字体名称(字体名称), 字号像素, 区域, 颜色,
            &HFF000000UI, 0, 定时文字对齐.靠前, 定时文字对齐.靠前, 定时文字样式.无,
            阴影色ARGB:=文字阴影颜色, 阴影X偏移:=阴影偏移, 阴影Y偏移:=阴影偏移)
    End Function

    Private Shared Function 创建信息字体(fontName As String, 当前字体 As Font,
                                      Optional 字号磅值 As Single = 0) As Font
        Dim 字体名称 = 规范信息字体名称(fontName)
        Dim 实际字号 = If(字号磅值 > 0, 字号磅值, 当前字体.SizeInPoints)
        Try
            Return New Font(字体名称, 实际字号, 当前字体.Style, GraphicsUnit.Point)
        Catch
            Try
                Return New Font(默认信息字体, 实际字号, 当前字体.Style, GraphicsUnit.Point)
            Catch
                Return New Font(SystemFonts.DefaultFont.FontFamily, 实际字号, 当前字体.Style, GraphicsUnit.Point)
            End Try
        End Try
    End Function

    Private Shared Function 规范信息字体名称(fontName As String) As String
        Return If(String.IsNullOrWhiteSpace(fontName), 默认信息字体, fontName.Trim())
    End Function

    Private Shared Function 配对行(标签 As String, 值 As String, 值颜色 As UInteger,
                               Optional 空隙前 As Single = 0) As 信息行
        Return New 信息行(空隙前, New 文本段(标签, 标签颜色), New 文本段(值, 值颜色))
    End Function

    ''' <summary>RTX 两行判词的配色：未生效用告警色，已生效用绿色。</summary>
    Friend Shared Function 判词颜色(文本 As String) As UInteger
        If 文本.StartsWith("未生效", StringComparison.Ordinal) Then Return 告警色
        Return 绿色
    End Function

    Private Shared Sub 添加配对行如果有值(结果 As List(Of 信息行), 标签 As String,
                                      值 As String, 值颜色 As UInteger,
                                      Optional 空隙前 As Single = 0)
        If String.IsNullOrWhiteSpace(值) Then Return
        结果.Add(配对行(标签, 值, 值颜色, 空隙前))
    End Sub

    Private Shared Function 合并字段(ParamArray 字段 As String()) As String
        Dim 有效数量 = 0
        For Each 值 In 字段
            If String.IsNullOrWhiteSpace(值) Then Continue For
            字段(有效数量) = 值
            有效数量 += 1
        Next
        Return If(有效数量 = 0, String.Empty, String.Join("   ", 字段, 0, 有效数量))
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
            上次播放位置 = If(快照 Is Nothing, TimeSpan.Zero, 快照.播放位置)
            上次时间轴代次 = If(快照 Is Nothing, 0UL, 快照.时间轴代次)
            最近媒体速度 = 1
            Return
        End If
        If 上次帧率采样时钟 = 0 OrElse 快照.时间轴代次 <> 上次时间轴代次 Then
            上次视频帧次数 = 快照.已呈现视频帧数
            上次总丢帧数 = 计算总丢帧数(快照)
            上次帧率采样时钟 = 当前时钟
            上次播放位置 = 快照.播放位置
            上次时间轴代次 = 快照.时间轴代次
            最近媒体速度 = 1
            Return
        End If
        Dim 已过秒数 = CDbl(当前时钟 - 上次帧率采样时钟) / Stopwatch.Frequency
        If 已过秒数 < 0.85R Then Return
        最近实际帧率 = If(快照.已呈现视频帧数 >= 上次视频帧次数,
            (快照.已呈现视频帧数 - 上次视频帧次数) / 已过秒数, 0)
        Dim 总丢帧 = 计算总丢帧数(快照)
        最近实时丢帧数 = If(总丢帧 >= 上次总丢帧数, 总丢帧 - 上次总丢帧数, 0UL)
        最近媒体速度 = Math.Max(0, (快照.播放位置 - 上次播放位置).TotalSeconds / 已过秒数)
        上次播放位置 = 快照.播放位置
        上次视频帧次数 = 快照.已呈现视频帧数
        上次总丢帧数 = 总丢帧
        上次帧率采样时钟 = 当前时钟
    End Sub

    Private Shared Function 视频输入(流 As 媒体流信息, 实时比特率 As ULong) As String
        Dim 格式 = If(String.IsNullOrWhiteSpace(流.解码表面像素格式), 流.像素格式, 流.解码表面像素格式)
        Return 合并字段(
            If(String.IsNullOrWhiteSpace(格式), String.Empty,
               $"格式 {格式.ToUpperInvariant()}"),
            If(流.宽度 > 0 AndAlso 流.高度 > 0, $"分辨率 {流.宽度}x{流.高度}", String.Empty),
            If(流.平均帧率 > 0, $"帧率 {流.平均帧率:0.###}fps", String.Empty),
            If(实时比特率 > 0, $"实时码率 {格式化比特率(实时比特率)}", String.Empty))
    End Function

    Private Shared Function 视频色彩(流 As 媒体流信息) As String
        ' 色度一行只写色度本身，值用标准写法（4:2:0 / 4:2:2 / 4:4:4）。
        ' 抽掉冒号会让读者以为是一个数字编号。
        Dim 采样 = If(流.色度抽样 Is Nothing, String.Empty, 流.色度抽样.Trim())
        ' 标准表述统一走 色彩表述 模块（Core\色彩表述.vb）：标签与媒体信息窗体
        ' 同口径（色彩标准/传输曲线/色彩矩阵/色彩范围），元数据缺席的字段不出现。
        Dim 矩阵 = 色彩表述.色彩矩阵(流.色彩空间)
        Dim 标准 = 色彩表述.色彩标准(流.色彩原色)
        Dim 传输 = 色彩表述.传输曲线(流.色彩传递)
        Dim 范围 = 色彩表述.色彩范围(流.色彩范围)
        Return 合并字段(
            If(String.IsNullOrEmpty(采样), String.Empty, $"色度 {采样}"),
            If(String.IsNullOrEmpty(标准), String.Empty, $"色彩标准 {标准}"),
            If(String.IsNullOrEmpty(传输), String.Empty, $"传输曲线 {传输}"),
            If(String.IsNullOrEmpty(矩阵), String.Empty, $"色彩矩阵 {矩阵}"),
            If(String.IsNullOrEmpty(范围), String.Empty, $"色彩范围 {范围}"))
    End Function

    Private Shared Function 视频输出(快照 As 播放器快照, 输出大小 As Size) As String
        Dim 格式 = 视频输出格式(快照.视频输出位深度)
        Return 合并字段(
            If(String.IsNullOrEmpty(格式), String.Empty, $"格式 {格式}"),
            If(输出大小.Width > 0 AndAlso 输出大小.Height > 0,
               $"分辨率 {输出大小.Width}x{输出大小.Height}", String.Empty),
            $"色彩模式 {色彩模式文本(快照)}")
    End Function

    Private Function 视频HDR(流 As 媒体流信息, 快照 As 播放器快照,
                             内容描述 As String, 合成文本 As String) As String
        ' 外部模块算出的增强规格词（"P8.1 …"，解锁后才有值）：宿主零外部格式语义，
        ' P 号与层词都来自模块文本。空值自然不出现。
        Dim 等级 = If(String.IsNullOrWhiteSpace(内容描述), String.Empty, 内容描述.Trim())
        ' 门控：HDR 源照常；但外部增强流可能被本体判成 SDR 基底（如 P5 无色彩标记）——
        ' 模块在场时这是"SDR 分类 + 外部色彩变换"的合法形态，不能因 SDR 就整行消失。
        If Not 快照.是HDR源 AndAlso Not 流.是HDR AndAlso 等级.Length = 0 Then Return String.Empty
        ' 规格优先流级口径（媒体 JSON 已与逐帧状态脱钩）；流只声明 SDR/缺声明时
        ' 才回退快照枚举 —— 且读的是"开流取一次"的锁存值，不再逐帧横跳。
        ' 用户裁定（2026-11-02）：基础 HDR 用传输函数表述（HDR10 PQ / HDR HLG）；
        ' 模块注入后规格整体变为 "HDR "+模块文本（HDR P8.1 …），基础规格词不显示。
        Dim 基础规格 = If(String.IsNullOrWhiteSpace(流.HDR格式) OrElse 流.HDR格式 = "SDR",
                    规格显示文本(粘滞HDR规格枚举, 流.HDR格式),
                    规格显示文本(HDR格式.SDR, 流.HDR格式))
        Dim 规格 = If(等级.Length > 0, "HDR " & 等级, 基础规格)
        ' 动态段是粘滞位（见 创建调试行），逐帧回落不再把这段抹掉。外部增强流上这份
        ' 动态元数据就是模块烘焙的外部映射表 ⇒ 文案升级为外部映射口径；HDR10+/Vivid 流
        ' （模块文本为空）维持原口径。
        Dim 动态 = If(粘滞动态已见过,
                    If(等级.Length = 0, "动态元数据已用于处理", "外部映射已用于处理"),
                    String.Empty)
        ' EL 残差合成状态：模块注入泵自己的措辞，原样显示（模块说"已请求"就是请求——
        ' 配对与否宿主无从知晓，也不许宿主替模块下结论）。泵没说时这段自然不出现。
        Dim 合成 = 合成文本
        ' 亮度段：模块解锁且已读到逐帧峰值时走动态口径（用户点名：DLL 回注的
        ' 动态每帧亮度直接进这一行，“逐帧”字样标明口径与静态锁存的区别）；
        ' 模块缺席/未解锁/流无 L1（HDR10+ 等）维持开流锁存值。
        Dim 亮度 = If(粘滞外部峰值已见过,
                    $"源峰值 {粘滞外部峰值:0}尼特 逐帧",
                    If(粘滞源峰值 > 0, $"源峰值 {粘滞源峰值:0}尼特", String.Empty))
        If 粘滞目标有效 Then
            亮度 = 合并字段(亮度, $"显示目标 {粘滞目标尼特:0}尼特")
        End If
        Dim 回退 = If(粘滞回退, "HDR fallback", String.Empty)
        Return 合并字段(规格, 动态, 合成, 亮度, 回退)
    End Function

    ''' <summary>HDR 行的规格词（2026-11-02 口径）：基础 HDR 用传输函数表述 ——
    ''' HDR10 ⇒ "HDR10 PQ"、HLG ⇒ "HDR HLG"；HDR10+/Vivid 保持注册名。枚举判不出的
    ''' （SDR/未知）回退媒体文本，且对常见的 "HDR10"/"HLG" 字样做同一映射。
    ''' 外部增强注入态不走这里 —— 模块的 "P…" 文本拼在 "HDR " 后（见 视频HDR）。</summary>
    Private Shared Function 规格显示文本(规格 As HDR格式, 媒体文本 As String) As String
        Select Case 规格
            Case HDR格式.HDR10 : Return "HDR10 PQ"
            Case HDR格式.HDR10Plus : Return "HDR10+"
            Case HDR格式.HLG : Return "HDR HLG"
            Case HDR格式.HDRVivid : Return "HDR Vivid"
            Case Else
                Select Case 媒体文本
                    Case "HDR10" : Return "HDR10 PQ"
                    Case "HLG" : Return "HDR HLG"
                    Case Else : Return 媒体文本
                End Select
        End Select
    End Function

    Private Function 视频渲染(快照 As 播放器快照) As String
        Return 合并字段(
            If(最近实际帧率 > 0, $"帧率 {最近实际帧率:0.00}fps", String.Empty),
            $"缓冲池 {快照.视频队列帧数}帧",
            $"实时丢帧 {最近实时丢帧数}",
            $"总丢帧 {计算总丢帧数(快照)}",
            If(最近媒体速度 < 0.95, $"媒体时钟 {最近媒体速度:0.00}x", String.Empty))
    End Function

    Private Shared Function 计算总丢帧数(快照 As 播放器快照) As ULong
        If ULong.MaxValue - 快照.已丢弃视频帧数 < 快照.已合并视频帧数 Then Return ULong.MaxValue
        Return 快照.已丢弃视频帧数 + 快照.已合并视频帧数
    End Function

    Private Shared Function 音频输入(流 As 媒体流信息, 实时比特率 As ULong) As String
        ' 编码名打头（AAC/OPUS/EAC3…）：输入行的身份来自编码，采样/声道是它的属性。
        Dim 编码 = If(String.IsNullOrWhiteSpace(流.编码), String.Empty, 流.编码.ToUpperInvariant())
        Dim 位深 = 音频采样格式化.输入位深(流)
        ' 声道数带布局（5.1 / 7.1.4 / 6.1…，FFmpeg 布局描述原样）；mono/stereo
        ' 不加——声道数 1/2 已经说明，括号只服务多声道布局这层信息。
        Dim 声道 As String = String.Empty
        If 流.声道数 > 0 Then
            声道 = $"声道数 {流.声道数}"
            Dim 布局 = 流.声道布局
            If Not String.IsNullOrWhiteSpace(布局) AndAlso 布局 <> "mono" AndAlso 布局 <> "stereo" Then
                声道 &= $"（{布局}）"
            End If
        End If
        Return 合并字段(
            编码,
            If(流.采样率 > 0, $"采样 {流.采样率}Hz", String.Empty),
            If(位深.Length > 0, "位深 " & 位深, String.Empty),
            声道,
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
            If(有输出格式, 格式, String.Empty),
            If(采样率 > 0, $"采样 {采样率}Hz", String.Empty),
            If(位深 > 0, $"位深 {位深}bit", String.Empty),
            If(声道 > 0, $"声道数 {声道}", String.Empty),
            $"缓冲区 {快照.音频缓冲时长.TotalMilliseconds:0}ms")
    End Function

    ''' <summary>音频健康度：只在异常时返回非空，正常播放不占一行。</summary>
    Private Shared Function 文件异常(快照 As 播放器快照) As String
        Dim 问题 As New List(Of String)
        If 快照.传入数据包异常 > 0 Then 问题.Add($"传入数据包异常 {快照.传入数据包异常}")
        If 快照.时间戳异常 > 0 Then 问题.Add($"时间戳异常 {快照.时间戳异常}")
        If 快照.读取过慢 > 0 Then 问题.Add($"读取速度太慢 {快照.读取过慢}")
        If 快照.读取错误 > 0 Then 问题.Add($"读取错误 {快照.读取错误}")
        If 快照.解码错误 > 0 Then 问题.Add($"解码错误 {快照.解码错误}")
        If 快照.数据未写入 > 0 Then 问题.Add($"数据未写入 {快照.数据未写入}")
        If 快照.等待文件写入 Then 问题.Add("等待文件写入")
        Return 合并字段(问题.ToArray())
    End Function

    Private Shared Function 音频健康度(快照 As 播放器快照) As String
        If 快照 Is Nothing Then Return String.Empty
        Dim 问题 As New List(Of String)
        If 快照.音频故障次数 > 0 Then 问题.Add($"故障 {快照.音频故障次数}")
        If 快照.音频欠载次数 > 0 Then 问题.Add($"欠载 {快照.音频欠载次数}")
        If 快照.音频不连续次数 > 0 Then 问题.Add($"不连续 {快照.音频不连续次数}")
        If 快照.音频时间戳抖动帧数 > 0 Then 问题.Add($"抖动 {快照.音频时间戳抖动帧数}")
        If 快照.音频插入静音帧数 > 0 Then 问题.Add($"补静音 {快照.音频插入静音帧数}")
        If 快照.音频丢弃重叠帧数 > 0 Then 问题.Add($"丢重叠 {快照.音频丢弃重叠帧数}")
        If 快照.音频拒绝帧数 > 0 Then 问题.Add($"拒绝 {快照.音频拒绝帧数}")
        If 问题.Count = 0 Then Return String.Empty
        Return 合并字段(问题.ToArray())
    End Function

    Private Shared Function 图层延迟(状态 As 定时文字状态) As String
        If 状态 Is Nothing OrElse 状态.已提交序号 <= 状态.已绘制序号 Then Return "<1ms"
        Return $"{(状态.已提交序号 - 状态.已绘制序号) * 1000.0R / 60.0R:F1}ms"
    End Function

    Private Shared Function 字幕条目数(字幕 As 外部字幕轨道) As String
        Return If(字幕.条目数 >= 0, 字幕.条目数.ToString(), "按需解码")
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

    Private Shared Function 色彩模式文本(快照 As 播放器快照) As String
        Select Case 快照.实际色彩模式
            Case 色彩输出模式.峰值映射HDR
                ' SDR 源现在也会被放到这条链上（默认策略：位深 >8 优先 16bit scRGB），
                ' 所以"真实高亮"必须按**源**说话——把高位深 SDR 报成 HDR 是假陈述。
                If 快照.是HDR源 Then
                    Return $"{规格显示文本(快照.HDR规格, "HDR")} 真实高亮"
                End If
                Return "scRGB 高位深（SDR 源）"
            Case 色彩输出模式.原始HDR按SDR呈现 : Return "原始 HDR 灰"
            Case Else : Return If(快照.是HDR源, "映射 SDR", "SDR")
        End Select
    End Function

    ' 色彩范围/色彩标准/传输曲线/色彩矩阵的标准表述统一在 色彩表述 模块
    ' （Core\色彩表述.vb）：信息图层与媒体信息窗体共用一份映射，口径不漂移。

    Private Shared Function 视频输出格式(位深 As Integer) As String
        Select Case 位深
            Case 8 : Return "BGRA8 (8bit)"
            Case 10 : Return "RGB10A2 (10bit)"
            Case 16 : Return "scRGB FP16 (16bit)"
            Case Else : Return String.Empty
        End Select
    End Function

    Private Function 测量文本(图形 As Graphics, 文本 As String, 字体 As Font) As Single
        If String.IsNullOrEmpty(文本) Then Return 0
        ' The command is rendered by DirectWrite. Measure with the same native
        ' font-variant resolver so names such as "MiSans Medium" cannot be
        ' narrower in GDI+ than the glyph run finally drawn.
        Dim 原生字号 = CSng(字体.SizeInPoints * Math.Max(1, 画面控件.DeviceDpi) / 72.0F)
        Dim 缓存键 = String.Concat(普通字体名称, ChrW(0), 文本, ChrW(0),
                                  BitConverter.SingleToUInt32Bits(原生字号).ToString("X8", CultureInfo.InvariantCulture))
        Dim 缓存宽度 As Single
        If 原生宽度缓存.TryGetValue(缓存键, 缓存宽度) Then Return 缓存宽度
        Dim 原生宽度 As Single = 0
        If Single.IsFinite(原生字号) AndAlso 原生字号 > 0 AndAlso
            播放器原生接口.FFF3FP_MeasureTimedTextWidth(
                文本, 普通字体名称, 原生字号, 原生定时文字标志.无, 原生宽度) = 原生播放器结果.成功 AndAlso
            Single.IsFinite(原生宽度) AndAlso 原生宽度 >= 0 Then
            If 原生宽度缓存.Count >= 512 Then 原生宽度缓存.Remove(原生宽度缓存.Keys.First())
            原生宽度缓存(缓存键) = 原生宽度
            Return 原生宽度
        End If
        Return 图形.MeasureString(文本, 字体, Integer.MaxValue, 文本测量格式).Width
    End Function

    Private Function 拟合文本(图形 As Graphics, 文本 As String, 字体 As Font, 最大宽度 As Single) As String
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
            混合(哈希, 项.阴影色ARGB)
            混合(哈希, BitConverter.SingleToUInt32Bits(项.阴影X偏移))
            混合(哈希, BitConverter.SingleToUInt32Bits(项.阴影Y偏移))
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
        刷新定时器.Dispose() : 文本测量格式.Dispose() : 普通字体.Dispose()
        GC.SuppressFinalize(Me)
    End Sub
End Class
