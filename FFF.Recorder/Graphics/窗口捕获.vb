Imports System.Diagnostics
Imports System.Runtime.InteropServices
Imports Vortice.Direct3D11
Imports Vortice.DXGI
Imports Windows.Graphics.Capture
Imports Windows.Graphics.DirectX
Imports Windows.Graphics.DirectX.Direct3D11
Imports WinRT

<ComImport, Guid("3628E81B-3CAC-4C60-B7F4-23CE0E0C3356"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
Friend Interface IGraphicsCaptureItemInterop
    <PreserveSig>
    Function CreateForWindow(窗口句柄 As IntPtr, ByRef 接口标识 As Guid, ByRef 捕获项目 As IntPtr) As Integer

    <PreserveSig>
    Function CreateForMonitor(显示器句柄 As IntPtr, ByRef 接口标识 As Guid, ByRef 捕获项目 As IntPtr) As Integer
End Interface

<ComImport, Guid("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)>
Friend Interface IWinRTDxgiInterfaceAccess
    <PreserveSig>
    Function GetInterface(ByRef 接口标识 As Guid, ByRef 原生接口 As IntPtr) As Integer
End Interface

Public NotInheritable Class 窗口捕获帧
    Implements IDisposable

    Friend ReadOnly 纹理 As ID3D11Texture2D
    Private ReadOnly 回收纹理 As Action(Of ID3D11Texture2D)
    Private 已释放 As Boolean

    Friend Sub New(自有纹理 As ID3D11Texture2D, 时间戳 As Long, 帧宽度 As Integer, 帧高度 As Integer,
        HDR源 As Boolean, 回收操作 As Action(Of ID3D11Texture2D))
        纹理 = 自有纹理
        QPC时间戳 = 时间戳
        宽度 = 帧宽度
        高度 = 帧高度
        是HDR源 = HDR源
        回收纹理 = 回收操作
    End Sub

    Public ReadOnly Property QPC时间戳 As Long
    Public ReadOnly Property 宽度 As Integer
    Public ReadOnly Property 高度 As Integer
    Public ReadOnly Property 是HDR源 As Boolean

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        回收纹理?.Invoke(纹理)
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub
End Class

Public NotInheritable Class 窗口捕获帧事件参数
    Inherits EventArgs

    Public Sub New(捕获帧 As 窗口捕获帧)
        帧 = 捕获帧
    End Sub

    Public ReadOnly Property 帧 As 窗口捕获帧
End Class

Public NotInheritable Class 窗口捕获错误事件参数
    Inherits EventArgs

    Public Sub New(错误消息 As String, 原始异常 As Exception)
        消息 = 错误消息
        异常 = 原始异常
    End Sub

    Public ReadOnly Property 消息 As String
    Public ReadOnly Property 异常 As Exception
End Class

Public NotInheritable Class 窗口捕获器
    Implements IDisposable

    Private Shared ReadOnly 捕获项目接口标识 As New Guid("79C3F95B-31F7-4EC2-A464-632EF5D30760")
    Private Shared ReadOnly D3D11纹理接口标识 As New Guid("6F15AAF2-D208-4E89-9AB4-489535D34F9C")

    Private ReadOnly 图形 As 图形设备
    Private ReadOnly 捕获项目 As GraphicsCaptureItem
    Private ReadOnly WinRT设备 As IDirect3DDevice
    Private ReadOnly 使用HDR As Boolean
    Private ReadOnly 直接使用帧纹理 As Boolean
    Private ReadOnly 目标显示器值 As 显示器信息
    Private ReadOnly 纹理池 As D3D11纹理池
    Private 帧池 As Direct3D11CaptureFramePool
    Private 捕获会话 As GraphicsCaptureSession
    Private 当前尺寸 As Windows.Graphics.SizeInt32
    Private 已启动 As Boolean
    Private 已释放 As Boolean
    Private 已收到有效帧 As Boolean
    Private 报告脏区域 As Boolean
    Private 诊断会话 As 录制会话

    Private Sub New(设备 As 图形设备, 项目 As GraphicsCaptureItem, Direct3D设备 As IDirect3DDevice,
        HDR As Boolean, 目标显示器 As 显示器信息, Optional 零拷贝 As Boolean = False)
        图形 = 设备
        捕获项目 = 项目
        WinRT设备 = Direct3D设备
        使用HDR = HDR
        直接使用帧纹理 = 零拷贝
        目标显示器值 = 目标显示器
        纹理池 = New D3D11纹理池(设备, 8)
        当前尺寸 = 项目.Size
    End Sub

    Public Event 收到帧 As EventHandler(Of 窗口捕获帧事件参数)
    Public Event 捕获失败 As EventHandler(Of 窗口捕获错误事件参数)
    Public Event 捕获已关闭 As EventHandler

    Public ReadOnly Property 设备 As 图形设备
        Get
            Return 图形
        End Get
    End Property

    Public ReadOnly Property 目标显示器 As 显示器信息
        Get
            Return 目标显示器值
        End Get
    End Property

    Public Shared Function 创建(窗口句柄 As IntPtr, Optional 请求HDR As Boolean = False,
        Optional 零拷贝帧 As Boolean = False) As 窗口捕获器
        If 窗口句柄 = IntPtr.Zero Then Throw New ArgumentException("窗口句柄不能为空。", NameOf(窗口句柄))
        If Not GraphicsCaptureSession.IsSupported() Then Throw New PlatformNotSupportedException("当前系统不支持 Windows Graphics Capture。")

        Dim 显示器句柄 = 查找窗口显示器(窗口句柄, 2UI)
        Dim 显示器 = 显示器捕获器.枚举显示器().FirstOrDefault(
            Function(项目) 项目.显示器句柄 = 显示器句柄)
        If 请求HDR AndAlso (显示器 Is Nothing OrElse Not 显示器.HDR已启用) Then
            Throw New InvalidOperationException("目标窗口所在显示器当前未启用 HDR/Advanced Color。")
        End If
        Dim 设备 = 创建显示器适配器设备(显示器句柄)
        Try
            Dim 项目 = 创建窗口捕获项目(窗口句柄)
            Dim Direct3D设备 = 创建WinRT设备(设备.设备)
            Return New 窗口捕获器(设备, 项目, Direct3D设备, 请求HDR, 显示器, 零拷贝帧)
        Catch
            设备.释放()
            Throw
        End Try
    End Function

    Friend Shared Function 获取窗口显示器(窗口句柄 As IntPtr) As 显示器信息
        If 窗口句柄 = IntPtr.Zero Then Return Nothing
        Dim 显示器句柄 = 查找窗口显示器(窗口句柄, 2UI)
        Return 显示器捕获器.枚举显示器().FirstOrDefault(Function(x) x.显示器句柄 = 显示器句柄)
    End Function

    Public Shared Function 创建显示器(显示器 As 显示器信息, Optional 请求HDR As Boolean = False,
        Optional 零拷贝帧 As Boolean = False) As 窗口捕获器
        ArgumentNullException.ThrowIfNull(显示器)
        If Not 显示器.连接到桌面 OrElse 显示器.显示器句柄 = IntPtr.Zero Then
            Throw New ArgumentException("目标显示器未连接到桌面。", NameOf(显示器))
        End If
        If Not GraphicsCaptureSession.IsSupported() Then Throw New PlatformNotSupportedException("当前系统不支持 Windows Graphics Capture。")
        If 请求HDR AndAlso Not 显示器.HDR已启用 Then
            Throw New InvalidOperationException("目标显示器当前未启用 HDR/Advanced Color。")
        End If

        Dim 设备 = 创建显示器适配器设备(显示器.显示器句柄)
        Try
            Dim 项目 = 创建显示器捕获项目(显示器.显示器句柄)
            Dim Direct3D设备 = 创建WinRT设备(设备.设备)
            Return New 窗口捕获器(设备, 项目, Direct3D设备, 请求HDR, 显示器, 零拷贝帧)
        Catch
            设备.释放()
            Throw
        End Try
    End Function

    Public Sub 应用到配置(配置 As 录制配置)
        ArgumentNullException.ThrowIfNull(配置)
        配置.捕获后端 = "Windows Graphics Capture"
        配置.捕获源说明 = If(String.IsNullOrWhiteSpace(捕获项目.DisplayName),
            If(目标显示器值 Is Nothing, "HWND window", 目标显示器值.名称), 捕获项目.DisplayName)
        配置.捕获源格式 = If(使用HDR, "R16G16B16A16_FLOAT scRGB", "B8G8R8A8_UNORM SDR")
    End Sub

    Public Sub 绑定诊断会话(录制会话 As 录制会话)
        ArgumentNullException.ThrowIfNull(录制会话)
        诊断会话 = 录制会话
    End Sub

    Public Sub 开始(Optional 捕获光标 As Boolean = True)
        确保未释放()
        If 已启动 Then Throw New InvalidOperationException("窗口捕获已经开始。")
        Dim 像素格式 = If(使用HDR, DirectXPixelFormat.R16G16B16A16Float,
            DirectXPixelFormat.B8G8R8A8UIntNormalized)
        帧池 = Direct3D11CaptureFramePool.CreateFreeThreaded(WinRT设备, 像素格式, 3, 当前尺寸)
        捕获会话 = 帧池.CreateCaptureSession(捕获项目)
        捕获会话.IsCursorCaptureEnabled = 捕获光标
        报告脏区域 = Windows.Foundation.Metadata.ApiInformation.IsPropertyPresent(
            "Windows.Graphics.Capture.GraphicsCaptureSession", "DirtyRegionMode") AndAlso
            Windows.Foundation.Metadata.ApiInformation.IsPropertyPresent(
                "Windows.Graphics.Capture.Direct3D11CaptureFrame", "DirtyRegions")
        If 报告脏区域 Then 捕获会话.DirtyRegionMode = GraphicsCaptureDirtyRegionMode.ReportOnly
        已收到有效帧 = False
        AddHandler 帧池.FrameArrived, AddressOf 处理帧到达
        AddHandler 捕获项目.Closed, AddressOf 处理捕获关闭
        捕获会话.StartCapture()
        已启动 = True
    End Sub

    Public Sub 停止()
        If 已释放 OrElse Not 已启动 Then Return
        RemoveHandler 捕获项目.Closed, AddressOf 处理捕获关闭
        RemoveHandler 帧池.FrameArrived, AddressOf 处理帧到达
        捕获会话.Dispose()
        帧池.Dispose()
        捕获会话 = Nothing
        帧池 = Nothing
        已启动 = False
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        停止()
        WinRT设备.Dispose()
        纹理池.释放()
        图形.释放()
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 处理帧到达(发送者 As Direct3D11CaptureFramePool, 参数 As Object)
        Try
            Using 系统帧 = 发送者.TryGetNextFrame()
                If 系统帧 Is Nothing Then Return
                Dim 新尺寸 = 系统帧.ContentSize
                Dim 尺寸已变化 = 新尺寸.Width <> 当前尺寸.Width OrElse 新尺寸.Height <> 当前尺寸.Height
                If 报告脏区域 AndAlso 已收到有效帧 AndAlso Not 尺寸已变化 AndAlso
                    系统帧.DirtyRegions.Count = 0 Then Return
                Using 源纹理 = 取得D3D11纹理(系统帧.Surface)
                    Dim 实际HDR = 源纹理.Description.Format = Format.R16G16B16A16_Float
                    If 使用HDR AndAlso Not 实际HDR Then
                        Throw New InvalidOperationException("WGC 未返回 FP16/scRGB 帧，不能把本次捕获标记为 HDR。")
                    End If
                    Dim 时间戳 = 转换系统相对时间(系统帧.SystemRelativeTime)
                    已收到有效帧 = True
                    If 直接使用帧纹理 Then
                        ' 事件在当前 FrameArrived 回调中同步执行，渲染完成后才离开
                        ' Using 范围，因此可以安全地直接采样 WGC 表面，省掉一次 CopyResource。
                        RaiseEvent 收到帧(Me, New 窗口捕获帧事件参数(
                            New 窗口捕获帧(源纹理, 时间戳, 新尺寸.Width, 新尺寸.Height, 实际HDR,
                                Nothing)))
                    Else
                        Dim 描述 = 源纹理.Description
                        描述.BindFlags = BindFlags.ShaderResource
                        描述.Usage = ResourceUsage.Default
                        描述.CPUAccessFlags = CpuAccessFlags.None
                        描述.MiscFlags = ResourceOptionFlags.None
                        Dim 自有纹理 = 纹理池.尝试租用(描述)
                        If 自有纹理 Is Nothing Then Return
                        图形.执行图形命令(Sub() 图形.上下文.CopyResource(自有纹理, 源纹理))
                        RaiseEvent 收到帧(Me, New 窗口捕获帧事件参数(
                            New 窗口捕获帧(自有纹理, 时间戳, 新尺寸.Width, 新尺寸.Height, 实际HDR,
                                AddressOf 纹理池.归还)))
                    End If
                End Using
                If 尺寸已变化 Then
                    诊断会话?.记录诊断事件("wgc_resize", $"{当前尺寸.Width}x{当前尺寸.Height} -> {新尺寸.Width}x{新尺寸.Height}")
                    当前尺寸 = 新尺寸
                    帧池.Recreate(WinRT设备, If(使用HDR, DirectXPixelFormat.R16G16B16A16Float,
                        DirectXPixelFormat.B8G8R8A8UIntNormalized), 3, 当前尺寸)
                End If
            End Using
        Catch 错误 As Exception
            诊断会话?.记录诊断事件("wgc_failed", 错误.Message)
            RaiseEvent 捕获失败(Me, New 窗口捕获错误事件参数("处理 WGC 帧失败。", 错误))
        End Try
    End Sub

    Private Sub 处理捕获关闭(发送者 As GraphicsCaptureItem, 参数 As Object)
        诊断会话?.记录诊断事件("wgc_closed", "目标窗口已关闭。")
        RaiseEvent 捕获已关闭(Me, EventArgs.Empty)
    End Sub

    Private Shared Function 创建窗口捕获项目(窗口句柄 As IntPtr) As GraphicsCaptureItem
        Using 工厂引用 = ActivationFactory.Get("Windows.Graphics.Capture.GraphicsCaptureItem",
            GetType(IGraphicsCaptureItemInterop).GUID)
            Dim 互操作 = 工厂引用.AsInterface(Of IGraphicsCaptureItemInterop)()
            Dim 原生项目 As IntPtr
            Dim 接口标识 = 捕获项目接口标识
            Dim 结果 = 互操作.CreateForWindow(窗口句柄, 接口标识, 原生项目)
            If 结果 < 0 Then Marshal.ThrowExceptionForHR(结果)
            Try
                Return MarshalInspectable(Of GraphicsCaptureItem).FromAbi(原生项目)
            Finally
                Marshal.Release(原生项目)
            End Try
        End Using
    End Function

    Private Shared Function 创建显示器捕获项目(显示器句柄 As IntPtr) As GraphicsCaptureItem
        Using 工厂引用 = ActivationFactory.Get("Windows.Graphics.Capture.GraphicsCaptureItem",
            GetType(IGraphicsCaptureItemInterop).GUID)
            Dim 互操作 = 工厂引用.AsInterface(Of IGraphicsCaptureItemInterop)()
            Dim 原生项目 As IntPtr
            Dim 接口标识 = 捕获项目接口标识
            Dim 结果 = 互操作.CreateForMonitor(显示器句柄, 接口标识, 原生项目)
            If 结果 < 0 Then Marshal.ThrowExceptionForHR(结果)
            Try
                Return MarshalInspectable(Of GraphicsCaptureItem).FromAbi(原生项目)
            Finally
                Marshal.Release(原生项目)
            End Try
        End Using
    End Function

    Private Shared Function 创建显示器适配器设备(显示器句柄 As IntPtr) As 图形设备
        Using 工厂 = DXGI.CreateDXGIFactory1(Of IDXGIFactory1)()
            Dim 适配器索引 As UInteger
            Do
                Dim 适配器 As IDXGIAdapter1 = Nothing
                If 工厂.EnumAdapters1(适配器索引, 适配器).Failure Then Exit Do
                Using 适配器
                    Dim 输出索引 As UInteger
                    Do
                        Dim 输出 As IDXGIOutput = Nothing
                        If 适配器.EnumOutputs(输出索引, 输出).Failure Then Exit Do
                        Using 输出
                            If 输出.Description.Monitor = 显示器句柄 Then
                                Return 图形设备.创建适配器设备(适配器, False)
                            End If
                        End Using
                        输出索引 += 1UI
                    Loop
                End Using
                适配器索引 += 1UI
            Loop
        End Using
        Return 图形设备.创建默认设备()
    End Function

    Private Shared Function 创建WinRT设备(D3D11设备 As ID3D11Device) As IDirect3DDevice
        Using DXGI设备 = D3D11设备.QueryInterface(Of IDXGIDevice)()
            Dim 原生Direct3D设备 As IntPtr
            Dim 结果 = CreateDirect3D11DeviceFromDXGIDevice(DXGI设备.NativePointer, 原生Direct3D设备)
            If 结果 < 0 Then Marshal.ThrowExceptionForHR(结果)
            Try
                Return MarshalInspectable(Of IDirect3DDevice).FromAbi(原生Direct3D设备)
            Finally
                Marshal.Release(原生Direct3D设备)
            End Try
        End Using
    End Function

    Private Shared Function 取得D3D11纹理(表面 As IDirect3DSurface) As ID3D11Texture2D
        Dim 表面指针 = MarshalInspectable(Of IDirect3DSurface).FromManaged(表面)
        Dim 访问指针 As IntPtr
        Dim 访问对象 As Object = Nothing
        Dim 纹理指针 As IntPtr
        Try
            Dim 访问接口标识 = GetType(IWinRTDxgiInterfaceAccess).GUID
            Dim 结果 = Marshal.QueryInterface(表面指针, 访问接口标识, 访问指针)
            If 结果 < 0 Then Marshal.ThrowExceptionForHR(结果)
            访问对象 = Marshal.GetObjectForIUnknown(访问指针)
            Dim 接口访问 = DirectCast(访问对象, IWinRTDxgiInterfaceAccess)
            Dim 纹理接口标识 = D3D11纹理接口标识
            结果 = 接口访问.GetInterface(纹理接口标识, 纹理指针)
            If 结果 < 0 Then Marshal.ThrowExceptionForHR(结果)
            Return New ID3D11Texture2D(纹理指针)
        Finally
            If 访问对象 IsNot Nothing Then Marshal.ReleaseComObject(访问对象)
            If 访问指针 <> IntPtr.Zero Then Marshal.Release(访问指针)
            Marshal.Release(表面指针)
        End Try
    End Function

    Private Shared Function 转换系统相对时间(系统相对时间 As TimeSpan) As Long
        Dim 整秒 = 系统相对时间.Ticks \ TimeSpan.TicksPerSecond
        Dim 余数 = 系统相对时间.Ticks Mod TimeSpan.TicksPerSecond
        Return 整秒 * Stopwatch.Frequency + 余数 * Stopwatch.Frequency \ TimeSpan.TicksPerSecond
    End Function

    Private Sub 确保未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub

    <DllImport("d3d11.dll", ExactSpelling:=True)>
    Private Shared Function CreateDirect3D11DeviceFromDXGIDevice(DXGI设备 As IntPtr,
        ByRef Direct3D设备 As IntPtr) As Integer
    End Function

    <DllImport("user32.dll", EntryPoint:="MonitorFromWindow")>
    Private Shared Function 查找窗口显示器(窗口句柄 As IntPtr, 标志 As UInteger) As IntPtr
    End Function
End Class
