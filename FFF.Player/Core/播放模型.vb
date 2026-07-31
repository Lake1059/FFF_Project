Imports System.Text.Json.Serialization

Public Enum 解码模式 As UInteger
    未指定 = 0
    CPU = 1
    GPU = 2
End Enum

Public Enum 色彩输出模式 As UInteger
    映射到SDR = 0
    原始HDR按SDR呈现 = 1
    峰值映射HDR = 2
End Enum

Public Enum 视频缩放模式 As UInteger
    着色器 = 0
    D3D11视频处理器 = 1
End Enum

Public Enum HDR格式 As UInteger
    SDR = 0
    HDR10 = 1
    HDR10Plus = 2
    HLG = 3
    杜比视界 = 4
    HDRVivid = 5
End Enum

Public Enum HDR处理路径 As UInteger
    无 = 0
    HDR10静态元数据 = 1
    HDR10Plus动态映射 = 2
    HLG显示器映射 = 3
    杜比视界兼容基础层回退 = 4
    杜比视界FEL基础层回退 = 5
    HDRVivid动态映射 = 6
End Enum

Public Enum 杜比视界增强层类型 As UInteger
    无 = 0
    MEL = 1
    FEL = 2
    未知 = 3
End Enum

Public Enum 播放状态 As UInteger
    空闲 = 0
    正在打开 = 1
    就绪 = 2
    正在播放 = 3
    已暂停 = 4
    播放结束 = 5
    失败 = 6
    已关闭 = 7
End Enum

Public Enum WASAPI共享模式
    共享
    独占
End Enum

Public Enum 播放器事件类型 As UInteger
    状态变化 = 1
    打开完成 = 2
    操作完成 = 3
    播放结束 = 4
    错误 = 5
    色彩模式变化 = 6
    设备变化 = 7
End Enum

Public NotInheritable Class 播放器配置
    Public Property 解码器 As 解码模式
    Public Property 色彩模式 As 色彩输出模式 = 色彩输出模式.映射到SDR
    Public Property SDR峰值尼特 As Single = 100.0F
    ' 0 = 自动使用显示器能力；正值由未来的用户峰值设置覆盖。
    Public Property HDR峰值尼特 As Single = 0.0F
    Public Property SDR纸白尼特 As Single = 203.0F
    Public Property 输出窗口句柄 As IntPtr
    Public Property 音频端点标识 As String = String.Empty
    Public Property 事件同步上下文 As Threading.SynchronizationContext

    Friend Sub 验证()
        If 解码器 = 解码模式.未指定 Then Throw New ArgumentException("必须由调用方明确选择 CPU 或 GPU 解码。", NameOf(解码器))
        If 解码器 <> 解码模式.CPU AndAlso 解码器 <> 解码模式.GPU Then Throw New ArgumentOutOfRangeException(NameOf(解码器))
        If SDR峰值尼特 <= 0 OrElse Not Single.IsFinite(SDR峰值尼特) Then Throw New ArgumentOutOfRangeException(NameOf(SDR峰值尼特))
        If HDR峰值尼特 < 0 OrElse HDR峰值尼特 > 10000 OrElse Not Single.IsFinite(HDR峰值尼特) Then Throw New ArgumentOutOfRangeException(NameOf(HDR峰值尼特))
        If SDR纸白尼特 <= 0 OrElse Not Single.IsFinite(SDR纸白尼特) Then Throw New ArgumentOutOfRangeException(NameOf(SDR纸白尼特))
    End Sub
End Class

