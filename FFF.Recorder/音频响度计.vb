Imports System.Runtime.InteropServices

Friend NotInheritable Class 音频响度计
    Implements IDisposable

    <ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E"), ClassInterface(ClassInterfaceType.None)>
    Private Class MMDeviceEnumerator
    End Class

    <ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
    Private Interface IMMDeviceEnumerator
        Function EnumAudioEndpoints(dataFlow As Integer, stateMask As Integer, ByRef devices As IntPtr) As Integer
        Function GetDefaultAudioEndpoint(dataFlow As Integer, role As Integer, ByRef device As IMMDevice) As Integer
        Function GetDevice(id As String, ByRef device As IMMDevice) As Integer
    End Interface

    <ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
    Private Interface IMMDevice
        Function Activate(ByRef iid As Guid, clsCtx As Integer, activationParams As IntPtr, ByRef interfacePointer As IntPtr) As Integer
        Function OpenPropertyStore(access As Integer, ByRef properties As IntPtr) As Integer
        Function GetId(ByRef id As IntPtr) As Integer
        Function GetState(ByRef state As Integer) As Integer
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
        <PreserveSig>
        Function QueryHardwareSupport(ByRef hardwareSupportMask As UInteger) As Integer
    End Interface

    Private Const ERoleMultimedia As Integer = 1
    Private Const EDataFlowRender As Integer = 0
    Private Const CLSCTX_ALL As Integer = &H17
    Private Const DEVICE_STATE_ACTIVE As Integer = 1
    Private meter As IAudioMeterInformation
    Private device As IMMDevice
    Private lastValues As Single() = Array.Empty(Of Single)()

    Public Shared Function 创建(端点标识 As String) As 音频响度计
        Dim result As New 音频响度计()
        Try
            Dim enumerator = CType(New MMDeviceEnumerator(), IMMDeviceEnumerator)
            Dim selected As IMMDevice = Nothing
            If String.IsNullOrWhiteSpace(端点标识) Then
                If enumerator.GetDefaultAudioEndpoint(EDataFlowRender, ERoleMultimedia, selected) <> 0 Then Return result
            ElseIf enumerator.GetDevice(端点标识, selected) <> 0 Then
                Return result
            End If
            Dim iid = GetType(IAudioMeterInformation).GUID
            Dim pointer As IntPtr
            If selected Is Nothing OrElse selected.Activate(iid, CLSCTX_ALL, IntPtr.Zero, pointer) <> 0 Then Return result
            result.device = selected
            Try
                result.meter = DirectCast(Marshal.GetTypedObjectForIUnknown(pointer, GetType(IAudioMeterInformation)), IAudioMeterInformation)
            Finally
                Marshal.Release(pointer)
            End Try
            Return result
        Catch
            result.释放()
            Return result
        End Try
    End Function

    Public Shared Function 获取默认设备标识() As String
        Dim selected As IMMDevice = Nothing
        Try
            Dim enumerator = CType(New MMDeviceEnumerator(), IMMDeviceEnumerator)
            If enumerator.GetDefaultAudioEndpoint(EDataFlowRender, ERoleMultimedia, selected) <> 0 OrElse selected Is Nothing Then
                Return String.Empty
            End If
            Dim pointer As IntPtr
            If selected.GetId(pointer) <> 0 OrElse pointer = IntPtr.Zero Then Return String.Empty
            Try
                Return Marshal.PtrToStringUni(pointer)
            Finally
                Marshal.FreeCoTaskMem(pointer)
            End Try
        Catch
            Return String.Empty
        Finally
            If selected IsNot Nothing Then Marshal.FinalReleaseComObject(selected)
        End Try
    End Function

    Public Function 读取() As Single()
        If meter Is Nothing Then Return Array.Empty(Of Single)()
        Try
            ' GetPeakValue is implemented by substantially more endpoint drivers than the
            ' optional per-channel interface. Read it first so a missing channel meter does
            ' not make the whole UI appear dead.
            Dim masterPeak As Single
            Dim masterResult = meter.GetPeakValue(masterPeak)
            If masterResult = 0 AndAlso Not Single.IsNaN(masterPeak) AndAlso Not Single.IsInfinity(masterPeak) Then
                masterPeak = Math.Max(0.0F, Math.Min(1.0F, masterPeak))
            End If
            Dim count As UInteger
            If meter.GetMeteringChannelCount(count) = 0 AndAlso count > 0 AndAlso count <= 32 Then
                Dim values(CInt(count) - 1) As Single
                If meter.GetChannelsPeakValues(count, values) = 0 Then
                    For i = 0 To values.Length - 1
                        values(i) = Math.Max(0.0F, Math.Min(1.0F, values(i)))
                    Next
                    lastValues = values
                    Return values
                End If
            End If
            If masterResult = 0 Then
                lastValues = {masterPeak}
                Return lastValues
            End If
            Return lastValues
        Catch
            Return lastValues
        End Try
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If meter IsNot Nothing Then Marshal.FinalReleaseComObject(meter) : meter = Nothing
        If device IsNot Nothing Then Marshal.FinalReleaseComObject(device) : device = Nothing
    End Sub
End Class
