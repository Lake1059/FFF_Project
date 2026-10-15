Imports System.Runtime.InteropServices

Friend Enum 音频数据流
    播放 = 0
    录音 = 1
    全部 = 2
End Enum

Friend Enum 音频设备角色
    控制台 = 0
    多媒体 = 1
    通讯 = 2
End Enum

<StructLayout(LayoutKind.Sequential)>
Friend Structure 音频属性键
    Public 格式标识 As Guid
    Public 属性标识 As UInteger
End Structure

<ComImport, Guid("7991EEC9-7E89-4D85-8390-6C703CEC60C0"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
Friend Interface I音频端点通知
    <PreserveSig>
    Function OnDeviceStateChanged(<MarshalAs(UnmanagedType.LPWStr)> 设备标识 As String, 新状态 As UInteger) As Integer
    <PreserveSig>
    Function OnDeviceAdded(<MarshalAs(UnmanagedType.LPWStr)> 设备标识 As String) As Integer
    <PreserveSig>
    Function OnDeviceRemoved(<MarshalAs(UnmanagedType.LPWStr)> 设备标识 As String) As Integer
    <PreserveSig>
    Function OnDefaultDeviceChanged(数据流 As 音频数据流, 角色 As 音频设备角色,
                                    <MarshalAs(UnmanagedType.LPWStr)> 默认设备标识 As String) As Integer
    <PreserveSig>
    Function OnPropertyValueChanged(<MarshalAs(UnmanagedType.LPWStr)> 设备标识 As String,
                                    属性键 As 音频属性键) As Integer
End Interface

<ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
Friend Interface I音频设备枚举器
    <PreserveSig>
    Function EnumAudioEndpoints(数据流 As 音频数据流, 状态掩码 As UInteger, ByRef 设备集合 As IntPtr) As Integer
    <PreserveSig>
    Function GetDefaultAudioEndpoint(数据流 As 音频数据流, 角色 As 音频设备角色, ByRef 设备 As IntPtr) As Integer
    <PreserveSig>
    Function GetDevice(<MarshalAs(UnmanagedType.LPWStr)> 设备标识 As String, ByRef 设备 As IntPtr) As Integer
    <PreserveSig>
    Function RegisterEndpointNotificationCallback(通知 As I音频端点通知) As Integer
    <PreserveSig>
    Function UnregisterEndpointNotificationCallback(通知 As I音频端点通知) As Integer
End Interface

Friend NotInheritable Class 音频设备监视器
    Implements I音频端点通知, IDisposable

    Private Shared ReadOnly 枚举器类标识 As New Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")
    Private 枚举器 As I音频设备枚举器
    Private 已释放 As Boolean

    Private Sub New()
        Dim 枚举器类型 = Type.GetTypeFromCLSID(枚举器类标识, True)
        枚举器 = DirectCast(Activator.CreateInstance(枚举器类型), I音频设备枚举器)
        Marshal.ThrowExceptionForHR(枚举器.RegisterEndpointNotificationCallback(Me))
    End Sub

    Public Event 设备已变更 As EventHandler

    Public Shared Function 创建() As 音频设备监视器
        Return New 音频设备监视器()
    End Function

    Private Sub 通知变更()
        If 已释放 Then Return
        Try
            RaiseEvent 设备已变更(Me, EventArgs.Empty)
        Catch
        End Try
    End Sub

    Private Function 设备状态改变(设备标识 As String, 新状态 As UInteger) As Integer Implements I音频端点通知.OnDeviceStateChanged
        通知变更()
        Return 0
    End Function

    Private Function 设备新增(设备标识 As String) As Integer Implements I音频端点通知.OnDeviceAdded
        通知变更()
        Return 0
    End Function

    Private Function 设备移除(设备标识 As String) As Integer Implements I音频端点通知.OnDeviceRemoved
        通知变更()
        Return 0
    End Function

    Private Function 默认设备改变(数据流 As 音频数据流, 角色 As 音频设备角色,
                                  默认设备标识 As String) As Integer Implements I音频端点通知.OnDefaultDeviceChanged
        If 数据流 = 音频数据流.播放 OrElse 数据流 = 音频数据流.全部 Then 通知变更()
        Return 0
    End Function

    Private Function 属性改变(设备标识 As String, 属性键 As 音频属性键) As Integer Implements I音频端点通知.OnPropertyValueChanged
        通知变更()
        Return 0
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        If 枚举器 IsNot Nothing Then
            Try
                枚举器.UnregisterEndpointNotificationCallback(Me)
            Catch
            End Try
            If Marshal.IsComObject(枚举器) Then Marshal.FinalReleaseComObject(枚举器)
            枚举器 = Nothing
        End If
        GC.SuppressFinalize(Me)
    End Sub
End Class