Public Structure 视频输出原始像素
    Friend Sub New(值 As 原生视频像素探针)
        红 = 值.红
        绿 = 值.绿
        蓝 = 值.蓝
        Alpha = 值.Alpha
        缩放模式 = CType(值.视频缩放模式, 视频缩放模式)
        输出位深度 = CInt(值.输出位深度)
        色彩模式 = CType(值.色彩模式, 色彩输出模式)
    End Sub

    Public ReadOnly Property 红 As Single
    Public ReadOnly Property 绿 As Single
    Public ReadOnly Property 蓝 As Single
    Public ReadOnly Property Alpha As Single
    Public ReadOnly Property 缩放模式 As 视频缩放模式
    Public ReadOnly Property 输出位深度 As Integer
    Public ReadOnly Property 色彩模式 As 色彩输出模式
End Structure

Public NotInheritable Class 播放器快照
    Friend Sub New(值 As 原生播放器快照)
        状态 = CType(值.状态, 播放状态)
        解码器 = CType(值.解码器, 解码模式)
        请求色彩模式 = CType(值.请求色彩模式, 色彩输出模式)
        实际色彩模式 = CType(值.实际色彩模式, 色彩输出模式)
        播放位置 = TimeSpan.FromTicks(值.位置100纳秒)
        总时长 = TimeSpan.FromTicks(值.时长100纳秒)
        帧序号 = 值.帧序号
        原始帧PTS = 值.原始帧PTS
        帧时间基分子 = 值.帧时间基分子
        帧时间基分母 = 值.帧时间基分母
        当前视频流 = 值.当前视频流
        当前音频流 = 值.当前音频流
        视频宽度 = 值.视频宽度
        视频高度 = 值.视频高度
        是HDR源 = 值.是HDR源 <> 0
        正在使用外部音轨 = 值.正在使用外部音轨 <> 0
        外部音轨偏移 = TimeSpan.FromTicks(值.外部音轨偏移100纳秒)
        已解码视频帧数 = 值.已解码视频帧数
        已呈现视频帧数 = 值.已呈现视频帧数
        已丢弃视频帧数 = 值.已丢弃视频帧数
        视频队列帧数 = CInt(值.视频队列帧数)
        源峰值尼特 = 值.源峰值尼特
        已解码音频帧数 = 值.已解码音频帧数
        音频位置 = TimeSpan.FromTicks(值.音频位置100纳秒)
        音频缓冲时长 = TimeSpan.FromTicks(Math.Max(0, 值.音频缓冲100纳秒))
        音频欠载次数 = 值.音频欠载次数
        音频时间戳抖动帧数 = 值.音频时间戳抖动帧数
        音频不连续次数 = 值.音频不连续次数
        音频插入静音帧数 = 值.音频插入静音帧数
        音频丢弃重叠帧数 = 值.音频丢弃重叠帧数
        已合并视频帧数 = 值.已合并视频帧数
        音频拒绝帧数 = 值.音频拒绝帧数
        交换链呈现次数 = 值.交换链呈现次数
        呈现等待时长 = TimeSpan.FromTicks(CLng(Math.Min(值.呈现等待100纳秒, CULng(Long.MaxValue))))
        设备锁等待时长 = TimeSpan.FromTicks(CLng(Math.Min(值.设备锁等待100纳秒, CULng(Long.MaxValue))))
        硬件传输时长 = TimeSpan.FromTicks(CLng(Math.Min(值.硬件传输100纳秒, CULng(Long.MaxValue))))
        软件转换时长 = TimeSpan.FromTicks(CLng(Math.Min(值.软件转换100纳秒, CULng(Long.MaxValue))))
        视频实时比特率 = 值.视频实时比特率
        音频实时比特率 = 值.音频实时比特率
        视频输出位深度 = CInt(值.视频输出位深度)
        视频缩放 = CType(值.视频缩放模式, 视频缩放模式)
        时间轴代次 = 值.时间轴代次
        HDR规格 = CType(值.HDR格式, HDR格式)
        兼容HDR规格 = 值.兼容HDR格式
        HDR处理路径 = CType(值.HDR处理路径, HDR处理路径)
        杜比视界配置档次 = CInt(值.杜比视界配置档次)
        杜比视界级别 = CInt(值.杜比视界级别)
        有杜比视界RPU = 值.有杜比视界RPU <> 0
        有杜比视界增强层 = 值.有杜比视界增强层 <> 0
        杜比视界增强层类型 = CType(值.杜比视界增强层类型, 杜比视界增强层类型)
        动态HDR元数据有效 = 值.动态HDR元数据有效 <> 0
        HDR回退有效 = 值.HDR回退有效 <> 0
        显示器最小亮度 = 值.显示器最小亮度毫尼特 / 1000.0F
        显示器峰值尼特 = 值.显示器峰值尼特
        显示器全屏峰值尼特 = 值.显示器全屏峰值尼特
        HDR有效目标峰值尼特 = 值.HDR有效目标峰值尼特
    End Sub

    Public ReadOnly Property 状态 As 播放状态
    Public ReadOnly Property 解码器 As 解码模式
    Public ReadOnly Property 请求色彩模式 As 色彩输出模式
    Public ReadOnly Property 实际色彩模式 As 色彩输出模式
    Public ReadOnly Property 播放位置 As TimeSpan
    Public ReadOnly Property 总时长 As TimeSpan
    Public ReadOnly Property 帧序号 As Long
    Public ReadOnly Property 原始帧PTS As Long
    Public ReadOnly Property 帧时间基分子 As Integer
    Public ReadOnly Property 帧时间基分母 As Integer
    Public ReadOnly Property 当前视频流 As Integer
    Public ReadOnly Property 当前音频流 As Integer
    Public ReadOnly Property 视频宽度 As UInteger
    Public ReadOnly Property 视频高度 As UInteger
    Public ReadOnly Property 是HDR源 As Boolean
    Public ReadOnly Property 正在使用外部音轨 As Boolean
    Public ReadOnly Property 外部音轨偏移 As TimeSpan
    Public ReadOnly Property 已解码视频帧数 As ULong
    Public ReadOnly Property 已呈现视频帧数 As ULong
    Public ReadOnly Property 已丢弃视频帧数 As ULong
    Public ReadOnly Property 视频队列帧数 As Integer
    Public ReadOnly Property 源峰值尼特 As UInteger
    Public ReadOnly Property 已解码音频帧数 As ULong
    Public ReadOnly Property 音频位置 As TimeSpan
    Public ReadOnly Property 音频缓冲时长 As TimeSpan
    Public ReadOnly Property 音频欠载次数 As ULong
    Public ReadOnly Property 音频时间戳抖动帧数 As ULong
    Public ReadOnly Property 音频不连续次数 As ULong
    Public ReadOnly Property 音频插入静音帧数 As ULong
    Public ReadOnly Property 音频丢弃重叠帧数 As ULong
    Public ReadOnly Property 已合并视频帧数 As ULong
    Public ReadOnly Property 音频拒绝帧数 As ULong
    Public ReadOnly Property 交换链呈现次数 As ULong
    Public ReadOnly Property 呈现等待时长 As TimeSpan
    Public ReadOnly Property 设备锁等待时长 As TimeSpan
    Public ReadOnly Property 硬件传输时长 As TimeSpan
    Public ReadOnly Property 软件转换时长 As TimeSpan
    Public ReadOnly Property 视频实时比特率 As ULong
    Public ReadOnly Property 音频实时比特率 As ULong
    Public ReadOnly Property 视频输出位深度 As Integer
    Public ReadOnly Property 视频缩放 As 视频缩放模式
    Public ReadOnly Property 时间轴代次 As ULong
    Public ReadOnly Property HDR规格 As HDR格式
    Public ReadOnly Property 兼容HDR规格 As UInteger
    Public ReadOnly Property HDR处理路径 As HDR处理路径
    Public ReadOnly Property 杜比视界配置档次 As Integer
    Public ReadOnly Property 杜比视界级别 As Integer
    Public ReadOnly Property 有杜比视界RPU As Boolean
    Public ReadOnly Property 有杜比视界增强层 As Boolean
    Public ReadOnly Property 杜比视界增强层类型 As 杜比视界增强层类型
    Public ReadOnly Property 动态HDR元数据有效 As Boolean
    Public ReadOnly Property HDR回退有效 As Boolean
    Public ReadOnly Property 显示器最小亮度 As Single
    Public ReadOnly Property 显示器峰值尼特 As UInteger
    Public ReadOnly Property 显示器全屏峰值尼特 As UInteger
    Public ReadOnly Property HDR有效目标峰值尼特 As UInteger
