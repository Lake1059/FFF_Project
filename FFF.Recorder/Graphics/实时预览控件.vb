Imports System.Drawing
Imports System.Threading.Tasks
Imports System.Windows.Forms
Imports Vortice.Direct3D
Imports Vortice.Direct3D11
Imports Vortice.DXGI

Public NotInheritable Class 实时预览控件
    Inherits Control

    Private ReadOnly 同步锁 As New Object
    Private 当前源 As 视频源条目
    Private 空闲预览 As 预览控制器
    Private 当前图形 As 图形设备
    Private 交换链 As IDXGISwapChain1
    Private 交换链后缓冲区 As ID3D11Texture2D
    Private 交换链渲染目标 As ID3D11RenderTargetView
    Private 顶点着色器 As ID3D11VertexShader
    Private 像素着色器 As ID3D11PixelShader
    Private 采样器 As ID3D11SamplerState
    Private 交换链格式 As Format = Format.Unknown
    Private 交换链宽度 As Integer
    Private 交换链高度 As Integer
    Private 录制接管 As Boolean
    Private 活动 As Boolean
    Private 已释放 As Boolean
    Private 源代次 As Integer
    Private 状态文本 As String = "请选择视频源"
    Private 切换任务 As Task = Task.CompletedTask

    Public Sub New()
        SetStyle(ControlStyles.Opaque Or ControlStyles.ResizeRedraw, True)
        BackColor = System.Drawing.Color.Black
        ForeColor = System.Drawing.Color.Silver
    End Sub

    Public Sub 设置源(源 As 视频源条目)
        Dim 待释放 As 预览控制器
        Dim 当前代次 As Integer
        Dim 应启动 As Boolean
        SyncLock 同步锁
            当前源 = 源
            源代次 += 1
            当前代次 = 源代次
            状态文本 = If(源 Is Nothing, "请选择视频源", "正在连接预览...")
            待释放 = 取出空闲预览()
            释放渲染资源()
            应启动 = 活动 AndAlso 源 IsNot Nothing AndAlso Not 录制接管 AndAlso IsHandleCreated
        End SyncLock
        Invalidate()
        排队切换空闲预览(待释放, If(应启动, 源, Nothing), 当前代次)
    End Sub

    Public Sub 设置活动(是否活动 As Boolean)
        Dim 待释放 As 预览控制器
        Dim 源 As 视频源条目
        Dim 当前代次 As Integer
        Dim 应启动 As Boolean
        SyncLock 同步锁
            If 活动 = 是否活动 Then Return
            活动 = 是否活动
            源代次 += 1
            当前代次 = 源代次
            源 = 当前源
            待释放 = 取出空闲预览()
            释放渲染资源()
            应启动 = 活动 AndAlso 源 IsNot Nothing AndAlso Not 录制接管 AndAlso IsHandleCreated
            状态文本 = If(Not 活动, String.Empty,
                If(源 Is Nothing, "请选择视频源", If(录制接管, "等待录制画面...", "正在连接预览...")))
        End SyncLock
        Invalidate()
        排队切换空闲预览(待释放, If(应启动, 源, Nothing), 当前代次)
    End Sub

    Public Sub 开始录制预览()
        Dim 待释放 As 预览控制器
        SyncLock 同步锁
            录制接管 = True
            源代次 += 1
            待释放 = 取出空闲预览()
            释放渲染资源()
            状态文本 = If(活动, "等待录制画面...", String.Empty)
        End SyncLock
        Invalidate()
        排队切换空闲预览(待释放, Nothing, 0)
    End Sub

    Public Sub 结束录制预览()
        Dim 源 As 视频源条目
        Dim 当前代次 As Integer
        Dim 应启动 As Boolean
        SyncLock 同步锁
            录制接管 = False
            源代次 += 1
            当前代次 = 源代次
            源 = 当前源
            释放渲染资源()
            状态文本 = If(Not 活动, String.Empty, If(源 Is Nothing, "请选择视频源", "正在连接预览..."))
            应启动 = 活动 AndAlso 源 IsNot Nothing AndAlso IsHandleCreated
        End SyncLock
        Invalidate()
        If 应启动 Then 排队切换空闲预览(Nothing, 源, 当前代次)
    End Sub

    Friend Sub 提交GPU帧(图形 As 图形设备, 帧 As 处理后视频帧)
        提交预览帧(Nothing, 图形, 帧)
    End Sub

    Friend Sub 提交空闲GPU帧(控制器 As 预览控制器, 图形 As 图形设备, 帧 As 处理后视频帧)
        提交预览帧(控制器, 图形, 帧)
    End Sub

    Private Sub 提交预览帧(控制器 As 预览控制器, 图形 As 图形设备, 帧 As 处理后视频帧)
        If 已释放 OrElse Not 活动 OrElse 图形 Is Nothing OrElse 帧 Is Nothing OrElse
            Not IsHandleCreated OrElse ClientSize.Width <= 0 OrElse ClientSize.Height <= 0 Then Return
        If Not Threading.Monitor.TryEnter(同步锁) Then Return
        Try
            If 已释放 OrElse Not 活动 Then Return
            If 控制器 Is Nothing Then
                If Not 录制接管 Then Return
            ElseIf 空闲预览 IsNot 控制器 OrElse 录制接管 Then
                Return
            End If
            Try
                图形.执行图形命令(Sub() 渲染(图形, 帧))
                状态文本 = String.Empty
            Catch ex As Exception
                状态文本 = $"预览不可用：{ex.Message}"
                释放渲染资源()
                Invalidate()
            End Try
        Finally
            Threading.Monitor.Exit(同步锁)
        End Try
    End Sub

    Friend Sub 报告预览错误(控制器 As 预览控制器, 消息 As String)
        If 已释放 Then Return
        SyncLock 同步锁
            If Not 活动 OrElse 空闲预览 IsNot 控制器 OrElse 录制接管 Then Return
            状态文本 = $"预览不可用：{消息}"
        End SyncLock
        If IsHandleCreated Then BeginInvoke(Sub() Invalidate())
    End Sub

    Protected Overrides Sub OnHandleCreated(e As EventArgs)
        MyBase.OnHandleCreated(e)
        Dim 源 As 视频源条目
        Dim 当前代次 As Integer
        Dim 允许启动 As Boolean
        SyncLock 同步锁
            源 = 当前源
            当前代次 = 源代次
            允许启动 = 活动 AndAlso Not 录制接管
        End SyncLock
        If 源 IsNot Nothing AndAlso 允许启动 Then 排队切换空闲预览(Nothing, 源, 当前代次)
    End Sub

    Protected Overrides Sub OnHandleDestroyed(e As EventArgs)
        Dim 待释放 As 预览控制器
        SyncLock 同步锁
            源代次 += 1
            待释放 = 取出空闲预览()
            释放渲染资源()
        End SyncLock
        待释放?.释放()
        MyBase.OnHandleDestroyed(e)
    End Sub

    Protected Overrides Sub OnPaint(e As PaintEventArgs)
        If 交换链 Is Nothing Then
            e.Graphics.Clear(System.Drawing.Color.Black)
            If Not String.IsNullOrWhiteSpace(状态文本) Then
                Using brush As New SolidBrush(System.Drawing.Color.FromArgb(190, ForeColor))
                    Dim size = e.Graphics.MeasureString(状态文本, Font, Math.Max(1, Width - 24))
                    e.Graphics.DrawString(状态文本, Font, brush,
                        Math.Max(12, (Width - size.Width) / 2), Math.Max(12, (Height - size.Height) / 2))
                End Using
            End If
        End If
    End Sub

    Protected Overrides Sub Dispose(disposing As Boolean)
        If disposing AndAlso Not 已释放 Then
            Dim 待释放 As 预览控制器
            SyncLock 同步锁
                已释放 = True
                源代次 += 1
                待释放 = 取出空闲预览()
                释放渲染资源()
            End SyncLock
            待释放?.释放()
        End If
        MyBase.Dispose(disposing)
    End Sub

    Private Sub 排队切换空闲预览(待释放 As 预览控制器, 源 As 视频源条目, 代次 As Integer)
        Dim 预览尺寸 = ClientSize
        Dim 捕获鼠标 = 设置.实例对象.捕获鼠标
        SyncLock 同步锁
            ' 所有释放和启动共用一条任务链，避免多个 WGC/D3D 初始化同时争用 GPU。
            切换任务 = 切换任务.ContinueWith(
                Sub(已完成任务) 执行空闲预览切换(待释放, 源, 代次, 预览尺寸, 捕获鼠标),
                TaskScheduler.Default)
        End SyncLock
    End Sub

    Private Sub 执行空闲预览切换(待释放 As 预览控制器, 源 As 视频源条目, 代次 As Integer,
        预览尺寸 As Size, 捕获鼠标 As Boolean)
        Try
            待释放?.释放()
        Catch
        End Try
        If 源 Is Nothing Then Return

        Dim 新预览 As New 预览控制器(Me, 源, 捕获鼠标,
            Math.Max(1, 预览尺寸.Width), Math.Max(1, 预览尺寸.Height))
        Try
            SyncLock 同步锁
                If 已释放 OrElse Not 活动 OrElse 录制接管 OrElse Not IsHandleCreated OrElse
                    代次 <> 源代次 OrElse 当前源 IsNot 源 OrElse 空闲预览 IsNot Nothing Then Return
                空闲预览 = 新预览
            End SyncLock
            新预览.开始()
        Catch ex As Exception
            SyncLock 同步锁
                If 空闲预览 Is 新预览 Then
                    空闲预览 = Nothing
                    If 代次 = 源代次 AndAlso 当前源 Is 源 Then
                        状态文本 = $"预览不可用：{ex.Message}"
                    End If
                End If
            End SyncLock
            If IsHandleCreated Then
                Try
                    BeginInvoke(Sub() Invalidate())
                Catch ex2 As InvalidOperationException
                End Try
            End If
        Finally
            SyncLock 同步锁
                If 空闲预览 Is 新预览 Then 新预览 = Nothing
            End SyncLock
            新预览?.释放()
        End Try
    End Sub

    Private Function 取出空闲预览() As 预览控制器
        Dim 结果 = 空闲预览
        空闲预览 = Nothing
        Return 结果
    End Function

    Private Sub 渲染(图形 As 图形设备, 帧 As 处理后视频帧)
        Dim 纹理 = 帧.纹理
        Dim source = 纹理.Description
        Dim swapFormat As Format = If(source.Format = Format.R10G10B10A2_UNorm,
            Format.R10G10B10A2_UNorm, Format.B8G8R8A8_UNorm)
        If 当前图形 IsNot 图形 OrElse 交换链 Is Nothing OrElse 交换链格式 <> swapFormat OrElse
            交换链宽度 <> ClientSize.Width OrElse 交换链高度 <> ClientSize.Height Then
            释放渲染资源()
            创建渲染资源(图形, swapFormat, 帧.是HDR输出)
        End If
        If source.Width <= 0 OrElse source.Height <= 0 Then Return
        ' Keep the source aspect ratio. The swap chain itself is stretched by
        ' the window system, so the viewport must be letterboxed explicitly.
        Dim scale = Math.Min(CDbl(交换链宽度) / source.Width, CDbl(交换链高度) / source.Height)
        Dim w = CSng(Math.Max(1.0, Math.Round(source.Width * scale)))
        Dim h = CSng(Math.Max(1.0, Math.Round(source.Height * scale)))
        图形.上下文.OMSetRenderTargets(交换链渲染目标, Nothing)
        图形.上下文.RSSetViewport((交换链宽度 - w) / 2.0F, (交换链高度 - h) / 2.0F, w, h, 0.0F, 1.0F)
        图形.上下文.IASetPrimitiveTopology(PrimitiveTopology.TriangleList)
        图形.上下文.VSSetShader(顶点着色器)
        图形.上下文.PSSetShader(像素着色器)
        图形.上下文.PSSetSampler(0, 采样器)
        图形.上下文.PSSetShaderResource(0, 帧.预览视图)
        图形.上下文.Draw(3, 0)
        图形.上下文.PSSetShaderResource(0, Nothing)
        图形.上下文.PSSetShader(Nothing)
        图形.上下文.VSSetShader(Nothing)
        图形.上下文.OMSetRenderTargets(CType(Nothing, ID3D11RenderTargetView), Nothing)
        交换链.Present(0UI, PresentFlags.None)
    End Sub

    Private Sub 创建渲染资源(图形 As 图形设备, format As Format, HDR输出 As Boolean)
        Using dxgiDevice = 图形.设备.QueryInterface(Of IDXGIDevice)()
            Dim adapter As IDXGIAdapter = Nothing
            Dim result = dxgiDevice.GetAdapter(adapter)
            If result.Failure Then Throw New InvalidOperationException($"读取预览适配器失败：0x{result.Code:X8}")
            Using adapter
                Using factory = adapter.GetParent(Of IDXGIFactory2)()
                    Dim description As New SwapChainDescription1(CUInt(ClientSize.Width), CUInt(ClientSize.Height),
                        format, False, Usage.RenderTargetOutput, 2UI, Scaling.Stretch,
                        SwapEffect.FlipDiscard, AlphaMode.Ignore, SwapChainFlags.None)
                    交换链 = factory.CreateSwapChainForHwnd(图形.设备, Handle, description, Nothing, Nothing)
                End Using
            End Using
        End Using
        Using swapChain3 = 交换链.QueryInterfaceOrNull(Of IDXGISwapChain3)()
            If swapChain3 IsNot Nothing Then
                swapChain3.SetColorSpace1(If(HDR输出, ColorSpaceType.RgbFullG2084NoneP2020,
                    ColorSpaceType.RgbFullG22NoneP709))
            End If
        End Using
        交换链后缓冲区 = 交换链.GetBuffer(Of ID3D11Texture2D)(0)
        交换链渲染目标 = 图形.设备.CreateRenderTargetView(交换链后缓冲区, Nothing)
        顶点着色器 = 图形.设备.CreateVertexShader(视频处理器.读取着色器("FFF.Recorder.Shaders.FullscreenTriangle.cso"), Nothing)
        像素着色器 = 图形.设备.CreatePixelShader(视频处理器.读取着色器("FFF.Recorder.Shaders.CopyBgra.cso"), Nothing)
        采样器 = 图形.设备.CreateSamplerState(New SamplerDescription(Filter.MinMagMipLinear,
            TextureAddressMode.Clamp, 0.0F, 1UI, ComparisonFunction.Never, 0.0F, Single.MaxValue))
        当前图形 = 图形
        交换链格式 = format
        交换链宽度 = ClientSize.Width
        交换链高度 = ClientSize.Height
    End Sub

    Private Sub 释放渲染资源()
        交换链渲染目标?.Dispose()
        交换链后缓冲区?.Dispose()
        采样器?.Dispose()
        像素着色器?.Dispose()
        顶点着色器?.Dispose()
        交换链?.Dispose()
        交换链渲染目标 = Nothing
        交换链后缓冲区 = Nothing
        采样器 = Nothing
        像素着色器 = Nothing
        顶点着色器 = Nothing
        交换链 = Nothing
        当前图形 = Nothing
        交换链格式 = Format.Unknown
    End Sub
End Class
