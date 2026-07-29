Imports System.Runtime.InteropServices

''' <summary>读取当前 WASAPI 渲染端点的每声道峰值；与录制器总控台使用同一套端点响度计逻辑。</summary>
Friend NotInheritable Class 播放器音频响度计
    Implements IDisposable

    <ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"), ClassInterface(ClassInterfaceType.None)>
    Private Class MMDeviceEnumerator
    End Class

    <ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
    Private Interface IMMDeviceEnumerator
        Function EnumAudioEndpoints(dataFlow As Integer, stateMask As Integer, ByRef devices As IntPtr) As Integer
        Function GetDefaultAudioEndpoint(dataFlow As Integer, role As Integer, ByRef device As IMMDevice) As Integer
    End Interface

    <ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
    Private Interface IMMDevice
        Function Activate(ByRef iid As Guid, clsCtx As Integer, activationParams As IntPtr, ByRef interfacePointer As IntPtr) As Integer
    End Interface

    <ComImport, Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
    Private Interface IAudioMeterInformation
        <PreserveSig>
        Function GetPeakValue(ByRef peak As Single) As Integer
        <PreserveSig>
        Function GetMeteringChannelCount(ByRef channelCount As UInteger) As Integer
        <PreserveSig>
        Function GetChannelsPeakValues(channelCount As UInteger,
            <Out, MarshalAs(UnmanagedType.LPArray, SizeParamIndex:=0)> values As Single()) As Integer
    End Interface

    Private Const ERoleMultimedia As Integer = 1
    Private Const EDataFlowRender As Integer = 0
    Private Const CLSCTX_ALL As Integer = &H17
    Private meter As IAudioMeterInformation
    Private device As IMMDevice
    Private lastValues As Single() = Array.Empty(Of Single)()

    Public Shared Function 创建默认设备() As 播放器音频响度计
        Dim result As New 播放器音频响度计()
        Try
            Dim enumerator = CType(New MMDeviceEnumerator(), IMMDeviceEnumerator)
            Dim selected As IMMDevice = Nothing
            If enumerator.GetDefaultAudioEndpoint(EDataFlowRender, ERoleMultimedia, selected) <> 0 OrElse selected Is Nothing Then Return result
            Dim iid = GetType(IAudioMeterInformation).GUID
            Dim pointer As IntPtr
            If selected.Activate(iid, CLSCTX_ALL, IntPtr.Zero, pointer) <> 0 OrElse pointer = IntPtr.Zero Then Return result
            result.device = selected
            Try
                result.meter = DirectCast(Marshal.GetTypedObjectForIUnknown(pointer, GetType(IAudioMeterInformation)), IAudioMeterInformation)
            Finally
                Marshal.Release(pointer)
            End Try
        Catch
            result.释放()
        End Try
        Return result
    End Function

    Public Function 读取() As Single()
        If meter Is Nothing Then Return lastValues
        Try
            Dim masterPeak As Single
            Dim masterResult = meter.GetPeakValue(masterPeak)
            If masterResult = 0 Then masterPeak = Math.Clamp(masterPeak, 0.0F, 1.0F)
            Dim count As UInteger
            If meter.GetMeteringChannelCount(count) = 0 AndAlso count > 0 AndAlso count <= 32 Then
                Dim values(CInt(count) - 1) As Single
                If meter.GetChannelsPeakValues(count, values) = 0 Then
                    For index = 0 To values.Length - 1
                        values(index) = Math.Clamp(values(index), 0.0F, 1.0F)
                    Next
                    lastValues = values
                    Return values
                End If
            End If
            If masterResult = 0 Then
                lastValues = {masterPeak}
            End If
        Catch
        End Try
        Return lastValues
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If meter IsNot Nothing Then Marshal.FinalReleaseComObject(meter) : meter = Nothing
        If device IsNot Nothing Then Marshal.FinalReleaseComObject(device) : device = Nothing
    End Sub
End Class