End Class

Public Enum 定时文字对齐
    靠前 = 0
    居中 = 1
    靠后 = 2
End Enum

<Flags>
Public Enum 定时文字样式
    无 = 0
    粗体 = 1
    斜体 = 2
    下划线 = 4
    删除线 = 8
    HDR高亮位图 = 16
End Enum

Public NotInheritable Class 定时文字命令
    Friend Sub New()
    End Sub

    Friend Property 是位图 As Boolean
    Friend Property 位图像素BGRA As Byte()
    Friend Property 位图宽度 As Integer
    Friend Property 位图高度 As Integer
    Friend Property 位图行跨度 As Integer

    Public Property 文本 As String = String.Empty
    Public Property 字体 As String = "Segoe UI"
    Public Property 字号 As Single
    Public Property 样式 As 定时文字样式
    Public Property 前景色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 描边色ARGB As UInteger = &HFF000000UI
    Public Property 描边宽度 As Single
    Public Property 阴影色ARGB As UInteger
    Public Property 阴影X偏移 As Single
    Public Property 阴影Y偏移 As Single
    Public Property X As Single
    Public Property Y As Single
    Public Property 宽度 As Single
    Public Property 高度 As Single
    Public Property 水平对齐 As 定时文字对齐
    Public Property 垂直对齐 As 定时文字对齐
    Public Property 内容标识 As ULong

    Public Shared Function 创建文字(文本 As String, 字体 As String, 字号 As Single,
                                  区域 As RectangleF, 前景色ARGB As UInteger,
                                  描边色ARGB As UInteger, 描边宽度 As Single,
                                  水平对齐 As 定时文字对齐, 垂直对齐 As 定时文字对齐,
                                  Optional 样式 As 定时文字样式 = 定时文字样式.无,
                                  Optional 内容标识 As ULong = 0,
                                  Optional 阴影色ARGB As UInteger = 0,
                                  Optional 阴影X偏移 As Single = 0,
                                  Optional 阴影Y偏移 As Single = 0) As 定时文字命令
        Dim result As New 定时文字命令()
        result.设置文字(文本, 字体, 字号, 区域, 前景色ARGB, 描边色ARGB, 描边宽度,
                    水平对齐, 垂直对齐, 样式, 内容标识,
                    阴影色ARGB, 阴影X偏移, 阴影Y偏移)
        Return result
    End Function

    Friend Sub 设置文字(文本值 As String, 字体值 As String, 字号值 As Single,
                      区域 As RectangleF, 前景色值 As UInteger, 描边色值 As UInteger,
                      描边宽度值 As Single, 水平对齐值 As 定时文字对齐,
                      垂直对齐值 As 定时文字对齐, 样式值 As 定时文字样式,
                      Optional 内容标识值 As ULong = 0,
                      Optional 阴影色值 As UInteger = 0,
                      Optional 阴影X值 As Single = 0,
                      Optional 阴影Y值 As Single = 0)
        是位图 = False
        位图像素BGRA = Nothing
        位图宽度 = 0 : 位图高度 = 0 : 位图行跨度 = 0
        文本 = If(文本值, String.Empty)
        字体 = If(字体值, "Segoe UI")
        字号 = 字号值
        X = 区域.X : Y = 区域.Y : 宽度 = 区域.Width : 高度 = 区域.Height
        前景色ARGB = 前景色值 : 描边色ARGB = 描边色值 : 描边宽度 = 描边宽度值
        阴影色ARGB = 阴影色值 : 阴影X偏移 = 阴影X值 : 阴影Y偏移 = 阴影Y值
        水平对齐 = 水平对齐值 : 垂直对齐 = 垂直对齐值 : 样式 = 样式值
        内容标识 = If(内容标识值 <> 0, 内容标识值, 计算文字内容标识(文本, 字体))
    End Sub

    Private Shared Function 计算文字内容标识(文本 As String, 字体 As String) As ULong
        ' 内容标识只描述不可变的文字负载；位置、大小和样式由原生布局键另行混合。
        ' 因而滚动弹幕逐帧只改变 X/Y 时，可以安全复用 DirectWrite 布局。
        Dim hash As ULong = &HCBF29CE484222325UL
        混合文字内容(hash, 文本)
        混合文字内容(hash, 字体)
        Return If(hash = 0, 1UL, hash)
    End Function

    Private Shared Sub 混合文字内容(ByRef hash As ULong, value As String)
        For Each character In value
            hash = Numerics.BitOperations.RotateLeft(hash, 7) Xor CULng(AscW(character) And &HFFFF&)
        Next
        hash = Numerics.BitOperations.RotateLeft(hash, 7) Xor &HFFFFUL
    End Sub

    Public Shared Function 创建位图(像素BGRA As Byte(), 位图宽度 As Integer, 位图高度 As Integer,
                                   行跨度 As Integer, 区域 As RectangleF,
                                   Optional 内容标识 As ULong = 0,
                                   Optional HDR高亮 As Boolean = False) As 定时文字命令
        ArgumentNullException.ThrowIfNull(像素BGRA)
        Dim result As New 定时文字命令()
        result.设置位图(像素BGRA, 位图宽度, 位图高度, 行跨度, 区域, 内容标识, HDR高亮)
        Return result
    End Function

    Friend Sub 设置位图(像素BGRA As Byte(), 位图宽度值 As Integer, 位图高度值 As Integer,
                       行跨度值 As Integer, 区域 As RectangleF, 内容标识值 As ULong,
                       Optional HDR高亮值 As Boolean = False)
        ArgumentNullException.ThrowIfNull(像素BGRA)
        是位图 = True : 位图像素BGRA = 像素BGRA
        位图宽度 = 位图宽度值 : 位图高度 = 位图高度值 : 位图行跨度 = 行跨度值
        X = 区域.X : Y = 区域.Y : 宽度 = 区域.Width : 高度 = 区域.Height
        ' 池对象可能上一帧是文字命令；清掉所有引用和值，避免旧字幕负载
        ' 被位图命令无谓保活，也避免以后新增原生字段时读到陈旧状态。
        文本 = String.Empty : 字体 = String.Empty : 字号 = 0
        前景色ARGB = 0 : 描边色ARGB = 0 : 描边宽度 = 0
        阴影色ARGB = 0 : 阴影X偏移 = 0 : 阴影Y偏移 = 0
        水平对齐 = 定时文字对齐.靠前 : 垂直对齐 = 定时文字对齐.靠前
        样式 = If(HDR高亮值, 定时文字样式.HDR高亮位图, 定时文字样式.无)
        内容标识 = 内容标识值
    End Sub
