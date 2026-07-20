Imports System.Reflection
Imports System.Runtime.InteropServices
Imports System.Collections.Concurrent
Imports Vortice.Direct3D
Imports Vortice.Direct3D11
Imports Vortice.DXGI

Public NotInheritable Class 处理后视频帧
    Implements IDisposable

    Friend ReadOnly 纹理 As ID3D11Texture2D
    Friend ReadOnly 预览视图 As ID3D11ShaderResourceView
    Private ReadOnly 回收纹理 As Action(Of ID3D11Texture2D)
    Private 已释放 As Boolean

    Friend Sub New(自有纹理 As ID3D11Texture2D, 自有预览视图 As ID3D11ShaderResourceView,
        时间戳 As Long, HDR输出 As Boolean,
        回收操作 As Action(Of ID3D11Texture2D))
        纹理 = 自有纹理
        预览视图 = 自有预览视图
        QPC时间戳 = 时间戳
        是HDR输出 = HDR输出
        回收纹理 = 回收操作
    End Sub

    Public ReadOnly Property 原生纹理指针 As IntPtr
        Get
            If 已释放 Then Throw New ObjectDisposedException(NameOf(处理后视频帧))
            Return 纹理.NativePointer
        End Get
    End Property

    Public ReadOnly Property QPC时间戳 As Long
    Public ReadOnly Property 是HDR输出 As Boolean

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        回收纹理(纹理)
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub
End Class

