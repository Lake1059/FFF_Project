Imports Vortice.Direct3D11
Imports Vortice.DXGI
Imports System.Runtime.InteropServices

Public NotInheritable Class 显示器信息
    Public Property 适配器索引 As UInteger
    Public Property 输出索引 As UInteger
    Public Property 名称 As String = String.Empty
    Public Property 左边 As Integer
    Public Property 顶边 As Integer
    Public Property 右边 As Integer
    Public Property 底边 As Integer
    Public Property 显示器句柄 As IntPtr
    Public Property 连接到桌面 As Boolean
    Public Property 适配器名称 As String = String.Empty
    Public Property 适配器标识 As String = String.Empty
    Public Property 宽度 As UInteger
    Public Property 高度 As UInteger
    Public Property 刷新率赫兹 As UInteger
    Public Property 旋转 As String = String.Empty
    Public Property 每通道位数 As UInteger
    Public Property 色彩空间 As String = String.Empty
    Public Property HDR已启用 As Boolean
    Public Property 最小亮度尼特 As Single
    Public Property 最大亮度尼特 As Single
    Public Property 全屏最大亮度尼特 As Single
    Public Property DPI横向 As UInteger = 96
    Public Property DPI纵向 As UInteger = 96
End Class

Public NotInheritable Class 显示器捕获帧
    Implements IDisposable

    Friend ReadOnly 纹理 As ID3D11Texture2D
    Private ReadOnly 回收纹理 As Action(Of ID3D11Texture2D)
    Private 已释放 As Boolean

    Friend Sub New(自有纹理 As ID3D11Texture2D, 时间戳 As Long, HDR源 As Boolean, 旋转 As 视频旋转方式,
        回收操作 As Action(Of ID3D11Texture2D))
        纹理 = 自有纹理
        QPC时间戳 = 时间戳
        是HDR源 = HDR源
        旋转方式 = 旋转
        回收纹理 = 回收操作
    End Sub

    Public ReadOnly Property QPC时间戳 As Long
    Public ReadOnly Property 是HDR源 As Boolean
    Public ReadOnly Property 旋转方式 As 视频旋转方式

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        回收纹理(纹理)
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub
End Class

