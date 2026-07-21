Public Class Form总控台
    Private Const 空统计文本 As String = "总帧数：0<br>已丢帧：0<br>重复帧：0<br>视频体积：0.0 MB<br>音频体积：0.0 MB"
    Public Shared Property 预览 As 实时预览控件

    Private ReadOnly 视频源列表 As New List(Of 视频源条目)
    Private ReadOnly 音频源列表 As New List(Of 音频源条目)
    Private 响度计时器 As LakeUI.PrecisionTimer
    Private 统计计时器 As LakeUI.PrecisionTimer
    Private 当前响度计 As 音频响度计
    Private 设备监视器 As 音频设备监视器
    Private 正在初始化 As Boolean
    Private 已初始化 As Boolean
    Private 设备刷新已排队 As Boolean
    Private 页面活动 As Boolean

    Friend ReadOnly Property 当前视频源条目 As 视频源条目
        Get
            If MCB_视频源.SelectedIndex < 0 OrElse MCB_视频源.SelectedIndex >= 视频源列表.Count Then Return Nothing
            Return 视频源列表(MCB_视频源.SelectedIndex)
        End Get
    End Property

    Friend ReadOnly Property 当前音频源条目 As 音频源条目
        Get
            If MCB_音频源.SelectedIndex < 0 OrElse MCB_音频源.SelectedIndex >= 音频源列表.Count Then Return Nothing
            Return 音频源列表(MCB_音频源.SelectedIndex)
        End Get
    End Property

    Public Sub 确保预览()
        If 预览 Is Nothing Then
            预览 = New 实时预览控件 With {.Dock = DockStyle.Fill, .Margin = New Padding(0)}
            ModernPanel4.Controls.Clear()
            ModernPanel4.Controls.Add(预览)
            预览.设置活动(页面活动)
        End If
    End Sub

    Friend Sub 设置页面活动(是否活动 As Boolean)
        If 页面活动 = 是否活动 Then Return
        页面活动 = 是否活动
        预览?.设置活动(页面活动)
        If 页面活动 Then
            刷新响度计()
            响度计时器?.Start()
            统计计时器?.Start()
            刷新统计(Nothing, EventArgs.Empty)
        Else
            响度计时器?.Stop()
            统计计时器?.Stop()
            当前响度计?.释放()
            当前响度计 = Nothing
            重置响度条()
        End If
    End Sub

    Friend Sub 初始化页面()
        If 已初始化 Then Return
        已初始化 = True
        正在初始化 = True
        Try
            MCB_视频捕获模式.SelectedIndex = 设置.实例对象.视频捕获模式
            重建视频源列表()
            重建音频源列表()
            恢复视频源()
            恢复音频源()
            MCK_防误触模式.Checked = 设置.实例对象.防误触模式
            重置响度条()
            重置录制统计()
        Finally
            正在初始化 = False
        End Try
        应用防误触模式()
        预览?.设置源(当前视频源条目)
        初始化刷新计时器()
        If 页面活动 Then 刷新响度计()
        Try
            设备监视器 = 音频设备监视器.创建()
            AddHandler 设备监视器.设备已变更, AddressOf 音频设备已变更
        Catch ex As Exception
            设备监视器 = Nothing
            MTB_执行日志.AppendText($"[{DateTime.Now:HH:mm:ss}] 音频设备变更通知初始化失败：{ex.Message}{vbCrLf}")
        End Try
        更新录制状态()
        刷新统计(Nothing, EventArgs.Empty)
    End Sub

    Friend Sub 释放页面资源()
        页面活动 = False
        预览?.设置活动(False)
        If 响度计时器 IsNot Nothing Then
            RemoveHandler 响度计时器.Tick, AddressOf 刷新响度条
            响度计时器.Dispose()
            响度计时器 = Nothing
        End If
        If 统计计时器 IsNot Nothing Then
            RemoveHandler 统计计时器.Tick, AddressOf 刷新统计
            统计计时器.Dispose()
            统计计时器 = Nothing
        End If
        当前响度计?.释放()
        当前响度计 = Nothing
        If 设备监视器 IsNot Nothing Then
            RemoveHandler 设备监视器.设备已变更, AddressOf 音频设备已变更
            设备监视器.Dispose()
            设备监视器 = Nothing
        End If
    End Sub

    Friend Sub 更新录制状态()
        Dim 录制中 = 录制交互.是否录制中
        Dim 已暂停 = 录制交互.是否已暂停
        MB_启动或暂停或继续录制.Enabled = True
        MB_结束.Enabled = 录制中
        MB_分割.Enabled = 录制中 AndAlso Not 已暂停
        MCB_视频捕获模式.Enabled = Not 录制中
        MCB_视频源.Enabled = Not 录制中
        If Not 录制中 Then
            MB_启动或暂停或继续录制.Text = "启动录制"
            MB_启动或暂停或继续录制.SubText = String.Empty
        ElseIf 已暂停 Then
            MB_启动或暂停或继续录制.Text = "继续录制"
            MB_启动或暂停或继续录制.SubText = 格式化时长(录制交互.已录制时长)
        Else
            MB_启动或暂停或继续录制.Text = "暂停录制"
            MB_启动或暂停或继续录制.SubText = 格式化时长(录制交互.已录制时长)
        End If
    End Sub

    Private Sub Form总控台_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        确保预览()
    End Sub

    Private Sub Form总控台_SizeChanged(sender As Object, e As EventArgs) Handles Me.SizeChanged
        Panel2.Width = CInt(Me.ClientSize.Width * 0.5)
    End Sub

    Private Sub MB_启动或暂停或继续录制_Click(sender As Object, e As EventArgs) Handles MB_启动或暂停或继续录制.Click
        录制交互.切换录制状态()
    End Sub

    Private Sub MB_结束_Click(sender As Object, e As EventArgs) Handles MB_结束.Click
        录制交互.停止录制()
    End Sub

    Private Sub MB_分割_Click(sender As Object, e As EventArgs) Handles MB_分割.Click
        录制交互.切分文件()
    End Sub

    Private Sub MCK_防误触模式_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_防误触模式.CheckedChanged
        If Not 正在初始化 Then 设置.实例对象.防误触模式 = MCK_防误触模式.Checked
        应用防误触模式()
    End Sub

    Private Sub 应用防误触模式()
        Dim 启用 = MCK_防误触模式.Checked
        MB_启动或暂停或继续录制.HoldClickEnabled = 启用
        MB_分割.HoldClickEnabled = 启用
        MB_结束.HoldClickEnabled = 启用
    End Sub

    Private Sub MCB_视频源_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_视频源.SelectedIndexChanged
        If 正在初始化 Then Return
        应用当前视频源选择
    End Sub

    Private Sub MCB_视频捕获模式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_视频捕获模式.SelectedIndexChanged
        If 正在初始化 OrElse MCB_视频捕获模式.SelectedIndex < 0 Then Return
        设置.实例对象.视频捕获模式 = MCB_视频捕获模式.SelectedIndex
        正在初始化 = True
        Try
            重建视频源列表()
            恢复视频源()
        Finally
            正在初始化 = False
        End Try
        应用当前视频源选择()
    End Sub

    Private Sub 应用当前视频源选择()
        Dim 项目 = 当前视频源条目
        If 项目 IsNot Nothing Then
            设置.实例对象.视频源键 = 项目.键
            设置.实例对象.视频源类型 = 项目.类型
            设置.实例对象.视频捕获模式 = 项目.捕获模式
        End If
        预览?.设置源(项目)
    End Sub

    Private Sub MCB_音频源_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_音频源.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 项目 = 当前音频源条目
        设置.实例对象.音频跟随默认设备 = 项目 Is Nothing OrElse 项目.默认设备
        设置.实例对象.音频源键 = If(项目 Is Nothing, String.Empty, 项目.标识)
        刷新响度计()
        Dim 端点 = If(项目 Is Nothing OrElse 项目.默认设备, 获取默认音频端点(), 项目.标识)
        If Not String.IsNullOrWhiteSpace(端点) Then 录制交互.同步录制音频端点(端点)
    End Sub

    Private Sub MCB_视频源_DropDownOpened(sender As Object, e As EventArgs) Handles MCB_视频源.DropDownOpened
        If 正在初始化 OrElse 是否录制中 Then Return
        Dim 原键 = If(当前视频源条目?.键, 设置.实例对象.视频源键)
        Dim 选择已回退 As Boolean
        正在初始化 = True
        Try
            重建视频源列表
            Dim 索引 = 视频源列表.FindIndex(Function(x) String.Equals(x.键, 原键, StringComparison.Ordinal))
            选择已回退 = 索引 < 0
            MCB_视频源.SelectedIndex = If(索引 >= 0, 索引, 0)
        Finally
            正在初始化 = False
        End Try
        ' 列表重建时选择事件会被抑制，源丢失后的回退选择需要在这里显式应用。
        If 选择已回退 Then 应用当前视频源选择
    End Sub

    Private Sub MCB_音频源_DropDownOpened(sender As Object, e As EventArgs) Handles MCB_音频源.DropDownOpened
        If 正在初始化 OrElse 录制交互.是否录制中 Then Return
        刷新音频设备列表()
    End Sub

    Private Sub 重建视频源列表()
        视频源列表.Clear()
        Dim 捕获模式 = Math.Clamp(MCB_视频捕获模式.SelectedIndex, 0, 2)
        If 捕获模式 = 0 Then
            For Each 显示器 In 显示器捕获器.枚举显示器().Where(Function(x) x.连接到桌面 AndAlso x.宽度 > 0 AndAlso x.高度 > 0)
                视频源列表.Add(New 视频源条目 With {
                    .类型 = 0, .捕获模式 = 捕获模式, .键 = 显示器.名称,
                    .显示文本 = $"显示器：{显示器.名称} ({显示器.宽度}x{显示器.高度})", .显示器 = 显示器})
            Next
        Else
            For Each 窗口 In 窗口发现.枚举可捕获窗口()
                视频源列表.Add(New 视频源条目 With {
                    .类型 = 1, .捕获模式 = 捕获模式, .键 = $"{窗口.进程标识}:{窗口.标题}",
                    .显示文本 = $"窗口：{窗口.标题} ({窗口.进程名称})", .窗口 = 窗口})
            Next
        End If
        MCB_视频源.Items.Clear()
        For Each 项目 In 视频源列表
            MCB_视频源.Items.Add(项目.显示文本)
        Next
    End Sub

    Private Sub 重建音频源列表()
        音频源列表.Clear()
        Dim 默认名称 = "默认设备"
        Try
            Dim 端点 = 录制引擎.枚举音频端点().Where(Function(x) String.Equals(x.类型, "render", StringComparison.OrdinalIgnoreCase)).ToList()
            Dim 默认标识 = 获取默认音频端点()
            Dim 默认端点 = 端点.FirstOrDefault(Function(x) String.Equals(x.标识, 默认标识, StringComparison.Ordinal))
            If 默认端点 IsNot Nothing Then 默认名称 = $"默认设备（{默认端点.名称}）"
            音频源列表.Add(New 音频源条目 With {.默认设备 = True, .名称 = 默认名称})
            For Each 项目 In 端点
                音频源列表.Add(New 音频源条目 With {.标识 = 项目.标识, .名称 = 项目.名称})
            Next
        Catch
            音频源列表.Add(New 音频源条目 With {.默认设备 = True, .名称 = 默认名称})
        End Try
        MCB_音频源.Items.Clear()
        For Each 项目 In 音频源列表
            MCB_音频源.Items.Add(项目.名称)
        Next
    End Sub

    Private Sub 恢复视频源()
        Dim 索引 = 视频源列表.FindIndex(Function(x) x.键 = 设置.实例对象.视频源键)
        If 索引 < 0 AndAlso 设置.实例对象.视频源类型 = 1 AndAlso 设置.实例对象.视频源键.Contains(":"c) Then
            Dim 标题 = 设置.实例对象.视频源键.Substring(设置.实例对象.视频源键.IndexOf(":"c) + 1)
            索引 = 视频源列表.FindIndex(Function(x) x.类型 = 1 AndAlso x.窗口 IsNot Nothing AndAlso String.Equals(x.窗口.标题, 标题, StringComparison.CurrentCulture))
        End If
        If 视频源列表.Count > 0 Then MCB_视频源.SelectedIndex = If(索引 >= 0, 索引, 0)
        Dim 项目 = 当前视频源条目
        If 项目 IsNot Nothing Then
            设置.实例对象.视频源键 = 项目.键
            设置.实例对象.视频源类型 = 项目.类型
            设置.实例对象.视频捕获模式 = 项目.捕获模式
        End If
    End Sub

    Private Sub 恢复音频源()
        Dim 索引 = 0
        If Not 设置.实例对象.音频跟随默认设备 Then
            索引 = 音频源列表.FindIndex(Function(x) x.标识 = 设置.实例对象.音频源键)
            If 索引 < 0 Then
                设置.实例对象.音频跟随默认设备 = True
                设置.实例对象.音频源键 = String.Empty
                索引 = 0
            End If
        End If
        If 音频源列表.Count > 0 Then MCB_音频源.SelectedIndex = 索引
    End Sub

    Private Sub 音频设备已变更(sender As Object, e As EventArgs)
        If IsDisposed OrElse Not IsHandleCreated Then Return
        Try
            If InvokeRequired Then
                BeginInvoke(Sub() 排队刷新音频设备())
            Else
                排队刷新音频设备()
            End If
        Catch ex As InvalidOperationException
        End Try
    End Sub

    Private Sub 排队刷新音频设备()
        If 设备刷新已排队 OrElse IsDisposed Then Return
        设备刷新已排队 = True
        Try
            BeginInvoke(Sub()
                            设备刷新已排队 = False
                            刷新音频设备列表()
                        End Sub)
        Catch ex As InvalidOperationException
            设备刷新已排队 = False
        End Try
    End Sub

    Private Sub 刷新音频设备列表()
        If 正在初始化 Then Return
        Dim 跟随默认 = MCB_音频源.SelectedIndex <= 0
        Dim 原标识 = If(当前音频源条目?.标识, 设置.实例对象.音频源键)
        正在初始化 = True
        Try
            重建音频源列表()
            Dim 索引 = If(跟随默认, 0, 音频源列表.FindIndex(Function(x) Not x.默认设备 AndAlso String.Equals(x.标识, 原标识, StringComparison.Ordinal)))
            MCB_音频源.SelectedIndex = If(索引 >= 0, 索引, 0)
        Finally
            正在初始化 = False
        End Try
        Dim 项目 = 当前音频源条目
        设置.实例对象.音频跟随默认设备 = 项目 Is Nothing OrElse 项目.默认设备
        设置.实例对象.音频源键 = If(项目 Is Nothing, String.Empty, 项目.标识)
        刷新响度计()
        Dim 端点 = If(项目 Is Nothing OrElse 项目.默认设备, 获取默认音频端点(), 项目.标识)
        If Not String.IsNullOrWhiteSpace(端点) Then 录制交互.同步录制音频端点(端点)
    End Sub

    Private Sub 初始化刷新计时器()
        响度计时器 = New LakeUI.PrecisionTimer With {
            .Interval = 33,
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .WorkerThreadCount = 1,
            .SynchronizingObject = Form1.当前主窗体}
        AddHandler 响度计时器.Tick, AddressOf 刷新响度条
        If 页面活动 Then 响度计时器.Start()

        统计计时器 = New LakeUI.PrecisionTimer With {
            .Interval = 500,
            .DispatchMode = LakeUI.PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = LakeUI.PrecisionTimer.OverrunPolicyEnum.Drop,
            .WorkerThreadCount = 1,
            .SynchronizingObject = Form1.当前主窗体}
        AddHandler 统计计时器.Tick, AddressOf 刷新统计
        If 页面活动 Then 统计计时器.Start()
    End Sub

    Private Sub 刷新统计(sender As Object, e As EventArgs)
        更新录制状态()
        Dim 统计 As 录制统计
        If Not 录制交互.尝试读取统计(统计) Then Return
        显示录制统计(统计)
    End Sub

    Friend Sub 重置录制统计()
        HtmlColorLabel1.Text = 空统计文本
    End Sub

    Friend Sub 显示录制统计(统计 As 录制统计)
        HtmlColorLabel1.Text = $"总帧数：{统计.已提交帧数}<br>已丢帧：{统计.已丢弃帧数}<br>重复帧：{统计.已重复帧数}<br>视频体积：{格式化体积(统计.视频字节数)}<br>音频体积：{格式化体积(统计.音频字节数)}"
    End Sub

    Private Sub 刷新响度计()
        当前响度计?.释放()
        当前响度计 = Nothing
        If Not 页面活动 Then Return
        Try
            Dim 项目 = 当前音频源条目
            当前响度计 = 音频响度计.创建(If(项目 Is Nothing OrElse 项目.默认设备, String.Empty, 项目.标识))
        Catch
        End Try
    End Sub

    Private Sub 刷新响度条(sender As Object, e As EventArgs)
        If Not 页面活动 Then Return
        Dim 原始 As Single() = Nothing
        Try
            原始 = 当前响度计?.读取()
        Catch
        End Try
        If 原始 Is Nothing OrElse 原始.Length = 0 Then 原始 = 读取原生响度兜底()
        Dim values = 映射响度声道(原始)
        Dim meters = {EPB_L, EPB_R, EPB_C, EPB_LFE, EPB_SL, EPB_SR, EPB_BL, EPB_BR}
        For i = 0 To meters.Length - 1
            If i >= values.Length OrElse Single.IsNaN(values(i)) Then
                meters(i).Value = -60
            Else
                Dim peak = Math.Clamp(values(i), 0.000001F, 1.0F)
                meters(i).Value = CInt(Math.Clamp(20.0 * Math.Log10(peak), -60.0, 0.0))
            End If
        Next
    End Sub

    Private Sub 重置响度条()
        For Each meter In {EPB_L, EPB_R, EPB_C, EPB_LFE, EPB_SL, EPB_SR, EPB_BL, EPB_BR}
            meter.Minimum = -60
            meter.Maximum = 0
            meter.Value = -60
        Next
    End Sub

    Private Shared Function 格式化体积(字节数 As ULong) As String
        Dim MB = 字节数 / 1024.0 / 1024.0
        If MB >= 1024.0 Then Return $"{MB / 1024.0:F2} GB"
        Return $"{MB:F1} MB"
    End Function

    Private Shared Function 格式化时长(时长 As TimeSpan) As String
        Dim 总小时 = CInt(Math.Floor(时长.TotalHours))
        Return $"{总小时:00}:{时长.Minutes:00}:{时长.Seconds:00}"
    End Function

    Private Shared Function 获取默认音频端点() As String
        Try
            Return 音频响度计.获取默认设备标识()
        Catch
            Return String.Empty
        End Try
    End Function

    Private Shared Function 读取原生响度兜底() As Single()
        Dim 统计 As 录制统计
        If Not 录制交互.尝试读取统计(统计) Then Return Array.Empty(Of Single)()
        Dim count = Math.Clamp(CInt(统计.音频声道数), 1, 8)
        Dim values(count - 1) As Single
        Dim peak = Math.Clamp(统计.系统音频峰值, 0.0F, 1.0F)
        For i = 0 To values.Length - 1
            values(i) = peak
        Next
        Return values
    End Function

    Private Shared Function 映射响度声道(原始 As Single()) As Single()
        Dim 输出(7) As Single
        For i = 0 To 输出.Length - 1
            输出(i) = Single.NaN
        Next
        If 原始 Is Nothing Then Return Array.Empty(Of Single)()
        Select Case 原始.Length
            Case 1
                输出(2) = 原始(0)
            Case 2
                输出(0) = 原始(0) : 输出(1) = 原始(1)
            Case 3
                输出(0) = 原始(0) : 输出(1) = 原始(1) : 输出(3) = 原始(2)
            Case 4
                输出(0) = 原始(0) : 输出(1) = 原始(1) : 输出(4) = 原始(2) : 输出(5) = 原始(3)
            Case 5
                输出(0) = 原始(0) : 输出(1) = 原始(1) : 输出(2) = 原始(2) : 输出(4) = 原始(3) : 输出(5) = 原始(4)
            Case 6
                输出(0) = 原始(0) : 输出(1) = 原始(1) : 输出(2) = 原始(2) : 输出(3) = 原始(3) : 输出(6) = 原始(4) : 输出(7) = 原始(5)
            Case 7
                输出(0) = 原始(0) : 输出(1) = 原始(1) : 输出(2) = 原始(2) : 输出(3) = 原始(3) : 输出(6) = 原始(4) : 输出(4) = 原始(5) : 输出(5) = 原始(6)
            Case Else
                For i = 0 To Math.Min(7, 原始.Length - 1)
                    输出(i) = 原始(i)
                Next
        End Select
        Return 输出
    End Function
End Class
