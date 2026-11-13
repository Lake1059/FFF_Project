<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_AI字幕
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
        ModernPanel1 = New LakeUI.ModernPanel()
        PanelAI9 = New LakeUI.ModernPanel()
        HCL_线程 = New LakeUI.HtmlColorLabel()
        MCB_线程 = New LakeUI.ModernComboBox()
        PanelAI8 = New LakeUI.ModernPanel()
        HCL_偏移 = New LakeUI.HtmlColorLabel()
        MTB_偏移 = New LakeUI.ModernTextBox()
        PanelAI7 = New LakeUI.ModernPanel()
        HCL_模型档值 = New LakeUI.HtmlColorLabel()
        MCB_模型档 = New LakeUI.ModernComboBox()
        PanelAI6 = New LakeUI.ModernPanel()
        HCL_预置 = New LakeUI.HtmlColorLabel()
        MCB_预置 = New LakeUI.ModernComboBox()
        MTB_步长秒 = New LakeUI.ModernTextBox()
        MTB_窗口秒 = New LakeUI.ModernTextBox()
        PanelAI5 = New LakeUI.ModernPanel()
        HCL_显示高级 = New LakeUI.HtmlColorLabel()
        MCK_显示高级 = New LakeUI.ModernCheckBox()
        HCL_状态 = New LakeUI.HtmlColorLabel()
        PanelAI4 = New LakeUI.ModernPanel()
        HCL_语言 = New LakeUI.HtmlColorLabel()
        MCK_仅所选语言 = New LakeUI.ModernCheckBox()
        MCB_语言 = New LakeUI.ModernComboBox()
        PanelAI3 = New LakeUI.ModernPanel()
        HCL_后端 = New LakeUI.HtmlColorLabel()
        MCB_后端 = New LakeUI.ModernComboBox()
        PanelAI2 = New LakeUI.ModernPanel()
        HCL_设备 = New LakeUI.HtmlColorLabel()
        MCB_设备 = New LakeUI.ModernComboBox()
        PanelAI1 = New LakeUI.ModernPanel()
        HCL_包目录 = New LakeUI.HtmlColorLabel()
        MB_浏览 = New LakeUI.ModernButton()
        MTB_包目录 = New LakeUI.ModernTextBox()
        PanelAI11 = New LakeUI.ModernPanel()
        EPB_下载 = New LakeUI.ExcellentProgressBar()
        HCL_下载进度 = New LakeUI.HtmlColorLabel()
        PanelAI12 = New LakeUI.ModernPanel()
        HCL_包类型 = New LakeUI.HtmlColorLabel()
        MCB_包类型 = New LakeUI.ModernComboBox()
        PanelAI10 = New LakeUI.ModernPanel()
        HCL_下载状态 = New LakeUI.HtmlColorLabel()
        MB_删除包 = New LakeUI.ModernButton()
        MB_取消下载 = New LakeUI.ModernButton()
        MB_下载包 = New LakeUI.ModernButton()
        HCL_标题 = New LakeUI.HtmlColorLabel()
        ' 各行容器按「后加者靠上」从底往上加：PanelAI9 最底，标题行最后加。
        ' 行内 Dock=Left 同理反序：先加 Dock=Fill 的说明标签，再加左侧控件
        ' （后加者靠左），视觉上才是 控件……说明标签。
        PanelAI9.Controls.Add(HCL_线程)
        PanelAI9.Controls.Add(MCB_线程)
        PanelAI8.Controls.Add(HCL_偏移)
        PanelAI8.Controls.Add(MTB_偏移)
        PanelAI7.Controls.Add(HCL_模型档值)
        PanelAI7.Controls.Add(MCB_模型档)
        PanelAI6.Controls.Add(HCL_预置)
        PanelAI6.Controls.Add(MCB_预置)
        PanelAI6.Controls.Add(MTB_步长秒)
        PanelAI6.Controls.Add(MTB_窗口秒)
        PanelAI5.Controls.Add(HCL_显示高级)
        PanelAI5.Controls.Add(MCK_显示高级)
        PanelAI4.Controls.Add(HCL_语言)
        PanelAI4.Controls.Add(MCK_仅所选语言)
        PanelAI4.Controls.Add(MCB_语言)
        PanelAI3.Controls.Add(HCL_后端)
        PanelAI3.Controls.Add(MCB_后端)
        PanelAI2.Controls.Add(HCL_设备)
        PanelAI2.Controls.Add(MCB_设备)
        PanelAI1.Controls.Add(HCL_包目录)
        PanelAI1.Controls.Add(MB_浏览)
        PanelAI1.Controls.Add(MTB_包目录)
        PanelAI11.Controls.Add(HCL_下载进度)
        PanelAI11.Controls.Add(EPB_下载)
        PanelAI12.Controls.Add(HCL_包类型)
        PanelAI12.Controls.Add(MCB_包类型)
        PanelAI10.Controls.Add(HCL_下载状态)
        PanelAI10.Controls.Add(MB_删除包)
        PanelAI10.Controls.Add(MB_取消下载)
        PanelAI10.Controls.Add(MB_下载包)
        ModernPanel1.Controls.Add(PanelAI9)
        ModernPanel1.Controls.Add(PanelAI8)
        ModernPanel1.Controls.Add(PanelAI7)
        ModernPanel1.Controls.Add(PanelAI6)
        ModernPanel1.Controls.Add(PanelAI5)
        ModernPanel1.Controls.Add(HCL_状态)
        ModernPanel1.Controls.Add(PanelAI4)
        ModernPanel1.Controls.Add(PanelAI3)
        ModernPanel1.Controls.Add(PanelAI2)
        ModernPanel1.Controls.Add(PanelAI11)
        ModernPanel1.Controls.Add(PanelAI10)
        ModernPanel1.Controls.Add(PanelAI12)
        ModernPanel1.Controls.Add(PanelAI1)
        ModernPanel1.Controls.Add(HCL_标题)
        ModernPanel1.SuspendLayout()
        PanelAI9.SuspendLayout()
        PanelAI8.SuspendLayout()
        PanelAI7.SuspendLayout()
        PanelAI6.SuspendLayout()
        PanelAI5.SuspendLayout()
        PanelAI4.SuspendLayout()
        PanelAI3.SuspendLayout()
        PanelAI2.SuspendLayout()
        PanelAI11.SuspendLayout()
        PanelAI12.SuspendLayout()
        PanelAI10.SuspendLayout()
        PanelAI1.SuspendLayout()
        SuspendLayout()
        '
        ' PanelAI9 / HCL_线程 / MCB_线程：推理线程数
        '
        PanelAI9.BackColor = Color.Transparent
        PanelAI9.BackColor1 = Color.Transparent
        PanelAI9.BorderSize = 0
        PanelAI9.Controls.Add(HCL_线程)
        PanelAI9.Dock = DockStyle.Top
        PanelAI9.Location = New Point(15, 602)
        PanelAI9.Name = "PanelAI9"
        PanelAI9.Padding = New Padding(0, 10, 0, 0)
        PanelAI9.Size = New Size(563, 42)
        PanelAI9.TabIndex = 21
        HCL_线程.AutoSize = True
        HCL_线程.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_线程.Dock = DockStyle.Fill
        HCL_线程.Location = New Point(200, 10)
        HCL_线程.Name = "HCL_线程"
        HCL_线程.Padding = New Padding(10, 0, 0, 0)
        HCL_线程.Size = New Size(363, 32)
        HCL_线程.TabIndex = 0
        HCL_线程.Text = "推理线程数（0 = 自动）"
        HCL_线程.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MCB_线程.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_线程.BorderRadius = 10
        MCB_线程.BorderSize = 0
        MCB_线程.Dock = DockStyle.Left
        MCB_线程.DropDownBackdropBlurPasses = 2
        MCB_线程.DropDownBackdropBlurRadius = 30
        MCB_线程.DropDownBackdropMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_线程.DropDownPadding = New Padding(10)
        MCB_线程.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_线程.DropDownSelectedForeColor = Color.White
        MCB_线程.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_线程.Location = New Point(0, 10)
        MCB_线程.Name = "MCB_线程"
        MCB_线程.Padding = New Padding(10, 0, 10, 0)
        MCB_线程.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_线程.Size = New Size(200, 32)
        MCB_线程.TabIndex = 1
        MCB_线程.ToolTipGap = -1
        MCB_线程.ToolTipMaxWidth = 350
        MCB_线程.ToolTipPadding = New Padding(15)
        MCB_线程.WaterText = "线程数"
        MCB_线程.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' PanelAI8 / HCL_偏移 / MTB_偏移：字幕驻留偏移
        '
        PanelAI8.BackColor = Color.Transparent
        PanelAI8.BackColor1 = Color.Transparent
        PanelAI8.BorderSize = 0
        PanelAI8.Dock = DockStyle.Top
        PanelAI8.Location = New Point(15, 560)
        PanelAI8.Name = "PanelAI8"
        PanelAI8.Padding = New Padding(0, 10, 0, 0)
        PanelAI8.Size = New Size(563, 42)
        PanelAI8.TabIndex = 20
        HCL_偏移.AutoSize = True
        HCL_偏移.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_偏移.Dock = DockStyle.Fill
        HCL_偏移.Location = New Point(90, 10)
        HCL_偏移.Name = "HCL_偏移"
        HCL_偏移.Padding = New Padding(10, 0, 0, 0)
        HCL_偏移.Size = New Size(473, 32)
        HCL_偏移.TabIndex = 0
        HCL_偏移.Text = "字幕驻留偏移（秒，±3）"
        HCL_偏移.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MTB_偏移.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_偏移.BorderColor = Color.Transparent
        MTB_偏移.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_偏移.BorderRadius = 10
        MTB_偏移.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_偏移.Dock = DockStyle.Left
        MTB_偏移.Location = New Point(0, 10)
        MTB_偏移.Name = "MTB_偏移"
        MTB_偏移.Padding = New Padding(10, 0, 10, 0)
        MTB_偏移.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_偏移.Size = New Size(90, 32)
        MTB_偏移.TabIndex = 1
        MTB_偏移.WaterText = "0"
        MTB_偏移.WaterTextForeColor = Color.DarkGray
        '
        ' PanelAI7 / HCL_模型档值 / MCB_模型档：模型档（单档=事实行，多档=下拉）
        '
        PanelAI7.BackColor = Color.Transparent
        PanelAI7.BackColor1 = Color.Transparent
        PanelAI7.BorderSize = 0
        PanelAI7.Dock = DockStyle.Top
        PanelAI7.Location = New Point(15, 518)
        PanelAI7.Name = "PanelAI7"
        PanelAI7.Padding = New Padding(0, 10, 0, 0)
        PanelAI7.Size = New Size(563, 42)
        PanelAI7.TabIndex = 19
        HCL_模型档值.AutoSize = True
        HCL_模型档值.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_模型档值.Dock = DockStyle.Fill
        HCL_模型档值.Location = New Point(200, 10)
        HCL_模型档值.Name = "HCL_模型档值"
        HCL_模型档值.Padding = New Padding(10, 0, 0, 0)
        HCL_模型档值.Size = New Size(363, 32)
        HCL_模型档值.TabIndex = 0
        HCL_模型档值.Text = "—"
        HCL_模型档值.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MCB_模型档.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_模型档.BorderRadius = 10
        MCB_模型档.BorderSize = 0
        MCB_模型档.Dock = DockStyle.Left
        MCB_模型档.DropDownBackdropBlurPasses = 2
        MCB_模型档.DropDownBackdropBlurRadius = 30
        MCB_模型档.DropDownBackdropMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_模型档.DropDownPadding = New Padding(10)
        MCB_模型档.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_模型档.DropDownSelectedForeColor = Color.White
        MCB_模型档.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_模型档.Location = New Point(0, 10)
        MCB_模型档.Name = "MCB_模型档"
        MCB_模型档.Padding = New Padding(10, 0, 10, 0)
        MCB_模型档.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_模型档.Size = New Size(200, 32)
        MCB_模型档.TabIndex = 1
        MCB_模型档.ToolTipGap = -1
        MCB_模型档.ToolTipMaxWidth = 350
        MCB_模型档.ToolTipPadding = New Padding(15)
        MCB_模型档.WaterText = "模型档位"
        MCB_模型档.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' PanelAI6 / HCL_预置 / MCB_预置 / MTB_步长秒 / MTB_窗口秒：窗口·步长·预置
        '
        PanelAI6.BackColor = Color.Transparent
        PanelAI6.BackColor1 = Color.Transparent
        PanelAI6.BorderSize = 0
        PanelAI6.Dock = DockStyle.Top
        PanelAI6.Location = New Point(15, 476)
        PanelAI6.Name = "PanelAI6"
        PanelAI6.Padding = New Padding(0, 10, 0, 0)
        PanelAI6.Size = New Size(563, 42)
        PanelAI6.TabIndex = 18
        HCL_预置.AutoSize = True
        HCL_预置.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_预置.Dock = DockStyle.Fill
        HCL_预置.Location = New Point(410, 10)
        HCL_预置.Name = "HCL_预置"
        HCL_预置.Padding = New Padding(10, 0, 0, 0)
        HCL_预置.Size = New Size(153, 32)
        HCL_预置.TabIndex = 0
        HCL_预置.Text = "窗口 / 步长（秒）；预置选中即填两格"
        HCL_预置.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MCB_预置.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_预置.BorderRadius = 10
        MCB_预置.BorderSize = 0
        MCB_预置.Dock = DockStyle.Left
        MCB_预置.DropDownBackdropBlurPasses = 2
        MCB_预置.DropDownBackdropBlurRadius = 30
        MCB_预置.DropDownBackdropMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_预置.DropDownPadding = New Padding(10)
        MCB_预置.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_预置.DropDownSelectedForeColor = Color.White
        MCB_预置.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_预置.Location = New Point(190, 10)
        MCB_预置.Name = "MCB_预置"
        MCB_预置.Padding = New Padding(10, 0, 10, 0)
        MCB_预置.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_预置.Size = New Size(220, 32)
        MCB_预置.TabIndex = 1
        MCB_预置.ToolTipGap = -1
        MCB_预置.ToolTipMaxWidth = 350
        MCB_预置.ToolTipPadding = New Padding(15)
        MCB_预置.WaterText = "预置"
        MCB_预置.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MTB_步长秒.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_步长秒.BorderColor = Color.Transparent
        MTB_步长秒.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_步长秒.BorderRadius = 10
        MTB_步长秒.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_步长秒.Dock = DockStyle.Left
        MTB_步长秒.Location = New Point(95, 10)
        MTB_步长秒.Name = "MTB_步长秒"
        MTB_步长秒.Padding = New Padding(10, 0, 10, 0)
        MTB_步长秒.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_步长秒.Size = New Size(95, 32)
        MTB_步长秒.TabIndex = 2
        MTB_步长秒.WaterText = "步长"
        MTB_步长秒.WaterTextForeColor = Color.DarkGray
        MTB_窗口秒.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_窗口秒.BorderColor = Color.Transparent
        MTB_窗口秒.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_窗口秒.BorderRadius = 10
        MTB_窗口秒.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_窗口秒.Dock = DockStyle.Left
        MTB_窗口秒.Location = New Point(0, 10)
        MTB_窗口秒.Name = "MTB_窗口秒"
        MTB_窗口秒.Padding = New Padding(10, 0, 10, 0)
        MTB_窗口秒.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_窗口秒.Size = New Size(95, 32)
        MTB_窗口秒.TabIndex = 3
        MTB_窗口秒.WaterText = "窗口"
        MTB_窗口秒.WaterTextForeColor = Color.DarkGray
        '
        ' PanelAI5 / HCL_显示高级 / MCK_显示高级：高级选项折叠开关
        '
        PanelAI5.BackColor = Color.Transparent
        PanelAI5.BackColor1 = Color.Transparent
        PanelAI5.BorderSize = 0
        PanelAI5.Dock = DockStyle.Top
        PanelAI5.Location = New Point(15, 434)
        PanelAI5.Name = "PanelAI5"
        PanelAI5.Padding = New Padding(0, 10, 0, 0)
        PanelAI5.Size = New Size(563, 42)
        PanelAI5.TabIndex = 17
        HCL_显示高级.AutoSize = True
        HCL_显示高级.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_显示高级.Dock = DockStyle.Fill
        HCL_显示高级.Location = New Point(150, 10)
        HCL_显示高级.Name = "HCL_显示高级"
        HCL_显示高级.Padding = New Padding(10, 0, 0, 0)
        HCL_显示高级.Size = New Size(413, 32)
        HCL_显示高级.TabIndex = 0
        HCL_显示高级.Text = "窗口 / 步长 / 模型档 / 偏移 / 线程"
        HCL_显示高级.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MCK_显示高级.BackColor = Color.Transparent
        MCK_显示高级.BoxBorderRadius = 5
        MCK_显示高级.BoxBorderSize = 0
        MCK_显示高级.BoxCheckedBackColor = Color.CornflowerBlue
        MCK_显示高级.BoxInnerPadding = 7
        MCK_显示高级.BoxSize = 24
        MCK_显示高级.BoxTextSpacing = 10
        MCK_显示高级.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_显示高级.ClickAnywhere = True
        MCK_显示高级.Dock = DockStyle.Left
        MCK_显示高级.Location = New Point(0, 10)
        MCK_显示高级.Name = "MCK_显示高级"
        MCK_显示高级.Size = New Size(150, 32)
        MCK_显示高级.TabIndex = 1
        MCK_显示高级.Text = "高级选项"
        '
        ' HCL_状态：多行富文本状态区（颜色由 刷新状态 用 <font> 内联）
        '
        HCL_状态.AutoSize = True
        HCL_状态.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_状态.BackColor = Color.Transparent
        HCL_状态.Dock = DockStyle.Top
        HCL_状态.Location = New Point(15, 356)
        HCL_状态.Name = "HCL_状态"
        HCL_状态.Padding = New Padding(0, 4, 0, 4)
        HCL_状态.Size = New Size(563, 78)
        HCL_状态.TabIndex = 16
        HCL_状态.Text = "未启用。选择一个扩展包目录（含 ai-subtitles.json）即可启用。"
        '
        ' PanelAI4 / HCL_语言 / MCK_仅所选语言 / MCB_语言：字幕语言
        '
        PanelAI4.BackColor = Color.Transparent
        PanelAI4.BackColor1 = Color.Transparent
        PanelAI4.BorderSize = 0
        PanelAI4.Dock = DockStyle.Top
        PanelAI4.Location = New Point(15, 314)
        PanelAI4.Name = "PanelAI4"
        PanelAI4.Padding = New Padding(0, 10, 0, 0)
        PanelAI4.Size = New Size(563, 42)
        PanelAI4.TabIndex = 15
        HCL_语言.AutoSize = True
        HCL_语言.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_语言.Dock = DockStyle.Fill
        HCL_语言.Location = New Point(430, 10)
        HCL_语言.Name = "HCL_语言"
        HCL_语言.Padding = New Padding(10, 0, 0, 0)
        HCL_语言.Size = New Size(133, 32)
        HCL_语言.TabIndex = 0
        HCL_语言.Text = "字幕语言（只是提示，不翻译）"
        HCL_语言.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MCK_仅所选语言.AutoSize = True
        MCK_仅所选语言.BackColor = Color.Transparent
        MCK_仅所选语言.BoxBorderRadius = 5
        MCK_仅所选语言.BoxBorderSize = 0
        MCK_仅所选语言.BoxCheckedBackColor = Color.CornflowerBlue
        MCK_仅所选语言.BoxInnerPadding = 7
        MCK_仅所选语言.BoxSize = 24
        MCK_仅所选语言.BoxTextSpacing = 10
        MCK_仅所选语言.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_仅所选语言.ClickAnywhere = True
        MCK_仅所选语言.Dock = DockStyle.Left
        MCK_仅所选语言.Location = New Point(200, 10)
        MCK_仅所选语言.Name = "MCK_仅所选语言"
        MCK_仅所选语言.Size = New Size(230, 32)
        MCK_仅所选语言.TabIndex = 1
        MCK_仅所选语言.Text = "只显示所选语言（不符的窗不上屏）"
        MCB_语言.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_语言.BorderRadius = 10
        MCB_语言.BorderSize = 0
        MCB_语言.Dock = DockStyle.Left
        MCB_语言.DropDownBackdropBlurPasses = 2
        MCB_语言.DropDownBackdropBlurRadius = 30
        MCB_语言.DropDownBackdropMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_语言.DropDownPadding = New Padding(10)
        MCB_语言.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_语言.DropDownSelectedForeColor = Color.White
        MCB_语言.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_语言.Location = New Point(0, 10)
        MCB_语言.Name = "MCB_语言"
        MCB_语言.Padding = New Padding(10, 0, 10, 0)
        MCB_语言.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_语言.Size = New Size(200, 32)
        MCB_语言.TabIndex = 2
        MCB_语言.ToolTipGap = -1
        MCB_语言.ToolTipMaxWidth = 350
        MCB_语言.ToolTipPadding = New Padding(15)
        MCB_语言.WaterText = "字幕语言"
        MCB_语言.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' PanelAI3 / HCL_后端 / MCB_后端：推理后端
        '
        PanelAI3.BackColor = Color.Transparent
        PanelAI3.BackColor1 = Color.Transparent
        PanelAI3.BorderSize = 0
        PanelAI3.Dock = DockStyle.Top
        PanelAI3.Location = New Point(15, 272)
        PanelAI3.Name = "PanelAI3"
        PanelAI3.Padding = New Padding(0, 10, 0, 0)
        PanelAI3.Size = New Size(563, 42)
        PanelAI3.TabIndex = 14
        HCL_后端.AutoSize = True
        HCL_后端.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_后端.Dock = DockStyle.Fill
        HCL_后端.Location = New Point(300, 10)
        HCL_后端.Name = "HCL_后端"
        HCL_后端.Padding = New Padding(10, 0, 0, 0)
        HCL_后端.Size = New Size(263, 32)
        HCL_后端.TabIndex = 0
        HCL_后端.Text = "后端——列出什么由设备决定"
        HCL_后端.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MCB_后端.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_后端.BorderRadius = 10
        MCB_后端.BorderSize = 0
        MCB_后端.Dock = DockStyle.Left
        MCB_后端.DropDownBackdropBlurPasses = 2
        MCB_后端.DropDownBackdropBlurRadius = 30
        MCB_后端.DropDownBackdropMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_后端.DropDownPadding = New Padding(10)
        MCB_后端.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_后端.DropDownSelectedForeColor = Color.White
        MCB_后端.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_后端.Location = New Point(0, 10)
        MCB_后端.Name = "MCB_后端"
        MCB_后端.Padding = New Padding(10, 0, 10, 0)
        MCB_后端.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_后端.Size = New Size(300, 32)
        MCB_后端.TabIndex = 1
        MCB_后端.ToolTipGap = -1
        MCB_后端.ToolTipMaxWidth = 350
        MCB_后端.ToolTipPadding = New Padding(15)
        MCB_后端.WaterText = "推理后端"
        MCB_后端.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' PanelAI2 / HCL_设备 / MCB_设备：推理设备
        '
        PanelAI2.BackColor = Color.Transparent
        PanelAI2.BackColor1 = Color.Transparent
        PanelAI2.BorderSize = 0
        PanelAI2.Dock = DockStyle.Top
        PanelAI2.Location = New Point(15, 230)
        PanelAI2.Name = "PanelAI2"
        PanelAI2.Padding = New Padding(0, 10, 0, 0)
        PanelAI2.Size = New Size(563, 42)
        PanelAI2.TabIndex = 13
        HCL_设备.AutoSize = True
        HCL_设备.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_设备.Dock = DockStyle.Fill
        HCL_设备.Location = New Point(200, 10)
        HCL_设备.Name = "HCL_设备"
        HCL_设备.Padding = New Padding(10, 0, 0, 0)
        HCL_设备.Size = New Size(363, 32)
        HCL_设备.TabIndex = 0
        HCL_设备.Text = "设备——哪块硬件来算"
        HCL_设备.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MCB_设备.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_设备.BorderRadius = 10
        MCB_设备.BorderSize = 0
        MCB_设备.Dock = DockStyle.Left
        MCB_设备.DropDownBackdropBlurPasses = 2
        MCB_设备.DropDownBackdropBlurRadius = 30
        MCB_设备.DropDownBackdropMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_设备.DropDownPadding = New Padding(10)
        MCB_设备.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_设备.DropDownSelectedForeColor = Color.White
        MCB_设备.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_设备.Location = New Point(0, 10)
        MCB_设备.Name = "MCB_设备"
        MCB_设备.Padding = New Padding(10, 0, 10, 0)
        MCB_设备.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_设备.Size = New Size(200, 32)
        MCB_设备.TabIndex = 1
        MCB_设备.ToolTipGap = -1
        MCB_设备.ToolTipMaxWidth = 350
        MCB_设备.ToolTipPadding = New Padding(15)
        MCB_设备.WaterText = "推理设备"
        MCB_设备.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' PanelAI1 / HCL_包目录 / MB_浏览 / MTB_包目录：扩展包目录
        '
        PanelAI1.BackColor = Color.Transparent
        PanelAI1.BackColor1 = Color.Transparent
        PanelAI1.BorderSize = 0
        PanelAI1.Dock = DockStyle.Top
        PanelAI1.Location = New Point(15, 188)
        PanelAI1.Name = "PanelAI1"
        PanelAI1.Padding = New Padding(0, 10, 0, 0)
        PanelAI1.Size = New Size(563, 42)
        PanelAI1.TabIndex = 12
        HCL_包目录.AutoSize = True
        HCL_包目录.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_包目录.Dock = DockStyle.Fill
        HCL_包目录.Location = New Point(400, 10)
        HCL_包目录.Name = "HCL_包目录"
        HCL_包目录.Padding = New Padding(10, 0, 0, 0)
        HCL_包目录.Size = New Size(163, 32)
        HCL_包目录.TabIndex = 0
        HCL_包目录.Text = "扩展包目录（含 ai-subtitles.json）"
        HCL_包目录.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MB_浏览.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_浏览.BorderRadius = 10
        MB_浏览.BorderSize = 0
        MB_浏览.Dock = DockStyle.Left
        MB_浏览.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_浏览.Location = New Point(300, 10)
        MB_浏览.Name = "MB_浏览"
        MB_浏览.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_浏览.Size = New Size(100, 32)
        MB_浏览.TabIndex = 1
        MB_浏览.Text = "浏览…"
        MTB_包目录.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_包目录.BorderColor = Color.Transparent
        MTB_包目录.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_包目录.BorderRadius = 10
        MTB_包目录.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_包目录.Dock = DockStyle.Left
        MTB_包目录.Location = New Point(0, 10)
        MTB_包目录.Name = "MTB_包目录"
        MTB_包目录.Padding = New Padding(10, 0, 10, 0)
        MTB_包目录.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_包目录.Size = New Size(300, 32)
        MTB_包目录.TabIndex = 2
        MTB_包目录.WaterText = "D:\...\3FP-AI-Subtitles"
        MTB_包目录.WaterTextForeColor = Color.DarkGray
        '
        ' PanelAI12 / MCB_包类型 / HCL_包类型：下载哪种包（GPU+CPU / NPU）。
        ' 两种包是不同引擎（GGUF/Vulkan vs ONNX/OpenVINO），下载前必须先选。
        '
        PanelAI12.BackColor = Color.Transparent
        PanelAI12.BackColor1 = Color.Transparent
        PanelAI12.BorderSize = 0
        PanelAI12.Dock = DockStyle.Top
        PanelAI12.Location = New Point(15, 188)
        PanelAI12.Name = "PanelAI12"
        PanelAI12.Padding = New Padding(0, 10, 0, 0)
        PanelAI12.Size = New Size(563, 42)
        PanelAI12.TabIndex = 24
        HCL_包类型.AutoSize = True
        HCL_包类型.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_包类型.Dock = DockStyle.Fill
        HCL_包类型.Location = New Point(250, 10)
        HCL_包类型.Name = "HCL_包类型"
        HCL_包类型.Padding = New Padding(10, 0, 0, 0)
        HCL_包类型.Size = New Size(313, 32)
        HCL_包类型.TabIndex = 0
        HCL_包类型.Text = "下载哪种包——GPU+CPU 通用，NPU 需 Core Ultra"
        HCL_包类型.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MCB_包类型.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_包类型.BorderRadius = 10
        MCB_包类型.BorderSize = 0
        MCB_包类型.Dock = DockStyle.Left
        MCB_包类型.DropDownBackdropBlurPasses = 2
        MCB_包类型.DropDownBackdropBlurRadius = 30
        MCB_包类型.DropDownBackdropMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_包类型.DropDownPadding = New Padding(10)
        MCB_包类型.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_包类型.DropDownSelectedForeColor = Color.White
        MCB_包类型.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_包类型.Location = New Point(0, 10)
        MCB_包类型.Name = "MCB_包类型"
        MCB_包类型.Padding = New Padding(10, 0, 10, 0)
        MCB_包类型.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_包类型.Size = New Size(250, 32)
        MCB_包类型.TabIndex = 1
        MCB_包类型.ToolTipGap = -1
        MCB_包类型.ToolTipMaxWidth = 350
        MCB_包类型.ToolTipPadding = New Padding(15)
        MCB_包类型.WaterText = "包类型"
        MCB_包类型.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' PanelAI11 / EPB_下载 / HCL_下载进度：整体下载进度（仅下载中显示）。
        ' 进度条占满整行宽，下面一行专用进度文本（百分比+总量+当前文件）——
        ' 不放按钮行右侧的窄标签里，那里放不下会被裁断。
        '
        PanelAI11.BackColor = Color.Transparent
        PanelAI11.BackColor1 = Color.Transparent
        PanelAI11.BorderSize = 0
        PanelAI11.Dock = DockStyle.Top
        PanelAI11.Location = New Point(15, 272)
        PanelAI11.Name = "PanelAI11"
        PanelAI11.Padding = New Padding(0, 4, 0, 2)
        PanelAI11.Size = New Size(563, 40)
        PanelAI11.TabIndex = 23
        PanelAI11.Visible = False
        EPB_下载.Dock = DockStyle.Top
        EPB_下载.FillColor = Color.CornflowerBlue
        EPB_下载.Location = New Point(0, 4)
        EPB_下载.Maximum = 100
        EPB_下载.Minimum = 0
        EPB_下载.Name = "EPB_下载"
        EPB_下载.Size = New Size(563, 12)
        EPB_下载.TabIndex = 0
        EPB_下载.TrackColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        EPB_下载.Value = 0
        HCL_下载进度.AutoSize = True
        HCL_下载进度.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_下载进度.Dock = DockStyle.Fill
        HCL_下载进度.Location = New Point(0, 16)
        HCL_下载进度.Name = "HCL_下载进度"
        HCL_下载进度.Padding = New Padding(0, 2, 0, 0)
        HCL_下载进度.Size = New Size(563, 22)
        HCL_下载进度.TabIndex = 1
        HCL_下载进度.Text = ""
        HCL_下载进度.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        '
        ' PanelAI10 / HCL_下载状态 / MB_下载包 / MB_取消下载 / MB_删除包：包的获取与移除
        '
        PanelAI10.BackColor = Color.Transparent
        PanelAI10.BackColor1 = Color.Transparent
        PanelAI10.BorderSize = 0
        PanelAI10.Dock = DockStyle.Top
        PanelAI10.Location = New Point(15, 230)
        PanelAI10.Name = "PanelAI10"
        PanelAI10.Padding = New Padding(0, 10, 0, 0)
        PanelAI10.Size = New Size(563, 42)
        PanelAI10.TabIndex = 22
        HCL_下载状态.AutoSize = True
        HCL_下载状态.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_下载状态.Dock = DockStyle.Fill
        HCL_下载状态.Location = New Point(360, 10)
        HCL_下载状态.Name = "HCL_下载状态"
        HCL_下载状态.Padding = New Padding(10, 0, 0, 0)
        HCL_下载状态.Size = New Size(203, 32)
        HCL_下载状态.TabIndex = 0
        HCL_下载状态.Text = "—"
        HCL_下载状态.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        MB_下载包.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_下载包.BorderRadius = 10
        MB_下载包.BorderSize = 0
        MB_下载包.Dock = DockStyle.Left
        MB_下载包.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_下载包.Location = New Point(0, 10)
        MB_下载包.Name = "MB_下载包"
        MB_下载包.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_下载包.Size = New Size(150, 32)
        MB_下载包.TabIndex = 1
        MB_下载包.Text = "下载官方包…"
        MB_下载包.ToolTipText = "从 Hugging Face 下载 Qwen3-ASR 官方模型与运行环境，自动生成本页要的扩展包"
        MB_下载包.ToolTipShowDelaySeconds = 1R
        MB_取消下载.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_取消下载.BorderRadius = 10
        MB_取消下载.BorderSize = 0
        MB_取消下载.Dock = DockStyle.Left
        MB_取消下载.Enabled = False
        MB_取消下载.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_取消下载.Location = New Point(150, 10)
        MB_取消下载.Name = "MB_取消下载"
        MB_取消下载.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_取消下载.Size = New Size(100, 32)
        MB_取消下载.TabIndex = 2
        MB_取消下载.Text = "取消"
        MB_删除包.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_删除包.BorderRadius = 10
        MB_删除包.BorderSize = 0
        MB_删除包.Dock = DockStyle.Left
        MB_删除包.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_删除包.Location = New Point(250, 10)
        MB_删除包.Name = "MB_删除包"
        MB_删除包.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_删除包.Size = New Size(110, 32)
        MB_删除包.TabIndex = 3
        MB_删除包.Text = "删除包"
        MB_删除包.ToolTipText = "删除本页当前选中的有效扩展包（会先停止 worker 并要求确认）"
        MB_删除包.ToolTipShowDelaySeconds = 1R
        '
        ' HCL_标题：页首标题 + 一句说明（与字幕/弹幕页同款）
        '
        HCL_标题.AutoSize = True
        HCL_标题.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_标题.BackColor = Color.Transparent
        HCL_标题.Dock = DockStyle.Top
        HCL_标题.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HCL_标题.Location = New Point(15, 20)
        HCL_标题.Name = "HCL_标题"
        HCL_标题.Padding = New Padding(0, 0, 0, 10)
        HCL_标题.Size = New Size(563, 168)
        HCL_标题.TabIndex = 0
        HCL_标题.Text = "<span style=""font-size:13; color:Silver"">AI 实时字幕</span><br>" &
                        "<span style=""color:Gray"">以扩展包形式提供：选一个含 ai-subtitles.json 的目录即可启用，" &
                        "包里自带 worker、模型与运行环境，本体不需要任何别的配置。<br>" &
                        "目录失效或删掉时功能自动停用，不影响播放。</span>"
        '
        ' ModernPanel1：绝对定位 + 固定设计高（AutoScroll 靠窗体滚动，见 对齐面板宽度）
        '
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(15, 20, 20, 20)
        ModernPanel1.Size = New Size(598, 644)
        ModernPanel1.TabIndex = 0
        '
        ' Form设置_AI字幕
        '
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        AutoScroll = True
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(598, 644)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_AI字幕"
        Text = "Form设置_AI字幕"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        PanelAI9.ResumeLayout(False)
        PanelAI8.ResumeLayout(False)
        PanelAI7.ResumeLayout(False)
        PanelAI6.ResumeLayout(False)
        PanelAI5.ResumeLayout(False)
        PanelAI4.ResumeLayout(False)
        PanelAI3.ResumeLayout(False)
        PanelAI2.ResumeLayout(False)
        PanelAI10.ResumeLayout(False)
        PanelAI12.ResumeLayout(False)
        PanelAI11.ResumeLayout(False)
        PanelAI1.ResumeLayout(False)
        PanelAI1.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents HCL_标题 As LakeUI.HtmlColorLabel
    Friend WithEvents PanelAI1 As LakeUI.ModernPanel
    Friend WithEvents HCL_包目录 As LakeUI.HtmlColorLabel
    Friend WithEvents MTB_包目录 As LakeUI.ModernTextBox
    Friend WithEvents MB_浏览 As LakeUI.ModernButton
    Friend WithEvents PanelAI11 As LakeUI.ModernPanel
    Friend WithEvents EPB_下载 As LakeUI.ExcellentProgressBar
    Friend WithEvents HCL_下载进度 As LakeUI.HtmlColorLabel
    Friend WithEvents PanelAI12 As LakeUI.ModernPanel
    Friend WithEvents MCB_包类型 As LakeUI.ModernComboBox
    Friend WithEvents HCL_包类型 As LakeUI.HtmlColorLabel
    Friend WithEvents PanelAI10 As LakeUI.ModernPanel
    Friend WithEvents HCL_下载状态 As LakeUI.HtmlColorLabel
    Friend WithEvents MB_下载包 As LakeUI.ModernButton
    Friend WithEvents MB_取消下载 As LakeUI.ModernButton
    Friend WithEvents MB_删除包 As LakeUI.ModernButton
    Friend WithEvents PanelAI2 As LakeUI.ModernPanel
    Friend WithEvents HCL_设备 As LakeUI.HtmlColorLabel
    Friend WithEvents MCB_设备 As LakeUI.ModernComboBox
    Friend WithEvents PanelAI3 As LakeUI.ModernPanel
    Friend WithEvents HCL_后端 As LakeUI.HtmlColorLabel
    Friend WithEvents MCB_后端 As LakeUI.ModernComboBox
    Friend WithEvents PanelAI4 As LakeUI.ModernPanel
    Friend WithEvents HCL_语言 As LakeUI.HtmlColorLabel
    Friend WithEvents MCK_仅所选语言 As LakeUI.ModernCheckBox
    Friend WithEvents MCB_语言 As LakeUI.ModernComboBox
    Friend WithEvents HCL_状态 As LakeUI.HtmlColorLabel
    Friend WithEvents PanelAI5 As LakeUI.ModernPanel
    Friend WithEvents HCL_显示高级 As LakeUI.HtmlColorLabel
    Friend WithEvents MCK_显示高级 As LakeUI.ModernCheckBox
    Friend WithEvents PanelAI6 As LakeUI.ModernPanel
    Friend WithEvents HCL_预置 As LakeUI.HtmlColorLabel
    Friend WithEvents MCB_预置 As LakeUI.ModernComboBox
    Friend WithEvents MTB_步长秒 As LakeUI.ModernTextBox
    Friend WithEvents MTB_窗口秒 As LakeUI.ModernTextBox
    Friend WithEvents PanelAI7 As LakeUI.ModernPanel
    Friend WithEvents HCL_模型档值 As LakeUI.HtmlColorLabel
    Friend WithEvents MCB_模型档 As LakeUI.ModernComboBox
    Friend WithEvents PanelAI8 As LakeUI.ModernPanel
    Friend WithEvents HCL_偏移 As LakeUI.HtmlColorLabel
    Friend WithEvents MTB_偏移 As LakeUI.ModernTextBox
    Friend WithEvents PanelAI9 As LakeUI.ModernPanel
    Friend WithEvents HCL_线程 As LakeUI.HtmlColorLabel
    Friend WithEvents MCB_线程 As LakeUI.ModernComboBox
End Class