End Class

Public NotInheritable Class 定时文字状态
    Friend Sub New(值 As 原生定时文字状态)
        已提交序号 = 值.已提交序号
        已绘制序号 = 值.已绘制序号
        命令数 = CInt(值.命令数)
        画布大小 = New Size(CInt(值.画布宽度), CInt(值.画布高度))
        图层呈现帧数 = 值.图层呈现帧数
        可见像素数 = 值.可见像素数
        精灵缓存命中次数 = 值.精灵缓存命中次数
        精灵缓存未命中次数 = 值.精灵缓存未命中次数
        后备缓冲获取次数 = 值.后备缓冲获取次数
        合成像素着色器调用次数 = 值.合成像素着色器调用次数
    End Sub
    Public ReadOnly Property 已提交序号 As ULong
    Public ReadOnly Property 已绘制序号 As ULong
    Public ReadOnly Property 命令数 As Integer
    Public ReadOnly Property 画布大小 As Size
    Public ReadOnly Property 图层呈现帧数 As UInteger
    Public ReadOnly Property 可见像素数 As ULong
    Public ReadOnly Property 精灵缓存命中次数 As ULong
    Public ReadOnly Property 精灵缓存未命中次数 As ULong
    Public ReadOnly Property 后备缓冲获取次数 As ULong
    Public ReadOnly Property 合成像素着色器调用次数 As ULong
