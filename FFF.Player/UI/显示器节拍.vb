Imports System.Runtime.InteropServices

''' <summary>读取窗口所在显示器的节拍，统一限制文字图层最高为 120 帧。</summary>
Friend NotInheritable Class 显示器节拍
    Private Sub New()
    End Sub

    Friend Shared Function 计算目标帧率(刷新率 As Integer) As Integer
        Return If(刷新率 <= 1, 60, Math.Min(刷新率, 120))
    End Function

    Friend Shared Function 读取刷新率(设备名称 As String) As Integer
        If String.IsNullOrWhiteSpace(设备名称) Then Return 0
        Dim 设备上下文 = CreateDC("DISPLAY", 设备名称, Nothing, IntPtr.Zero)
        If 设备上下文 = IntPtr.Zero Then Return 0
        Try
            Return GetDeviceCaps(设备上下文, 116)
        Finally
            DeleteDC(设备上下文)
        End Try
    End Function

    <DllImport("gdi32.dll", CharSet:=CharSet.Unicode)>
    Private Shared Function CreateDC(驱动名称 As String, 设备名称 As String,
                                     输出名称 As String, 初始化数据 As IntPtr) As IntPtr
    End Function

    <DllImport("gdi32.dll")>
    Private Shared Function GetDeviceCaps(设备上下文 As IntPtr, 索引 As Integer) As Integer
    End Function

    <DllImport("gdi32.dll")>
    Private Shared Function DeleteDC(设备上下文 As IntPtr) As Boolean
    End Function
End Class
