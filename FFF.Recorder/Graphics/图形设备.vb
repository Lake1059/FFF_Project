Imports Vortice.Direct3D
Imports Vortice.Direct3D11
Imports Vortice.DXGI

Public NotInheritable Class 图形设备
    Implements IDisposable

    Friend ReadOnly 设备 As ID3D11Device
    Friend ReadOnly 上下文 As ID3D11DeviceContext
    Private ReadOnly 命令调度器 As 图形命令调度器
    Private 已释放 As Boolean

    Private Sub New(原生设备 As ID3D11Device, 原生上下文 As ID3D11DeviceContext)
        设备 = 原生设备
        上下文 = 原生上下文
        命令调度器 = New 图形命令调度器()
    End Sub

    Public ReadOnly Property 原生设备指针 As IntPtr
        Get
            确保未释放()
            Return 设备.NativePointer
        End Get
    End Property

    Public ReadOnly Property 适配器标识 As String
        Get
            确保未释放()
            Using DXGI设备 = 设备.QueryInterface(Of IDXGIDevice)()
                Dim 原生适配器 As IDXGIAdapter = Nothing
                Dim 结果 = DXGI设备.GetAdapter(原生适配器)
                If 结果.Failure Then Throw New InvalidOperationException($"读取图形适配器失败：0x{结果.Code:X8}")
                Using 原生适配器
                    Using 适配器一 = 原生适配器.QueryInterface(Of IDXGIAdapter1)()
                        Dim 描述 = 适配器一.Description1
                        Return $"{描述.Description}|{描述.VendorId:X8}|{描述.DeviceId:X8}|{描述.Luid}"
                    End Using
                End Using
            End Using
        End Get
    End Property

    Public Shared Function 创建默认设备(Optional 启用调试层 As Boolean = False) As 图形设备
        Return 创建设备(IntPtr.Zero, DriverType.Hardware, 启用调试层)
    End Function

    Friend Shared Function 创建适配器设备(适配器 As IDXGIAdapter, 启用调试层 As Boolean) As 图形设备
        ArgumentNullException.ThrowIfNull(适配器)
        Return 创建设备(适配器.NativePointer, DriverType.Unknown, 启用调试层)
    End Function

    Private Shared Function 创建设备(适配器指针 As IntPtr, 驱动类型 As DriverType, 启用调试层 As Boolean) As 图形设备
        Dim 标志 = DeviceCreationFlags.BgraSupport Or DeviceCreationFlags.VideoSupport
        If 启用调试层 Then 标志 = 标志 Or DeviceCreationFlags.Debug
        Dim 特性级别 = {FeatureLevel.Level_12_1, FeatureLevel.Level_12_0, FeatureLevel.Level_11_1, FeatureLevel.Level_11_0}
        Dim 原生设备 As ID3D11Device = Nothing
        Dim 原生上下文 As ID3D11DeviceContext = Nothing
        Dim 已选级别 As FeatureLevel
        Dim 结果 = D3D11.D3D11CreateDevice(适配器指针, 驱动类型, 标志, 特性级别,
            原生设备, 已选级别, 原生上下文)
        If 结果.Failure Then Throw New InvalidOperationException($"创建 D3D11 设备失败：0x{结果.Code:X8}")
        Return New 图形设备(原生设备, 原生上下文)
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        Try
            清空并刷新()
        Finally
            已释放 = True
            命令调度器.释放()
            上下文.Dispose()
            设备.Dispose()
            GC.SuppressFinalize(Me)
        End Try
    End Sub

    Friend Sub 清空并刷新()
        确保未释放()
        ' ClearState 解除上下文对 SRV/RTV/采样器等对象的内部引用，Flush 将释放命令
        ' 及时提交给驱动，避免反复创建预览资源时显存延迟堆积。
        命令调度器.执行(
            Sub()
                上下文.ClearState()
                上下文.Flush()
            End Sub)
    End Sub

    Friend Sub 执行图形命令(操作 As Action)
        确保未释放()
        命令调度器.执行(操作)
    End Sub

    Private Sub 确保未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub
End Class
