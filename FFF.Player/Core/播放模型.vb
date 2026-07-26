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
    Public Property HDR峰值尼特 As Single = 1000.0F
    Public Property SDR纸白尼特 As Single = 203.0F
    Public Property 输出窗口句柄 As IntPtr
    Public Property 音频端点标识 As String = String.Empty
    Public Property 事件同步上下文 As Threading.SynchronizationContext

    Friend Sub 验证()
        If 解码器 = 解码模式.未指定 Then Throw New ArgumentException("必须由调用方明确选择 CPU 或 GPU 解码。", NameOf(解码器))
        If 解码器 <> 解码模式.CPU AndAlso 解码器 <> 解码模式.GPU Then Throw New ArgumentOutOfRangeException(NameOf(解码器))
        If SDR峰值尼特 <= 0 OrElse Not Single.IsFinite(SDR峰值尼特) Then Throw New ArgumentOutOfRangeException(NameOf(SDR峰值尼特))
        If HDR峰值尼特 <= 0 OrElse HDR峰值尼特 > 10000 OrElse Not Single.IsFinite(HDR峰值尼特) Then Throw New ArgumentOutOfRangeException(NameOf(HDR峰值尼特))
        If SDR纸白尼特 <= 0 OrElse Not Single.IsFinite(SDR纸白尼特) Then Throw New ArgumentOutOfRangeException(NameOf(SDR纸白尼特))
    End Sub
End Class

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
End Class

Public NotInheritable Class 媒体信息
    <JsonPropertyName("format")>
    Public Property 格式 As String = String.Empty
    <JsonPropertyName("duration100ns")>
    Public Property 时长100纳秒 As Long
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
    <JsonPropertyName("type")>
    Public Property 类型 As String = String.Empty
    <JsonPropertyName("codec")>
    Public Property 编码 As String = String.Empty
    <JsonPropertyName("timeBaseNumerator")>
    Public Property 时间基分子 As Integer
    <JsonPropertyName("timeBaseDenominator")>
    Public Property 时间基分母 As Integer
    <JsonPropertyName("width")>
    Public Property 宽度 As Integer
    <JsonPropertyName("height")>
    Public Property 高度 As Integer
    <JsonPropertyName("hdr")>
    Public Property 是HDR As Boolean
    <JsonPropertyName("attachedPicture")>
    Public Property 是封面图 As Boolean
    <JsonPropertyName("sampleRate")>
    Public Property 采样率 As Integer
    <JsonPropertyName("channels")>
    Public Property 声道数 As Integer
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
