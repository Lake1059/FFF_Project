Imports Vortice.DirectWrite

''' <summary>
''' 使用 LakeUI 的窗口级 D3D11/Direct2D 合成器绘制剪辑时间轴。
''' 控件只保存显示和鼠标交互状态，不直接操作播放器会话。
''' </summary>
Friend NotInheritable Class 剪辑区间进度条控件
    Inherits Control
    Implements LakeUI.D3D_IGpuRenderable, LakeUI.D3D_IGpuInvalidationSource,
               LakeUI.V5_IGpuPresentationSource

    Private 当前播放位置 As TimeSpan
    Private 媒体总时长 As TimeSpan
    Private 当前入点 As TimeSpan?
    Private 当前出点 As TimeSpan?
    Private 正在拖动 As Boolean
    Private 拖动位置 As TimeSpan?

    Friend Sub New()
        SetStyle(ControlStyles.UserPaint Or ControlStyles.AllPaintingInWmPaint Or
                 ControlStyles.ResizeRedraw Or
                 ControlStyles.SupportsTransparentBackColor, True)
        Font = New Font("Microsoft YaHei UI", 9.0F)
        ForeColor = Color.Silver
        BackColor = Color.FromArgb(40, 40, 40)
        Cursor = Cursors.Hand
        TabStop = False
    End Sub

    Friend Event 请求跳转 As EventHandler(Of 播放器跳转请求事件参数)

    Friend ReadOnly Property 入点 As TimeSpan?
        Get
            Return 当前入点
        End Get
    End Property

    Friend ReadOnly Property 出点 As TimeSpan?
        Get
            Return 当前出点
        End Get
    End Property

    Friend Sub 更新播放状态(位置 As TimeSpan, 总时长 As TimeSpan)
        Dim 新总时长 = If(总时长 > TimeSpan.Zero, 总时长, TimeSpan.Zero)
        Dim 新位置 = 限制到媒体(位置, 新总时长)
        If 当前播放位置 = 新位置 AndAlso 媒体总时长 = 新总时长 Then Return
        当前播放位置 = 新位置
        媒体总时长 = 新总时长
        Invalidate()
    End Sub

    Friend Sub 清除媒体()
        当前播放位置 = TimeSpan.Zero
        媒体总时长 = TimeSpan.Zero
        当前入点 = Nothing
        当前出点 = Nothing
        正在拖动 = False
        拖动位置 = Nothing
        Capture = False
        Invalidate()
    End Sub

    Friend Sub 设为入点(位置 As TimeSpan)
        当前入点 = 限制到媒体(位置, 媒体总时长)
        Invalidate()
    End Sub

    Friend Sub 设为出点(位置 As TimeSpan)
        当前出点 = 限制到媒体(位置, 媒体总时长)
        Invalidate()
    End Sub

    Protected Overrides Sub OnPaintBackground(e As PaintEventArgs)
        MyBase.OnPaintBackground(e)
    End Sub

    Protected Overrides Sub OnPaint(e As PaintEventArgs)
        If Not LakeUI.D3D_PaintBridge.PaintRenderable(e, Me, Me) Then MyBase.OnPaint(e)
    End Sub

    Public Sub RenderGpu(context As LakeUI.D3D_PaintContext) Implements LakeUI.D3D_IGpuRenderable.RenderGpu
        If context Is Nothing OrElse ClientSize.Width <= 0 OrElse ClientSize.Height <= 0 Then Return

        Dim 宽度 = CSng(ClientSize.Width)
        Dim 高度 = CSng(ClientSize.Height)
        Dim 全区域 As New RectangleF(0.0F, 0.0F, 宽度, 高度)
        If BackColor.A > 0 Then context.FillRectangle(全区域, BackColor)

        If 媒体总时长 <= TimeSpan.Zero Then
            context.DrawText("打开媒体后可设置剪辑区间", Font, Color.Gray, 全区域,
                TextAlignment.Center, ParagraphAlignment.Center)
            Return
        End If

        Dim 显示位置 = If(拖动位置.HasValue, 拖动位置.Value, 当前播放位置)
        Dim 进度宽度 = 时间到X(显示位置, 宽度)
        If 进度宽度 > 0.0F Then
            Dim 进度区域 As New RectangleF(0.0F, 0.0F, 进度宽度, 高度)
            context.FillRectangle(进度区域, Color.FromArgb(120, 220, 220, 220))
            context.DrawRectangle(New RectangleF(0.5F, 0.5F,
                Math.Max(0.0F, 进度宽度 - 1.0F), Math.Max(0.0F, 高度 - 1.0F)),
                Color.FromArgb(120, 120, 120))
        End If

        If 当前入点.HasValue Then
            绘制标记(context, 时间到X(当前入点.Value, 宽度), 高度,
                Color.FromArgb(255, 100, 100), True)
        End If
        If 当前出点.HasValue Then
            绘制标记(context, 时间到X(当前出点.Value, 宽度), 高度,
                Color.FromArgb(100, 150, 255), False)
        End If

        Dim 当前X = 时间到X(显示位置, 宽度)
        context.FillRectangle(New RectangleF(Math.Max(0.0F, 当前X - 1.0F), 0.0F, 2.0F, 高度),
            Color.FromArgb(200, 200, 200))
    End Sub

    Public Function GetRenderBounds() As Rectangle Implements LakeUI.D3D_IGpuInvalidationSource.GetRenderBounds
        Return New Rectangle(Point.Empty, Size)
    End Function

    Protected Overrides Sub OnMouseDown(e As MouseEventArgs)
        MyBase.OnMouseDown(e)
        If e.Button <> MouseButtons.Left OrElse 媒体总时长 <= TimeSpan.Zero Then Return
        正在拖动 = True
        Capture = True
        拖动位置 = X到时间(e.X)
        Invalidate()
    End Sub

    Protected Overrides Sub OnMouseMove(e As MouseEventArgs)
        MyBase.OnMouseMove(e)
        If Not 正在拖动 Then Return
        拖动位置 = X到时间(e.X)
        Invalidate()
    End Sub

    Protected Overrides Sub OnMouseUp(e As MouseEventArgs)
        MyBase.OnMouseUp(e)
        If e.Button <> MouseButtons.Left OrElse Not 正在拖动 Then Return
        完成拖动(True)
    End Sub

    Protected Overrides Sub OnMouseCaptureChanged(e As EventArgs)
        MyBase.OnMouseCaptureChanged(e)
        If 正在拖动 AndAlso Not Capture Then 完成拖动(False)
    End Sub

    Private Sub 完成拖动(提交跳转 As Boolean)
        Dim 目标 = If(拖动位置.HasValue, 拖动位置.Value, 当前播放位置)
        正在拖动 = False
        拖动位置 = Nothing
        Capture = False
        当前播放位置 = 目标
        Invalidate()
        If 提交跳转 Then RaiseEvent 请求跳转(Me, New 播放器跳转请求事件参数(目标))
    End Sub

    Private Function X到时间(x As Integer) As TimeSpan
        If 媒体总时长 <= TimeSpan.Zero Then Return TimeSpan.Zero
        Dim 最大X = Math.Max(1, ClientSize.Width - 1)
        Dim 比例 = Math.Clamp(CDbl(x) / 最大X, 0.0R, 1.0R)
        Return TimeSpan.FromTicks(CLng(Math.Round(媒体总时长.Ticks * 比例)))
    End Function

    Private Function 时间到X(时间 As TimeSpan, 宽度 As Single) As Single
        If 媒体总时长 <= TimeSpan.Zero OrElse 宽度 <= 0.0F Then Return 0.0F
        Dim 比例 = Math.Clamp(时间.TotalMilliseconds / 媒体总时长.TotalMilliseconds, 0.0R, 1.0R)
        Return CSng(比例 * Math.Max(0.0F, 宽度 - 1.0F))
    End Function

    Private Shared Sub 绘制标记(context As LakeUI.D3D_PaintContext, x As Single, 高度 As Single,
                              颜色 As Color, 顶部 As Boolean)
        context.FillRectangle(New RectangleF(Math.Max(0.0F, x - 1.0F), 0.0F, 2.0F, 高度), 颜色)
        Const 标记高度 As Integer = 10
        Const 半宽 As Single = 5.0F
        For 行 = 0 To 标记高度 - 1
            Dim 本行半宽 = 半宽 * (1.0F - CSng(行) / 标记高度)
            Dim y = If(顶部, CSng(行), Math.Max(0.0F, 高度 - 行 - 1.0F))
            context.FillRectangle(New RectangleF(x - 本行半宽, y,
                Math.Max(1.0F, 本行半宽 * 2.0F), 1.0F), 颜色)
        Next
    End Sub

    Private Shared Function 限制到媒体(位置 As TimeSpan, 总时长 As TimeSpan) As TimeSpan
        If 位置 < TimeSpan.Zero Then Return TimeSpan.Zero
        If 总时长 > TimeSpan.Zero AndAlso 位置 > 总时长 Then Return 总时长
        Return 位置
    End Function

    Friend Shared Function 格式化时长(时间 As TimeSpan) As String
        If 时间 < TimeSpan.Zero Then 时间 = TimeSpan.Zero
        Dim 总小时 = CLng(Math.Floor(时间.TotalHours))
        Return $"{总小时:00}:{时间.Minutes:00}:{时间.Seconds:00}.{时间.Milliseconds:000}"
    End Function
End Class
