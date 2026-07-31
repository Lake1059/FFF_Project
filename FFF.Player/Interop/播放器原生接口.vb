Imports System.Runtime.InteropServices
Imports Microsoft.Win32.SafeHandles

Friend Enum 原生播放器结果 As Integer
    成功 = 0
    参数无效 = -1
    状态无效 = -2
    缓冲区不足 = -3
    原生失败 = -4
    FFmpeg失败 = -5
    设备失败 = -6
    不支持 = -7
End Enum

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Sub 原生播放器回调(上下文 As IntPtr, 事件类型 As UInteger, 详情UTF8 As IntPtr)

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生播放器配置
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 输出窗口 As IntPtr
    Public 解码器 As UInteger
    Public 色彩模式 As UInteger
    Public SDR峰值 As Single
    Public HDR峰值 As Single
    Public SDR纸白 As Single
    Public 音频端点UTF8 As IntPtr
    Public 回调 As IntPtr
    Public 回调上下文 As IntPtr
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生播放器快照
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 状态 As UInteger
    Public 解码器 As UInteger
    Public 请求色彩模式 As UInteger
    Public 实际色彩模式 As UInteger
    Public 位置100纳秒 As Long
    Public 时长100纳秒 As Long
    Public 帧序号 As Long
    Public 原始帧PTS As Long
    Public 帧时间基分子 As Integer
    Public 帧时间基分母 As Integer
    Public 当前视频流 As Integer
    Public 当前音频流 As Integer
    Public 视频宽度 As UInteger
    Public 视频高度 As UInteger
    Public 是HDR源 As UInteger
    Public 正在使用外部音轨 As UInteger
    Public 外部音轨偏移100纳秒 As Long
    Public 已解码视频帧数 As ULong
    Public 已呈现视频帧数 As ULong
    Public 已丢弃视频帧数 As ULong
    Public 视频队列帧数 As UInteger
    Public 源峰值尼特 As UInteger
    Public 已解码音频帧数 As ULong
    Public 音频位置100纳秒 As Long
    Public 音频缓冲100纳秒 As Long
    Public 音频欠载次数 As ULong
    Public 音频时间戳抖动帧数 As ULong
    Public 音频不连续次数 As ULong
    Public 音频插入静音帧数 As ULong
    Public 音频丢弃重叠帧数 As ULong
    Public 已合并视频帧数 As ULong
    Public 音频拒绝帧数 As ULong
    Public 交换链呈现次数 As ULong
    Public 呈现等待100纳秒 As ULong
    Public 设备锁等待100纳秒 As ULong
    Public 硬件传输100纳秒 As ULong
    Public 软件转换100纳秒 As ULong
    Public 视频实时比特率 As ULong
    Public 音频实时比特率 As ULong
    Public 视频输出位深度 As UInteger
    Public 视频缩放模式 As UInteger
    Public 时间轴代次 As ULong
    Public HDR格式 As UInteger
    Public 兼容HDR格式 As UInteger
    Public HDR处理路径 As UInteger
    Public 杜比视界配置档次 As UInteger
    Public 杜比视界级别 As UInteger
    Public 有杜比视界RPU As UInteger
    Public 有杜比视界增强层 As UInteger
    Public 杜比视界增强层类型 As UInteger
    Public 动态HDR元数据有效 As UInteger
    Public HDR回退有效 As UInteger
    Public 显示器最小亮度毫尼特 As UInteger
    Public 显示器峰值尼特 As UInteger
    Public 显示器全屏峰值尼特 As UInteger
    Public HDR有效目标峰值尼特 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生视频像素探针
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public X As UInteger
    Public Y As UInteger
    Public 红 As Single
    Public 绿 As Single
    Public 蓝 As Single
    Public Alpha As Single
    Public 视频缩放模式 As UInteger
    Public 输出位深度 As UInteger
    Public 色彩模式 As UInteger
    Public 保留 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生音频峰值
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 声道数 As UInteger
    Public 保留 As UInteger
    Public 峰值1 As Single
    Public 峰值2 As Single
    Public 峰值3 As Single
    Public 峰值4 As Single
    Public 峰值5 As Single
    Public 峰值6 As Single
    Public 峰值7 As Single
    Public 峰值8 As Single
End Structure

