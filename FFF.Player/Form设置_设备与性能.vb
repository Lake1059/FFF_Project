Public Class Form设置_设备与性能

    Private 正在初始化 As Boolean = True
    Private 设备列表 As New List(Of GPU解码设备)
    Private ReadOnly 枚举设备 As Func(Of IReadOnlyList(Of GPU解码设备))

    ' 蓝牙行加在页面最底部（后加者靠上，因此显式 SetChildIndex 到 0/1），
    ' 不挤占既有的 GPU/缓冲/音频效果行；这一页底部本来就有空白。
    Private ReadOnly 蓝牙开关 As New LakeUI.ModernCheckBox()
    Private ReadOnly 蓝牙提前量 As New LakeUI.ModernTextBox()
    Private ReadOnly 蓝牙检测按钮 As New LakeUI.ModernButton()
    Private ReadOnly 蓝牙控制行 As New LakeUI.ModernPanel()
    Private ReadOnly 蓝牙状态行 As New LakeUI.ModernPanel()
    Private ReadOnly 蓝牙状态 As New Label()

    Public Sub New()
        Me.New(AddressOf GPU解码设备.枚举)
    End Sub

    Friend Sub New(枚举设备 As Func(Of IReadOnlyList(Of GPU解码设备)))
        Me.枚举设备 = 枚举设备
        InitializeComponent()
        初始化蓝牙区域()
        正在初始化 = False
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            Dim 配置 = 设置.实例对象
            配置.规范化()
            设备列表 = New List(Of GPU解码设备) From {New GPU解码设备(String.Empty, "自动选择", -1)}
            Try
                设备列表.AddRange(枚举设备())
            Catch ex As Exception
                Debug.WriteLine("设备设置页：" & ex.Message)
            End Try
            Dim 选中 = 设备列表.FindIndex(Function(x) x.标识 = 配置.GPU解码设备标识)
            If 选中 < 0 Then
                选中 = 设备列表.Count
                设备列表.Add(New GPU解码设备(配置.GPU解码设备标识, "已保存设备当前不可用（暂用自动选择）", -1))
            End If
            MCB_GPU解码设备.Items.Clear()
            For Each 设备 In 设备列表
                MCB_GPU解码设备.Items.Add(设备.名称)
            Next
            MCB_GPU解码设备.SelectedIndex = 选中
            MCB_视频缓冲区策略.SelectedIndex = 配置.视频缓冲区策略
            MCB_音频缓冲区策略.SelectedIndex = 配置.音频缓冲区策略
            MCB_视频缩放质量.SelectedIndex = 配置.视频缩放质量选项
            MCB_音频效果.SelectedIndex = If(配置.启用单声道音频, 2, If(配置.启用空间音频, 1, 0))
            MCB_音频直通.SelectedIndex = 配置.音频直通选项
            ' LakeUI 图形性能组：档位下标直接回显；下拉 Items 与 设置.vb 档位表逐位对齐
            ' （一致性由两处注释互相钉住，改任何一边必须同步另一边）。
            MCB_LakeUI抗锯齿.SelectedIndex = 配置.LakeUI抗锯齿
            MCB_LakeUI文字渲染.SelectedIndex = 配置.LakeUI文字渲染
            MCB_LakeUI超采样.SelectedIndex = 配置.LakeUI超采样
            MCB_LakeUI_GPU缓存.SelectedIndex = 配置.LakeUI_GPU缓存档
            MCB_LakeUI_画刷缓存.SelectedIndex = 配置.LakeUI_画刷缓存档
            MCB_LakeUI_字体缓存.SelectedIndex = 配置.LakeUI_字体缓存档
            MCB_LakeUI_CPU缓存.SelectedIndex = 配置.LakeUI_CPU缓存档
            MCB_LakeUI_背景脏区.SelectedIndex = 配置.LakeUI_背景脏区档
            MCB_LakeUI_SSAA分桶.SelectedIndex = 配置.LakeUI_SSAA分桶档
            Dim 帧率档 = Array.IndexOf(设置.LakeUI动画帧率档, 配置.LakeUI动画帧率)
            MCB_LakeUI动画帧率.SelectedIndex = If(帧率档 >= 0, 帧率档, 5)
            ' 清理是动作不是状态，永远空档；规范化保证上面每个下标都落在合法区间。
            MCB_LakeUI缓存清理.SelectedIndex = -1
            蓝牙开关.Checked = 配置.蓝牙低延迟模式
            蓝牙提前量.Text = CStr(配置.蓝牙额外提前量毫秒)
            刷新蓝牙状态()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub 选项_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_GPU解码设备.SelectedIndexChanged,
        MCB_视频缓冲区策略.SelectedIndexChanged, MCB_音频缓冲区策略.SelectedIndexChanged,
        MCB_视频缩放质量.SelectedIndexChanged, MCB_音频效果.SelectedIndexChanged,
        MCB_音频直通.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 控件 = DirectCast(sender, LakeUI.ModernComboBox)
        If 控件.SelectedIndex < 0 Then Return
        Dim 配置 = 设置.实例对象
        If 控件 Is MCB_GPU解码设备 Then
            配置.GPU解码设备标识 = 设备列表(控件.SelectedIndex).标识
        ElseIf 控件 Is MCB_视频缓冲区策略 Then
            配置.视频缓冲区策略 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_音频缓冲区策略 Then
            配置.音频缓冲区策略 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_视频缩放质量 Then
            配置.视频缩放质量选项 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_音频效果 Then
            配置.启用空间音频 = 控件.SelectedIndex = 1
            配置.启用单声道音频 = 控件.SelectedIndex = 2
            Form1.当前主窗体?.应用音频效果设置()
        ElseIf 控件 Is MCB_音频直通 Then
            配置.音频直通选项 = 控件.SelectedIndex
            Form1.当前主窗体?.应用音频直通设置()
        End If
    End Sub

    ' LakeUI 图形性能组（2026-11-11 自 3FUI 移植）：写档位下标 → 统一走
    ' 设置.应用图形性能设置() 下发 GlobalOptions。动画帧率例外——它不走
    ' GlobalOptions，靠字体管道反射下发，所以这里触发一次全量字体重放。
    Private Sub 图形性能选项_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_LakeUI抗锯齿.SelectedIndexChanged,
        MCB_LakeUI文字渲染.SelectedIndexChanged, MCB_LakeUI超采样.SelectedIndexChanged,
        MCB_LakeUI_GPU缓存.SelectedIndexChanged, MCB_LakeUI_画刷缓存.SelectedIndexChanged,
        MCB_LakeUI_字体缓存.SelectedIndexChanged, MCB_LakeUI_CPU缓存.SelectedIndexChanged,
        MCB_LakeUI_背景脏区.SelectedIndexChanged, MCB_LakeUI_SSAA分桶.SelectedIndexChanged,
        MCB_LakeUI动画帧率.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 控件 = DirectCast(sender, LakeUI.ModernComboBox)
        If 控件.SelectedIndex < 0 Then Return
        Dim 配置 = 设置.实例对象
        If 控件 Is MCB_LakeUI抗锯齿 Then
            配置.LakeUI抗锯齿 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI文字渲染 Then
            配置.LakeUI文字渲染 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI超采样 Then
            配置.LakeUI超采样 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI_GPU缓存 Then
            配置.LakeUI_GPU缓存档 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI_画刷缓存 Then
            配置.LakeUI_画刷缓存档 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI_字体缓存 Then
            配置.LakeUI_字体缓存档 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI_CPU缓存 Then
            配置.LakeUI_CPU缓存档 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI_背景脏区 Then
            配置.LakeUI_背景脏区档 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI_SSAA分桶 Then
            配置.LakeUI_SSAA分桶档 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_LakeUI动画帧率 Then
            配置.LakeUI动画帧率 = 设置.LakeUI动画帧率档(控件.SelectedIndex)
            ' 帧率搭字体管道下发（3FUI 语义：动字体才生效），这里主动重放一次
            ' 让改动即时到达主窗体与设置窗全部控件；后开窗体由各自 Load 的字体
            ' 应用自然带上。
            字体控制.更新所有控件字体属性()
            Return
        End If
        设置.应用图形性能设置()
    End Sub

    ' 缓存清理是「动作」不是「设置」：执行完把下拉弹回空档（3FUI 起始页同款交互），
    ' 用户的清理诉求不落盘，也不该在下一次打开设置页时被重放。
    Private Sub MCB_LakeUI缓存清理_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_LakeUI缓存清理.SelectedIndexChanged
        If 正在初始化 Then Return
        Select Case MCB_LakeUI缓存清理.SelectedIndex
            Case 0
                GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, True, True)
                GC.WaitForPendingFinalizers()
            Case 1
                GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, True, True)
                GC.WaitForPendingFinalizers()
                EmptyWorkingSet(System.Diagnostics.Process.GetCurrentProcess().Handle)
            Case 2
                LakeUI.D3D_PaintBridge.CleanupD2DResources(LakeUI.D3DCacheCleanupLevel.TrimToBudget, Me)
            Case 3
                LakeUI.D3D_PaintBridge.CleanupD2DResources(LakeUI.D3DCacheCleanupLevel.ReleaseAllCaches, Me)
            Case 4
                ' 最重的一档：连 D2D 设备一起重建。播放画面走内核自己的交换链不受影响，
                ' 受影响的只是 LakeUI 控件的下一次重绘（会按需重建资源）。
                LakeUI.D3D_PaintBridge.CleanupD2DResources(LakeUI.D3DCacheCleanupLevel.ReleaseEverything, Me)
        End Select
        MCB_LakeUI缓存清理.SelectedIndex = -1
    End Sub

    ' 与 Form设置_AI字幕 同款：根面板绝对定位 + AutoScroll，宽度跟容器走。
    Private Sub 对齐面板宽度()
        Dim 宽 = Math.Max(652, ClientSize.Width)
        If Math.Abs(ModernPanel1.Width - 宽) > 2 Then ModernPanel1.Width = 宽
    End Sub

    Private Sub Form设置_设备与性能_Resize(sender As Object, e As EventArgs) Handles MyBase.Resize
        对齐面板宽度()
    End Sub

    <Runtime.InteropServices.DllImport("psapi.dll")>
    Private Shared Function EmptyWorkingSet(hProcess As IntPtr) As Boolean
    End Function

    Private Sub 初始化蓝牙区域()
        蓝牙控制行.BackColor = Color.Transparent
        蓝牙控制行.BackColor1 = Color.Transparent
        蓝牙控制行.BorderSize = 0
        蓝牙控制行.Dock = DockStyle.Top
        蓝牙控制行.Name = "蓝牙控制行"
        蓝牙控制行.Padding = New Padding(0, 10, 0, 0)
        蓝牙控制行.Size = New Size(617, 42)

        蓝牙开关.BackColor = Color.Transparent
        蓝牙开关.BoxBorderRadius = 5
        蓝牙开关.BoxBorderSize = 0
        蓝牙开关.BoxCheckedBackColor = Color.CornflowerBlue
        蓝牙开关.BoxInnerPadding = 7
        蓝牙开关.BoxSize = 24
        蓝牙开关.BoxTextSpacing = 10
        蓝牙开关.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        蓝牙开关.ClickAnywhere = True
        蓝牙开关.Dock = DockStyle.Left
        蓝牙开关.Size = New Size(200, 32)
        蓝牙开关.Text = "蓝牙低延迟模式"

        Dim 提前量标签 As New Label With {
            .AutoSize = False,
            .Dock = DockStyle.Left,
            .ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255)),
            .Text = "额外提前量 ms",
            .TextAlign = ContentAlignment.MiddleLeft,
            .Size = New Size(90, 32)
        }

        蓝牙提前量.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        蓝牙提前量.BorderRadius = 10
        蓝牙提前量.BorderSize = 0
        蓝牙提前量.Dock = DockStyle.Left
        蓝牙提前量.Padding = New Padding(10, 0, 10, 0)
        蓝牙提前量.Size = New Size(80, 32)
        蓝牙提前量.WaterText = "0"
        蓝牙提前量.WaterTextForeColor = Color.DarkGray

        蓝牙检测按钮.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        蓝牙检测按钮.BorderRadius = 10
        蓝牙检测按钮.BorderSize = 0
        蓝牙检测按钮.Dock = DockStyle.Left
        蓝牙检测按钮.Size = New Size(90, 32)
        蓝牙检测按钮.Text = "重新检测"

        Dim 蓝牙说明 As New Label With {
            .AutoSize = False,
            .Dock = DockStyle.Fill,
            .ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255)),
            .Text = "正 = 音频更早",
            .TextAlign = ContentAlignment.MiddleLeft,
            .Padding = New Padding(10, 0, 0, 0)
        }
        ' 行内 Dock=Left 后加者靠左：说明填满剩余宽度，按钮/输入框/标签/开关依次向左。
        蓝牙控制行.Controls.Add(蓝牙说明)
        蓝牙控制行.Controls.Add(蓝牙检测按钮)
        蓝牙控制行.Controls.Add(蓝牙提前量)
        蓝牙控制行.Controls.Add(提前量标签)
        蓝牙控制行.Controls.Add(蓝牙开关)

        蓝牙状态行.BackColor = Color.Transparent
        蓝牙状态行.BackColor1 = Color.Transparent
        蓝牙状态行.BorderSize = 0
        蓝牙状态行.Dock = DockStyle.Top
        蓝牙状态行.Name = "蓝牙状态行"
        蓝牙状态行.Size = New Size(617, 54)

        蓝牙状态.AutoSize = False
        蓝牙状态.Dock = DockStyle.Fill
        蓝牙状态.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        蓝牙状态.TextAlign = ContentAlignment.MiddleLeft
        蓝牙状态.Text = "尚未检测蓝牙输出"
        蓝牙状态行.Controls.Add(蓝牙状态)

        ' 页面是 652×835 的 Dock=Top 栈 + 窗体 AutoScroll；Dock 次序与 Add 相反，
        ' 后加者靠上，所以把蓝牙两行插到最底部（索引 0/1），不依赖固定 y 坐标。
        ' 运行时新增的控件不会参与 InitializeComponent 的 Dpi 自动缩放，这里按页面
        ' 已缩放的 835 高度反推倍率，补做一次 Scale，避免 150% 下变成小号控件。
        Dim 缩放 = CSng(Math.Max(1.0, ModernPanel1.Height / 835.0))
        If 缩放 > 1.01 Then
            蓝牙控制行.Scale(New SizeF(缩放, 缩放))
            蓝牙状态行.Scale(New SizeF(缩放, 缩放))
        End If
        ModernPanel1.Controls.Add(蓝牙状态行)
        ModernPanel1.Controls.SetChildIndex(蓝牙状态行, 0)
        ModernPanel1.Controls.Add(蓝牙控制行)
        ModernPanel1.Controls.SetChildIndex(蓝牙控制行, 1)
        ' 设计高度 959 = 既有内容底 815 + 控制行 42 + 间隔 8 + 状态行 54 + 底部 20。
        ' 保持与 Dpi 缩放一致的页面高度，AutoScroll 才能滚到新增区域。
        ModernPanel1.Height = Math.Max(ModernPanel1.Height, CInt(959 * 缩放))

        AddHandler 蓝牙开关.CheckedChanged, AddressOf 蓝牙开关_CheckedChanged
        AddHandler 蓝牙提前量.Leave, AddressOf 蓝牙提前量_Leave
        AddHandler 蓝牙检测按钮.Click, AddressOf 蓝牙检测按钮_Click
    End Sub

    Private Sub 蓝牙开关_CheckedChanged(sender As Object, e As EventArgs)
        If 正在初始化 Then Return
        设置.实例对象.蓝牙低延迟模式 = 蓝牙开关.Checked
        刷新蓝牙状态()
        Form1.当前主窗体?.应用蓝牙音频延迟()
    End Sub

    Private Sub 蓝牙提前量_Leave(sender As Object, e As EventArgs)
        If 正在初始化 Then Return
        Dim 值 As Integer = 0
        Integer.TryParse(蓝牙提前量.Text.Trim(), 值)
        值 = Math.Clamp(值, -100, 100)
        设置.实例对象.蓝牙额外提前量毫秒 = 值
        蓝牙提前量.Text = CStr(值)
        刷新蓝牙状态()
        Form1.当前主窗体?.应用蓝牙音频延迟()
    End Sub

    Private Sub 蓝牙检测按钮_Click(sender As Object, e As EventArgs)
        If 正在初始化 Then Return
        刷新蓝牙状态()
        Form1.当前主窗体?.应用蓝牙音频延迟()
    End Sub

    Private Sub 刷新蓝牙状态()
        Dim 配置 = 设置.实例对象
        Dim 信息 = 蓝牙音频检测器.检测默认输出()
        Dim 刷新率 = 蓝牙音频检测器.取刷新率(IntPtr.Zero)
        Dim 补偿 = 蓝牙音频延迟策略.计算补偿100纳秒(
            配置.蓝牙低延迟模式, 信息, 刷新率, 配置.蓝牙额外提前量毫秒)

        Dim 文本 As String
        If 信息.是蓝牙 Then
            Dim 设备 = If(String.IsNullOrWhiteSpace(信息.设备名称), "蓝牙音频", 信息.设备名称)
            文本 = 设备 & "｜" & 信息.协议名称 & "｜" &
                CStr(信息.延迟下界毫秒) & "–" & CStr(信息.延迟上界毫秒) & " ms｜" & 信息.说明
            If Not 信息.已连接 Then
                文本 &= "｜未连接，不补偿"
            Else
                文本 &= "｜补偿 " & 蓝牙音频延迟策略.格式化毫秒(补偿)
            End If
        Else
            文本 = 信息.说明
        End If
        If Not 配置.蓝牙低延迟模式 Then 文本 &= "｜模式未启用"
        蓝牙状态.Text = 文本
        蓝牙状态.ForeColor = If(信息.是蓝牙 AndAlso 信息.已连接,
            Color.LightGreen, Color.FromArgb(CByte(150), CByte(220), CByte(220), CByte(220)))
    End Sub
End Class