End Class

Public NotInheritable Class 播放器弹幕事件参数
    Inherits EventArgs

    Friend Sub New(路径值 As String, 数量值 As Integer)
        路径 = 路径值
        数量 = 数量值
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 数量 As Integer
End Class

Public NotInheritable Class 媒体信息
    <JsonPropertyName("format")>
    Public Property 格式 As String = String.Empty
    <JsonPropertyName("formatLongName")>
    Public Property 格式全名 As String = String.Empty
    <JsonPropertyName("formatCodecId")>
    Public Property 格式编码ID As String = String.Empty
    <JsonPropertyName("compatibleBrands")>
    Public Property 兼容品牌 As String = String.Empty
    <JsonPropertyName("duration100ns")>
    Public Property 时长100纳秒 As Long
    <JsonPropertyName("startTime100ns")>
    Public Property 开始时间100纳秒 As Long
    <JsonPropertyName("bitRate")>
    Public Property 比特率 As Long
    <JsonPropertyName("fileSize")>
    Public Property 文件大小 As Long
    <JsonPropertyName("probeScore")>
    Public Property 探测可信度 As Integer
    <JsonPropertyName("metadata")>
    Public Property 元数据 As Dictionary(Of String, String) = New Dictionary(Of String, String)(StringComparer.OrdinalIgnoreCase)
    <JsonPropertyName("streams")>
    Public Property 流 As List(Of 媒体流信息) = New List(Of 媒体流信息)()
    <JsonIgnore>
    Public ReadOnly Property 时长 As TimeSpan
        Get
            Return TimeSpan.FromTicks(时长100纳秒)
        End Get
    End Property