Public NotInheritable Class 显示器捕获器
    Implements IDisposable

    Private Const DXGI等待超时 As Integer = -2005270489
    Private Const DXGI访问丢失 As Integer = -2005270490
    Private ReadOnly 工厂 As IDXGIFactory1
    Private ReadOnly 适配器 As IDXGIAdapter1
    Private ReadOnly 输出 As IDXGIOutput
    Private 复制输出 As IDXGIOutputDuplication
    Private ReadOnly 图形 As 图形设备
    Private ReadOnly 纹理池 As D3D11纹理池
    Private ReadOnly HDR模式 As Boolean
    Private 已释放 As Boolean
    Private 已获取帧 As Boolean
    Private 重建次数值 As UInteger
    Private 诊断会话 As 录制会话

    Private Sub New(原生工厂 As IDXGIFactory1, 原生适配器 As IDXGIAdapter1, 原生输出 As IDXGIOutput,
        原生复制输出 As IDXGIOutputDuplication, 设备 As 图形设备, 请求HDR As Boolean)
        工厂 = 原生工厂
        适配器 = 原生适配器
        输出 = 原生输出
        复制输出 = 原生复制输出
        图形 = 设备
        纹理池 = New D3D11纹理池(设备, 8)
        HDR模式 = 请求HDR
    End Sub

    Public ReadOnly Property 设备 As 图形设备
        Get
            Return 图形
        End Get
    End Property

    Public ReadOnly Property 重建次数 As UInteger
        Get
            Return 重建次数值
        End Get
    End Property

    Public Sub 应用到配置(配置 As 录制配置)
        ArgumentNullException.ThrowIfNull(配置)
        配置.捕获后端 = "DXGI Desktop Duplication"
        配置.捕获源说明 = 输出.Description.DeviceName
        配置.捕获源格式 = If(HDR模式, "R16G16B16A16_FLOAT scRGB", "B8G8R8A8_UNORM SDR")
    End Sub

    Public Sub 绑定诊断会话(录制会话 As 录制会话)
        ArgumentNullException.ThrowIfNull(录制会话)
        诊断会话 = 录制会话
    End Sub

    Public Shared Function 枚举显示器() As IReadOnlyList(Of 显示器信息)
        Dim 结果 As New List(Of 显示器信息)
        Using 原生工厂 = DXGI.CreateDXGIFactory1(Of IDXGIFactory1)()
            Dim 适配器索引 As UInteger
            Do
                Dim 原生适配器 As IDXGIAdapter1 = Nothing
                If 原生工厂.EnumAdapters1(适配器索引, 原生适配器).Failure Then Exit Do
                Using 原生适配器
                    Dim 适配器描述 = 原生适配器.Description1
                    Dim 输出索引 As UInteger
                    Do
                        Dim 原生输出 As IDXGIOutput = Nothing
                        If 原生适配器.EnumOutputs(输出索引, 原生输出).Failure Then Exit Do
                        Using 原生输出
                            Dim 描述 = 原生输出.Description
                            Dim 模式 = 读取当前显示模式(描述.DeviceName)
                            Dim 每通道位数 As UInteger
                            Dim 色彩空间 = String.Empty
                            Dim HDR已启用 = False
                            Dim 最小亮度 As Single
                            Dim 最大亮度 As Single
                            Dim 全屏亮度 As Single
                            Dim 旋转 = 描述.Rotation.ToString()
                            Dim DPI横向 As UInteger = 96
                            Dim DPI纵向 As UInteger = 96
                            读取显示器DPI(描述.Monitor, 0, DPI横向, DPI纵向)
                            Using 输出六 = 原生输出.QueryInterfaceOrNull(Of IDXGIOutput6)()
                                If 输出六 IsNot Nothing Then
                                    Dim 高级描述 = 输出六.Description1
                                    每通道位数 = 高级描述.BitsPerColor
                                    色彩空间 = 高级描述.ColorSpace.ToString()
                                    HDR已启用 = CInt(高级描述.ColorSpace) = 12 AndAlso 高级描述.BitsPerColor >= 10
                                    最小亮度 = 高级描述.MinLuminance
                                    最大亮度 = 高级描述.MaxLuminance
                                    全屏亮度 = 高级描述.MaxFullFrameLuminance
                                    旋转 = 高级描述.Rotation.ToString()
                                End If
                            End Using
                            结果.Add(New 显示器信息 With {
                                .适配器索引 = 适配器索引, .输出索引 = 输出索引, .名称 = 描述.DeviceName,
                                .左边 = 描述.DesktopCoordinates.Left, .顶边 = 描述.DesktopCoordinates.Top,
                                .右边 = 描述.DesktopCoordinates.Right, .底边 = 描述.DesktopCoordinates.Bottom,
                                .显示器句柄 = 描述.Monitor, .连接到桌面 = CBool(描述.AttachedToDesktop),
                                .适配器名称 = 适配器描述.Description,
                                .适配器标识 = $"{适配器描述.VendorId:X8}|{适配器描述.DeviceId:X8}|{适配器描述.Luid}",
                                .宽度 = CUInt(Math.Max(0, 描述.DesktopCoordinates.Right - 描述.DesktopCoordinates.Left)),
                                .高度 = CUInt(Math.Max(0, 描述.DesktopCoordinates.Bottom - 描述.DesktopCoordinates.Top)),
                                .刷新率赫兹 = 模式.刷新率, .旋转 = 旋转, .每通道位数 = 每通道位数,
                                .色彩空间 = 色彩空间, .HDR已启用 = HDR已启用,
                                .最小亮度尼特 = 最小亮度, .最大亮度尼特 = 最大亮度,
                                .全屏最大亮度尼特 = 全屏亮度, .DPI横向 = DPI横向, .DPI纵向 = DPI纵向
                            })
                        End Using
                        输出索引 += 1UI
                    Loop
                End Using
                适配器索引 += 1UI
            Loop
        End Using
        Return 结果
    End Function

    Public Shared Function 创建(显示器 As 显示器信息, Optional 请求HDR As Boolean = False) As 显示器捕获器
        Return 创建(显示器, 请求HDR, False)
    End Function

    Public Shared Function 创建(显示器 As 显示器信息, 请求HDR As Boolean,
        启用调试层 As Boolean) As 显示器捕获器
        ArgumentNullException.ThrowIfNull(显示器)
        Dim 原生工厂 = DXGI.CreateDXGIFactory1(Of IDXGIFactory1)()
        Dim 原生适配器 As IDXGIAdapter1 = Nothing
        Dim 原生输出 As IDXGIOutput = Nothing
        Dim 图形 As 图形设备 = Nothing
        Try
            Dim 结果 = 原生工厂.EnumAdapters1(显示器.适配器索引, 原生适配器)
            If 结果.Failure Then Throw New ArgumentException("指定的显示适配器已不存在。", NameOf(显示器))
            结果 = 原生适配器.EnumOutputs(显示器.输出索引, 原生输出)
            If 结果.Failure Then Throw New ArgumentException("指定的显示输出已不存在。", NameOf(显示器))
            图形 = 图形设备.创建适配器设备(原生适配器, 启用调试层)
            If 请求HDR Then
                Using 输出六 = 原生输出.QueryInterfaceOrNull(Of IDXGIOutput6)()
                    If 输出六 Is Nothing Then Throw New PlatformNotSupportedException("该显示输出不支持 Advanced Color 状态查询。")
                    Dim 描述 = 输出六.Description1
                    If CInt(描述.ColorSpace) <> 12 OrElse 描述.BitsPerColor < 10 Then
                        Throw New InvalidOperationException("目标显示器当前未启用 HDR/Advanced Color。")
                    End If
                End Using
            End If
            Dim 复制输出 = 创建复制输出(原生输出, 图形, 请求HDR)
            Return New 显示器捕获器(原生工厂, 原生适配器, 原生输出, 复制输出, 图形, 请求HDR)
        Catch
            图形?.释放()
            原生输出?.Dispose()
            原生适配器?.Dispose()
            原生工厂.Dispose()
            Throw
        End Try
    End Function

    Public Function 捕获一帧(Optional 超时毫秒 As UInteger = 16) As 显示器捕获帧
        确保未释放()
        If 已获取帧 Then Throw New InvalidOperationException("上一帧尚未归还给 Desktop Duplication。")
        Dim 帧信息 As OutduplFrameInfo
        Dim 资源 As IDXGIResource = Nothing
        Dim 结果 = 复制输出.AcquireNextFrame(超时毫秒, 帧信息, 资源)
        If 结果.Code = DXGI等待超时 Then Return Nothing
        If 结果.Code = DXGI访问丢失 Then
            重建复制输出()
            Return Nothing
        End If
        If 结果.Failure Then Throw New InvalidOperationException($"捕获显示器帧失败：0x{结果.Code:X8}")
        已获取帧 = True
        Try
            ' AcquireNextFrame also wakes for pointer-only updates. The desktop texture did not
            ' change in that case, so VFR must not treat it as a new picture.
            If 帧信息.LastPresentTime <= 0 Then Return Nothing
            Using 资源
                Using 源纹理 = 资源.QueryInterface(Of ID3D11Texture2D)()
                    Dim 实际格式 = 源纹理.Description.Format
                    If HDR模式 AndAlso 实际格式 = Format.B8G8R8A8_UNorm Then
                        Throw New InvalidOperationException("Desktop Duplication 降级为 BGRA8，不能继续 HDR 录制。")
                    End If
                    If HDR模式 AndAlso 实际格式 = Format.R10G10B10A2_UNorm Then
                        Throw New NotSupportedException("当前图像处理链只接受线性 FP16/scRGB HDR；不能把 PQ R10 表面误作线性 HDR。")
                    End If
                    If Not HDR模式 AndAlso 实际格式 <> Format.B8G8R8A8_UNorm AndAlso
                        实际格式 <> Format.R16G16B16A16_Float Then
                        Throw New InvalidOperationException($"SDR Desktop Duplication 返回了意外格式 {实际格式}。")
                    End If
                    Dim 描述 = 源纹理.Description
                    描述.BindFlags = BindFlags.ShaderResource
                    描述.Usage = ResourceUsage.Default
                    描述.CPUAccessFlags = CpuAccessFlags.None
                    描述.MiscFlags = ResourceOptionFlags.None
                    Dim 自有纹理 = 纹理池.尝试租用(描述)
                    If 自有纹理 Is Nothing Then Return Nothing
                    图形.执行图形命令(Sub() 图形.上下文.CopyResource(自有纹理, 源纹理))
                    Return New 显示器捕获帧(自有纹理, 帧信息.LastPresentTime,
                        实际格式 = Format.R16G16B16A16_Float, 转换旋转(输出.Description.Rotation),
                        AddressOf 纹理池.归还)
                End Using
            End Using
        Finally
            复制输出.ReleaseFrame()
            已获取帧 = False
        End Try
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        If 已获取帧 Then
            复制输出.ReleaseFrame()
            已获取帧 = False
        End If
        复制输出.Dispose()
        纹理池.释放()
        输出.Dispose()
        适配器.Dispose()
        工厂.Dispose()
        图形.释放()
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 确保未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub

    Private Shared Function 转换旋转(旋转 As ModeRotation) As 视频旋转方式
        Select Case 旋转
            Case ModeRotation.Rotate90
                Return 视频旋转方式.顺时针九十度
            Case ModeRotation.Rotate180
                Return 视频旋转方式.旋转一百八十度
            Case ModeRotation.Rotate270
                Return 视频旋转方式.顺时针二百七十度
            Case Else
                Return 视频旋转方式.不旋转
        End Select
    End Function

    Private Shared Function 创建复制输出(原生输出 As IDXGIOutput, 图形 As 图形设备,
        请求HDR As Boolean) As IDXGIOutputDuplication
        Using 输出五 = 原生输出.QueryInterfaceOrNull(Of IDXGIOutput5)()
            If 输出五 IsNot Nothing Then
                Dim 格式 = If(请求HDR,
                    {Format.R16G16B16A16_Float, Format.R10G10B10A2_UNorm, Format.B8G8R8A8_UNorm},
                    {Format.B8G8R8A8_UNorm})
                ' Output5 可用时保留显式格式协商和底层失败原因。部分 Advanced Color 驱动即使
                ' SDR 列表只含 BGRA8 仍可能返回 FP16，取帧时会按调用者是否允许 tone mapping 处理。
                Try
                    Return 输出五.DuplicateOutput1(图形.设备, 格式)
                Catch when Not 请求HDR
                    ' A few WDDM drivers expose IDXGIOutput5 but reject
                    ' DuplicateOutput1 even for BGRA8. The legacy duplication
                    ' path is equivalent for SDR and avoids DXGI_ERROR_UNSUPPORTED.
                    Using 输出一 = 原生输出.QueryInterface(Of IDXGIOutput1)()
                        Return 输出一.DuplicateOutput(图形.设备)
                    End Using
                End Try
            End If
        End Using
        If 请求HDR Then Throw New PlatformNotSupportedException("该显示输出不支持 HDR DuplicateOutput1。")
        Using 输出一 = 原生输出.QueryInterface(Of IDXGIOutput1)()
            Return 输出一.DuplicateOutput(图形.设备)
        End Using
    End Function

    Private Sub 重建复制输出()
        复制输出.Dispose()
        复制输出 = 创建复制输出(输出, 图形, HDR模式)
        重建次数值 += 1UI
        诊断会话?.记录诊断事件("dxgi_access_lost_rebuilt", $"重建次数={重建次数值}")
    End Sub

    Private Structure 当前显示模式
        Public 刷新率 As UInteger
    End Structure

    <StructLayout(LayoutKind.Sequential, CharSet:=CharSet.Unicode)>
    Private Structure 原生显示模式
        <MarshalAs(UnmanagedType.ByValTStr, SizeConst:=32)> Public 设备名称 As String
        Public 规格版本 As UShort
        Public 驱动版本 As UShort
        Public 大小 As UShort
        Public 驱动额外大小 As UShort
        Public 字段 As UInteger
        Public 位置X As Integer
        Public 位置Y As Integer
        Public 显示方向 As UInteger
        Public 固定输出 As UInteger
        Public 颜色 As Short
        Public 双面 As Short
        Public Y分辨率 As Short
        Public 字体选项 As Short
        Public 分页 As Short
        <MarshalAs(UnmanagedType.ByValTStr, SizeConst:=32)> Public 表单名称 As String
        Public 每英寸像素 As UShort
        Public 每像素位数 As UInteger
        Public 像素宽度 As UInteger
        Public 像素高度 As UInteger
        Public 显示标志 As UInteger
        Public 显示频率 As UInteger
        Public ICM方法 As UInteger
        Public ICM意图 As UInteger
        Public 媒体类型 As UInteger
        Public 抖动类型 As UInteger
        Public 保留一 As UInteger
        Public 保留二 As UInteger
        Public 平移宽度 As UInteger
        Public 平移高度 As UInteger
    End Structure

    Private Shared Function 读取当前显示模式(设备名称 As String) As 当前显示模式
        Dim 模式 As New 原生显示模式 With {.大小 = CUShort(Marshal.SizeOf(Of 原生显示模式)())}
        If 枚举显示设置(设备名称, -1, 模式) Then Return New 当前显示模式 With {.刷新率 = 模式.显示频率}
        Return New 当前显示模式()
    End Function

    <DllImport("user32.dll", EntryPoint:="EnumDisplaySettingsW", CharSet:=CharSet.Unicode)>
    Private Shared Function 枚举显示设置(设备名称 As String, 模式编号 As Integer,
        ByRef 模式 As 原生显示模式) As Boolean
    End Function

    <DllImport("shcore.dll", EntryPoint:="GetDpiForMonitor")>
    Private Shared Function 读取显示器DPI(显示器句柄 As IntPtr, DPI类型 As Integer,
        ByRef DPI横向 As UInteger, ByRef DPI纵向 As UInteger) As Integer
    End Function
End Class