Public NotInheritable Class 视频处理器
    Implements IDisposable

    Private Shared ReadOnly 着色器缓存 As New ConcurrentDictionary(Of String, Byte())(StringComparer.Ordinal)

    <StructLayout(LayoutKind.Sequential)>
    Private Structure 缩放常量
        Public 输出宽度 As UInteger
        Public 输出高度 As UInteger
        Public 保留一 As UInteger
        Public 保留二 As UInteger
        Public 目标左边 As UInteger
        Public 目标顶边 As UInteger
        Public 目标宽度 As UInteger
        Public 目标高度 As UInteger
        Public 源左边比例 As Single
        Public 源顶边比例 As Single
        Public 源宽度比例 As Single
        Public 源高度比例 As Single
        Public 参考白尼特 As Single
        Public 目标峰值尼特 As Single
        Public 曝光 As Single
        Public 高光压缩 As Single
        Public 饱和度 As Single
        Public 旋转 As UInteger
        Public 高质量缩放 As UInteger
        Public 保留四 As UInteger
    End Structure

    Private ReadOnly 图形 As 图形设备
    Private ReadOnly 输出宽度值 As UInteger
    Private ReadOnly 输出高度值 As UInteger
    Private ReadOnly 输出HDR模式 As Boolean
    Private ReadOnly 输出十位模式 As Boolean
    Private ReadOnly 允许HDR转SDR值 As Boolean
    Private ReadOnly 高质量缩放值 As Boolean
    Private ReadOnly 缩放方式值 As 视频缩放方式
    Private ReadOnly 裁剪左边值 As UInteger
    Private ReadOnly 裁剪顶边值 As UInteger
    Private ReadOnly 裁剪右边值 As UInteger
    Private ReadOnly 裁剪底边值 As UInteger
    Private ReadOnly 参考白尼特值 As Single
    Private ReadOnly 目标峰值尼特值 As Single
    Private ReadOnly 曝光值 As Single
    Private ReadOnly 高光压缩值 As Single
    Private ReadOnly 饱和度值 As Single
    Private ReadOnly SDR计算着色器 As ID3D11ComputeShader
    Private ReadOnly HDR计算着色器 As ID3D11ComputeShader
    Private ReadOnly 色调映射着色器 As ID3D11ComputeShader
    Private ReadOnly 顶点着色器 As ID3D11VertexShader
    Private ReadOnly 像素着色器 As ID3D11PixelShader
    Private ReadOnly 线性采样器 As ID3D11SamplerState
    Private ReadOnly 常量缓冲区 As ID3D11Buffer
    Private ReadOnly 中间纹理池 As D3D11纹理池
    Private ReadOnly 输出纹理池 As D3D11纹理池
    Private ReadOnly 中间读取视图缓存 As New Dictionary(Of IntPtr, ID3D11ShaderResourceView)
    Private ReadOnly 中间写入视图缓存 As New Dictionary(Of IntPtr, ID3D11UnorderedAccessView)
    Private ReadOnly 输出渲染视图缓存 As New Dictionary(Of IntPtr, ID3D11RenderTargetView)
    Private ReadOnly 输出预览视图缓存 As New Dictionary(Of IntPtr, ID3D11ShaderResourceView)
    Private 已释放 As Boolean

    Public Sub New(图形设备 As 图形设备, 输出宽度 As UInteger, 输出高度 As UInteger)
        Me.New(图形设备, New 视频处理配置 With {.输出宽度 = 输出宽度, .输出高度 = 输出高度})
    End Sub

    Public Sub New(图形设备 As 图形设备, 配置 As 视频处理配置)
        ArgumentNullException.ThrowIfNull(图形设备)
        ArgumentNullException.ThrowIfNull(配置)
        配置.验证()
        图形 = 图形设备
        输出宽度值 = 配置.输出宽度
        输出高度值 = 配置.输出高度
        输出HDR模式 = 配置.输出HDR10
        输出十位模式 = 配置.输出HDR10 OrElse 配置.输出十位SDR
        允许HDR转SDR值 = 配置.允许HDR转SDR
        高质量缩放值 = 配置.高质量缩放
        缩放方式值 = 配置.缩放方式
        裁剪左边值 = 配置.裁剪左边
        裁剪顶边值 = 配置.裁剪顶边
        裁剪右边值 = 配置.裁剪右边
        裁剪底边值 = 配置.裁剪底边
        参考白尼特值 = 配置.参考白尼特
        目标峰值尼特值 = 配置.目标峰值尼特
        曝光值 = 配置.曝光
        高光压缩值 = 配置.高光压缩
        饱和度值 = 配置.饱和度
        SDR计算着色器 = 图形.设备.CreateComputeShader(读取着色器("FFF.Recorder.Shaders.ScaleRgba.cso"), Nothing)
        HDR计算着色器 = 图形.设备.CreateComputeShader(读取着色器("FFF.Recorder.Shaders.HdrToPq.cso"), Nothing)
        色调映射着色器 = 图形.设备.CreateComputeShader(读取着色器("FFF.Recorder.Shaders.HdrToSdr.cso"), Nothing)
        顶点着色器 = 图形.设备.CreateVertexShader(读取着色器("FFF.Recorder.Shaders.FullscreenTriangle.cso"), Nothing)
        像素着色器 = 图形.设备.CreatePixelShader(读取着色器("FFF.Recorder.Shaders.CopyBgra.cso"), Nothing)
        Dim 采样描述 As New SamplerDescription(Filter.MinMagMipLinear, TextureAddressMode.Clamp,
            0.0F, 1UI, ComparisonFunction.Never, 0.0F, Single.MaxValue)
        线性采样器 = 图形.设备.CreateSamplerState(采样描述)
        Dim 缓冲描述 As New BufferDescription(CUInt(Marshal.SizeOf(Of 缩放常量)()), BindFlags.ConstantBuffer,
            ResourceUsage.Default, CpuAccessFlags.None, ResourceOptionFlags.None, 0)
        常量缓冲区 = 图形.设备.CreateBuffer(缓冲描述, IntPtr.Zero)
        中间纹理池 = New D3D11纹理池(图形, 8)
        输出纹理池 = New D3D11纹理池(图形, 8)
    End Sub

    Public Shared Function 创建HDR(图形设备 As 图形设备, 输出宽度 As UInteger, 输出高度 As UInteger,
        Optional 参考白尼特 As Single = 80.0F, Optional 目标峰值尼特 As Single = 1000.0F,
        Optional 曝光 As Single = 0.0F) As 视频处理器
        Return New 视频处理器(图形设备, New 视频处理配置 With {
            .输出宽度 = 输出宽度, .输出高度 = 输出高度, .输出HDR10 = True,
            .参考白尼特 = 参考白尼特, .目标峰值尼特 = 目标峰值尼特, .曝光 = 曝光
        })
    End Function

    Public Shared Function 创建HDR转SDR(图形设备 As 图形设备, 输出宽度 As UInteger, 输出高度 As UInteger,
        Optional 参考白尼特 As Single = 80.0F, Optional 源峰值尼特 As Single = 1000.0F) As 视频处理器
        Return New 视频处理器(图形设备, New 视频处理配置 With {
            .输出宽度 = 输出宽度, .输出高度 = 输出高度, .允许HDR转SDR = True,
            .参考白尼特 = 参考白尼特, .目标峰值尼特 = 源峰值尼特
        })
    End Function

    Public ReadOnly Property 输出宽度 As UInteger
        Get
            Return 输出宽度值
        End Get
    End Property

    Public ReadOnly Property 输出高度 As UInteger
        Get
            Return 输出高度值
        End Get
    End Property

    Public Sub 应用到配置(配置 As 录制配置)
        ArgumentNullException.ThrowIfNull(配置)
        配置.宽度 = 输出宽度值
        配置.高度 = 输出高度值
        If 输出HDR模式 Then
            配置.使用十位色 = True
            配置.使用HDR10 = True
            配置.输入纹理格式 = 视频纹理格式.RGB十位
        Else
            配置.使用十位色 = 输出十位模式
            配置.使用HDR10 = False
            配置.输入纹理格式 = If(输出十位模式, 视频纹理格式.RGB十位, 视频纹理格式.BGRA八位)
        End If
    End Sub

    Public Function 处理帧(帧 As 显示器捕获帧) As 处理后视频帧
        ArgumentNullException.ThrowIfNull(帧)
        If 输出HDR模式 AndAlso Not 帧.是HDR源 Then Throw New InvalidOperationException("HDR 输出只接受实际 FP16/scRGB 显示器帧。")
        If Not 输出HDR模式 AndAlso 帧.是HDR源 AndAlso Not 允许HDR转SDR值 Then
            Throw New InvalidOperationException("HDR 显示器帧转成 SDR 必须明确启用。")
        End If
        Return 处理纹理(帧.纹理, 帧.QPC时间戳, 帧.旋转方式)
    End Function

    Public Function 处理帧(帧 As 窗口捕获帧) As 处理后视频帧
        ArgumentNullException.ThrowIfNull(帧)
        If 输出HDR模式 AndAlso Not 帧.是HDR源 Then Throw New InvalidOperationException("HDR 输出只接受实际 FP16/scRGB 捕获帧。")
        If Not 输出HDR模式 AndAlso 帧.是HDR源 AndAlso Not 允许HDR转SDR值 Then
            Throw New InvalidOperationException("HDR 源转成 SDR 必须在视频处理配置中明确启用。")
        End If
        Return 处理纹理(帧.纹理, 帧.QPC时间戳, 视频旋转方式.不旋转)
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        For Each 视图 In 输出预览视图缓存.Values
            视图.Dispose()
        Next
        For Each 视图 In 输出渲染视图缓存.Values
            视图.Dispose()
        Next
        For Each 视图 In 中间写入视图缓存.Values
            视图.Dispose()
        Next
        For Each 视图 In 中间读取视图缓存.Values
            视图.Dispose()
        Next
        输出预览视图缓存.Clear()
        输出渲染视图缓存.Clear()
        中间写入视图缓存.Clear()
        中间读取视图缓存.Clear()
        输出纹理池.释放()
        中间纹理池.释放()
        常量缓冲区.Dispose()
        线性采样器.Dispose()
        像素着色器.Dispose()
        顶点着色器.Dispose()
        色调映射着色器.Dispose()
        HDR计算着色器.Dispose()
        SDR计算着色器.Dispose()
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub

    Private Function 处理纹理(源纹理 As ID3D11Texture2D, 时间戳 As Long,
        旋转方式 As 视频旋转方式) As 处理后视频帧
        If 已释放 Then Throw New ObjectDisposedException(NameOf(视频处理器))
        Dim 源描述 = 源纹理.Description
        Dim 输入HDR = 源描述.Format = Format.R16G16B16A16_Float
        If 输出HDR模式 AndAlso Not 输入HDR Then Throw New NotSupportedException("禁止把 SDR 输入转换为 HDR 输出。")
        If Not 输入HDR AndAlso 源描述.Format <> Format.B8G8R8A8_UNorm AndAlso 源描述.Format <> Format.R8G8B8A8_UNorm Then
            Throw New NotSupportedException($"视频处理器不支持输入格式 {源描述.Format}。")
        End If
        If 输入HDR AndAlso Not 输出HDR模式 AndAlso Not 允许HDR转SDR值 Then Throw New InvalidOperationException("未启用 HDR 转 SDR。")
        Dim 中间格式 = If(输入HDR, Format.R16G16B16A16_Float, Format.R8G8B8A8_UNorm)
        Dim 最终格式 = If(输出十位模式, Format.R10G10B10A2_UNorm, Format.B8G8R8A8_UNorm)
        Dim 中间描述 As New Texture2DDescription(中间格式, 输出宽度值, 输出高度值,
            1, 1, BindFlags.ShaderResource Or BindFlags.UnorderedAccess, ResourceUsage.Default,
            CpuAccessFlags.None, 1, 0, ResourceOptionFlags.None)
        Dim 输出描述 As New Texture2DDescription(最终格式, 输出宽度值, 输出高度值,
            1, 1, BindFlags.ShaderResource Or BindFlags.RenderTarget, ResourceUsage.Default,
            CpuAccessFlags.None, 1, 0, ResourceOptionFlags.None)
        Dim 中间纹理 = 中间纹理池.尝试租用(中间描述)
        If 中间纹理 Is Nothing Then Return Nothing
        Dim 输出纹理 = 输出纹理池.尝试租用(输出描述)
        If 输出纹理 Is Nothing Then
            中间纹理池.归还(中间纹理)
            Return Nothing
        End If
        Try
            If 裁剪左边值 + 裁剪右边值 >= 源描述.Width OrElse 裁剪顶边值 + 裁剪底边值 >= 源描述.Height Then
                Throw New ArgumentOutOfRangeException("裁剪", "裁剪区域不能是空区域。")
            End If
            Dim 裁剪源宽 = 源描述.Width - 裁剪左边值 - 裁剪右边值
            Dim 裁剪源高 = 源描述.Height - 裁剪顶边值 - 裁剪底边值
            Dim 交换尺寸 = 旋转方式 = 视频旋转方式.顺时针九十度 OrElse
                旋转方式 = 视频旋转方式.顺时针二百七十度
            Dim 有效源宽 = If(交换尺寸, 裁剪源高, 裁剪源宽)
            Dim 有效源高 = If(交换尺寸, 裁剪源宽, 裁剪源高)
            Dim 目标宽 = 输出宽度值
            Dim 目标高 = 输出高度值
            Dim 源左 = CSng(裁剪左边值) / 源描述.Width
            Dim 源顶 = CSng(裁剪顶边值) / 源描述.Height
            Dim 源宽比例 = CSng(裁剪源宽) / 源描述.Width
            Dim 源高比例 = CSng(裁剪源高) / 源描述.Height
            If 缩放方式值 = 视频缩放方式.适应 Then
                Dim 比例 = Math.Min(CDbl(输出宽度值) / 有效源宽, CDbl(输出高度值) / 有效源高)
                目标宽 = Math.Max(1UI, CUInt(Math.Round(有效源宽 * 比例)))
                目标高 = Math.Max(1UI, CUInt(Math.Round(有效源高 * 比例)))
            ElseIf 缩放方式值 = 视频缩放方式.填充裁剪 Then
                Dim 源宽高比 = CDbl(有效源宽) / 有效源高
                Dim 输出宽高比 = CDbl(输出宽度值) / 输出高度值
                If 源宽高比 > 输出宽高比 Then
                    Dim 保留宽 = CSng(有效源高 * 输出宽高比 / 源描述.Width)
                    源左 += (源宽比例 - 保留宽) / 2.0F
                    源宽比例 = 保留宽
                Else
                    Dim 保留高 = CSng(有效源宽 / 输出宽高比 / 源描述.Height)
                    源顶 += (源高比例 - 保留高) / 2.0F
                    源高比例 = 保留高
                End If
            End If
            Dim 常量 As New 缩放常量 With {
                .输出宽度 = 输出宽度值, .输出高度 = 输出高度值,
                .目标左边 = (输出宽度值 - 目标宽) \ 2UI, .目标顶边 = (输出高度值 - 目标高) \ 2UI,
                .目标宽度 = 目标宽, .目标高度 = 目标高,
                .源左边比例 = 源左, .源顶边比例 = 源顶, .源宽度比例 = 源宽比例, .源高度比例 = 源高比例,
                .参考白尼特 = 参考白尼特值, .目标峰值尼特 = 目标峰值尼特值,
                .曝光 = 曝光值, .高光压缩 = 高光压缩值, .饱和度 = 饱和度值,
                .旋转 = CUInt(旋转方式), .高质量缩放 = If(高质量缩放值, 1UI, 0UI)
            }
            Dim 本次着色器 = If(输入HDR, If(输出HDR模式, HDR计算着色器, 色调映射着色器), SDR计算着色器)
            Dim 本次预览视图 As ID3D11ShaderResourceView = Nothing
            图形.执行图形命令(
                Sub()
                    Using 输入视图 = 图形.设备.CreateShaderResourceView(源纹理, Nothing)
                        Dim 输出视图 = 获取中间写入视图(中间纹理)
                        图形.上下文.UpdateSubresource(常量, 常量缓冲区)
                        图形.上下文.CSSetShader(本次着色器)
                        图形.上下文.CSSetConstantBuffer(0, 常量缓冲区)
                        图形.上下文.CSSetSampler(0, 线性采样器)
                        图形.上下文.CSSetShaderResource(0, 输入视图)
                        图形.上下文.CSSetUnorderedAccessView(0, 输出视图)
                        图形.上下文.Dispatch((输出宽度值 + 7UI) \ 8UI, (输出高度值 + 7UI) \ 8UI, 1)
                        图形.上下文.CSSetShaderResource(0, Nothing)
                        图形.上下文.CSSetUnorderedAccessView(0, Nothing)
                        图形.上下文.CSSetConstantBuffer(0, Nothing)
                        图形.上下文.CSSetShader(Nothing)
                    End Using
                    Dim 中间视图 = 获取中间读取视图(中间纹理)
                    Dim 最终视图 = 获取输出渲染视图(输出纹理)
                    图形.上下文.OMSetRenderTargets(最终视图, Nothing)
                    图形.上下文.RSSetViewport(0.0F, 0.0F, CSng(输出宽度值), CSng(输出高度值), 0.0F, 1.0F)
                    图形.上下文.IASetPrimitiveTopology(PrimitiveTopology.TriangleList)
                    图形.上下文.VSSetShader(顶点着色器)
                    图形.上下文.PSSetShader(像素着色器)
                    图形.上下文.PSSetSampler(0, 线性采样器)
                    图形.上下文.PSSetShaderResource(0, 中间视图)
                    图形.上下文.Draw(3, 0)
                    图形.上下文.PSSetShaderResource(0, Nothing)
                    图形.上下文.PSSetShader(Nothing)
                    图形.上下文.VSSetShader(Nothing)
                    图形.上下文.OMSetRenderTargets(CType(Nothing, ID3D11RenderTargetView), Nothing)
                    本次预览视图 = 获取输出预览视图(输出纹理)
                End Sub)
            中间纹理池.归还(中间纹理)
            Return New 处理后视频帧(输出纹理, 本次预览视图, 时间戳, 输出HDR模式,
                AddressOf 输出纹理池.归还)
        Catch
            中间纹理池.归还(中间纹理)
            输出纹理池.归还(输出纹理)
            Throw
        End Try
    End Function

    Private Function 获取中间读取视图(纹理 As ID3D11Texture2D) As ID3D11ShaderResourceView
        Return 获取或创建视图(中间读取视图缓存, 纹理.NativePointer,
            Function() 图形.设备.CreateShaderResourceView(纹理, Nothing))
    End Function

    Private Function 获取中间写入视图(纹理 As ID3D11Texture2D) As ID3D11UnorderedAccessView
        Return 获取或创建视图(中间写入视图缓存, 纹理.NativePointer,
            Function() 图形.设备.CreateUnorderedAccessView(纹理, Nothing))
    End Function

    Private Function 获取输出渲染视图(纹理 As ID3D11Texture2D) As ID3D11RenderTargetView
        Return 获取或创建视图(输出渲染视图缓存, 纹理.NativePointer,
            Function() 图形.设备.CreateRenderTargetView(纹理, Nothing))
    End Function

    Private Function 获取输出预览视图(纹理 As ID3D11Texture2D) As ID3D11ShaderResourceView
        Return 获取或创建视图(输出预览视图缓存, 纹理.NativePointer,
            Function() 图形.设备.CreateShaderResourceView(纹理, Nothing))
    End Function

    Private Shared Function 获取或创建视图(Of T)(缓存 As Dictionary(Of IntPtr, T), 键 As IntPtr,
        创建 As Func(Of T)) As T
        Dim 视图 As T = Nothing
        If 缓存.TryGetValue(键, 视图) Then Return 视图
        视图 = 创建()
        缓存.Add(键, 视图)
        Return 视图
    End Function

    Friend Shared Function 读取着色器(资源名称 As String) As Byte()
        Return 着色器缓存.GetOrAdd(资源名称, AddressOf 从资源读取着色器)
    End Function

    Private Shared Function 从资源读取着色器(资源名称 As String) As Byte()
        Using 流 = Assembly.GetExecutingAssembly().GetManifestResourceStream(资源名称)
            If 流 Is Nothing Then Throw New InvalidOperationException($"缺少内嵌 shader bytecode：{资源名称}")
            Using 内存 As New IO.MemoryStream()
                流.CopyTo(内存)
                Return 内存.ToArray()
            End Using
        End Using
    End Function
End Class
