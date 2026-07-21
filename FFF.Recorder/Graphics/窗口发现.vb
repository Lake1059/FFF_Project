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

Public NotInheritable Class 窗口裁剪信息
    Public Property 左边 As UInteger
    Public Property 顶边 As UInteger
    Public Property 右边 As UInteger
    Public Property 底边 As UInteger
    Public Property 宽度 As UInteger
    Public Property 高度 As UInteger
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
                If Not 读取窗口捕获边界(窗口句柄, 边界) Then Return True
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

    Public Shared Function 获取客户区裁剪(窗口句柄 As IntPtr) As 窗口裁剪信息
        If 窗口句柄 = IntPtr.Zero Then Throw New ArgumentException("窗口句柄不能为空。", NameOf(窗口句柄))

        Dim 捕获边界 As 原生矩形
        If Not 读取窗口捕获边界(窗口句柄, 捕获边界) Then
            Throw New InvalidOperationException("无法读取目标窗口边界。")
        End If
        Dim 客户区 As 原生矩形
        If Not 读取客户区边界(窗口句柄, 客户区) Then
            Throw New InvalidOperationException("无法读取目标窗口客户区。")
        End If
        If 捕获边界.右边 <= 捕获边界.左边 OrElse 捕获边界.底边 <= 捕获边界.顶边 OrElse
            客户区.右边 <= 客户区.左边 OrElse 客户区.底边 <= 客户区.顶边 Then
            Throw New InvalidOperationException("目标窗口客户区尺寸无效。")
        End If
        Dim 客户区左上角 As 原生点
        If Not 客户区转屏幕(窗口句柄, 客户区左上角) Then
            Throw New InvalidOperationException("无法转换目标窗口客户区坐标。")
        End If

        Dim 捕获宽度 = Math.Max(1, 捕获边界.右边 - 捕获边界.左边)
        Dim 捕获高度 = Math.Max(1, 捕获边界.底边 - 捕获边界.顶边)
        Dim 客户区左 = Math.Clamp(客户区左上角.X - 捕获边界.左边, 0, 捕获宽度 - 1)
        Dim 客户区顶 = Math.Clamp(客户区左上角.Y - 捕获边界.顶边, 0, 捕获高度 - 1)
        Dim 客户区右 = Math.Clamp(客户区左上角.X - 捕获边界.左边 + 客户区.右边, 客户区左 + 1, 捕获宽度)
        Dim 客户区下 = Math.Clamp(客户区左上角.Y - 捕获边界.顶边 + 客户区.底边, 客户区顶 + 1, 捕获高度)
        Dim 客户区宽度 = 客户区右 - 客户区左
        Dim 客户区高度 = 客户区下 - 客户区顶

        Return New 窗口裁剪信息 With {
            .左边 = CUInt(客户区左), .顶边 = CUInt(客户区顶),
            .右边 = CUInt(Math.Max(0, 捕获宽度 - 客户区左 - 客户区宽度)),
            .底边 = CUInt(Math.Max(0, 捕获高度 - 客户区顶 - 客户区高度)),
            .宽度 = CUInt(客户区宽度), .高度 = CUInt(客户区高度)}
    End Function

    Private Delegate Function 枚举窗口回调(窗口句柄 As IntPtr, 参数 As IntPtr) As Boolean

    <StructLayout(LayoutKind.Sequential)>
    Private Structure 原生矩形
        Public 左边 As Integer
        Public 顶边 As Integer
        Public 右边 As Integer
        Public 底边 As Integer
    End Structure

    <StructLayout(LayoutKind.Sequential)>
    Private Structure 原生点
        Public X As Integer
        Public Y As Integer
    End Structure

    Private Shared Function 读取窗口捕获边界(窗口句柄 As IntPtr, ByRef 边界 As 原生矩形) As Boolean
        If 读取桌面窗口矩形属性(窗口句柄, 9UI, 边界, CUInt(Marshal.SizeOf(Of 原生矩形)())) = 0 AndAlso
            边界.右边 > 边界.左边 AndAlso 边界.底边 > 边界.顶边 Then Return True
        Return 读取窗口边界(窗口句柄, 边界)
    End Function

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

    <DllImport("user32.dll", EntryPoint:="GetClientRect")>
    Private Shared Function 读取客户区边界(窗口句柄 As IntPtr, ByRef 边界 As 原生矩形) As Boolean
    End Function

    <DllImport("user32.dll", EntryPoint:="ClientToScreen")>
    Private Shared Function 客户区转屏幕(窗口句柄 As IntPtr, ByRef 点 As 原生点) As Boolean
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

    <DllImport("dwmapi.dll", EntryPoint:="DwmGetWindowAttribute")>
    Private Shared Function 读取桌面窗口矩形属性(窗口句柄 As IntPtr, 属性 As UInteger,
        ByRef 值 As 原生矩形, 值大小 As UInteger) As Integer
    End Function
End Class