<Flags>
Friend Enum 原生位图字幕标志 As UInteger
    无 = 0
    清除 = 1
    流结束 = 2
    强制 = 4
    仍需读取 = 8
End Enum

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生位图字幕帧
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 标志 As 原生位图字幕标志
    Public 保留 As UInteger
    Public 开始100纳秒 As Long
    Public 结束100纳秒 As Long
    Public 画布宽度 As Integer
    Public 画布高度 As Integer
    Public X As Integer
    Public Y As Integer
    Public 宽度 As Integer
    Public 高度 As Integer
    Public 行跨度 As Integer
    Public 像素字节数 As UInteger
    Public 序号 As Long
End Structure

Friend Enum 原生定时文字命令类型 As UInteger
    文字 = 1
    位图 = 2
End Enum

<Flags>
Friend Enum 原生定时文字标志 As UInteger
    无 = 0
    粗体 = 1
    斜体 = 2
    下划线 = 4
    删除线 = 8
End Enum

Friend Enum 原生定时文字对齐 As UInteger
    靠前 = 0
    居中 = 1
    靠后 = 2
End Enum

Friend Enum 原生定时文字图层槽位 As UInteger
    字幕 = 0
    弹幕 = 1
    播放器信息 = 2
End Enum

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生定时文字命令
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 类型 As 原生定时文字命令类型
    Public 标志 As 原生定时文字标志
    Public X As Single
    Public Y As Single
    Public 宽度 As Single
    Public 高度 As Single
    Public 前景色ARGB As UInteger
    Public 描边色ARGB As UInteger
    Public 字号 As Single
    Public 描边宽度 As Single
    Public 水平对齐 As 原生定时文字对齐
    Public 垂直对齐 As 原生定时文字对齐
    Public 文本UTF8 As IntPtr
    Public 字体UTF8 As IntPtr
    Public 位图BGRA As IntPtr
    Public 位图宽度 As UInteger
    Public 位图高度 As UInteger
    Public 位图行跨度 As UInteger
    Public 位图字节数 As UInteger
    Public 内容标识 As ULong
    Public 阴影色ARGB As UInteger
    Public 阴影X偏移 As Single
    Public 阴影Y偏移 As Single
    Public 保留 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生定时文字图层
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 画布宽度 As UInteger
    Public 画布高度 As UInteger
    Public 命令数 As UInteger
    Public 图层槽位 As 原生定时文字图层槽位
    Public 序号 As ULong
    Public 命令 As IntPtr
    Public 目标帧率 As Single
    Public 保留2 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生定时文字状态
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 已提交序号 As ULong
    Public 已绘制序号 As ULong
    Public 命令数 As UInteger
    Public 画布宽度 As UInteger
    Public 画布高度 As UInteger
    Public 图层呈现帧数 As UInteger
    Public 可见像素数 As ULong
    Public 精灵缓存命中次数 As ULong
    Public 精灵缓存未命中次数 As ULong
    Public 后备缓冲获取次数 As ULong
    Public 合成像素着色器调用次数 As ULong
End Structure

Friend NotInheritable Class 播放器原生句柄
    Inherits SafeHandleZeroOrMinusOneIsInvalid
    Private 回调句柄 As GCHandle

    Friend Sub New(原生指针 As IntPtr, 持有回调 As GCHandle)
        MyBase.New(True)
        SetHandle(原生指针)
        回调句柄 = 持有回调
    End Sub

    Protected Overrides Function ReleaseHandle() As Boolean
        播放器原生接口.FFF3FP_Destroy(handle)
        If 回调句柄.IsAllocated Then 回调句柄.Free()
        Return True
    End Function
End Class

Friend NotInheritable Class 位图字幕原生句柄
    Inherits SafeHandleZeroOrMinusOneIsInvalid

    Friend Sub New(原生指针 As IntPtr)
        MyBase.New(True)
        SetHandle(原生指针)
    End Sub

    Protected Overrides Function ReleaseHandle() As Boolean
        播放器原生接口.FFF3FP_DestroyBitmapSubtitle(handle)
        Return True
    End Function
End Class

Friend NotInheritable Class ASS字幕原生句柄
    Inherits SafeHandleZeroOrMinusOneIsInvalid

    Friend Sub New(原生指针 As IntPtr)
        MyBase.New(True)
        SetHandle(原生指针)
    End Sub

    Protected Overrides Function ReleaseHandle() As Boolean
        播放器原生接口.FFF3FP_DestroyAssSubtitle(handle)
        Return True
    End Function
