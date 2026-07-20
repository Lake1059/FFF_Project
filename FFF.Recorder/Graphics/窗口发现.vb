Imports System.Diagnostics
Imports System.Runtime.InteropServices
Imports System.Text

Public NotInheritable Class 窗口信息
    Public Property 窗口句柄 As IntPtr
    Public Property 标题 As String = String.Empty
    Public Property 进程标识 As UInteger
    Public Property 进程名称 As String = String.Empty
    Public Property 左边 As Integer
    Public Property 顶边 As Integer
    Public Property 右边 As Integer
    Public Property 底边 As Integer
    Public Property 可见 As Boolean
    Public Property 已最小化 As Boolean
    Public Property 已被桌面合成器遮蔽 As Boolean
End Class

Public NotInheritable Class 窗口发现
    Private Const 扩展样式索引 As Integer = -20
    Private Const 工具窗口样式 As Long = &H80L
    Private Const 遮蔽属性 As UInteger = 14

    Private Sub New()
    End Sub

    Public Shared Function 枚举可捕获窗口(Optional 排除当前进程 As Boolean = True) As IReadOnlyList(Of 窗口信息)
        Dim 结果 As New List(Of 窗口信息)
        Dim 当前进程标识 = CUInt(Environment.ProcessId)
        枚举窗口(
            Function(窗口句柄, 参数)
                If Not 判断窗口可见(窗口句柄) Then Return True
                Dim 标题长度 = 读取窗口标题长度(窗口句柄)
                If 标题长度 <= 0 Then Return True
                If (读取窗口扩展样式(窗口句柄) And 工具窗口样式) <> 0 Then Return True
                Dim 进程标识 As UInteger
                读取窗口线程进程标识(窗口句柄, 进程标识)
                If 排除当前进程 AndAlso 进程标识 = 当前进程标识 Then Return True
                Dim 标题缓冲 As New StringBuilder(标题长度 + 1)
                读取窗口标题(窗口句柄, 标题缓冲, 标题缓冲.Capacity)
                Dim 边界 As 原生矩形
                If Not 读取窗口边界(窗口句柄, 边界) Then Return True
                If 边界.右边 <= 边界.左边 OrElse 边界.底边 <= 边界.顶边 Then Return True
                Dim 遮蔽 As UInteger
                读取桌面窗口属性(窗口句柄, 遮蔽属性, 遮蔽, CUInt(Marshal.SizeOf(Of UInteger)()))
                If 遮蔽 <> 0 OrElse 判断窗口最小化(窗口句柄) Then Return True
                Dim 进程名称 = String.Empty
                Try
                    Using 进程 = Process.GetProcessById(CInt(进程标识))
                        进程名称 = 进程.ProcessName
                    End Using
                Catch
                End Try
                结果.Add(New 窗口信息 With {
                    .窗口句柄 = 窗口句柄, .标题 = 标题缓冲.ToString(), .进程标识 = 进程标识,
                    .进程名称 = 进程名称, .左边 = 边界.左边, .顶边 = 边界.顶边,
                    .右边 = 边界.右边, .底边 = 边界.底边, .可见 = True,
                    .已最小化 = 判断窗口最小化(窗口句柄), .已被桌面合成器遮蔽 = 遮蔽 <> 0
                })
                Return True
            End Function, IntPtr.Zero)
        Return 结果.OrderBy(Function(窗口) 窗口.标题, StringComparer.CurrentCultureIgnoreCase).ToArray()
    End Function

    Private Delegate Function 枚举窗口回调(窗口句柄 As IntPtr, 参数 As IntPtr) As Boolean

    <StructLayout(LayoutKind.Sequential)>
    Private Structure 原生矩形
        Public 左边 As Integer
        Public 顶边 As Integer
        Public 右边 As Integer
        Public 底边 As Integer
    End Structure

    <DllImport("user32.dll", EntryPoint:="EnumWindows")>
    Private Shared Function 枚举窗口(回调 As 枚举窗口回调, 参数 As IntPtr) As Boolean
    End Function

    <DllImport("user32.dll", EntryPoint:="IsWindowVisible")>
    Private Shared Function 判断窗口可见(窗口句柄 As IntPtr) As Boolean
    End Function

    <DllImport("user32.dll", EntryPoint:="IsIconic")>
    Private Shared Function 判断窗口最小化(窗口句柄 As IntPtr) As Boolean
    End Function

    <DllImport("user32.dll", EntryPoint:="GetWindowTextLengthW")>
    Private Shared Function 读取窗口标题长度(窗口句柄 As IntPtr) As Integer
    End Function

    <DllImport("user32.dll", EntryPoint:="GetWindowTextW", CharSet:=CharSet.Unicode)>
    Private Shared Function 读取窗口标题(窗口句柄 As IntPtr, 标题 As StringBuilder, 容量 As Integer) As Integer
    End Function

    <DllImport("user32.dll", EntryPoint:="GetWindowRect")>
    Private Shared Function 读取窗口边界(窗口句柄 As IntPtr, ByRef 边界 As 原生矩形) As Boolean
    End Function

    <DllImport("user32.dll", EntryPoint:="GetWindowThreadProcessId")>
    Private Shared Function 读取窗口线程进程标识(窗口句柄 As IntPtr, ByRef 进程标识 As UInteger) As UInteger
    End Function

    <DllImport("user32.dll", EntryPoint:="GetWindowLongPtrW")>
    Private Shared Function 读取窗口扩展样式(窗口句柄 As IntPtr, Optional 索引 As Integer = 扩展样式索引) As Long
    End Function

    <DllImport("dwmapi.dll", EntryPoint:="DwmGetWindowAttribute")>
    Private Shared Function 读取桌面窗口属性(窗口句柄 As IntPtr, 属性 As UInteger,
        ByRef 值 As UInteger, 值大小 As UInteger) As Integer
    End Function
End Class
