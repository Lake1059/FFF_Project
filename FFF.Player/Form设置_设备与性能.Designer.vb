<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_设备与性能
    Inherits System.Windows.Forms.Form

    'Form 重写 Dispose，以清理组件列表。
    <System.Diagnostics.DebuggerNonUserCode()> _
    Protected Overrides Sub Dispose(ByVal disposing As Boolean)
        Try
            If disposing AndAlso components IsNot Nothing Then
                components.Dispose()
            End If
        Finally
            MyBase.Dispose(disposing)
        End Try
    End Sub

    'Windows 窗体设计器所必需的
    Private components As System.ComponentModel.IContainer

    '注意: 以下过程是 Windows 窗体设计器所必需的
    '可以使用 Windows 窗体设计器修改它。  
    '不要使用代码编辑器修改它。
    <System.Diagnostics.DebuggerStepThrough()> _
    Private Sub InitializeComponent()
        ModernPanel6 = New LakeUI.ModernPanel()
        MCB_LakeUI渲染引擎API = New LakeUI.ModernComboBox()
        JustEmptyControl1 = New LakeUI.JustEmptyControl()
        MCB_视频渲染引擎API = New LakeUI.ModernComboBox()
        ModernPanel1 = New LakeUI.ModernPanel()
        ModernPanel7 = New LakeUI.ModernPanel()
        MCB_动态范围 = New LakeUI.ModernComboBox()
        JustEmptyControl5 = New LakeUI.JustEmptyControl()
        MCB_目标响度 = New LakeUI.ModernComboBox()
        JustEmptyControl7 = New LakeUI.JustEmptyControl()
        MCK_音频响度标准化 = New LakeUI.ModernCheckBox()
        HCL_蓝牙延迟补偿说明和显示 = New LakeUI.HtmlColorLabel()
        ModernPanel3 = New LakeUI.ModernPanel()
        ETB_蓝牙额外延迟补偿 = New LakeUI.ExcellentTrackBar()
        JustEmptyControl4 = New LakeUI.JustEmptyControl()
        MCB_蓝牙编码 = New LakeUI.ModernComboBox()
        JustEmptyControl3 = New LakeUI.JustEmptyControl()
        MCK_蓝牙延迟补偿 = New LakeUI.ModernCheckBox()
        ModernPanel5 = New LakeUI.ModernPanel()
        MCB_音频直通 = New LakeUI.ModernComboBox()
        JustEmptyControl6 = New LakeUI.JustEmptyControl()
        MCB_音频效果 = New LakeUI.ModernComboBox()
        ModernPanel4 = New LakeUI.ModernPanel()
        MCB_视频缩放质量 = New LakeUI.ModernComboBox()
        ModernPanel2 = New LakeUI.ModernPanel()
        MCB_音频缓冲区策略 = New LakeUI.ModernComboBox()
        JustEmptyControl2 = New LakeUI.JustEmptyControl()
        MCB_视频缓冲区策略 = New LakeUI.ModernComboBox()
        HCL_渲染引擎说明文本 = New LakeUI.HtmlColorLabel()
        Panel1 = New LakeUI.ModernPanel()
        MCB_GPU视频解码设备 = New LakeUI.ModernComboBox()
        ModernPanel6.SuspendLayout()
        ModernPanel1.SuspendLayout()
        ModernPanel7.SuspendLayout()
        ModernPanel3.SuspendLayout()
        ModernPanel5.SuspendLayout()
        ModernPanel4.SuspendLayout()
        ModernPanel2.SuspendLayout()
        Panel1.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel6
        ' 
        ModernPanel6.BackColor = Color.Transparent
        ModernPanel6.BackColor1 = Color.Transparent
        ModernPanel6.BorderSize = 0
        ModernPanel6.Controls.Add(MCB_LakeUI渲染引擎API)
        ModernPanel6.Controls.Add(JustEmptyControl1)
        ModernPanel6.Controls.Add(MCB_视频渲染引擎API)
        ModernPanel6.Dock = DockStyle.Top
        ModernPanel6.Location = New Point(15, 62)
        ModernPanel6.Name = "ModernPanel6"
        ModernPanel6.Padding = New Padding(0, 0, 0, 5)
        ModernPanel6.Size = New Size(608, 37)
        ModernPanel6.TabIndex = 23
        ' 
        ' MCB_LakeUI渲染引擎API
        ' 
        MCB_LakeUI渲染引擎API.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI渲染引擎API.BorderRadius = 10
        MCB_LakeUI渲染引擎API.BorderSize = 0
        MCB_LakeUI渲染引擎API.Dock = DockStyle.Fill
        MCB_LakeUI渲染引擎API.DropDownBackdropBlurPasses = 2
        MCB_LakeUI渲染引擎API.DropDownBackdropBlurRadius = 30
        MCB_LakeUI渲染引擎API.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI渲染引擎API.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI渲染引擎API.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI渲染引擎API.DropDownPadding = New Padding(10)
        MCB_LakeUI渲染引擎API.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI渲染引擎API.DropDownSelectedForeColor = Color.White
        MCB_LakeUI渲染引擎API.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI渲染引擎API.Items.Add("Direct3D 11")
        MCB_LakeUI渲染引擎API.Items.Add("Direct3D 12")
        MCB_LakeUI渲染引擎API.Location = New Point(309, 0)
        MCB_LakeUI渲染引擎API.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI渲染引擎API.MaxDropDownItems = 16
        MCB_LakeUI渲染引擎API.Name = "MCB_LakeUI渲染引擎API"
        MCB_LakeUI渲染引擎API.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI渲染引擎API.PrefixForeColor = Color.Orange
        MCB_LakeUI渲染引擎API.PrefixGap = 10
        MCB_LakeUI渲染引擎API.PrefixText = "LakeUI 渲染引擎 API"
        MCB_LakeUI渲染引擎API.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI渲染引擎API.Size = New Size(299, 32)
        MCB_LakeUI渲染引擎API.TabIndex = 23
        MCB_LakeUI渲染引擎API.ToolTipGap = -1
        MCB_LakeUI渲染引擎API.ToolTipMaxWidth = 350
        MCB_LakeUI渲染引擎API.ToolTipPadding = New Padding(15)
        MCB_LakeUI渲染引擎API.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl1
        ' 
        JustEmptyControl1.Dock = DockStyle.Left
        JustEmptyControl1.Location = New Point(299, 0)
        JustEmptyControl1.Name = "JustEmptyControl1"
        JustEmptyControl1.Size = New Size(10, 32)
        JustEmptyControl1.TabIndex = 22
        ' 
        ' MCB_视频渲染引擎API
        ' 
        MCB_视频渲染引擎API.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频渲染引擎API.BorderRadius = 10
        MCB_视频渲染引擎API.BorderSize = 0
        MCB_视频渲染引擎API.Dock = DockStyle.Left
        MCB_视频渲染引擎API.DropDownBackdropBlurPasses = 2
        MCB_视频渲染引擎API.DropDownBackdropBlurRadius = 30
        MCB_视频渲染引擎API.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_视频渲染引擎API.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_视频渲染引擎API.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_视频渲染引擎API.DropDownPadding = New Padding(10)
        MCB_视频渲染引擎API.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频渲染引擎API.DropDownSelectedForeColor = Color.White
        MCB_视频渲染引擎API.Enabled = False
        MCB_视频渲染引擎API.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_视频渲染引擎API.Items.Add("Direct3D 11")
        MCB_视频渲染引擎API.Items.Add("Direct3D 12")
        MCB_视频渲染引擎API.Location = New Point(0, 0)
        MCB_视频渲染引擎API.Margin = New Padding(2, 2, 2, 2)
        MCB_视频渲染引擎API.MaxDropDownItems = 16
        MCB_视频渲染引擎API.Name = "MCB_视频渲染引擎API"
        MCB_视频渲染引擎API.Padding = New Padding(10, 0, 10, 0)
        MCB_视频渲染引擎API.PrefixForeColor = Color.Orange
        MCB_视频渲染引擎API.PrefixGap = 10
        MCB_视频渲染引擎API.PrefixText = "视频渲染引擎 API"
        MCB_视频渲染引擎API.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频渲染引擎API.Size = New Size(299, 32)
        MCB_视频渲染引擎API.TabIndex = 1
        MCB_视频渲染引擎API.ToolTipGap = -1
        MCB_视频渲染引擎API.ToolTipMaxWidth = 350
        MCB_视频渲染引擎API.ToolTipPadding = New Padding(15)
        MCB_视频渲染引擎API.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(ModernPanel7)
        ModernPanel1.Controls.Add(HCL_蓝牙延迟补偿说明和显示)
        ModernPanel1.Controls.Add(ModernPanel3)
        ModernPanel1.Controls.Add(ModernPanel5)
        ModernPanel1.Controls.Add(ModernPanel4)
        ModernPanel1.Controls.Add(ModernPanel2)
        ModernPanel1.Controls.Add(HCL_渲染引擎说明文本)
        ModernPanel1.Controls.Add(ModernPanel6)
        ModernPanel1.Controls.Add(Panel1)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(15, 20, 20, 20)
        ModernPanel1.ScrollBarMode = LakeUI.ModernPanel.ScrollMode.Vertical
        ModernPanel1.Size = New Size(643, 498)
        ModernPanel1.TabIndex = 1
        ' 
        ' ModernPanel7
        ' 
        ModernPanel7.BackColor = Color.Transparent
        ModernPanel7.BackColor1 = Color.Transparent
        ModernPanel7.BorderSize = 0
        ModernPanel7.Controls.Add(MCB_动态范围)
        ModernPanel7.Controls.Add(JustEmptyControl5)
        ModernPanel7.Controls.Add(MCB_目标响度)
        ModernPanel7.Controls.Add(JustEmptyControl7)
        ModernPanel7.Controls.Add(MCK_音频响度标准化)
        ModernPanel7.Dock = DockStyle.Top
        ModernPanel7.Location = New Point(15, 346)
        ModernPanel7.Name = "ModernPanel7"
        ModernPanel7.Padding = New Padding(0, 10, 0, 0)
        ModernPanel7.Size = New Size(608, 42)
        ModernPanel7.TabIndex = 26
        ' 
        ' MCB_动态范围
        ' 
        MCB_动态范围.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_动态范围.BorderRadius = 10
        MCB_动态范围.BorderSize = 0
        MCB_动态范围.Dock = DockStyle.Fill
        MCB_动态范围.DropDownBackdropBlurPasses = 2
        MCB_动态范围.DropDownBackdropBlurRadius = 30
        MCB_动态范围.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_动态范围.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_动态范围.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_动态范围.DropDownPadding = New Padding(10)
        MCB_动态范围.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_动态范围.DropDownSelectedForeColor = Color.White
        MCB_动态范围.Enabled = False
        MCB_动态范围.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_动态范围.Items.Add("1")
        MCB_动态范围.Items.Add("2")
        MCB_动态范围.Items.Add("3")
        MCB_动态范围.Items.Add("4")
        MCB_动态范围.Items.Add("5")
        MCB_动态范围.Items.Add("6")
        MCB_动态范围.Items.Add("7")
        MCB_动态范围.Items.Add("8")
        MCB_动态范围.Items.Add("9")
        MCB_动态范围.Items.Add("10")
        MCB_动态范围.Location = New Point(381, 10)
        MCB_动态范围.Margin = New Padding(2, 2, 2, 2)
        MCB_动态范围.MaxDropDownItems = 16
        MCB_动态范围.Name = "MCB_动态范围"
        MCB_动态范围.Padding = New Padding(10, 0, 10, 0)
        MCB_动态范围.PrefixForeColor = Color.YellowGreen
        MCB_动态范围.PrefixGap = 10
        MCB_动态范围.PrefixText = "动态范围 LU"
        MCB_动态范围.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_动态范围.Size = New Size(227, 32)
        MCB_动态范围.TabIndex = 32
        MCB_动态范围.ToolTipGap = -1
        MCB_动态范围.ToolTipMaxWidth = 350
        MCB_动态范围.ToolTipPadding = New Padding(15)
        MCB_动态范围.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl5
        ' 
        JustEmptyControl5.Dock = DockStyle.Left
        JustEmptyControl5.Location = New Point(370, 10)
        JustEmptyControl5.Name = "JustEmptyControl5"
        JustEmptyControl5.Size = New Size(11, 32)
        JustEmptyControl5.TabIndex = 31
        ' 
        ' MCB_目标响度
        ' 
        MCB_目标响度.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_目标响度.BorderRadius = 10
        MCB_目标响度.BorderSize = 0
        MCB_目标响度.Dock = DockStyle.Left
        MCB_目标响度.DropDownBackdropBlurPasses = 2
        MCB_目标响度.DropDownBackdropBlurRadius = 30
        MCB_目标响度.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_目标响度.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_目标响度.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_目标响度.DropDownPadding = New Padding(10)
        MCB_目标响度.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_目标响度.DropDownSelectedForeColor = Color.White
        MCB_目标响度.Enabled = False
        MCB_目标响度.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_目标响度.Items.Add("-12")
        MCB_目标响度.Items.Add("-13")
        MCB_目标响度.Items.Add("-14")
        MCB_目标响度.Items.Add("-15")
        MCB_目标响度.Items.Add("-16")
        MCB_目标响度.Items.Add("-17")
        MCB_目标响度.Items.Add("-18")
        MCB_目标响度.Items.Add("-19")
        MCB_目标响度.Items.Add("-20")
        MCB_目标响度.Items.Add("-21")
        MCB_目标响度.Items.Add("-22")
        MCB_目标响度.Items.Add("-23")
        MCB_目标响度.Items.Add("-24")
        MCB_目标响度.Location = New Point(142, 10)
        MCB_目标响度.Margin = New Padding(2, 2, 2, 2)
        MCB_目标响度.MaxDropDownItems = 16
        MCB_目标响度.Name = "MCB_目标响度"
        MCB_目标响度.Padding = New Padding(10, 0, 10, 0)
        MCB_目标响度.PrefixForeColor = Color.YellowGreen
        MCB_目标响度.PrefixGap = 10
        MCB_目标响度.PrefixText = "目标响度 LUFS"
        MCB_目标响度.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_目标响度.Size = New Size(228, 32)
        MCB_目标响度.TabIndex = 30
        MCB_目标响度.ToolTipGap = -1
        MCB_目标响度.ToolTipMaxWidth = 350
        MCB_目标响度.ToolTipPadding = New Padding(15)
        MCB_目标响度.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl7
        ' 
        JustEmptyControl7.Dock = DockStyle.Left
        JustEmptyControl7.Location = New Point(132, 10)
        JustEmptyControl7.Name = "JustEmptyControl7"
        JustEmptyControl7.Size = New Size(10, 32)
        JustEmptyControl7.TabIndex = 21
        ' 
        ' MCK_音频响度标准化
        ' 
        MCK_音频响度标准化.AccessibleDescription = "调整解码后的 PCM 音频，源码直通保持原码流。"
        MCK_音频响度标准化.AutoSize = True
        MCK_音频响度标准化.BackColor = Color.Transparent
        MCK_音频响度标准化.BoxBorderRadius = 5
        MCK_音频响度标准化.BoxBorderSize = 0
        MCK_音频响度标准化.BoxCheckedBackColor = Color.OliveDrab
        MCK_音频响度标准化.BoxInnerPadding = 7
        MCK_音频响度标准化.BoxSize = 24
        MCK_音频响度标准化.BoxTextSpacing = 10
        MCK_音频响度标准化.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_音频响度标准化.ClickAnywhere = True
        MCK_音频响度标准化.Dock = DockStyle.Left
        MCK_音频响度标准化.Location = New Point(0, 10)
        MCK_音频响度标准化.Name = "MCK_音频响度标准化"
        MCK_音频响度标准化.Size = New Size(132, 32)
        MCK_音频响度标准化.TabIndex = 29
        MCK_音频响度标准化.Text = "音频响度标准化"
        ' 
        ' HCL_蓝牙延迟补偿说明和显示
        ' 
        HCL_蓝牙延迟补偿说明和显示.AutoSize = True
        HCL_蓝牙延迟补偿说明和显示.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_蓝牙延迟补偿说明和显示.Dock = DockStyle.Top
        HCL_蓝牙延迟补偿说明和显示.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HCL_蓝牙延迟补偿说明和显示.LineSpacing = 3
        HCL_蓝牙延迟补偿说明和显示.Location = New Point(15, 304)
        HCL_蓝牙延迟补偿说明和显示.Margin = New Padding(2)
        HCL_蓝牙延迟补偿说明和显示.Name = "HCL_蓝牙延迟补偿说明和显示"
        HCL_蓝牙延迟补偿说明和显示.Padding = New Padding(0, 5, 0, 0)
        HCL_蓝牙延迟补偿说明和显示.Size = New Size(608, 42)
        HCL_蓝牙延迟补偿说明和显示.TabIndex = 25
        HCL_蓝牙延迟补偿说明和显示.Text = "仅输出到蓝牙设备时生效，推荐选择自动识别协议编码，还可使用滑块进行额外调整<br>当前蓝牙设备：无 | 当前编码协议：无"
        ' 
        ' ModernPanel3
        ' 
        ModernPanel3.BackColor = Color.Transparent
        ModernPanel3.BackColor1 = Color.Transparent
        ModernPanel3.BorderSize = 0
        ModernPanel3.Controls.Add(ETB_蓝牙额外延迟补偿)
        ModernPanel3.Controls.Add(JustEmptyControl4)
        ModernPanel3.Controls.Add(MCB_蓝牙编码)
        ModernPanel3.Controls.Add(JustEmptyControl3)
        ModernPanel3.Controls.Add(MCK_蓝牙延迟补偿)
        ModernPanel3.Dock = DockStyle.Top
        ModernPanel3.Location = New Point(15, 262)
        ModernPanel3.Name = "ModernPanel3"
        ModernPanel3.Padding = New Padding(0, 10, 0, 0)
        ModernPanel3.Size = New Size(608, 42)
        ModernPanel3.TabIndex = 24
        ' 
        ' ETB_蓝牙额外延迟补偿
        ' 
        ETB_蓝牙额外延迟补偿.BackColor = Color.Transparent
        ETB_蓝牙额外延迟补偿.Dock = DockStyle.Fill
        ETB_蓝牙额外延迟补偿.LabelColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_蓝牙额外延迟补偿.LabelLineColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_蓝牙额外延迟补偿.LabelLineLength = 16
        ETB_蓝牙额外延迟补偿.Location = New Point(311, 10)
        ETB_蓝牙额外延迟补偿.Margin = New Padding(2, 2, 2, 2)
        ETB_蓝牙额外延迟补偿.Minimum = -100R
        ETB_蓝牙额外延迟补偿.Name = "ETB_蓝牙额外延迟补偿"
        ETB_蓝牙额外延迟补偿.Size = New Size(297, 32)
        ETB_蓝牙额外延迟补偿.TabIndex = 32
        ETB_蓝牙额外延迟补偿.ThumbBorderWidth = 0
        ETB_蓝牙额外延迟补偿.ThumbColor = Color.OliveDrab
        ETB_蓝牙额外延迟补偿.ThumbHeight = 28
        ETB_蓝牙额外延迟补偿.ThumbTextDecimalPlaces = 0
        ETB_蓝牙额外延迟补偿.ThumbTextMode = LakeUI.ExcellentTrackBar.ThumbTextModeEnum.Value
        ETB_蓝牙额外延迟补偿.ThumbWidth = 38
        ETB_蓝牙额外延迟补偿.TrackColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_蓝牙额外延迟补偿.TrackFillColor = Color.Transparent
        ' 
        ' JustEmptyControl4
        ' 
        JustEmptyControl4.Dock = DockStyle.Left
        JustEmptyControl4.Location = New Point(300, 10)
        JustEmptyControl4.Name = "JustEmptyControl4"
        JustEmptyControl4.Size = New Size(11, 32)
        JustEmptyControl4.TabIndex = 31
        ' 
        ' MCB_蓝牙编码
        ' 
        MCB_蓝牙编码.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_蓝牙编码.BorderRadius = 10
        MCB_蓝牙编码.BorderSize = 0
        MCB_蓝牙编码.Dock = DockStyle.Left
        MCB_蓝牙编码.DropDownBackdropBlurPasses = 2
        MCB_蓝牙编码.DropDownBackdropBlurRadius = 30
        MCB_蓝牙编码.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_蓝牙编码.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_蓝牙编码.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_蓝牙编码.DropDownPadding = New Padding(10)
        MCB_蓝牙编码.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_蓝牙编码.DropDownSelectedForeColor = Color.White
        MCB_蓝牙编码.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_蓝牙编码.Items.Add("自动")
        MCB_蓝牙编码.Items.Add("SBC")
        MCB_蓝牙编码.Items.Add("AAC")
        MCB_蓝牙编码.Items.Add("aptX")
        MCB_蓝牙编码.Items.Add("aptX HD")
        MCB_蓝牙编码.Items.Add("aptX LL")
        MCB_蓝牙编码.Items.Add("LDAC")
        MCB_蓝牙编码.Items.Add("LHDC/HWA")
        MCB_蓝牙编码.Items.Add("L2HC")
        MCB_蓝牙编码.Items.Add("LC3")
        MCB_蓝牙编码.Location = New Point(128, 10)
        MCB_蓝牙编码.Margin = New Padding(2, 2, 2, 2)
        MCB_蓝牙编码.MaxDropDownItems = 16
        MCB_蓝牙编码.Name = "MCB_蓝牙编码"
        MCB_蓝牙编码.Padding = New Padding(10, 0, 10, 0)
        MCB_蓝牙编码.PrefixForeColor = Color.YellowGreen
        MCB_蓝牙编码.PrefixGap = 10
        MCB_蓝牙编码.PrefixText = "编码"
        MCB_蓝牙编码.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_蓝牙编码.Size = New Size(172, 32)
        MCB_蓝牙编码.TabIndex = 30
        MCB_蓝牙编码.ToolTipGap = -1
        MCB_蓝牙编码.ToolTipMaxWidth = 350
        MCB_蓝牙编码.ToolTipPadding = New Padding(15)
        MCB_蓝牙编码.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl3
        ' 
        JustEmptyControl3.Dock = DockStyle.Left
        JustEmptyControl3.Location = New Point(118, 10)
        JustEmptyControl3.Name = "JustEmptyControl3"
        JustEmptyControl3.Size = New Size(10, 32)
        JustEmptyControl3.TabIndex = 21
        ' 
        ' MCK_蓝牙延迟补偿
        ' 
        MCK_蓝牙延迟补偿.AutoSize = True
        MCK_蓝牙延迟补偿.BackColor = Color.Transparent
        MCK_蓝牙延迟补偿.BoxBorderRadius = 5
        MCK_蓝牙延迟补偿.BoxBorderSize = 0
        MCK_蓝牙延迟补偿.BoxCheckedBackColor = Color.OliveDrab
        MCK_蓝牙延迟补偿.BoxInnerPadding = 7
        MCK_蓝牙延迟补偿.BoxSize = 24
        MCK_蓝牙延迟补偿.BoxTextSpacing = 10
        MCK_蓝牙延迟补偿.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_蓝牙延迟补偿.ClickAnywhere = True
        MCK_蓝牙延迟补偿.Dock = DockStyle.Left
        MCK_蓝牙延迟补偿.Location = New Point(0, 10)
        MCK_蓝牙延迟补偿.Name = "MCK_蓝牙延迟补偿"
        MCK_蓝牙延迟补偿.Size = New Size(118, 32)
        MCK_蓝牙延迟补偿.TabIndex = 29
        MCK_蓝牙延迟补偿.Text = "蓝牙延迟补偿"
        ' 
        ' ModernPanel5
        ' 
        ModernPanel5.BackColor = Color.Transparent
        ModernPanel5.BackColor1 = Color.Transparent
        ModernPanel5.BorderSize = 0
        ModernPanel5.Controls.Add(MCB_音频直通)
        ModernPanel5.Controls.Add(JustEmptyControl6)
        ModernPanel5.Controls.Add(MCB_音频效果)
        ModernPanel5.Dock = DockStyle.Top
        ModernPanel5.Location = New Point(15, 220)
        ModernPanel5.Name = "ModernPanel5"
        ModernPanel5.Padding = New Padding(0, 10, 0, 0)
        ModernPanel5.Size = New Size(608, 42)
        ModernPanel5.TabIndex = 22
        ' 
        ' MCB_音频直通
        ' 
        MCB_音频直通.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.BorderRadius = 10
        MCB_音频直通.BorderSize = 0
        MCB_音频直通.Dock = DockStyle.Fill
        MCB_音频直通.DropDownBackdropBlurPasses = 2
        MCB_音频直通.DropDownBackdropBlurRadius = 30
        MCB_音频直通.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_音频直通.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_音频直通.DropDownPadding = New Padding(10)
        MCB_音频直通.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.DropDownSelectedForeColor = Color.White
        MCB_音频直通.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.Items.Add("自动（支持时源码直通）")
        MCB_音频直通.Items.Add("强制（失败时显式提示）")
        MCB_音频直通.Items.Add("关")
        MCB_音频直通.Location = New Point(309, 10)
        MCB_音频直通.Margin = New Padding(2, 2, 2, 2)
        MCB_音频直通.MaxDropDownItems = 16
        MCB_音频直通.Name = "MCB_音频直通"
        MCB_音频直通.Padding = New Padding(10, 0, 10, 0)
        MCB_音频直通.PrefixForeColor = Color.YellowGreen
        MCB_音频直通.PrefixGap = 10
        MCB_音频直通.PrefixText = "音频直通"
        MCB_音频直通.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.Size = New Size(299, 32)
        MCB_音频直通.TabIndex = 22
        MCB_音频直通.ToolTipGap = -1
        MCB_音频直通.ToolTipMaxWidth = 350
        MCB_音频直通.ToolTipPadding = New Padding(15)
        MCB_音频直通.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl6
        ' 
        JustEmptyControl6.Dock = DockStyle.Left
        JustEmptyControl6.Location = New Point(299, 10)
        JustEmptyControl6.Name = "JustEmptyControl6"
        JustEmptyControl6.Size = New Size(10, 32)
        JustEmptyControl6.TabIndex = 21
        ' 
        ' MCB_音频效果
        ' 
        MCB_音频效果.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.BorderRadius = 10
        MCB_音频效果.BorderSize = 0
        MCB_音频效果.Dock = DockStyle.Left
        MCB_音频效果.DropDownBackdropBlurPasses = 2
        MCB_音频效果.DropDownBackdropBlurRadius = 30
        MCB_音频效果.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_音频效果.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_音频效果.DropDownPadding = New Padding(10)
        MCB_音频效果.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.DropDownSelectedForeColor = Color.White
        MCB_音频效果.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.Items.Add("无")
        MCB_音频效果.Items.Add("自动 HRTF 空间音频")
        MCB_音频效果.Items.Add("单声道音频")
        MCB_音频效果.Location = New Point(0, 10)
        MCB_音频效果.Margin = New Padding(2, 2, 2, 2)
        MCB_音频效果.MaxDropDownItems = 16
        MCB_音频效果.Name = "MCB_音频效果"
        MCB_音频效果.Padding = New Padding(10, 0, 10, 0)
        MCB_音频效果.PrefixForeColor = Color.YellowGreen
        MCB_音频效果.PrefixGap = 10
        MCB_音频效果.PrefixText = "音频效果"
        MCB_音频效果.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.Size = New Size(299, 32)
        MCB_音频效果.TabIndex = 0
        MCB_音频效果.ToolTipGap = -1
        MCB_音频效果.ToolTipMaxWidth = 350
        MCB_音频效果.ToolTipPadding = New Padding(15)
        MCB_音频效果.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' ModernPanel4
        ' 
        ModernPanel4.BackColor = Color.Transparent
        ModernPanel4.BackColor1 = Color.Transparent
        ModernPanel4.BorderSize = 0
        ModernPanel4.Controls.Add(MCB_视频缩放质量)
        ModernPanel4.Dock = DockStyle.Top
        ModernPanel4.Location = New Point(15, 178)
        ModernPanel4.Name = "ModernPanel4"
        ModernPanel4.Padding = New Padding(0, 10, 0, 0)
        ModernPanel4.Size = New Size(608, 42)
        ModernPanel4.TabIndex = 21
        ' 
        ' MCB_视频缩放质量
        ' 
        MCB_视频缩放质量.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缩放质量.BorderRadius = 10
        MCB_视频缩放质量.BorderSize = 0
        MCB_视频缩放质量.Dock = DockStyle.Left
        MCB_视频缩放质量.DropDownBackdropBlurPasses = 2
        MCB_视频缩放质量.DropDownBackdropBlurRadius = 30
        MCB_视频缩放质量.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_视频缩放质量.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_视频缩放质量.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_视频缩放质量.DropDownPadding = New Padding(10)
        MCB_视频缩放质量.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缩放质量.DropDownSelectedForeColor = Color.White
        MCB_视频缩放质量.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_视频缩放质量.Items.Add("均衡 Hermite")
        MCB_视频缩放质量.Items.Add("高质量 Lanczos3")
        MCB_视频缩放质量.Location = New Point(0, 10)
        MCB_视频缩放质量.Margin = New Padding(2, 2, 2, 2)
        MCB_视频缩放质量.MaxDropDownItems = 16
        MCB_视频缩放质量.Name = "MCB_视频缩放质量"
        MCB_视频缩放质量.Padding = New Padding(10, 0, 10, 0)
        MCB_视频缩放质量.PrefixForeColor = Color.CornflowerBlue
        MCB_视频缩放质量.PrefixGap = 10
        MCB_视频缩放质量.PrefixText = "视频缩放质量"
        MCB_视频缩放质量.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缩放质量.Size = New Size(300, 32)
        MCB_视频缩放质量.TabIndex = 0
        MCB_视频缩放质量.ToolTipGap = -1
        MCB_视频缩放质量.ToolTipMaxWidth = 350
        MCB_视频缩放质量.ToolTipPadding = New Padding(15)
        MCB_视频缩放质量.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' ModernPanel2
        ' 
        ModernPanel2.BackColor = Color.Transparent
        ModernPanel2.BackColor1 = Color.Transparent
        ModernPanel2.BorderSize = 0
        ModernPanel2.Controls.Add(MCB_音频缓冲区策略)
        ModernPanel2.Controls.Add(JustEmptyControl2)
        ModernPanel2.Controls.Add(MCB_视频缓冲区策略)
        ModernPanel2.Dock = DockStyle.Top
        ModernPanel2.Location = New Point(15, 136)
        ModernPanel2.Name = "ModernPanel2"
        ModernPanel2.Padding = New Padding(0, 10, 0, 0)
        ModernPanel2.Size = New Size(608, 42)
        ModernPanel2.TabIndex = 19
        ' 
        ' MCB_音频缓冲区策略
        ' 
        MCB_音频缓冲区策略.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频缓冲区策略.BorderRadius = 10
        MCB_音频缓冲区策略.BorderSize = 0
        MCB_音频缓冲区策略.Dock = DockStyle.Fill
        MCB_音频缓冲区策略.DropDownBackdropBlurPasses = 2
        MCB_音频缓冲区策略.DropDownBackdropBlurRadius = 30
        MCB_音频缓冲区策略.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_音频缓冲区策略.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_音频缓冲区策略.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_音频缓冲区策略.DropDownPadding = New Padding(10)
        MCB_音频缓冲区策略.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频缓冲区策略.DropDownSelectedForeColor = Color.White
        MCB_音频缓冲区策略.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_音频缓冲区策略.Items.Add("均衡策略")
        MCB_音频缓冲区策略.Items.Add("适用于低性能设备")
        MCB_音频缓冲区策略.Items.Add("适用于高性能设备")
        MCB_音频缓冲区策略.Location = New Point(309, 10)
        MCB_音频缓冲区策略.Margin = New Padding(2, 2, 2, 2)
        MCB_音频缓冲区策略.MaxDropDownItems = 16
        MCB_音频缓冲区策略.Name = "MCB_音频缓冲区策略"
        MCB_音频缓冲区策略.Padding = New Padding(10, 0, 10, 0)
        MCB_音频缓冲区策略.PrefixForeColor = Color.YellowGreen
        MCB_音频缓冲区策略.PrefixGap = 10
        MCB_音频缓冲区策略.PrefixText = "音频缓冲区策略"
        MCB_音频缓冲区策略.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频缓冲区策略.Size = New Size(299, 32)
        MCB_音频缓冲区策略.TabIndex = 23
        MCB_音频缓冲区策略.ToolTipGap = -1
        MCB_音频缓冲区策略.ToolTipMaxWidth = 350
        MCB_音频缓冲区策略.ToolTipPadding = New Padding(15)
        MCB_音频缓冲区策略.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl2
        ' 
        JustEmptyControl2.Dock = DockStyle.Left
        JustEmptyControl2.Location = New Point(299, 10)
        JustEmptyControl2.Name = "JustEmptyControl2"
        JustEmptyControl2.Size = New Size(10, 32)
        JustEmptyControl2.TabIndex = 22
        ' 
        ' MCB_视频缓冲区策略
        ' 
        MCB_视频缓冲区策略.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.BorderRadius = 10
        MCB_视频缓冲区策略.BorderSize = 0
        MCB_视频缓冲区策略.Dock = DockStyle.Left
        MCB_视频缓冲区策略.DropDownBackdropBlurPasses = 2
        MCB_视频缓冲区策略.DropDownBackdropBlurRadius = 30
        MCB_视频缓冲区策略.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_视频缓冲区策略.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_视频缓冲区策略.DropDownPadding = New Padding(10)
        MCB_视频缓冲区策略.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.DropDownSelectedForeColor = Color.White
        MCB_视频缓冲区策略.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.Items.Add("均衡策略")
        MCB_视频缓冲区策略.Items.Add("适用于低性能设备")
        MCB_视频缓冲区策略.Items.Add("适用于高性能设备")
        MCB_视频缓冲区策略.Location = New Point(0, 10)
        MCB_视频缓冲区策略.Margin = New Padding(2, 2, 2, 2)
        MCB_视频缓冲区策略.MaxDropDownItems = 16
        MCB_视频缓冲区策略.Name = "MCB_视频缓冲区策略"
        MCB_视频缓冲区策略.Padding = New Padding(10, 0, 10, 0)
        MCB_视频缓冲区策略.PrefixForeColor = Color.CornflowerBlue
        MCB_视频缓冲区策略.PrefixGap = 10
        MCB_视频缓冲区策略.PrefixText = "视频缓冲区策略"
        MCB_视频缓冲区策略.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.Size = New Size(299, 32)
        MCB_视频缓冲区策略.TabIndex = 0
        MCB_视频缓冲区策略.ToolTipGap = -1
        MCB_视频缓冲区策略.ToolTipMaxWidth = 350
        MCB_视频缓冲区策略.ToolTipPadding = New Padding(15)
        MCB_视频缓冲区策略.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' HCL_渲染引擎说明文本
        ' 
        HCL_渲染引擎说明文本.AutoSize = True
        HCL_渲染引擎说明文本.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_渲染引擎说明文本.Dock = DockStyle.Top
        HCL_渲染引擎说明文本.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HCL_渲染引擎说明文本.LineSpacing = 3
        HCL_渲染引擎说明文本.Location = New Point(15, 99)
        HCL_渲染引擎说明文本.Margin = New Padding(2)
        HCL_渲染引擎说明文本.Name = "HCL_渲染引擎说明文本"
        HCL_渲染引擎说明文本.Size = New Size(608, 37)
        HCL_渲染引擎说明文本.TabIndex = 18
        HCL_渲染引擎说明文本.Text = "播放器的渲染引擎更改在下一次播放时生效，LakeUI 渲染引擎的更改会立刻重载<br>DX12 在不同的硬件上可能会有不同的性能表现，不建议旧显卡使用 DX12"
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.BackColor1 = Color.Transparent
        Panel1.BorderSize = 0
        Panel1.Controls.Add(MCB_GPU视频解码设备)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(15, 20)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 0, 0, 10)
        Panel1.Size = New Size(608, 42)
        Panel1.TabIndex = 17
        ' 
        ' MCB_GPU视频解码设备
        ' 
        MCB_GPU视频解码设备.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_GPU视频解码设备.BorderRadius = 10
        MCB_GPU视频解码设备.BorderSize = 0
        MCB_GPU视频解码设备.Dock = DockStyle.Fill
        MCB_GPU视频解码设备.DropDownBackdropBlurPasses = 2
        MCB_GPU视频解码设备.DropDownBackdropBlurRadius = 30
        MCB_GPU视频解码设备.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_GPU视频解码设备.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_GPU视频解码设备.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_GPU视频解码设备.DropDownPadding = New Padding(10)
        MCB_GPU视频解码设备.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_GPU视频解码设备.DropDownSelectedForeColor = Color.White
        MCB_GPU视频解码设备.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_GPU视频解码设备.Items.Add("自动选择")
        MCB_GPU视频解码设备.Location = New Point(0, 0)
        MCB_GPU视频解码设备.Margin = New Padding(2, 2, 2, 2)
        MCB_GPU视频解码设备.MaxDropDownItems = 16
        MCB_GPU视频解码设备.Name = "MCB_GPU视频解码设备"
        MCB_GPU视频解码设备.Padding = New Padding(10, 0, 10, 0)
        MCB_GPU视频解码设备.PrefixForeColor = Color.Orange
        MCB_GPU视频解码设备.PrefixGap = 10
        MCB_GPU视频解码设备.PrefixText = "GPU 视频解码设备"
        MCB_GPU视频解码设备.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_GPU视频解码设备.Size = New Size(608, 32)
        MCB_GPU视频解码设备.TabIndex = 0
        MCB_GPU视频解码设备.ToolTipGap = -1
        MCB_GPU视频解码设备.ToolTipMaxWidth = 350
        MCB_GPU视频解码设备.ToolTipPadding = New Padding(15)
        MCB_GPU视频解码设备.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' Form设置_设备与性能
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(643, 498)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_设备与性能"
        Text = "Form设置_设备与性能"
        ModernPanel6.ResumeLayout(False)
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ModernPanel7.ResumeLayout(False)
        ModernPanel7.PerformLayout()
        ModernPanel3.ResumeLayout(False)
        ModernPanel3.PerformLayout()
        ModernPanel5.ResumeLayout(False)
        ModernPanel4.ResumeLayout(False)
        ModernPanel2.ResumeLayout(False)
        Panel1.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents Panel1 As LakeUI.ModernPanel
    Friend WithEvents MCB_GPU视频解码设备 As LakeUI.ModernComboBox
    Friend WithEvents HCL_渲染引擎说明文本 As LakeUI.HtmlColorLabel
    Friend WithEvents ModernPanel2 As LakeUI.ModernPanel
    Friend WithEvents MCB_视频缓冲区策略 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel4 As LakeUI.ModernPanel
    Friend WithEvents MCB_视频缩放质量 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel5 As LakeUI.ModernPanel
    Friend WithEvents MCB_音频效果 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel6 As LakeUI.ModernPanel
    Friend WithEvents MCB_视频渲染引擎API As LakeUI.ModernComboBox
    Friend WithEvents MCB_音频直通 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl6 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_LakeUI渲染引擎API As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl1 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_音频缓冲区策略 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl2 As LakeUI.JustEmptyControl
    Friend WithEvents ModernPanel3 As LakeUI.ModernPanel
    Friend WithEvents JustEmptyControl3 As LakeUI.JustEmptyControl
    Friend WithEvents MCK_蓝牙延迟补偿 As LakeUI.ModernCheckBox
    Friend WithEvents MCB_蓝牙编码 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl4 As LakeUI.JustEmptyControl
    Friend WithEvents HCL_蓝牙延迟补偿说明和显示 As LakeUI.HtmlColorLabel
    Friend WithEvents ETB_蓝牙额外延迟补偿 As LakeUI.ExcellentTrackBar
    Friend WithEvents ModernPanel7 As LakeUI.ModernPanel
    Friend WithEvents JustEmptyControl5 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_目标响度 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl7 As LakeUI.JustEmptyControl
    Friend WithEvents MCK_音频响度标准化 As LakeUI.ModernCheckBox
    Friend WithEvents MCB_动态范围 As LakeUI.ModernComboBox
End Class
