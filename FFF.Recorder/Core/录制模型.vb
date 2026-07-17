Public Enum 录制会话状态 As UInteger
    已创建 = 0
    录制中 = 1
    已暂停 = 2
    正在停止 = 3
    已停止 = 4
    已失败 = 5
    已中止 = 6
End Enum

Public Enum 视频纹理格式 As UInteger
    BGRA八位 = 0
    RGBA八位 = 1
    RGB十位 = 2
End Enum

Public Enum 视频采样格式 As UInteger
    YUV四二零 = 0
    YUV四四四 = 1
End Enum

Public Enum 编码速率控制 As UInteger
    可变码率 = 0
    恒定质量 = 1
    恒定码率 = 2
End Enum

Public Enum 编码多遍模式 As UInteger
    禁用 = 0
    四分之一分辨率 = 1
    完整分辨率 = 2
End Enum

Public Enum 视频色彩范围 As UInteger
    自动 = 0
    有限 = 1
    完整 = 2
End Enum

Public Enum 视频缩放方式 As UInteger
    适应 = 0
    填充裁剪 = 1
    拉伸 = 2
End Enum

Public Enum 视频旋转方式 As UInteger
    不旋转 = 0
    顺时针九十度 = 1
    旋转一百八十度 = 2
    顺时针二百七十度 = 3
End Enum

Public NotInheritable Class 视频处理配置
    Public Property 输出宽度 As UInteger = 1920
    Public Property 输出高度 As UInteger = 1080
    Public Property 输出HDR10 As Boolean
    Public Property 输出十位SDR As Boolean
    Public Property 允许HDR转SDR As Boolean
    Public Property 缩放方式 As 视频缩放方式 = 视频缩放方式.适应
    Public Property 裁剪左边 As UInteger
    Public Property 裁剪顶边 As UInteger
    Public Property 裁剪右边 As UInteger
    Public Property 裁剪底边 As UInteger
    Public Property 参考白尼特 As Single = 80.0F
    Public Property 目标峰值尼特 As Single = 1000.0F
    Public Property 曝光 As Single
    Public Property 高光压缩 As Single = 0.25F
    Public Property 饱和度 As Single = 1.0F

    Public Sub 验证()
        If 输出宽度 = 0 OrElse 输出高度 = 0 Then Throw New ArgumentOutOfRangeException(NameOf(输出宽度), "输出尺寸必须大于零。")
        If 参考白尼特 <= 0 OrElse 目标峰值尼特 < 参考白尼特 Then
            Throw New ArgumentOutOfRangeException(NameOf(目标峰值尼特), "目标峰值必须不低于参考白。")
        End If
        If 高光压缩 < 0 OrElse 高光压缩 > 1 Then Throw New ArgumentOutOfRangeException(NameOf(高光压缩))
        If 饱和度 < 0 OrElse 饱和度 > 4 Then Throw New ArgumentOutOfRangeException(NameOf(饱和度))
        If 输出HDR10 AndAlso 输出十位SDR Then Throw New ArgumentException("HDR10 与十位 SDR 不能同时选择。")
    End Sub
End Class

Public NotInheritable Class 录制配置
    Public Property 输出文件 As String = String.Empty
    Public Property 编码器名称 As String = "h264_nvenc"
    Public Property 宽度 As UInteger = 1920
    Public Property 高度 As UInteger = 1080
    Public Property 帧率分子 As UInteger = 60
    Public Property 帧率分母 As UInteger = 1
    Public Property 视频码率 As Long = 20_000_000
    Public Property 关键帧间隔 As UInteger = 120
    Public Property B帧数量 As UInteger = 2
    Public Property 使用十位色 As Boolean
    Public Property 使用HDR10 As Boolean
    Public Property 系统音频端点标识 As String = String.Empty
    Public Property 麦克风端点标识 As String = String.Empty
    Public Property 保留独立音轨 As Boolean = True
    Public Property 输入纹理格式 As 视频纹理格式 = 视频纹理格式.BGRA八位
    Public Property 视频采样 As 视频采样格式 = 视频采样格式.YUV四二零
    Public Property 速率控制 As 编码速率控制 = 编码速率控制.可变码率
    Public Property 质量值 As Integer = 23
    Public Property 最大码率 As Long = 30_000_000
    Public Property 前瞻帧数 As UInteger
    Public Property 编码预设 As String = "p4"
    Public Property 编码配置档 As String = String.Empty
    Public Property 多遍模式 As 编码多遍模式 = 编码多遍模式.禁用
    Public Property 色彩范围 As 视频色彩范围 = 视频色彩范围.自动
    Public Property 系统音频增益 As Single = 1.0F
    Public Property 麦克风增益 As Single = 1.0F
    Public Property 静音系统音频 As Boolean
    Public Property 静音麦克风 As Boolean
    Public Property 诊断日志文件 As String = String.Empty
    Public Property 捕获后端 As String = String.Empty
    Public Property 捕获源说明 As String = String.Empty
    Public Property 捕获源格式 As String = String.Empty

    Public Sub 验证()
        If String.IsNullOrWhiteSpace(输出文件) Then Throw New ArgumentException("必须指定输出文件。", NameOf(输出文件))
        If Not String.Equals(IO.Path.GetExtension(输出文件), ".mkv", StringComparison.OrdinalIgnoreCase) Then
            Throw New ArgumentException("输出容器固定为 MKV，文件扩展名必须是 .mkv。", NameOf(输出文件))
        End If
        If String.IsNullOrWhiteSpace(编码器名称) Then Throw New ArgumentException("必须指定编码器名称。", NameOf(编码器名称))
        If 宽度 = 0 OrElse 高度 = 0 Then Throw New ArgumentOutOfRangeException(NameOf(宽度), "输出尺寸必须大于零。")
        If 帧率分子 = 0 OrElse 帧率分母 = 0 Then Throw New ArgumentOutOfRangeException(NameOf(帧率分子), "帧率必须大于零。")
        If 使用HDR10 AndAlso Not 使用十位色 Then Throw New ArgumentException("HDR10 必须使用十位色输入。", NameOf(使用HDR10))
        If 视频采样 = 视频采样格式.YUV四二零 AndAlso ((宽度 And 1UI) <> 0 OrElse (高度 And 1UI) <> 0) Then
            Throw New ArgumentException("4:2:0 输出的宽度和高度必须是偶数。", NameOf(视频采样))
        End If
        If 视频码率 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(视频码率), "视频码率必须大于零。")
        If 最大码率 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(最大码率), "最大码率不能小于零。")
        If 质量值 < -1 OrElse 质量值 > 63 Then Throw New ArgumentOutOfRangeException(NameOf(质量值), "质量值必须在 -1 到 63 之间。")
        If 前瞻帧数 > 64 Then Throw New ArgumentOutOfRangeException(NameOf(前瞻帧数), "前瞻帧数不能超过 64。")
        If 系统音频增益 < 0 OrElse 系统音频增益 > 8 Then Throw New ArgumentOutOfRangeException(NameOf(系统音频增益))
        If 麦克风增益 < 0 OrElse 麦克风增益 > 8 Then Throw New ArgumentOutOfRangeException(NameOf(麦克风增益))
    End Sub
