Imports System
Imports System.Runtime.InteropServices

Friend Module 杜比视界状态文本
    Friend Const 摘要用途 As UInteger = 4UI
    Friend Const RPU用途 As UInteger = 5UI
    Friend Const 增强层用途 As UInteger = 6UI
    Friend Const 格式用途 As UInteger = 7UI
    Friend Const 路径用途 As UInteger = 8UI

    Friend Function 打包(快照 As 播放器快照) As UInteger
        Dim 值 = CUInt(Math.Clamp(快照.杜比视界配置档次, 0, 255)) Or
                 (CUInt(Math.Clamp(快照.杜比视界级别, 0, 255)) << 8)
        If 快照.有杜比视界RPU Then 值 = 值 Or &H10000UI
        If 快照.有杜比视界增强层 Then 值 = 值 Or &H20000UI
        值 = 值 Or ((CUInt(快照.杜比视界增强层类型) And 3UI) << 18)
        If 快照.HDR处理路径 = HDR处理路径.外部RPU处理 Then 值 = 值 Or &H100000UI
        Return 值
    End Function

    Friend Function 文案(用途 As UInteger, 快照 As 播放器快照) As String
        Try
            Dim 指针 = 播放器原生接口.FFF3FP_GetColorExtensionStatusText(用途, 打包(快照))
            If 指针 <> IntPtr.Zero Then Return Marshal.PtrToStringUTF8(指针)
        Catch
        End Try
        Return "Dolby Vision"
    End Function

    Friend Function 摘要(快照 As 播放器快照) As String
        Return 文案(摘要用途, 快照)
    End Function

    Friend Function 未选中摘要(流 As 媒体流信息) As String
        Dim 值 = CUInt(Math.Clamp(流.杜比视界配置档次, 0, 255)) Or
                 (CUInt(Math.Clamp(流.杜比视界级别, 0, 255)) << 8) Or &H200000UI
        Dim 指针 = 播放器原生接口.FFF3FP_GetColorExtensionStatusText(摘要用途, 值)
        If 指针 <> IntPtr.Zero Then Return Marshal.PtrToStringUTF8(指针)
        Return "Dolby Vision"
    End Function
End Module