End Class

Friend Module 播放器原生接口
    Friend Const 动态库名称 As String = "FFF.Native.dll"

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetApiVersion() As UInteger
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Create(ByRef 配置 As 原生播放器配置, ByRef 播放器 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Open(播放器 As 播放器原生句柄, 路径UTF8 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Play(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Pause(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Stop(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Close(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Seek(播放器 As 播放器原生句柄, 位置100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SeekKeyframe(播放器 As 播放器原生句柄, 位置100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SeekFrame(播放器 As 播放器原生句柄, 帧序号 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_StepFrame(播放器 As 播放器原生句柄, 方向 As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_StepKeyframe(播放器 As 播放器原生句柄, 方向 As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SelectVideoStream(播放器 As 播放器原生句柄, 流索引 As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SelectAudioStream(播放器 As 播放器原生句柄, 流索引 As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_LoadExternalAudio(播放器 As 播放器原生句柄, 路径UTF8 As IntPtr, 流索引 As Integer, 偏移100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_ClearExternalAudio(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetExternalAudioOffset(播放器 As 播放器原生句柄, 偏移100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetColorMode(播放器 As 播放器原生句柄, 模式 As UInteger, SDR峰值 As Single, HDR峰值 As Single, SDR纸白 As Single) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetOutputWindow(播放器 As 播放器原生句柄, 窗口 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetAudioEndpoint(播放器 As 播放器原生句柄, 端点UTF8 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetAudioExclusiveMode(播放器 As 播放器原生句柄, 独占 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetVolume(播放器 As 播放器原生句柄, 音量 As Single, 静音 As UInteger) As 原生播放器结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl)>
    Friend Function FFF3FP_SetTimedTextLayer(播放器 As 播放器原生句柄, ByRef 图层 As 原生定时文字图层) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetSnapshot(播放器 As 播放器原生句柄, ByRef 快照 As 原生播放器快照) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_ReadVideoPixel(播放器 As 播放器原生句柄, ByRef 探针 As 原生视频像素探针) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetAudioPeakLevels(播放器 As 播放器原生句柄, ByRef 峰值 As 原生音频峰值) As 原生播放器结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl)>
    Friend Function FFF3FP_GetTimedTextStatus(播放器 As 播放器原生句柄, ByRef 状态 As 原生定时文字状态) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl)>
    Friend Function FFF3FP_GetDanmakuStatus(播放器 As 播放器原生句柄, ByRef 状态 As 原生定时文字状态) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetMediaInfo(播放器 As 播放器原生句柄, 输出UTF8 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetLastError(播放器 As 播放器原生句柄, 输出UTF8 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Sub FFF3FP_Destroy(播放器 As IntPtr)
    End Sub
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_OpenBitmapSubtitle(路径UTF8 As IntPtr, 流索引 As Integer, ByRef 解码器 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_ReadBitmapSubtitle(解码器 As 位图字幕原生句柄, ByRef 帧 As 原生位图字幕帧) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_CopyBitmapSubtitlePixels(解码器 As 位图字幕原生句柄, 输出 As IntPtr, 输出大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SeekBitmapSubtitle(解码器 As 位图字幕原生句柄, 位置100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetBitmapSubtitleLastError(解码器 As 位图字幕原生句柄, 输出UTF8 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Sub FFF3FP_DestroyBitmapSubtitle(解码器 As IntPtr)
    End Sub
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_OpenAssSubtitle(路径UTF8 As IntPtr, 字体目录UTF8 As IntPtr,
                                            流索引 As Integer, ByRef 渲染器 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_RenderAssSubtitle(渲染器 As ASS字幕原生句柄, 位置100纳秒 As Long,
                                             画布宽度 As Integer, 画布高度 As Integer,
                                             ByRef 帧 As 原生位图字幕帧) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_CopyAssSubtitlePixels(渲染器 As ASS字幕原生句柄, 输出 As IntPtr,
                                                 输出大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetAssSubtitleLastError(渲染器 As ASS字幕原生句柄, 输出UTF8 As IntPtr,
                                                   输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Sub FFF3FP_DestroyAssSubtitle(渲染器 As IntPtr)
    End Sub
End Module