End Class

Public NotInheritable Class 音频端点信息
    Public Property 类型 As String = String.Empty
    Public Property 标识 As String = String.Empty
    Public Property 名称 As String = String.Empty
End Class

Public NotInheritable Class 编码器探测结果
    Public Property 支持 As Boolean
    Public Property 编码器名称 As String = String.Empty
    Public Property 原因 As String = String.Empty
    Public Property 适配器标识 As String = String.Empty
    Public Property 使用十位色 As Boolean
    Public Property 视频采样 As 视频采样格式
    Public Property 速率控制 As 编码速率控制
    Public Property 编码预设 As String = String.Empty
    Public Property 编码配置档 As String = String.Empty
    Public Property 多遍模式 As 编码多遍模式
End Class

Public NotInheritable Class 音频端点测试结果
    Public Property 通过 As Boolean
    Public Property 数据包数 As ULong
    Public Property 采样帧数 As ULong
    Public Property 静音包数 As ULong
    Public Property 不连续次数 As ULong
    Public Property 时间戳错误次数 As ULong
    Public Property 首设备位置 As ULong
    Public Property 末设备位置 As ULong
    Public Property 首QPC位置 As ULong
    Public Property 末QPC位置 As ULong
    Public Property 音频时钟频率 As ULong
    Public Property 采样率 As UInteger
    Public Property 声道数 As UInteger
    Public Property 位深 As UInteger
    Public Property 错误 As String = String.Empty
End Class

Public NotInheritable Class 录制诊断事件参数
    Inherits EventArgs

    Public Sub New(事件名称 As String, 详细信息JSON As String)
        Me.事件名称 = 事件名称
        Me.详细信息JSON = 详细信息JSON
    End Sub

    Public ReadOnly Property 事件名称 As String
    Public ReadOnly Property 详细信息JSON As String
End Class

Public Structure 录制统计
    Public Property 状态 As 录制会话状态
    Public Property 已提交帧数 As ULong
    Public Property 已丢弃帧数 As ULong
    Public Property 已重复帧数 As ULong
    Public Property 最后视频时间戳 As Long
    Public Property 累计暂停计数 As Long
    Public Property 最后错误码 As Integer
    Public Property 队列深度 As UInteger
    Public Property 最后编码微秒 As ULong
    Public Property 峰值编码微秒 As ULong
    Public Property 系统音频不连续次数 As ULong
    Public Property 麦克风不连续次数 As ULong
    Public Property 系统音频时间戳错误次数 As ULong
    Public Property 麦克风时间戳错误次数 As ULong
    Public Property 系统音频漂移微秒 As Long
    Public Property 麦克风漂移微秒 As Long
    Public Property 已写正常文件尾 As Boolean
    Public Property 峰值队列深度 As UInteger
    Public Property 最后写入微秒 As ULong
    Public Property 峰值写入微秒 As ULong
    Public Property 系统音频时间线误差微秒 As Long
    Public Property 麦克风时间线误差微秒 As Long
    Public Property 系统音频补偿PPM As Integer
    Public Property 麦克风补偿PPM As Integer
End Structure

Public NotInheritable Class 原生调用异常
    Inherits Exception

    Public Sub New(错误码 As Integer, 消息 As String)
        MyBase.New($"原生录制模块调用失败（{错误码}）：{消息}")
        Me.错误码 = 错误码
    End Sub

    Public ReadOnly Property 错误码 As Integer
End Class
