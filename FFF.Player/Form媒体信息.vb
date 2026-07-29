Imports System.Diagnostics

''' <summary>播放器的实时诊断与完整媒体元数据视图。</summary>
Public Class Form媒体信息
    Private ReadOnly 获取媒体 As Func(Of 媒体信息)
    Private ReadOnly 获取快照 As Func(Of 播放器快照)
    Private ReadOnly 获取字幕状态 As Func(Of 定时文字状态)
    Private ReadOnly 获取弹幕状态 As Func(Of 定时文字状态)
    Private ReadOnly 获取字幕 As Func(Of 外部字幕轨道)
    Private ReadOnly 获取弹幕 As Func(Of 弹幕资料库)
    Private ReadOnly 获取WASAPI模式 As Func(Of WASAPI共享模式)
    Private ReadOnly 获取输出尺寸 As Func(Of Size)
    Private ReadOnly 刷新定时器 As New LakeUI.PrecisionTimer With {.Interval = 200}
    Private ReadOnly 响度刷新定时器 As New LakeUI.PrecisionTimer With {.Interval = 67}
    Private ReadOnly 帧率刷新定时器 As New LakeUI.PrecisionTimer With {.Interval = 1000}
    Private 响度计 As 播放器音频响度计
    Private 上次视频帧次数 As ULong
    Private 上次采样时钟 As Long
    Private 最近实际帧率 As Double
    Private 元数据签名 As String = String.Empty

    Public Sub New(Optional mediaProvider As Func(Of 媒体信息) = Nothing,
                   Optional snapshotProvider As Func(Of 播放器快照) = Nothing,
                   Optional subtitleStatusProvider As Func(Of 定时文字状态) = Nothing,
                   Optional danmakuStatusProvider As Func(Of 定时文字状态) = Nothing,
                   Optional subtitleProvider As Func(Of 外部字幕轨道) = Nothing,
                   Optional danmakuProvider As Func(Of 弹幕资料库) = Nothing,
                   Optional wasapiProvider As Func(Of WASAPI共享模式) = Nothing,
                   Optional outputSizeProvider As Func(Of Size) = Nothing)
        InitializeComponent()
        获取媒体 = mediaProvider : 获取快照 = snapshotProvider
        获取字幕状态 = subtitleStatusProvider : 获取弹幕状态 = danmakuStatusProvider
        获取字幕 = subtitleProvider : 获取弹幕 = danmakuProvider
        获取WASAPI模式 = wasapiProvider : 获取输出尺寸 = outputSizeProvider
    End Sub

    Private Sub Form媒体信息_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        Form1.ThisIsYourWindow1.Attach(Me)
        If UltraDetailListView1.Columns.Count > 0 Then UltraDetailListView1.Columns(0).Text = "媒体参数"
        调整左栏宽度() : 调整列表列宽() : 重置响度条() : 刷新响度计() : 刷新()
        ' 在启动一秒定时器前先建立基线，这样首个 Tick 就能给出完整的一秒统计值。
        更新实际帧率(安全获取(获取快照))
        AddHandler 刷新定时器.Tick, AddressOf 刷新定时器_Tick
        AddHandler 响度刷新定时器.Tick, AddressOf 响度刷新定时器_Tick
        AddHandler 帧率刷新定时器.Tick, AddressOf 帧率刷新定时器_Tick
        刷新定时器.Start()
        响度刷新定时器.Start()
        帧率刷新定时器.Start()
    End Sub

    Private Sub Form媒体信息_FormClosed(sender As Object, e As FormClosedEventArgs) Handles MyBase.FormClosed
        刷新定时器.Stop() : 刷新定时器.Dispose()
        响度刷新定时器.Stop() : 响度刷新定时器.Dispose()
        帧率刷新定时器.Stop() : 帧率刷新定时器.Dispose()
        响度计?.释放() : 响度计 = Nothing
    End Sub

    Private Sub Form媒体信息_SizeChanged(sender As Object, e As EventArgs) Handles MyBase.SizeChanged
        调整列表列宽()
    End Sub

    Private Sub 刷新定时器_Tick(sender As Object, e As EventArgs)
        刷新()
    End Sub

    Private Sub 响度刷新定时器_Tick(sender As Object, e As EventArgs)
        刷新响度条()
    End Sub

    Private Sub 帧率刷新定时器_Tick(sender As Object, e As EventArgs)
        更新实际帧率(安全获取(获取快照))
    End Sub

    Public Sub 刷新()
        Dim 信息 = 安全获取(获取媒体)
        Dim 快照 = 安全获取(获取快照)
        刷新概要(信息, 快照)
        Dim 字幕 = 安全获取(获取字幕), 弹幕 = 安全获取(获取弹幕)
        Dim 字幕状态 = 安全获取(获取字幕状态), 弹幕状态 = 安全获取(获取弹幕状态)
        HtmlColorLabel7.Text = 标签("已加载字幕条目数", If(字幕 Is Nothing, "未加载", If(字幕.条目数 >= 0, 字幕.条目数.ToString("N0"), "按需解码")), "#B7D7F0")
        HtmlColorLabel14.Text = 标签("正在渲染的字幕数量", If(字幕状态 Is Nothing, "0", 字幕状态.命令数.ToString("N0")), "#9ED7C5")
        HtmlColorLabel8.Text = 标签("平均渲染延迟", 图层延迟(字幕状态), "#CDB6EA")
        HtmlColorLabel12.Text = 标签("已加载弹幕条目数", If(弹幕 Is Nothing, "未加载", 弹幕.数量.ToString("N0")), "#B7D7F0")
        HtmlColorLabel13.Text = 标签("正在渲染的弹幕数量", If(弹幕状态 Is Nothing, "0", 弹幕状态.命令数.ToString("N0")), "#9ED7C5")
        HtmlColorLabel11.Text = 标签("平均渲染延迟", 图层延迟(弹幕状态), "#CDB6EA")
        刷新列表(信息, 快照)
    End Sub

    Private Sub 刷新概要(信息 As 媒体信息, 快照 As 播放器快照)
        Dim 视频 = 查找流(信息, 快照, "video"), 音频 = 查找流(信息, 快照, "audio")
        Dim 输出 = 安全获取(获取输出尺寸)
        Dim 解码 = If(快照 Is Nothing, "—", If(快照.解码器 = 解码模式.GPU, "DXVA (D3D11VA)", "CPU"))
        If 视频 IsNot Nothing AndAlso Not String.IsNullOrWhiteSpace(视频.硬件加速) Then 解码 = 视频.硬件加速
        HtmlColorLabel1.Text = 标签("视频解码器", If(视频 Is Nothing, "—", $"{视频.编码.ToUpperInvariant()} | {解码}"), "#C6D8FF")
        HtmlColorLabel2.Text = 标签("输入格式", If(视频 Is Nothing, "—", 视频输入格式(视频)), "#B7D7F0")
        HtmlColorLabel16.Text = 标签("输出格式", If(视频 Is Nothing, "—", 视频输出格式(视频, 快照)), "#9ED7C5")
        HtmlColorLabel3.Text = 标签("分辨率", If(视频 Is Nothing, "—", $"输入 {分辨率(视频.宽度, 视频.高度)} → 渲染 {尺寸(输出)}"), "#F0D8A8")
        HtmlColorLabel4.Text = 标签("帧率", If(视频 Is Nothing, "—", $"输入 {帧率(视频.平均帧率)} → 实际 {最近实际帧率:F2} fps"), "#B7D7F0")
        HtmlColorLabel5.Text = 标签("视频实时比特率", 比特率(If(快照 Is Nothing, 0UL, 快照.视频实时比特率)), "#CDB6EA")
        Dim 模式 = If(获取WASAPI模式 Is Nothing, WASAPI共享模式.共享, 安全获取(获取WASAPI模式))
        HtmlColorLabel10.Text = 标签("音频 WASAPI", If(音频 Is Nothing, $"— · {模式}", $"{音频.编码.ToUpperInvariant()} | {模式}"), If(模式 = WASAPI共享模式.独占, "IndianRed", "#9ED7C5"))
        HtmlColorLabel9.Text = 标签("输入格式", If(音频 Is Nothing, "—", 音频输入格式(音频)), "#B7D7F0")
        HtmlColorLabel15.Text = 标签("输出格式", If(音频 Is Nothing, "—", 音频输出格式(音频)), "#9ED7C5")
        HtmlColorLabel6.Text = 标签("音频实时比特率", 比特率(If(快照 Is Nothing, 0UL, 快照.音频实时比特率)), "#CDB6EA")
    End Sub

    Private Sub 刷新响度计()
        响度计?.释放() : 响度计 = Nothing
        Try : 响度计 = 播放器音频响度计.创建默认设备() : Catch : End Try
    End Sub

    Private Sub 重置响度条()
        For Each bar In {EPB_L, EPB_R, EPB_C, EPB_LFE, EPB_SL, EPB_SR, EPB_BL, EPB_BR}
            bar.Minimum = -60 : bar.Maximum = 0 : bar.Value = -60
        Next
    End Sub

    Private Sub 刷新响度条()
        Dim peaks As Single() = Nothing
        Try : peaks = 响度计?.读取() : Catch : End Try
        Dim bars = {EPB_L, EPB_R, EPB_C, EPB_LFE, EPB_SL, EPB_SR, EPB_BL, EPB_BR}
        For i = 0 To bars.Length - 1
            Dim peak = If(peaks IsNot Nothing AndAlso i < peaks.Length, peaks(i), 0.0F)
            bars(i).Value = CInt(Math.Clamp(20.0 * Math.Log10(Math.Clamp(peak, 0.000001F, 1.0F)), -60.0, 0.0))
        Next
    End Sub

    Private Sub 刷新列表(信息 As 媒体信息, 快照 As 播放器快照)
        Dim 签名 = If(信息 Is Nothing, "", $"{信息.格式}|{信息.文件大小}|{信息.时长100纳秒}|{String.Join(";", 信息.流.Select(Function(x) $"{x.索引}:{x.流ID}:{x.编码}:{x.比特率}:{x.流大小}:{x.位深度}:{x.标称帧率分子}/{x.标称帧率分母}:{x.主显示器色域}:{x.最大内容光照}:{x.原始采样位数}:{x.未压缩内容MD5}"))}")
        If 签名 = 元数据签名 Then Return
        元数据签名 = 签名
        UltraDetailListView1.BeginUpdate()
        Try
            UltraDetailListView1.Groups.Clear() : UltraDetailListView1.Items.Clear()
            If 信息 Is Nothing Then 添加分组条目("empty", "媒体信息", "状态", "尚未打开媒体") : Return
            添加分组("general", "常规")
            添加条目("general", "格式", 容器格式(信息))
            添加条目("general", "格式配置档次", 容器配置档次(信息))
            添加条目("general", "编码 ID", 容器编码ID(信息))
            添加条目("general", "文件大小", 大小(信息.文件大小))
            添加条目("general", "时长", 时间(信息.时长))
            添加条目("general", "总体码率模式", If(信息.比特率 > 0, "可变", "—"))
            添加条目("general", "总体码率", 比特率(信息.比特率))
            Dim 首个视频 = 信息.流.FirstOrDefault(Function(x) x.类型.Equals("video", StringComparison.OrdinalIgnoreCase) AndAlso Not x.是封面图)
            添加条目("general", "帧率", If(首个视频 Is Nothing, "—", 帧率分数(首个视频)))
            添加条目("general", "写入应用", 元数据值(信息.元数据, "encoder"))

            Dim 视频序号 As Integer = 0, 音频序号 As Integer = 0
            For Each 流 In 信息.流.Where(Function(x) Not x.是封面图 OrElse Not x.类型.Equals("video", StringComparison.OrdinalIgnoreCase)).OrderBy(Function(x) x.索引)
                Dim 是视频 = 流.类型.Equals("video", StringComparison.OrdinalIgnoreCase)
                Dim 是音频 = 流.类型.Equals("audio", StringComparison.OrdinalIgnoreCase)
                If Not 是视频 AndAlso Not 是音频 Then Continue For
                If 是视频 Then 视频序号 += 1 Else 音频序号 += 1
                Dim group = $"stream-{流.索引}"
                Dim groupText = If(是视频, "视频" & If(视频序号 > 1, $" #{视频序号}", ""), "音频" & If(音频序号 > 1, $" #{音频序号}", ""))
                添加分组(group, groupText)
                If 是视频 Then
                    添加条目(group, "ID", If(流.流ID >= 0, 流.流ID.ToString(), 流.索引.ToString()))
                    添加条目(group, "格式", 流格式(流))
                    添加条目(group, "格式信息", 流格式信息(流))
                    添加条目(group, "格式配置档次", 视频配置档次(流))
                    添加条目如果有值(group, "HDR 格式", 流.HDR格式)
                    添加条目(group, "编码 ID", 空值(流.编码标签, 流.编码))
                    添加条目(group, "时长", 时间(TimeSpan.FromTicks(Math.Max(0L, 流.时长100纳秒))))
                    添加条目(group, "码率", 比特率(流.比特率))
                    添加条目(group, "宽度", If(流.宽度 > 0, $"{流.宽度} 像素", "—"))
                    添加条目(group, "高度", If(流.高度 > 0, $"{流.高度} 像素", "—"))
                    添加条目(group, "显示宽高比", 宽高比(流.显示宽高比分子, 流.显示宽高比分母, 流.宽度, 流.高度))
                    添加条目(group, "帧率模式", If(流.帧率模式 = "constant", "恒定", If(流.帧率模式 = "variable", "可变", "—")))
                    添加条目(group, "帧率", 帧率分数(流))
                    添加条目(group, "色彩模型", 空值(流.色彩模型))
                    添加条目(group, "色度抽样", 空值(流.色度抽样))
                    添加条目(group, "位深", 位深(流))
                    添加条目(group, "扫描类型", 扫描类型(流.场序))
                    添加条目如果有值(group, "位/像素/帧", 位像素帧(流))
                    添加条目如果有值(group, "流大小", 流大小(流, 信息.文件大小))
                    添加条目如果有值(group, "写入库", 元数据值(流.元数据, "encoder", "writing_library"))
                    添加条目(group, "色彩范围", 色彩范围(流.色彩范围))
                    添加条目(group, "色彩原色", 色彩原色(流.色彩原色))
                    添加条目(group, "传递特性", 色彩传递(流.色彩传递))
                    添加条目(group, "矩阵系数", 色彩空间(流.色彩空间))
                    添加条目如果有值(group, "主显示器色域", 流.主显示器色域)
                    If 流.主显示器最大亮度 > 0 Then 添加条目(group, "主显示器亮度", $"最小 {流.主显示器最小亮度:0.####} cd/m²，最大 {流.主显示器最大亮度:0.##} cd/m²")
                    If 流.最大内容光照 > 0 Then 添加条目(group, "最大内容光照", $"{流.最大内容光照:N0} cd/m²")
                    If 流.最大帧平均光照 > 0 Then 添加条目(group, "最大帧平均光照", $"{流.最大帧平均光照:N0} cd/m²")
                    添加条目如果有值(group, "编码配置盒", 流.编码配置盒)
                Else
                    添加条目(group, "ID", If(流.流ID >= 0, 流.流ID.ToString(), 流.索引.ToString()))
                    添加条目(group, "格式", 流格式(流))
                    添加条目(group, "格式信息", 流格式信息(流))
                    添加条目(group, "编码 ID", 空值(流.编码标签, 流.编码))
                    添加条目(group, "时长", 时间(TimeSpan.FromTicks(Math.Max(0L, 流.时长100纳秒))))
                    添加条目(group, "码率模式", If(流.无损 AndAlso 流.比特率 > 0, "可变", "—"))
                    添加条目(group, "码率", 比特率(流.比特率))
                    添加条目(group, "声道数", If(流.声道数 > 0, $"{流.声道数} 声道", "—"))
                    添加条目(group, "声道布局", 空值(流.声道布局))
                    添加条目(group, "采样率", 采样率(流.采样率))
                    添加条目(group, "位深", 位深(流))
                    添加条目(group, "压缩模式", 空值(流.压缩模式, If(流.无损, "无损", "有损")))
                    添加条目如果有值(group, "流大小", 流大小(流, 信息.文件大小))
                    添加条目(group, "默认", If(流.是默认流, "是", "否"))
                    添加条目如果有值(group, "未压缩内容 MD5", 流.未压缩内容MD5)
                End If
            Next
        Finally
            UltraDetailListView1.EndUpdate() : UltraDetailListView1.RefreshItems()
        End Try
    End Sub

    Private Sub 添加分组(name As String, text As String)
        UltraDetailListView1.Groups.Add(New LakeUI.UltraDetailListView.ListGroup(name, text))
    End Sub
    Private Sub 添加条目(group As String, name As String, value As String)
        UltraDetailListView1.Items.Add(New LakeUI.UltraDetailListView.ListItem(New LakeUI.UltraDetailListView.ListSubItem($"{name}：{空值(value)}")) With {.GroupName = group})
    End Sub
    Private Sub 添加条目如果有值(group As String, name As String, value As String)
        If Not String.IsNullOrWhiteSpace(value) AndAlso value <> "—" Then 添加条目(group, name, value)
    End Sub
    Private Sub 添加分组条目(group As String, groupText As String, name As String, value As String)
        添加分组(group, groupText) : 添加条目(group, name, value)
    End Sub
    Private Sub 调整左栏宽度()
        If Panel3 Is Nothing OrElse Panel3.ClientSize.Width <= 0 Then Return
        Dim contentWidth = Panel3.Controls.OfType(Of Control)().Where(Function(x) x.Dock = DockStyle.Left).Sum(Function(x) x.Width)
        If contentWidth <= 0 Then Return
        Panel1.Width = Math.Max(Panel1.Padding.Horizontal + contentWidth, Panel1.Width + contentWidth - Panel3.ClientSize.Width)
        Panel1.MinimumSize = New Size(Panel1.Width, 0)
    End Sub
    Private Sub 调整列表列宽()
        UltraDetailListView1.Columns(0).Width = UltraDetailListView1.Width - UltraDetailListView1.Padding.Left - UltraDetailListView1.Padding.Right - UltraDetailListView1.BorderRadius * 2 - UltraDetailListView1.ScrollBarWidth * 2
    End Sub

    Private Sub 更新实际帧率(snapshot As 播放器快照)
        Dim now = Stopwatch.GetTimestamp()
        If snapshot Is Nothing OrElse snapshot.状态 <> 播放状态.正在播放 Then
            上次视频帧次数 = If(snapshot Is Nothing, 0UL, snapshot.已呈现视频帧数)
            上次采样时钟 = now
            最近实际帧率 = 0
            Return
        End If

        Dim current = snapshot.已呈现视频帧数
        If 上次采样时钟 > 0 AndAlso now > 上次采样时钟 AndAlso current >= 上次视频帧次数 Then
            Dim elapsedSeconds = CDbl(now - 上次采样时钟) / Stopwatch.Frequency
            If elapsedSeconds >= 0.75R Then
                Dim value = CDbl(current - 上次视频帧次数) / elapsedSeconds
                最近实际帧率 = If(Double.IsFinite(value), value, 0)
            End If
        Else
            ' 播放新文件或渲染器重置后计数会回到零，从新的基线重新统计。
            最近实际帧率 = 0
        End If
        上次视频帧次数 = current
        上次采样时钟 = now
    End Sub

    Protected Overrides Function ProcessCmdKey(ByRef msg As Message, keyData As Keys) As Boolean
        If keyData = (Keys.Control Or Keys.C) AndAlso UltraDetailListView1 IsNot Nothing AndAlso
            UltraDetailListView1.ContainsFocus Then
            复制选中媒体参数()
            Return True
        End If
        Return MyBase.ProcessCmdKey(msg, keyData)
    End Function

    Private Sub 复制选中媒体参数()
        Dim selected = UltraDetailListView1.SelectedItems
        If selected Is Nothing OrElse selected.Count = 0 Then Return

        Dim groupTexts = UltraDetailListView1.Groups.ToDictionary(
            Function(group) group.Name,
            Function(group) 空值(group.Text, group.Name),
            StringComparer.Ordinal)
        Dim lines As New List(Of String)
        Dim previousGroup As String = Nothing
        For Each item In selected
            If Not String.Equals(previousGroup, item.GroupName, StringComparison.Ordinal) Then
                Dim groupText As String = Nothing
                If Not groupTexts.TryGetValue(item.GroupName, groupText) Then groupText = 空值(item.GroupName, "未分组")
                lines.Add($"【{groupText}】")
                previousGroup = item.GroupName
            End If
            lines.Add(String.Join(vbTab, item.SubItems.Select(Function(subItem) subItem.Text)))
        Next

        Try
            Clipboard.SetText(String.Join(Environment.NewLine, lines))
        Catch
            ' 剪贴板被其他进程暂时占用时保持界面与选择状态不变。
        End Try
    End Sub
    Private Shared Function 查找流(info As 媒体信息, snapshot As 播放器快照, type As String) As 媒体流信息
        If info Is Nothing Then Return Nothing
        Dim index = If(snapshot Is Nothing, -1, If(type = "video", snapshot.当前视频流, snapshot.当前音频流))
        Return info.流.FirstOrDefault(Function(x) x.类型.Equals(type, StringComparison.OrdinalIgnoreCase) AndAlso (index < 0 OrElse x.索引 = index) AndAlso (type <> "video" OrElse Not x.是封面图))
    End Function
    Private Shared Function 安全获取(Of T)(provider As Func(Of T)) As T
        If provider Is Nothing Then Return Nothing
        Try
            Return provider()
        Catch
            Return Nothing
        End Try
    End Function
    Private Shared Function 标签(name As String, value As String, color As String) As String
        Return $"<font color=""#AAB0B9"">{编码HTML(name)}：</font><font color=""{color}"">{编码HTML(If(value, "—"))}</font>"
    End Function
    Private Shared Function 编码HTML(value As String) As String
        Return If(value, String.Empty).Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;").Replace("""", "&quot;")
    End Function
    Private Shared Function 图层延迟(status As 定时文字状态) As String
        If status Is Nothing OrElse status.已提交序号 <= status.已绘制序号 Then Return "< 1 ms"
        Return $"{(status.已提交序号 - status.已绘制序号) * 1000.0R / 60.0R:F1} ms"
    End Function
    Private Shared Function 空值(value As String, Optional fallback As String = "—") As String
        Return If(String.IsNullOrWhiteSpace(value), fallback, value)
    End Function
    Private Shared Function 比特率(value As Long) As String
        Return If(value <= 0, "—", If(value >= 1000000, $"{value / 1000000.0R:F2} Mbps", $"{value / 1000.0R:F0} kbps"))
    End Function
    Private Shared Function 比特率(value As ULong) As String
        Return If(value = 0, "—", If(value >= 1000000UL, $"{value / 1000000.0R:F2} Mbps", $"{value / 1000.0R:F0} kbps"))
    End Function
    Private Shared Function 采样率(value As Integer) As String
        Return If(value > 0, $"{value / 1000.0R:0.##} kHz", "—")
    End Function
    Private Shared Function 位深(stream As 媒体流信息) As String
        Dim d = If(stream.位深度 > 0, stream.位深度, If(stream.原始采样位数 > 0, stream.原始采样位数, stream.编码采样位数))
        Return If(d > 0, $"{d}bit", 空值(stream.采样格式))
    End Function
    Private Shared Function 视频输入格式(stream As 媒体流信息) As String
        If stream Is Nothing Then Return "—"
        Dim parts As New List(Of String)
        If Not String.IsNullOrWhiteSpace(stream.像素格式) Then parts.Add(stream.像素格式)
        If stream.位深度 > 0 Then parts.Add($"{stream.位深度}bit")
        Return If(parts.Count > 0, String.Join(" | ", parts), "—")
    End Function
    Private Shared Function 视频输出格式(stream As 媒体流信息, snapshot As 播放器快照) As String
        If stream Is Nothing Then Return "—"
        Dim str As String = 输出色彩(stream, snapshot)
        Return If(String.IsNullOrWhiteSpace(str), "—", str)
    End Function
    Private Shared Function 音频输入格式(stream As 媒体流信息) As String
        Return $"{采样率(stream.采样率)} | {位深(stream)} | {声道(stream.声道数, stream.声道布局)}"
    End Function
    Private Shared Function 音频输出格式(stream As 媒体流信息) As String
        If stream Is Nothing OrElse stream.输出采样率 <= 0 Then Return "—"
        Dim bits = If(stream.输出有效采样位数 > 0, stream.输出有效采样位数, stream.输出采样位数)
        Dim sample = If(stream.输出浮点, $"{bits}bit 浮点", $"{bits}bit PCM")
        Return $"{采样率(stream.输出采样率)} | {sample} | {声道(stream.输出声道数, String.Empty)}"
    End Function
    Private Shared Function 声道(count As Integer, layout As String) As String
        Return If(count <= 0, "—", If(String.IsNullOrWhiteSpace(layout), $"{count} 声道", $"{count} 声道 ({layout})"))
    End Function
    Private Shared Function 帧率(value As Double) As String
        Return If(value > 0, $"{value:F3} fps", "—")
    End Function
    Private Shared Function 分辨率(w As Integer, h As Integer) As String
        Return If(w > 0 AndAlso h > 0, $"{w}×{h}", "—")
    End Function
    Private Shared Function 尺寸(value As Size) As String
        Return If(value.Width > 0 AndAlso value.Height > 0, $"{value.Width}×{value.Height}", "—")
    End Function
    Private Shared Function 时间(value As TimeSpan) As String
        Return If(value > TimeSpan.Zero, $"{CInt(Math.Floor(value.TotalHours)):00}:{value.Minutes:00}:{value.Seconds:00}.{value.Milliseconds:000}", "—")
    End Function
    Private Shared Function 大小(value As Long) As String
        Return If(value <= 0, "—", If(value >= 1073741824, $"{value / 1073741824.0R:F2} GiB", $"{value / 1048576.0R:F2} MiB"))
    End Function
    Private Shared Function 容器格式(info As 媒体信息) As String
        If info Is Nothing Then Return "—"
        If info.格式.IndexOf("mp4", StringComparison.OrdinalIgnoreCase) >= 0 OrElse info.格式.IndexOf("mov", StringComparison.OrdinalIgnoreCase) >= 0 Then Return "MPEG-4"
        Return 空值(info.格式全名, info.格式)
    End Function
    Private Shared Function 容器配置档次(info As 媒体信息) As String
        If info Is Nothing Then Return "—"
        Dim brand = info.格式编码ID.ToLowerInvariant()
        If brand = "isom" OrElse brand = "iso2" Then Return "Base Media"
        If brand = "iso6" Then Return "Base Media v6"
        Return 空值(info.格式全名, "—")
    End Function
    Private Shared Function 容器编码ID(info As 媒体信息) As String
        If info Is Nothing Then Return "—"
        Dim first = 空值(info.格式编码ID)
        If String.IsNullOrWhiteSpace(info.兼容品牌) Then Return first
        Dim brands = info.兼容品牌
        If brands.Length >= 4 Then
            Dim parts As New List(Of String)
            For i = 0 To brands.Length - 4 Step 4
                parts.Add(brands.Substring(i, 4))
            Next
            brands = String.Join("/", parts)
        End If
        Return $"{first} ({brands})"
    End Function
    Private Shared Function 流格式(stream As 媒体流信息) As String
        If stream Is Nothing Then Return "—"
        If String.Equals(stream.编码, "av1", StringComparison.OrdinalIgnoreCase) Then Return "AV1"
        If String.Equals(stream.编码, "h264", StringComparison.OrdinalIgnoreCase) Then Return "AVC"
        If String.Equals(stream.编码, "hevc", StringComparison.OrdinalIgnoreCase) Then Return "HEVC"
        If String.Equals(stream.编码, "flac", StringComparison.OrdinalIgnoreCase) Then Return "FLAC"
        Return 空值(stream.编码).ToUpperInvariant()
    End Function
    Private Shared Function 流格式信息(stream As 媒体流信息) As String
        If stream Is Nothing Then Return "—"
        If stream.编码.Equals("av1", StringComparison.OrdinalIgnoreCase) Then Return "AOMedia Video 1"
        If stream.编码.Equals("flac", StringComparison.OrdinalIgnoreCase) Then Return "Free Lossless Audio Codec"
        Return 空值(stream.编码全名)
    End Function
    Private Shared Function 视频配置档次(stream As 媒体流信息) As String
        If stream Is Nothing Then Return "—"
        Dim profile = 空值(stream.配置档次)
        If stream.编码.Equals("av1", StringComparison.OrdinalIgnoreCase) AndAlso stream.编码级别 > 0 Then
            Dim level = If(stream.编码级别 = 12, "5.0", If(stream.编码级别 = 13, "5.1", If(stream.编码级别 = 14, "5.2", stream.编码级别.ToString())))
            Return $"{profile}@L{level}"
        End If
        Return profile
    End Function
    Private Shared Function 帧率分数(stream As 媒体流信息) As String
        If stream Is Nothing Then Return "—"
        Dim value = If(stream.标称帧率分母 > 0, CDbl(stream.标称帧率分子) / stream.标称帧率分母, stream.平均帧率)
        If value <= 0 Then Return "—"
        If stream.标称帧率分子 > 0 AndAlso stream.标称帧率分母 > 0 Then Return $"{value:F3} ({stream.标称帧率分子}/{stream.标称帧率分母}) FPS"
        Return $"{value:F3} FPS"
    End Function
    Private Shared Function 宽高比(numerator As Integer, denominator As Integer, width As Integer, height As Integer) As String
        If numerator > 0 AndAlso denominator > 0 Then Return $"{numerator}:{denominator}"
        If width > 0 AndAlso height > 0 Then Return $"{CDbl(width) / height:0.##}:1"
        Return "—"
    End Function
    Private Shared Function 扫描类型(fieldOrder As Integer) As String
        Return If(fieldOrder = 1, "Progressive", If(fieldOrder = 2 OrElse fieldOrder = 6, "Interlaced", "—"))
    End Function
    Private Shared Function 位像素帧(stream As 媒体流信息) As String
        If stream Is Nothing OrElse stream.宽度 <= 0 OrElse stream.高度 <= 0 OrElse stream.比特率 <= 0 Then Return ""
        Dim fps = If(stream.标称帧率分母 > 0, stream.标称帧率, stream.平均帧率)
        If fps <= 0 Then Return ""
        Return $"{stream.比特率 / (CDbl(stream.宽度) * stream.高度 * fps):0.000}"
    End Function
    Private Shared Function 流大小(stream As 媒体流信息, totalBytes As Long) As String
        If stream Is Nothing OrElse stream.流大小 <= 0 Then Return ""
        Dim percent = If(totalBytes > 0, stream.流大小 * 100.0R / totalBytes, 0.0R)
        Return $"{大小(stream.流大小)} ({percent:0}%)"
    End Function
    Private Shared Function 元数据值(values As Dictionary(Of String, String), ParamArray keys As String()) As String
        If values Is Nothing Then Return "—"
        For Each key In keys
            Dim value As String = Nothing
            If values.TryGetValue(key, value) AndAlso Not String.IsNullOrWhiteSpace(value) Then Return value
        Next
        Return "—"
    End Function
    Private Shared Function 色彩范围(value As Integer) As String
        Return If(value = 1, "Limited", If(value = 2, "Full", "—"))
    End Function
    Private Shared Function 色彩原色(value As Integer) As String
        Select Case value
            Case 1 : Return "BT.709"
            Case 9 : Return "BT.2020"
            Case 12 : Return "Display P3"
            Case Else : Return "—"
        End Select
    End Function
    Private Shared Function 色彩传递(value As Integer) As String
        Select Case value
            Case 1 : Return "BT.709"
            Case 16 : Return "PQ"
            Case 18 : Return "HLG"
            Case Else : Return "—"
        End Select
    End Function
    Private Shared Function 色彩空间(value As Integer) As String
        Select Case value
            Case 1 : Return "BT.709"
            Case 9 : Return "BT.2020 non-constant"
            Case 10 : Return "BT.2020 constant"
            Case Else : Return "—"
        End Select
    End Function
    Private Shared Function 输出色彩(stream As 媒体流信息, snapshot As 播放器快照) As String
        If stream Is Nothing Then Return "—"
        Dim sourceBits = If(stream.位深度 > 0, stream.位深度, stream.解码输出位深度)
        Dim outputBits = If(snapshot Is Nothing OrElse snapshot.视频输出位深度 <= 0,
            If(sourceBits > 10, 16, If(sourceBits > 8, 10, 8)), snapshot.视频输出位深度)
        Dim hdr = snapshot IsNot Nothing AndAlso snapshot.实际色彩模式 = 色彩输出模式.峰值映射HDR
        If outputBits >= 16 Then Return $"scRGB(16bit 浮点) | {(If(hdr, "HDR 线性", "SDR"))}"
        If outputBits >= 10 Then Return $"RGB10A2(10bit) | {(If(hdr, "HDR PQ", "SDR"))}"
        Return $"BGRA8(8bit) | {(If(hdr, "HDR 转 SDR", "SDR"))}"
    End Function
End Class