End Class

Public NotInheritable Class 媒体流信息
    <JsonPropertyName("index")>
    Public Property 索引 As Integer
    <JsonPropertyName("streamId")>
    Public Property 流ID As Integer
    <JsonPropertyName("type")>
    Public Property 类型 As String = String.Empty
    <JsonPropertyName("codec")>
    Public Property 编码 As String = String.Empty
    <JsonPropertyName("codecLongName")>
    Public Property 编码全名 As String = String.Empty
    <JsonPropertyName("codecTag")>
    Public Property 编码标签 As String = String.Empty
    <JsonPropertyName("timeBaseNumerator")>
    Public Property 时间基分子 As Integer
    <JsonPropertyName("timeBaseDenominator")>
    Public Property 时间基分母 As Integer
    <JsonPropertyName("bitRate")>
    Public Property 比特率 As Long
    <JsonPropertyName("streamSize")>
    Public Property 流大小 As Long
    <JsonPropertyName("lossless")>
    Public Property 无损 As Boolean
    <JsonPropertyName("startTime100ns")>
    Public Property 开始时间100纳秒 As Long
    <JsonPropertyName("duration100ns")>
    Public Property 时长100纳秒 As Long
    <JsonPropertyName("frames")>
    Public Property 帧数 As Long
    <JsonPropertyName("extradataSize")>
    Public Property 编码附加数据字节数 As Integer
    <JsonPropertyName("default")>
    Public Property 是默认流 As Boolean
    <JsonPropertyName("forced")>
    Public Property 是强制流 As Boolean
    <JsonPropertyName("disposition")>
    Public Property 特性 As String = String.Empty
    <JsonPropertyName("metadata")>
    Public Property 元数据 As Dictionary(Of String, String) = New Dictionary(Of String, String)(StringComparer.OrdinalIgnoreCase)
    <JsonPropertyName("profile")>
    Public Property 配置档次 As String = String.Empty
    <JsonPropertyName("width")>
    Public Property 宽度 As Integer
    <JsonPropertyName("height")>
    Public Property 高度 As Integer
    <JsonPropertyName("averageFrameRateNumerator")>
    Public Property 平均帧率分子 As Integer
    <JsonPropertyName("averageFrameRateDenominator")>
    Public Property 平均帧率分母 As Integer
    <JsonPropertyName("nominalFrameRateNumerator")>
    Public Property 标称帧率分子 As Integer
    <JsonPropertyName("nominalFrameRateDenominator")>
    Public Property 标称帧率分母 As Integer
    <JsonPropertyName("frameRateMode")>
    Public Property 帧率模式 As String = String.Empty
    <JsonPropertyName("sampleAspectNumerator")>
    Public Property 采样宽高比分子 As Integer
    <JsonPropertyName("sampleAspectDenominator")>
    Public Property 采样宽高比分母 As Integer
    <JsonPropertyName("displayAspectNumerator")>
    Public Property 显示宽高比分子 As Integer
    <JsonPropertyName("displayAspectDenominator")>
    Public Property 显示宽高比分母 As Integer
    <JsonIgnore>
    Public ReadOnly Property 平均帧率 As Double
        Get
            Return If(平均帧率分母 > 0, CDbl(平均帧率分子) / 平均帧率分母, 0.0R)
        End Get
    End Property
    <JsonIgnore>
    Public ReadOnly Property 标称帧率 As Double
        Get
            Return If(标称帧率分母 > 0, CDbl(标称帧率分子) / 标称帧率分母, 0.0R)
        End Get
    End Property
    <JsonPropertyName("hdr")>
    Public Property 是HDR As Boolean
    <JsonPropertyName("attachedPicture")>
    Public Property 是封面图 As Boolean
    <JsonPropertyName("pixelFormat")>
    Public Property 像素格式 As String = String.Empty
    <JsonPropertyName("colorModel")>
    Public Property 色彩模型 As String = String.Empty
    <JsonPropertyName("chromaSubsampling")>
    Public Property 色度抽样 As String = String.Empty
    <JsonPropertyName("bitDepth")>
    Public Property 位深度 As Integer
    <JsonPropertyName("decoderPixelFormat")>
    Public Property 解码输出像素格式 As String = String.Empty
    <JsonPropertyName("decoderSurfaceFormat")>
    Public Property 解码表面像素格式 As String = String.Empty
    <JsonPropertyName("decoderBitDepth")>
    Public Property 解码输出位深度 As Integer
    <JsonPropertyName("hardwareAcceleration")>
    Public Property 硬件加速 As String = String.Empty
    <JsonPropertyName("colorRange")>
    Public Property 色彩范围 As Integer
    <JsonPropertyName("colorSpace")>
    Public Property 色彩空间 As Integer
    <JsonPropertyName("colorPrimaries")>
    Public Property 色彩原色 As Integer
    <JsonPropertyName("colorTransfer")>
    Public Property 色彩传递 As Integer
    <JsonPropertyName("chromaLocation")>
    Public Property 色度位置 As Integer
    <JsonPropertyName("fieldOrder")>
    Public Property 场序 As Integer
    <JsonPropertyName("level")>
    Public Property 编码级别 As Integer
    <JsonPropertyName("hdrFormat")>
    Public Property HDR格式 As String = String.Empty
    <JsonPropertyName("hdrCompatibility")>
    Public Property HDR兼容规格 As String = String.Empty
    <JsonPropertyName("hdrProcessingPath")>
    Public Property HDR处理说明 As String = String.Empty
    <JsonPropertyName("dolbyVisionProfile")>
    Public Property 杜比视界配置档次 As Integer
    <JsonPropertyName("dolbyVisionLevel")>
    Public Property 杜比视界级别 As Integer
    <JsonPropertyName("dolbyVisionRpu")>
    Public Property 有杜比视界RPU As Boolean
    <JsonPropertyName("dolbyVisionEnhancementLayer")>
    Public Property 杜比视界增强层 As String = String.Empty
    <JsonPropertyName("hdrFallback")>
    Public Property HDR回退 As Boolean
    <JsonPropertyName("dynamicHdrMetadata")>
    Public Property 动态HDR元数据 As Boolean
    <JsonPropertyName("masteringPrimaries")>
    Public Property 主显示器色域 As String = String.Empty
    <JsonPropertyName("masteringMinLuminance")>
    Public Property 主显示器最小亮度 As Double
    <JsonPropertyName("masteringMaxLuminance")>
    Public Property 主显示器最大亮度 As Double
    <JsonPropertyName("maxCLL")>
    Public Property 最大内容光照 As Integer
    <JsonPropertyName("maxFALL")>
    Public Property 最大帧平均光照 As Integer
    <JsonPropertyName("codecConfigurationBox")>
    Public Property 编码配置盒 As String = String.Empty
    <JsonPropertyName("sampleRate")>
    Public Property 采样率 As Integer
    <JsonPropertyName("channels")>
    Public Property 声道数 As Integer
    <JsonPropertyName("channelLayout")>
    Public Property 声道布局 As String = String.Empty
    <JsonPropertyName("sampleFormat")>
    Public Property 采样格式 As String = String.Empty
    <JsonPropertyName("bitsPerCodedSample")>
    Public Property 编码采样位数 As Integer
    <JsonPropertyName("rawSampleBits")>
    Public Property 原始采样位数 As Integer
    <JsonPropertyName("compressionMode")>
    Public Property 压缩模式 As String = String.Empty
    <JsonPropertyName("md5")>
    Public Property 未压缩内容MD5 As String = String.Empty
    <JsonPropertyName("frameSize")>
    Public Property 每帧采样数 As Integer
    <JsonPropertyName("initialPadding")>
    Public Property 起始填充采样数 As Integer
    <JsonPropertyName("trailingPadding")>
    Public Property 末尾填充采样数 As Integer
    <JsonPropertyName("seekPreroll")>
    Public Property 跳转预卷采样数 As Integer
    <JsonPropertyName("outputSampleRate")>
    Public Property 输出采样率 As Integer
    <JsonPropertyName("outputChannels")>
    Public Property 输出声道数 As Integer
    <JsonPropertyName("outputBitsPerSample")>
    Public Property 输出采样位数 As Integer
    <JsonPropertyName("outputValidBitsPerSample")>
    Public Property 输出有效采样位数 As Integer
    <JsonPropertyName("outputFloat")>
    Public Property 输出浮点 As Boolean
    <JsonPropertyName("language")>
    Public Property 语言 As String = String.Empty
    <JsonPropertyName("title")>
    Public Property 标题 As String = String.Empty
End Class

Public NotInheritable Class 播放器事件参数
    Inherits EventArgs
    Friend Sub New(类型值 As 播放器事件类型, 详情值 As String)
        类型 = 类型值
        详情JSON = If(详情值, "{}")
    End Sub
    Public ReadOnly Property 类型 As 播放器事件类型
    Public ReadOnly Property 详情JSON As String
End Class

Public NotInheritable Class 播放器异常
    Inherits InvalidOperationException
    Friend Sub New(结果码值 As Integer, 消息 As String)
        MyBase.New(消息)
        结果码 = 结果码值
    End Sub
    Public ReadOnly Property 结果码 As Integer
End Class
