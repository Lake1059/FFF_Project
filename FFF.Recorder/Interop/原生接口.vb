Imports System.Runtime.InteropServices
Imports Microsoft.Win32.SafeHandles

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Sub 原生诊断回调(上下文 As IntPtr, 事件名称UTF8 As IntPtr, 详细信息UTF8 As IntPtr)

Friend Enum 原生结果 As Integer
    成功 = 0
    参数无效 = -1
    状态无效 = -2
    缓冲区不足 = -3
    原生失败 = -4
    FFmpeg失败 = -5
    设备失败 = -6
    不支持 = -7
End Enum

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生会话配置
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public D3D11设备 As IntPtr
    Public 输出路径UTF8 As IntPtr
    Public 编码器名称UTF8 As IntPtr
    Public 宽度 As UInteger
    Public 高度 As UInteger
    Public 帧率分子 As UInteger
    Public 帧率分母 As UInteger
    Public 视频码率 As Long
    Public 关键帧间隔 As UInteger
    Public B帧数量 As UInteger
    Public 十位色 As UInteger
    Public HDR10 As UInteger
    Public 系统音频端点标识UTF8 As IntPtr
    Public 麦克风端点标识UTF8 As IntPtr
    Public 保留独立音轨 As UInteger
    Public 输入纹理格式 As UInteger
    Public 视频采样 As UInteger
    Public 速率控制 As UInteger
    Public 质量值 As Integer
    Public 最大码率 As Long
    Public 前瞻帧数 As UInteger
    Public 预设UTF8 As IntPtr
    Public 配置档UTF8 As IntPtr
    Public 多遍模式 As UInteger
    Public 色彩范围 As UInteger
    Public 系统音频增益 As Single
    Public 麦克风增益 As Single
    Public 静音系统音频 As UInteger
    Public 静音麦克风 As UInteger
    Public 诊断日志路径UTF8 As IntPtr
    Public 捕获后端UTF8 As IntPtr
    Public 源说明UTF8 As IntPtr
    Public 源格式UTF8 As IntPtr
    Public 诊断回调 As IntPtr
    Public 诊断回调上下文 As IntPtr
    Public 音频编码器名称UTF8 As IntPtr
    Public 音频采样率 As UInteger
    Public 音频声道数 As UInteger
    Public 音频码率 As Long
    Public 音频模式 As UInteger
    Public 场景优化UTF8 As IntPtr
    Public 跟随默认系统音频设备 As UInteger
    Public 质量控制模式 As UInteger
    Public 自定义视频参数UTF8 As IntPtr
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生会话统计
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 状态 As UInteger
    Public 保留 As UInteger
    Public 已提交帧数 As ULong
    Public 已丢弃帧数 As ULong
    Public 已重复帧数 As ULong
    Public 最后视频QPC As Long
    Public 暂停QPC As Long
    Public 最后错误码 As Integer
    Public 队列深度 As UInteger
    Public 最后编码微秒 As ULong
    Public 峰值编码微秒 As ULong
    Public 系统音频不连续次数 As ULong
    Public 麦克风不连续次数 As ULong
    Public 系统音频时间戳错误次数 As ULong
    Public 麦克风时间戳错误次数 As ULong
    Public 系统音频漂移微秒 As Long
    Public 麦克风漂移微秒 As Long
    Public 已写正常文件尾 As UInteger
    Public 保留二 As UInteger
    Public 峰值队列深度 As UInteger
    Public 保留三 As UInteger
    Public 最后写入微秒 As ULong
    Public 峰值写入微秒 As ULong
    Public 系统音频时间线误差微秒 As Long
    Public 麦克风时间线误差微秒 As Long
    Public 系统音频补偿PPM As Integer
    Public 麦克风补偿PPM As Integer
    Public 视频字节数 As ULong
    Public 音频字节数 As ULong
    Public 音频声道数 As UInteger
    Public 音频声道掩码 As UInteger
    Public 系统音频峰值 As Single
    Public 麦克风峰值 As Single
End Structure

Friend NotInheritable Class 原生会话句柄
    Inherits SafeHandleZeroOrMinusOneIsInvalid

    Private Sub New()
        MyBase.New(True)
    End Sub

    Friend Sub New(原生指针 As IntPtr)
        MyBase.New(True)
        SetHandle(原生指针)
    End Sub

    Protected Overrides Function ReleaseHandle() As Boolean
        原生接口.FFF_DestroySession(handle)
        Return True
    End Function
End Class

Friend Module 原生接口
    Friend Const 动态库名称 As String = "FFF.Native.dll"

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_GetApiVersion() As UInteger
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_RunSelfTest(输出 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_GetRuntimeInfo(输出 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_EnumerateAudioEndpoints(输出 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_TestAudioEndpoint(端点标识 As IntPtr, 是否回环 As UInteger, 测试毫秒 As UInteger,
        输出 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_ProbeEncoder(编码器名称 As IntPtr, 宽度 As UInteger, 高度 As UInteger,
        帧率分子 As UInteger, 帧率分母 As UInteger, 输出 As IntPtr, 输出大小 As UInteger,
        ByRef 所需大小 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_ProbeD3D11Encoder(ByRef 配置 As 原生会话配置, 输出 As IntPtr,
        输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_CreateSession(ByRef 配置 As 原生会话配置, ByRef 会话 As IntPtr) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_StartSession(会话 As 原生会话句柄) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_SubmitVideoTexture(会话 As 原生会话句柄, 纹理 As IntPtr,
        纹理数组索引 As UInteger, QPC时间戳 As Long, 提交标志 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_ReportDroppedVideoFrames(会话 As 原生会话句柄,
        帧数 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_ReportDiagnosticEvent(会话 As 原生会话句柄, 事件名称UTF8 As IntPtr,
        消息UTF8 As IntPtr) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_PauseSession(会话 As 原生会话句柄, QPC时间戳 As Long) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_ResumeSession(会话 As 原生会话句柄, QPC时间戳 As Long) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_SplitSession(会话 As 原生会话句柄, 输出路径UTF8 As IntPtr) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_SwitchSystemAudioEndpoint(会话 As 原生会话句柄, 端点标识UTF8 As IntPtr) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_StopSession(会话 As 原生会话句柄) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_AbortSession(会话 As 原生会话句柄) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_GetSessionStatistics(会话 As 原生会话句柄, ByRef 统计 As 原生会话统计) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF_GetLastError(会话 As 原生会话句柄, 输出 As IntPtr,
        输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Sub FFF_DestroySession(会话 As IntPtr)
    End Sub
End Module
