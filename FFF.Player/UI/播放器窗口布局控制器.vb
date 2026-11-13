''' <summary>
''' 管理视频宿主窗口的重绑节流、首次 16:9 容器比例校正，以及“按内容原始尺寸 1:1”模式。
''' 1:1 模式只负责窗口几何；画面不放大由原生视口封顶兜底（旧内核缺失时静默跳过）。
''' </summary>
Friend NotInheritable Class 播放器窗口布局控制器
    Implements IDisposable

    Private ReadOnly 窗体 As Form
    Private ReadOnly 视频容器 As Control
    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 重绑输出窗口 As Action
    Private ReadOnly 设置原生尺寸封顶 As Func(Of Boolean, Boolean)
    Private ReadOnly 重绑计时器 As LakeUI.PrecisionTimer
    Private ReadOnly 默认最小尺寸 As Size

    Private 已校正启动视频比例 As Boolean
    Private 已释放 As Boolean

    ' 内容尺寸模式状态。
    Private 内容模式启用 As Boolean
    Private 界面已就绪标志 As Boolean
    Private 全屏状态 As Boolean
    Private 手动覆盖 As Boolean
    Private 最后内容原始大小 As Size = Size.Empty
    Private 最后内容设备大小 As Size = Size.Empty
    Private 最后内容旋转 As UInteger
    Private 应用代数 As Integer

    Friend Sub New(窗体 As Form, 视频容器 As Control, 画面控件 As 播放器画面控件,
                   重绑输出窗口 As Action,
                   Optional 设置原生尺寸封顶 As Func(Of Boolean, Boolean) = Nothing,
                   Optional 界面已就绪值 As Boolean = True)
        ArgumentNullException.ThrowIfNull(窗体)
        ArgumentNullException.ThrowIfNull(视频容器)
        ArgumentNullException.ThrowIfNull(画面控件)
        ArgumentNullException.ThrowIfNull(重绑输出窗口)
        Me.窗体 = 窗体
        Me.视频容器 = 视频容器
        Me.画面控件 = 画面控件
        Me.重绑输出窗口 = 重绑输出窗口
        Me.设置原生尺寸封顶 = 设置原生尺寸封顶
        界面已就绪标志 = 界面已就绪值
        默认最小尺寸 = 窗体.MinimumSize
        重绑计时器 = New LakeUI.PrecisionTimer With {
            .Interval = 180,
            .AutoReset = False,
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .SynchronizingObject = 窗体
        }

        AddHandler 画面控件.输出窗口创建, AddressOf 输出窗口已创建
        AddHandler 画面控件.SizeChanged, AddressOf 请求重绑
        AddHandler 窗体.LocationChanged, AddressOf 请求重绑
        AddHandler 重绑计时器.Tick, AddressOf 重绑计时器_Tick
    End Sub

    Friend Sub 校正初始视频比例()
        If 已释放 OrElse 已校正启动视频比例 OrElse 窗体.IsDisposed OrElse Not 窗体.IsHandleCreated Then Return
        Dim 容器宽度 = 视频容器.ClientSize.Width
        If 容器宽度 <= 0 OrElse 视频容器.Height <= 0 Then Return

        Dim 目标容器高度 = CInt(Math.Round(容器宽度 * 9.0R / 16.0R))
        ' 使用已完成停靠布局的实际边界计算：不可见的剪辑区间面板不会占用视频容器上下空间。
        ' 视频容器的 Top / Bottom 同时涵盖 ThisIsYourWindow 注入的标题栏、绘制边框与所有可见工具栏。
        Dim 客户区非视频高度 = 视频容器.Top + (窗体.ClientSize.Height - 视频容器.Bottom)
        Dim 非客户区边框高度 = Math.Max(0, 窗体.Height - 窗体.ClientSize.Height)
        Dim 目标窗体高度 = 目标容器高度 + 客户区非视频高度 + 非客户区边框高度

        窗体.Size = New Size(窗体.Width, Math.Max(窗体.MinimumSize.Height, 目标窗体高度))
        已校正启动视频比例 = True
    End Sub

    Friend Sub 应用初始画面尺寸(目标画面大小 As Size)
        If 已释放 OrElse 已校正启动视频比例 OrElse 窗体.IsDisposed OrElse Not 窗体.IsHandleCreated OrElse
            目标画面大小.Width <= 0 OrElse 目标画面大小.Height <= 0 Then Return
        应用画面尺寸(目标画面大小)
        已校正启动视频比例 = True
    End Sub

    ''' <summary>媒体打开后调用；保存旋转后的内容尺寸并允许自动应用。每次调用都会清除上一次的手动覆盖。</summary>
    Friend Sub 应用内容原始尺寸(内容大小 As Size, 四分一转 As UInteger)
        If 已释放 OrElse 窗体.IsDisposed Then Return
        手动覆盖 = False
        Dim 内容设备大小 = 计算旋转后内容尺寸(内容大小, 四分一转)
        If 内容设备大小.IsEmpty Then
            ' 纯音频/无视频：清掉上一次媒体的尺寸，避免 DPI 变化时拿旧尺寸改窗口。
            最后内容原始大小 = Size.Empty
            最后内容设备大小 = Size.Empty
            Return
        End If
        最后内容原始大小 = 内容大小
        最后内容旋转 = 四分一转
        最后内容设备大小 = 内容设备大小
        If 内容模式启用 AndAlso Not 全屏状态 AndAlso 界面已就绪标志 Then 请求应用内容尺寸()
    End Sub

    ''' <summary>用户旋转当前视图后调用；旋转后的内容尺寸需要交换宽高并重算窗口。</summary>
    Friend Sub 更新内容旋转(四分一转 As UInteger)
        If 已释放 OrElse 最后内容原始大小.IsEmpty Then Return
        If 四分一转 = 最后内容旋转 Then Return
        最后内容旋转 = 四分一转
        最后内容设备大小 = 计算旋转后内容尺寸(最后内容原始大小, 四分一转)
        If 内容模式启用 AndAlso Not 全屏状态 AndAlso Not 手动覆盖 AndAlso 界面已就绪标志 Then
            请求应用内容尺寸()
        End If
    End Sub

    ''' <summary>模式切换、DPI 变化、退出全屏时重放当前内容尺寸；没有媒体时自然早退。</summary>
    Friend Sub 重新应用内容尺寸()
        If 已释放 OrElse 最后内容设备大小.IsEmpty OrElse Not 界面已就绪标志 Then Return
        If 内容模式启用 AndAlso Not 全屏状态 AndAlso Not 手动覆盖 Then 请求应用内容尺寸()
    End Sub

    ''' <summary>设置内容尺寸模式；关闭时释放原生封顶，当前窗口不做强制缩回。</summary>
    Friend Sub 设置内容尺寸模式(启用 As Boolean)
        If 已释放 Then Return
        内容模式启用 = 启用
        手动覆盖 = False
        If Not 启用 Then
            设置封顶(False)
            窗体.MinimumSize = 默认最小尺寸
            Return
        End If
        If Not 最后内容设备大小.IsEmpty AndAlso 界面已就绪标志 AndAlso Not 全屏状态 Then
            请求应用内容尺寸()
        End If
    End Sub

    ''' <summary>图片启动时 Attach 完成后再调用；此前媒体事件可能已经存下内容尺寸。</summary>
    Friend Sub 界面已就绪()
        If 已释放 Then Return
        界面已就绪标志 = True
        If 内容模式启用 AndAlso Not 全屏状态 AndAlso Not 手动覆盖 Then 请求应用内容尺寸()
    End Sub

    ''' <summary>全屏优先；进入时挂起自动尺寸，退出后再恢复。</summary>
    Friend Sub 设置全屏状态(全屏 As Boolean)
        If 已释放 Then Return
        全屏状态 = 全屏
        If 全屏 Then
            ' 全屏必须铺满屏幕；1:1 的视口封顶只属于普通窗口/最大化窗口。
            设置封顶(False)
            Return
        End If
        If 内容模式启用 AndAlso Not 手动覆盖 Then 请求应用内容尺寸()
    End Sub

    ''' <summary>用户从“调整渲染区域大小”菜单选过尺寸后，本次媒体不再自动覆盖。</summary>
    Friend Sub 标记手动尺寸()
        If 已释放 Then Return
        手动覆盖 = True
        应用代数 += 1
        ' 手动尺寸按菜单语义缩放适配，不能继续用 1:1 封顶把画面钉在源尺寸。
        设置封顶(False)
    End Sub

    Friend Sub 调整画面尺寸(目标画面大小 As Size, Optional 使用物理像素 As Boolean = False)
        If 已释放 OrElse 窗体.IsDisposed OrElse Not 窗体.IsHandleCreated OrElse
            目标画面大小.Width <= 0 OrElse 目标画面大小.Height <= 0 Then Return
        标记手动尺寸()
        If 窗体.WindowState <> FormWindowState.Normal Then 窗体.WindowState = FormWindowState.Normal
        应用画面尺寸(目标画面大小, 使用物理像素, 居中窗口:=False)
    End Sub

    Private Sub 应用画面尺寸(目标画面大小 As Size, Optional 使用物理像素 As Boolean = False,
                        Optional 居中窗口 As Boolean = True)
        窗体.PerformLayout()
        Dim 目标画面设备大小 = 计算目标画面设备大小(目标画面大小, 窗体.DeviceDpi, 使用物理像素)
        Dim 客户区非视频宽度 = Math.Max(0, 窗体.ClientSize.Width - 视频容器.ClientSize.Width)
        Dim 客户区非视频高度 = Math.Max(0, 窗体.ClientSize.Height - 视频容器.ClientSize.Height)
        Dim 非客户区宽度 = Math.Max(0, 窗体.Width - 窗体.ClientSize.Width)
        Dim 非客户区高度 = Math.Max(0, 窗体.Height - 窗体.ClientSize.Height)
        Dim 目标宽度 = 目标画面设备大小.Width + 客户区非视频宽度 + 非客户区宽度
        Dim 目标高度 = 目标画面设备大小.Height + 客户区非视频高度 + 非客户区高度
        Dim workingArea = Screen.FromControl(窗体).WorkingArea
        Dim 最终大小 = New Size(Math.Clamp(目标宽度, 窗体.MinimumSize.Width, workingArea.Width),
                               Math.Clamp(目标高度, 窗体.MinimumSize.Height, workingArea.Height))
        窗体.StartPosition = FormStartPosition.Manual
        If 居中窗口 Then
            窗体.Bounds = 计算工作区居中边界(最终大小, workingArea)
        Else
            窗体.Size = 最终大小
        End If
    End Sub

    ''' <summary>排队一次应用；用代数丢弃“用户已手动改尺寸/又开了新媒体”前的过期回调。</summary>
    Private Sub 请求应用内容尺寸()
        If 已释放 OrElse 窗体.IsDisposed Then Return
        应用代数 += 1
        Dim 本次代数 = 应用代数
        If Not 窗体.IsHandleCreated Then Return
        Try
            窗体.BeginInvoke(Sub()
                               If Not 已释放 AndAlso 本次代数 = 应用代数 Then 应用内容尺寸布局()
                           End Sub)
        Catch ex As InvalidOperationException
            ' 窗体正在销毁；下一次媒体事件会重新请求。
        End Try
    End Sub

    Private Sub 应用内容尺寸布局()
        If 已释放 OrElse 窗体.IsDisposed OrElse Not 窗体.IsHandleCreated Then Return
        If Not 内容模式启用 OrElse 手动覆盖 OrElse 全屏状态 OrElse 最后内容设备大小.IsEmpty Then Return

        ' 只有在 Normal 下才量得到“非最大化”的 chrome；最终要最大化时再切回去。
        If 窗体.WindowState <> FormWindowState.Normal Then
            窗体.WindowState = FormWindowState.Normal
        End If
        窗体.PerformLayout()

        Dim 客户区非内容宽度 = Math.Max(0, 窗体.ClientSize.Width - 视频容器.ClientSize.Width)
        Dim 客户区非内容高度 = Math.Max(0, 窗体.ClientSize.Height - 视频容器.ClientSize.Height)
        Dim 非客户区宽度 = Math.Max(0, 窗体.Width - 窗体.ClientSize.Width)
        Dim 非客户区高度 = Math.Max(0, 窗体.Height - 窗体.ClientSize.Height)
        Dim 工作区 = Screen.FromControl(窗体).WorkingArea
        If 工作区.Width <= 0 OrElse 工作区.Height <= 0 Then Return

        Dim 目标窗口大小 = 计算内容窗口大小(最后内容设备大小,
            New Size(客户区非内容宽度, 客户区非内容高度),
            New Size(非客户区宽度, 非客户区高度))
        Dim 放置 = 决定内容窗口放置(最后内容设备大小, 目标窗口大小, 工作区)

        If 放置 = 内容窗口放置.最大化 Then
            窗体.WindowState = FormWindowState.Maximized
        Else
            ' 内容模式的目标可能小于平时允许的 MinimumSize（例如 640×360 源视频）。
            ' 这里只按本次目标临时放宽最小尺寸；退出模式时恢复原值，保证内容区仍是严格的 1:1。
            Dim 最小窗口 = 计算内容模式最小尺寸(默认最小尺寸, 目标窗口大小)
            If 窗体.MinimumSize <> 最小窗口 Then 窗体.MinimumSize = 最小窗口
            Dim 最终大小 = New Size(
                Math.Min(工作区.Width, Math.Max(窗体.MinimumSize.Width, 目标窗口大小.Width)),
                Math.Min(工作区.Height, Math.Max(窗体.MinimumSize.Height, 目标窗口大小.Height)))
            窗体.StartPosition = FormStartPosition.Manual
            窗体.Bounds = 计算工作区居中边界(最终大小, 工作区)
        End If

        ' 窗口受 MinimumSize/最大化影响可能大于内容；封顶让画面按源原生尺寸居中，不放大。
        设置封顶(True)
        重绑输出窗口()
    End Sub

    Private Sub 设置封顶(开启 As Boolean)
        Try
            设置原生尺寸封顶?.Invoke(开启)
        Catch ex As Exception
            ' 旧内核缺导出时上面的包装已经返回 False；这里只防 UI 线程被异常打断。
            System.Diagnostics.Debug.WriteLine("视口封顶设置失败：" & ex.Message)
        End Try
    End Sub

    Friend Shared Function 计算旋转后内容尺寸(内容 As Size, 四分一转 As UInteger) As Size
        If 内容.Width <= 0 OrElse 内容.Height <= 0 Then Return Size.Empty
        If (四分一转 And 1UI) <> 0UI Then
            Return New Size(内容.Height, 内容.Width)
        End If
        Return 内容
    End Function

    Friend Shared Function 计算内容窗口大小(内容设备大小 As Size,
                                           客户区非内容 As Size,
                                           非客户区 As Size) As Size
        If 内容设备大小.Width <= 0 OrElse 内容设备大小.Height <= 0 Then Return Size.Empty
        Return New Size(
            Math.Max(1, 内容设备大小.Width + 客户区非内容.Width + 非客户区.Width),
            Math.Max(1, 内容设备大小.Height + 客户区非内容.Height + 非客户区.Height))
    End Function

    ''' <summary>内容模式下允许窗口小于平时 MinimumSize，才能让 640×360 这类小内容保持内容区 1:1。
    ''' 目标窗口本身仍以工作区为上限；该函数只负责放宽最小尺寸，不放大目标。</summary>
    Friend Shared Function 计算内容模式最小尺寸(基础最小尺寸 As Size, 目标窗口大小 As Size) As Size
        If 目标窗口大小.Width <= 0 OrElse 目标窗口大小.Height <= 0 Then Return 基础最小尺寸
        Return New Size(
            Math.Max(1, Math.Min(基础最小尺寸.Width, 目标窗口大小.Width)),
            Math.Max(1, Math.Min(基础最小尺寸.Height, 目标窗口大小.Height)))
    End Function

    Friend Shared Function 决定内容窗口放置(内容设备大小 As Size,
                                           目标窗口大小 As Size,
                                           工作区 As Rectangle) As 内容窗口放置
        If 内容设备大小.Width <= 0 OrElse 内容设备大小.Height <= 0 OrElse
            工作区.Width <= 0 OrElse 工作区.Height <= 0 Then Return 内容窗口放置.普通
        If 内容设备大小.Width >= 工作区.Width OrElse
            内容设备大小.Height >= 工作区.Height Then Return 内容窗口放置.最大化
        If 目标窗口大小.Width >= 工作区.Width OrElse
            目标窗口大小.Height >= 工作区.Height Then Return 内容窗口放置.最大化
        Return 内容窗口放置.普通
    End Function

    Friend Shared Function 计算目标画面设备大小(目标画面大小 As Size, DPI As Integer,
                                      使用物理像素 As Boolean) As Size
        If 目标画面大小.Width <= 0 OrElse 目标画面大小.Height <= 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(目标画面大小))
        End If
        If 使用物理像素 Then Return 目标画面大小
        Return 按DPI缩放画面尺寸(目标画面大小, DPI)
    End Function

    Friend Shared Function 按DPI缩放画面尺寸(逻辑画面大小 As Size, DPI As Integer) As Size
        If 逻辑画面大小.Width <= 0 OrElse 逻辑画面大小.Height <= 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(逻辑画面大小))
        End If
        If DPI <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(DPI))

        Const 基准DPI As Double = 96.0R
        Return New Size(
            CInt(Math.Round(逻辑画面大小.Width * DPI / 基准DPI, MidpointRounding.AwayFromZero)),
            CInt(Math.Round(逻辑画面大小.Height * DPI / 基准DPI, MidpointRounding.AwayFromZero)))
    End Function

    Friend Shared Function 计算工作区居中边界(窗口大小 As Size, 工作区 As Rectangle) As Rectangle
        If 窗口大小.Width <= 0 OrElse 窗口大小.Height <= 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(窗口大小))
        End If
        If 工作区.Width <= 0 OrElse 工作区.Height <= 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(工作区))
        End If

        Dim x = 工作区.Left + (工作区.Width - 窗口大小.Width) \ 2
        Dim y = 工作区.Top + (工作区.Height - 窗口大小.Height) \ 2
        Return New Rectangle(New Point(x, y), 窗口大小)
    End Function

    Private Sub 输出窗口已创建(sender As Object, e As EventArgs)
        If Not 已释放 Then 重绑输出窗口()
    End Sub

    Private Sub 请求重绑(sender As Object, e As EventArgs)
        If 已释放 Then Return
        重绑计时器.Stop()
        重绑计时器.Start()
    End Sub

    Private Sub 重绑计时器_Tick(sender As Object, e As EventArgs)
        If 已释放 OrElse Not 画面控件.IsHandleCreated Then Return
        重绑输出窗口()
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        应用代数 += 1
        重绑计时器.Stop()
        RemoveHandler 画面控件.输出窗口创建, AddressOf 输出窗口已创建
        RemoveHandler 画面控件.SizeChanged, AddressOf 请求重绑
        RemoveHandler 窗体.LocationChanged, AddressOf 请求重绑
        RemoveHandler 重绑计时器.Tick, AddressOf 重绑计时器_Tick
        重绑计时器.Dispose()
    End Sub
End Class

''' <summary>内容窗口放置结果；最大化时不再做普通窗口 bounds 居中。</summary>
Friend Enum 内容窗口放置
    普通
    最大化
End Enum
