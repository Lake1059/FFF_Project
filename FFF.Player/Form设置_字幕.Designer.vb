<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_字幕
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
        Panel7 = New LakeUI.ModernPanel()
        HtmlColorLabel8 = New LakeUI.HtmlColorLabel()
        MCB_尺寸缩放方式 = New LakeUI.ModernComboBox()
        Panel6 = New LakeUI.ModernPanel()
        MB_重置其他位置样式 = New LakeUI.ModernButton()
        JustEmptyControl3 = New LakeUI.JustEmptyControl()
        MB_设置其他位置字体样式 = New LakeUI.ModernButton()
        Panel5 = New LakeUI.ModernPanel()
        HtmlColorLabel6 = New LakeUI.HtmlColorLabel()
        MCB_底部对齐方式 = New LakeUI.ModernComboBox()
        Panel4 = New LakeUI.ModernPanel()
        HtmlColorLabel5 = New LakeUI.HtmlColorLabel()
        ETB_字幕底部间距 = New LakeUI.ExcellentTrackBar()
        Panel3 = New LakeUI.ModernPanel()
        HtmlColorLabel4 = New LakeUI.HtmlColorLabel()
        ETB_字幕行间距 = New LakeUI.ExcellentTrackBar()
        Panel2 = New LakeUI.ModernPanel()
        MB_重置第二行样式和颜色 = New LakeUI.ModernButton()
        JustEmptyControl5 = New LakeUI.JustEmptyControl()
        MB_设置第二行文字颜色 = New LakeUI.ModernButton()
        JustEmptyControl1 = New LakeUI.JustEmptyControl()
        MB_设置第二行字体样式 = New LakeUI.ModernButton()
        Panel1 = New LakeUI.ModernPanel()
        MB_重置第一行样式和颜色 = New LakeUI.ModernButton()
        JustEmptyControl4 = New LakeUI.JustEmptyControl()
        MB_设置第一行文字颜色 = New LakeUI.ModernButton()
        JustEmptyControl2 = New LakeUI.JustEmptyControl()
        MB_设置第一行字体样式 = New LakeUI.ModernButton()
        HtmlColorLabel2 = New LakeUI.HtmlColorLabel()
        Panel8 = New LakeUI.ModernPanel()
        MCB_字幕阴影样式 = New LakeUI.ModernComboBox()
        JustEmptyControl6 = New LakeUI.JustEmptyControl()
        MCB_字幕描边样式 = New LakeUI.ModernComboBox()
        Panel9 = New LakeUI.ModernPanel()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        ETB_字幕不透明度 = New LakeUI.ExcellentTrackBar()
        ModernPanel1.SuspendLayout()
        Panel7.SuspendLayout()
        Panel6.SuspendLayout()
        Panel5.SuspendLayout()
        Panel4.SuspendLayout()
        Panel3.SuspendLayout()
        Panel2.SuspendLayout()
        Panel1.SuspendLayout()
        Panel8.SuspendLayout()
        Panel9.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(Panel7)
        ModernPanel1.Controls.Add(Panel6)
        ModernPanel1.Controls.Add(Panel5)
        ModernPanel1.Controls.Add(Panel9)
        ModernPanel1.Controls.Add(Panel4)
        ModernPanel1.Controls.Add(Panel3)
        ModernPanel1.Controls.Add(Panel8)
        ModernPanel1.Controls.Add(Panel2)
        ModernPanel1.Controls.Add(Panel1)
        ModernPanel1.Controls.Add(HtmlColorLabel2)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(20)
        ModernPanel1.Size = New Size(651, 486)
        ModernPanel1.TabIndex = 0
        ' 
        ' Panel7
        ' 
        Panel7.BackColor = Color.Transparent
        Panel7.BackColor1 = Color.Transparent
        Panel7.BorderSize = 0
        Panel7.Controls.Add(HtmlColorLabel8)
        Panel7.Controls.Add(MCB_尺寸缩放方式)
        Panel7.Dock = DockStyle.Top
        Panel7.Location = New Point(20, 379)
        Panel7.Name = "Panel7"
        Panel7.Padding = New Padding(0, 10, 0, 0)
        Panel7.Size = New Size(611, 42)
        Panel7.TabIndex = 25
        ' 
        ' HtmlColorLabel8
        ' 
        HtmlColorLabel8.AutoSize = True
        HtmlColorLabel8.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel8.Dock = DockStyle.Fill
        HtmlColorLabel8.Location = New Point(200, 10)
        HtmlColorLabel8.Margin = New Padding(2)
        HtmlColorLabel8.Name = "HtmlColorLabel8"
        HtmlColorLabel8.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel8.Size = New Size(411, 32)
        HtmlColorLabel8.TabIndex = 15
        HtmlColorLabel8.Text = "尺寸缩放方式"
        HtmlColorLabel8.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' MCB_尺寸缩放方式
        ' 
        MCB_尺寸缩放方式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_尺寸缩放方式.BorderRadius = 10
        MCB_尺寸缩放方式.BorderSize = 0
        MCB_尺寸缩放方式.Dock = DockStyle.Left
        MCB_尺寸缩放方式.DropDownBackdropBlurPasses = 2
        MCB_尺寸缩放方式.DropDownBackdropBlurRadius = 30
        MCB_尺寸缩放方式.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_尺寸缩放方式.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_尺寸缩放方式.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_尺寸缩放方式.DropDownPadding = New Padding(10)
        MCB_尺寸缩放方式.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_尺寸缩放方式.DropDownSelectedForeColor = Color.White
        MCB_尺寸缩放方式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_尺寸缩放方式.Items.Add("视频呈现缩放（推荐）")
        MCB_尺寸缩放方式.Items.Add("渲染区域宽度")
        MCB_尺寸缩放方式.Items.Add("渲染区域高度")
        MCB_尺寸缩放方式.Items.Add("不缩放（不推荐）")
        MCB_尺寸缩放方式.Location = New Point(0, 10)
        MCB_尺寸缩放方式.Margin = New Padding(2, 2, 2, 2)
        MCB_尺寸缩放方式.Name = "MCB_尺寸缩放方式"
        MCB_尺寸缩放方式.Padding = New Padding(10, 0, 10, 0)
        MCB_尺寸缩放方式.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_尺寸缩放方式.Size = New Size(200, 32)
        MCB_尺寸缩放方式.TabIndex = 16
        MCB_尺寸缩放方式.ToolTipGap = -1
        MCB_尺寸缩放方式.ToolTipMaxWidth = 350
        MCB_尺寸缩放方式.ToolTipPadding = New Padding(15)
        MCB_尺寸缩放方式.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' Panel6
        ' 
        Panel6.BackColor = Color.Transparent
        Panel6.BackColor1 = Color.Transparent
        Panel6.BorderSize = 0
        Panel6.Controls.Add(MB_重置其他位置样式)
        Panel6.Controls.Add(JustEmptyControl3)
        Panel6.Controls.Add(MB_设置其他位置字体样式)
        Panel6.Dock = DockStyle.Top
        Panel6.Location = New Point(20, 337)
        Panel6.Name = "Panel6"
        Panel6.Padding = New Padding(0, 10, 0, 0)
        Panel6.Size = New Size(611, 42)
        Panel6.TabIndex = 24
        ' 
        ' MB_重置其他位置样式
        ' 
        MB_重置其他位置样式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_重置其他位置样式.BorderRadius = 10
        MB_重置其他位置样式.BorderSize = 0
        MB_重置其他位置样式.Dock = DockStyle.Left
        MB_重置其他位置样式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_重置其他位置样式.Location = New Point(210, 10)
        MB_重置其他位置样式.Margin = New Padding(2)
        MB_重置其他位置样式.Name = "MB_重置其他位置样式"
        MB_重置其他位置样式.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_重置其他位置样式.Size = New Size(80, 32)
        MB_重置其他位置样式.TabIndex = 18
        MB_重置其他位置样式.Text = "重置"
        ' 
        ' JustEmptyControl3
        ' 
        JustEmptyControl3.Dock = DockStyle.Left
        JustEmptyControl3.Location = New Point(200, 10)
        JustEmptyControl3.Name = "JustEmptyControl3"
        JustEmptyControl3.Size = New Size(10, 32)
        JustEmptyControl3.TabIndex = 19
        ' 
        ' MB_设置其他位置字体样式
        ' 
        MB_设置其他位置字体样式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设置其他位置字体样式.BorderRadius = 10
        MB_设置其他位置字体样式.BorderSize = 0
        MB_设置其他位置字体样式.Dock = DockStyle.Left
        MB_设置其他位置字体样式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设置其他位置字体样式.Location = New Point(0, 10)
        MB_设置其他位置字体样式.Margin = New Padding(2)
        MB_设置其他位置字体样式.Name = "MB_设置其他位置字体样式"
        MB_设置其他位置字体样式.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设置其他位置字体样式.Size = New Size(200, 32)
        MB_设置其他位置字体样式.TabIndex = 17
        MB_设置其他位置字体样式.Text = "设置其他位置字体样式"
        ' 
        ' Panel5
        ' 
        Panel5.BackColor = Color.Transparent
        Panel5.BackColor1 = Color.Transparent
        Panel5.BorderSize = 0
        Panel5.Controls.Add(HtmlColorLabel6)
        Panel5.Controls.Add(MCB_底部对齐方式)
        Panel5.Dock = DockStyle.Top
        Panel5.Location = New Point(20, 295)
        Panel5.Name = "Panel5"
        Panel5.Padding = New Padding(0, 10, 0, 0)
        Panel5.Size = New Size(611, 42)
        Panel5.TabIndex = 23
        ' 
        ' HtmlColorLabel6
        ' 
        HtmlColorLabel6.AutoSize = True
        HtmlColorLabel6.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel6.Dock = DockStyle.Fill
        HtmlColorLabel6.Location = New Point(200, 10)
        HtmlColorLabel6.Margin = New Padding(2)
        HtmlColorLabel6.Name = "HtmlColorLabel6"
        HtmlColorLabel6.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel6.Size = New Size(411, 32)
        HtmlColorLabel6.TabIndex = 15
        HtmlColorLabel6.Text = "底部对齐方式"
        HtmlColorLabel6.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' MCB_底部对齐方式
        ' 
        MCB_底部对齐方式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_底部对齐方式.BorderRadius = 10
        MCB_底部对齐方式.BorderSize = 0
        MCB_底部对齐方式.Dock = DockStyle.Left
        MCB_底部对齐方式.DropDownBackdropBlurPasses = 2
        MCB_底部对齐方式.DropDownBackdropBlurRadius = 30
        MCB_底部对齐方式.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_底部对齐方式.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_底部对齐方式.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_底部对齐方式.DropDownPadding = New Padding(10)
        MCB_底部对齐方式.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_底部对齐方式.DropDownSelectedForeColor = Color.White
        MCB_底部对齐方式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_底部对齐方式.Items.Add("对齐到视频缩放底部")
        MCB_底部对齐方式.Items.Add("对齐到渲染区域底部")
        MCB_底部对齐方式.Location = New Point(0, 10)
        MCB_底部对齐方式.Margin = New Padding(2, 2, 2, 2)
        MCB_底部对齐方式.Name = "MCB_底部对齐方式"
        MCB_底部对齐方式.Padding = New Padding(10, 0, 10, 0)
        MCB_底部对齐方式.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_底部对齐方式.Size = New Size(200, 32)
        MCB_底部对齐方式.TabIndex = 16
        MCB_底部对齐方式.ToolTipGap = -1
        MCB_底部对齐方式.ToolTipMaxWidth = 350
        MCB_底部对齐方式.ToolTipPadding = New Padding(15)
        MCB_底部对齐方式.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' Panel4
        ' 
        Panel4.BackColor = Color.Transparent
        Panel4.BackColor1 = Color.Transparent
        Panel4.BorderSize = 0
        Panel4.Controls.Add(HtmlColorLabel5)
        Panel4.Controls.Add(ETB_字幕底部间距)
        Panel4.Dock = DockStyle.Top
        Panel4.Location = New Point(20, 211)
        Panel4.Name = "Panel4"
        Panel4.Padding = New Padding(0, 10, 0, 0)
        Panel4.Size = New Size(611, 42)
        Panel4.TabIndex = 22
        ' 
        ' HtmlColorLabel5
        ' 
        HtmlColorLabel5.AutoSize = True
        HtmlColorLabel5.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel5.Dock = DockStyle.Fill
        HtmlColorLabel5.Location = New Point(410, 10)
        HtmlColorLabel5.Margin = New Padding(2)
        HtmlColorLabel5.Name = "HtmlColorLabel5"
        HtmlColorLabel5.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel5.Size = New Size(201, 32)
        HtmlColorLabel5.TabIndex = 15
        HtmlColorLabel5.Text = "底部间距"
        HtmlColorLabel5.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' ETB_字幕底部间距
        ' 
        ETB_字幕底部间距.BackColor = Color.Transparent
        ETB_字幕底部间距.Dock = DockStyle.Left
        ETB_字幕底部间距.LabelColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕底部间距.LabelLineColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕底部间距.LabelLineLength = 16
        ETB_字幕底部间距.Location = New Point(0, 10)
        ETB_字幕底部间距.Margin = New Padding(2, 2, 2, 2)
        ETB_字幕底部间距.Name = "ETB_字幕底部间距"
        ETB_字幕底部间距.Size = New Size(410, 32)
        ETB_字幕底部间距.TabIndex = 31
        ETB_字幕底部间距.ThumbBorderWidth = 0
        ETB_字幕底部间距.ThumbColor = Color.OliveDrab
        ETB_字幕底部间距.ThumbHeight = 28
        ETB_字幕底部间距.ThumbTextDecimalPlaces = 0
        ETB_字幕底部间距.ThumbTextMode = LakeUI.ExcellentTrackBar.ThumbTextModeEnum.Value
        ETB_字幕底部间距.ThumbWidth = 38
        ETB_字幕底部间距.TrackColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕底部间距.TrackFillColor = Color.Transparent
        ETB_字幕底部间距.Value = 10R
        ' 
        ' Panel3
        ' 
        Panel3.BackColor = Color.Transparent
        Panel3.BackColor1 = Color.Transparent
        Panel3.BorderSize = 0
        Panel3.Controls.Add(HtmlColorLabel4)
        Panel3.Controls.Add(ETB_字幕行间距)
        Panel3.Dock = DockStyle.Top
        Panel3.Location = New Point(20, 169)
        Panel3.Name = "Panel3"
        Panel3.Padding = New Padding(0, 10, 0, 0)
        Panel3.Size = New Size(611, 42)
        Panel3.TabIndex = 21
        ' 
        ' HtmlColorLabel4
        ' 
        HtmlColorLabel4.AutoSize = True
        HtmlColorLabel4.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel4.Dock = DockStyle.Fill
        HtmlColorLabel4.Location = New Point(410, 10)
        HtmlColorLabel4.Margin = New Padding(2)
        HtmlColorLabel4.Name = "HtmlColorLabel4"
        HtmlColorLabel4.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel4.Size = New Size(201, 32)
        HtmlColorLabel4.TabIndex = 15
        HtmlColorLabel4.Text = "行间距"
        HtmlColorLabel4.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' ETB_字幕行间距
        ' 
        ETB_字幕行间距.BackColor = Color.Transparent
        ETB_字幕行间距.Dock = DockStyle.Left
        ETB_字幕行间距.LabelColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕行间距.LabelLineColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕行间距.LabelLineLength = 16
        ETB_字幕行间距.Location = New Point(0, 10)
        ETB_字幕行间距.Margin = New Padding(2, 2, 2, 2)
        ETB_字幕行间距.Maximum = 50R
        ETB_字幕行间距.Name = "ETB_字幕行间距"
        ETB_字幕行间距.Size = New Size(410, 32)
        ETB_字幕行间距.TabIndex = 30
        ETB_字幕行间距.ThumbBorderWidth = 0
        ETB_字幕行间距.ThumbColor = Color.OliveDrab
        ETB_字幕行间距.ThumbHeight = 28
        ETB_字幕行间距.ThumbTextDecimalPlaces = 0
        ETB_字幕行间距.ThumbTextMode = LakeUI.ExcellentTrackBar.ThumbTextModeEnum.Value
        ETB_字幕行间距.ThumbWidth = 38
        ETB_字幕行间距.TrackColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕行间距.TrackFillColor = Color.Transparent
        ETB_字幕行间距.Value = 10R
        ' 
        ' Panel2
        ' 
        Panel2.BackColor = Color.Transparent
        Panel2.BackColor1 = Color.Transparent
        Panel2.BorderSize = 0
        Panel2.Controls.Add(MB_重置第二行样式和颜色)
        Panel2.Controls.Add(JustEmptyControl5)
        Panel2.Controls.Add(MB_设置第二行文字颜色)
        Panel2.Controls.Add(JustEmptyControl1)
        Panel2.Controls.Add(MB_设置第二行字体样式)
        Panel2.Dock = DockStyle.Top
        Panel2.Location = New Point(20, 85)
        Panel2.Name = "Panel2"
        Panel2.Padding = New Padding(0, 10, 0, 0)
        Panel2.Size = New Size(611, 42)
        Panel2.TabIndex = 20
        ' 
        ' MB_重置第二行样式和颜色
        ' 
        MB_重置第二行样式和颜色.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_重置第二行样式和颜色.BorderRadius = 10
        MB_重置第二行样式和颜色.BorderSize = 0
        MB_重置第二行样式和颜色.Dock = DockStyle.Left
        MB_重置第二行样式和颜色.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_重置第二行样式和颜色.Location = New Point(420, 10)
        MB_重置第二行样式和颜色.Margin = New Padding(2)
        MB_重置第二行样式和颜色.Name = "MB_重置第二行样式和颜色"
        MB_重置第二行样式和颜色.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_重置第二行样式和颜色.Size = New Size(80, 32)
        MB_重置第二行样式和颜色.TabIndex = 18
        MB_重置第二行样式和颜色.Text = "重置"
        ' 
        ' JustEmptyControl5
        ' 
        JustEmptyControl5.Dock = DockStyle.Left
        JustEmptyControl5.Location = New Point(410, 10)
        JustEmptyControl5.Name = "JustEmptyControl5"
        JustEmptyControl5.Size = New Size(10, 32)
        JustEmptyControl5.TabIndex = 21
        ' 
        ' MB_设置第二行文字颜色
        ' 
        MB_设置第二行文字颜色.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设置第二行文字颜色.BorderRadius = 10
        MB_设置第二行文字颜色.BorderSize = 0
        MB_设置第二行文字颜色.Dock = DockStyle.Left
        MB_设置第二行文字颜色.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设置第二行文字颜色.Location = New Point(210, 10)
        MB_设置第二行文字颜色.Margin = New Padding(2)
        MB_设置第二行文字颜色.Name = "MB_设置第二行文字颜色"
        MB_设置第二行文字颜色.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设置第二行文字颜色.Size = New Size(200, 32)
        MB_设置第二行文字颜色.TabIndex = 20
        MB_设置第二行文字颜色.Text = "设置第二行文字颜色"
        ' 
        ' JustEmptyControl1
        ' 
        JustEmptyControl1.Dock = DockStyle.Left
        JustEmptyControl1.Location = New Point(200, 10)
        JustEmptyControl1.Name = "JustEmptyControl1"
        JustEmptyControl1.Size = New Size(10, 32)
        JustEmptyControl1.TabIndex = 19
        ' 
        ' MB_设置第二行字体样式
        ' 
        MB_设置第二行字体样式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设置第二行字体样式.BorderRadius = 10
        MB_设置第二行字体样式.BorderSize = 0
        MB_设置第二行字体样式.Dock = DockStyle.Left
        MB_设置第二行字体样式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设置第二行字体样式.Location = New Point(0, 10)
        MB_设置第二行字体样式.Margin = New Padding(2)
        MB_设置第二行字体样式.Name = "MB_设置第二行字体样式"
        MB_设置第二行字体样式.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设置第二行字体样式.Size = New Size(200, 32)
        MB_设置第二行字体样式.TabIndex = 17
        MB_设置第二行字体样式.Text = "设置第二行字体样式"
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.BackColor1 = Color.Transparent
        Panel1.BorderSize = 0
        Panel1.Controls.Add(MB_重置第一行样式和颜色)
        Panel1.Controls.Add(JustEmptyControl4)
        Panel1.Controls.Add(MB_设置第一行文字颜色)
        Panel1.Controls.Add(JustEmptyControl2)
        Panel1.Controls.Add(MB_设置第一行字体样式)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(20, 43)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 10, 0, 0)
        Panel1.Size = New Size(611, 42)
        Panel1.TabIndex = 19
        ' 
        ' MB_重置第一行样式和颜色
        ' 
        MB_重置第一行样式和颜色.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_重置第一行样式和颜色.BorderRadius = 10
        MB_重置第一行样式和颜色.BorderSize = 0
        MB_重置第一行样式和颜色.Dock = DockStyle.Left
        MB_重置第一行样式和颜色.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_重置第一行样式和颜色.Location = New Point(420, 10)
        MB_重置第一行样式和颜色.Margin = New Padding(2)
        MB_重置第一行样式和颜色.Name = "MB_重置第一行样式和颜色"
        MB_重置第一行样式和颜色.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_重置第一行样式和颜色.Size = New Size(80, 32)
        MB_重置第一行样式和颜色.TabIndex = 18
        MB_重置第一行样式和颜色.Text = "重置"
        ' 
        ' JustEmptyControl4
        ' 
        JustEmptyControl4.Dock = DockStyle.Left
        JustEmptyControl4.Location = New Point(410, 10)
        JustEmptyControl4.Name = "JustEmptyControl4"
        JustEmptyControl4.Size = New Size(10, 32)
        JustEmptyControl4.TabIndex = 21
        ' 
        ' MB_设置第一行文字颜色
        ' 
        MB_设置第一行文字颜色.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设置第一行文字颜色.BorderRadius = 10
        MB_设置第一行文字颜色.BorderSize = 0
        MB_设置第一行文字颜色.Dock = DockStyle.Left
        MB_设置第一行文字颜色.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设置第一行文字颜色.Location = New Point(210, 10)
        MB_设置第一行文字颜色.Margin = New Padding(2)
        MB_设置第一行文字颜色.Name = "MB_设置第一行文字颜色"
        MB_设置第一行文字颜色.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设置第一行文字颜色.Size = New Size(200, 32)
        MB_设置第一行文字颜色.TabIndex = 20
        MB_设置第一行文字颜色.Text = "设置第一行文字颜色"
        ' 
        ' JustEmptyControl2
        ' 
        JustEmptyControl2.Dock = DockStyle.Left
        JustEmptyControl2.Location = New Point(200, 10)
        JustEmptyControl2.Name = "JustEmptyControl2"
        JustEmptyControl2.Size = New Size(10, 32)
        JustEmptyControl2.TabIndex = 19
        ' 
        ' MB_设置第一行字体样式
        ' 
        MB_设置第一行字体样式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设置第一行字体样式.BorderRadius = 10
        MB_设置第一行字体样式.BorderSize = 0
        MB_设置第一行字体样式.Dock = DockStyle.Left
        MB_设置第一行字体样式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设置第一行字体样式.Location = New Point(0, 10)
        MB_设置第一行字体样式.Margin = New Padding(2)
        MB_设置第一行字体样式.Name = "MB_设置第一行字体样式"
        MB_设置第一行字体样式.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设置第一行字体样式.Size = New Size(200, 32)
        MB_设置第一行字体样式.TabIndex = 17
        MB_设置第一行字体样式.Text = "设置第一行字体样式"
        ' 
        ' HtmlColorLabel2
        ' 
        HtmlColorLabel2.AutoSize = True
        HtmlColorLabel2.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel2.Dock = DockStyle.Top
        HtmlColorLabel2.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel2.Location = New Point(20, 20)
        HtmlColorLabel2.Margin = New Padding(2)
        HtmlColorLabel2.Name = "HtmlColorLabel2"
        HtmlColorLabel2.Size = New Size(611, 23)
        HtmlColorLabel2.TabIndex = 18
        HtmlColorLabel2.Text = "<span style=""font-size:13; color:Silver"">SRT 字幕设置</span>   特效和位图字幕不支持调整"
        ' 
        ' Panel8
        ' 
        Panel8.BackColor = Color.Transparent
        Panel8.BackColor1 = Color.Transparent
        Panel8.BorderSize = 0
        Panel8.Controls.Add(MCB_字幕阴影样式)
        Panel8.Controls.Add(JustEmptyControl6)
        Panel8.Controls.Add(MCB_字幕描边样式)
        Panel8.Dock = DockStyle.Top
        Panel8.Location = New Point(20, 127)
        Panel8.Name = "Panel8"
        Panel8.Padding = New Padding(0, 10, 0, 0)
        Panel8.Size = New Size(611, 42)
        Panel8.TabIndex = 26
        ' 
        ' MCB_字幕阴影样式
        ' 
        MCB_字幕阴影样式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_字幕阴影样式.BorderRadius = 10
        MCB_字幕阴影样式.BorderSize = 0
        MCB_字幕阴影样式.Dock = DockStyle.Left
        MCB_字幕阴影样式.DropDownBackdropBlurPasses = 2
        MCB_字幕阴影样式.DropDownBackdropBlurRadius = 30
        MCB_字幕阴影样式.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_字幕阴影样式.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_字幕阴影样式.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_字幕阴影样式.DropDownPadding = New Padding(10)
        MCB_字幕阴影样式.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_字幕阴影样式.DropDownSelectedForeColor = Color.White
        MCB_字幕阴影样式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_字幕阴影样式.Items.Add("不使用阴影")
        MCB_字幕阴影样式.Items.Add("使用基础阴影")
        MCB_字幕阴影样式.Items.Add("使用更深阴影")
        MCB_字幕阴影样式.Location = New Point(210, 10)
        MCB_字幕阴影样式.Margin = New Padding(2, 2, 2, 2)
        MCB_字幕阴影样式.Name = "MCB_字幕阴影样式"
        MCB_字幕阴影样式.Padding = New Padding(10, 0, 10, 0)
        MCB_字幕阴影样式.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_字幕阴影样式.Size = New Size(200, 32)
        MCB_字幕阴影样式.TabIndex = 21
        MCB_字幕阴影样式.ToolTipGap = -1
        MCB_字幕阴影样式.ToolTipMaxWidth = 350
        MCB_字幕阴影样式.ToolTipPadding = New Padding(15)
        MCB_字幕阴影样式.WaterText = "阴影样式"
        MCB_字幕阴影样式.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl6
        ' 
        JustEmptyControl6.Dock = DockStyle.Left
        JustEmptyControl6.Location = New Point(200, 10)
        JustEmptyControl6.Name = "JustEmptyControl6"
        JustEmptyControl6.Size = New Size(10, 32)
        JustEmptyControl6.TabIndex = 20
        ' 
        ' MCB_字幕描边样式
        ' 
        MCB_字幕描边样式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_字幕描边样式.BorderRadius = 10
        MCB_字幕描边样式.BorderSize = 0
        MCB_字幕描边样式.Dock = DockStyle.Left
        MCB_字幕描边样式.DropDownBackdropBlurPasses = 2
        MCB_字幕描边样式.DropDownBackdropBlurRadius = 30
        MCB_字幕描边样式.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_字幕描边样式.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_字幕描边样式.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_字幕描边样式.DropDownPadding = New Padding(10)
        MCB_字幕描边样式.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_字幕描边样式.DropDownSelectedForeColor = Color.White
        MCB_字幕描边样式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_字幕描边样式.Items.Add("不使用描边")
        MCB_字幕描边样式.Items.Add("使用基础描边")
        MCB_字幕描边样式.Items.Add("使用更深描边")
        MCB_字幕描边样式.Location = New Point(0, 10)
        MCB_字幕描边样式.Margin = New Padding(2, 2, 2, 2)
        MCB_字幕描边样式.Name = "MCB_字幕描边样式"
        MCB_字幕描边样式.Padding = New Padding(10, 0, 10, 0)
        MCB_字幕描边样式.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_字幕描边样式.Size = New Size(200, 32)
        MCB_字幕描边样式.TabIndex = 17
        MCB_字幕描边样式.ToolTipGap = -1
        MCB_字幕描边样式.ToolTipMaxWidth = 350
        MCB_字幕描边样式.ToolTipPadding = New Padding(15)
        MCB_字幕描边样式.WaterText = "描边样式"
        MCB_字幕描边样式.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' Panel9
        ' 
        Panel9.BackColor = Color.Transparent
        Panel9.BackColor1 = Color.Transparent
        Panel9.BorderSize = 0
        Panel9.Controls.Add(HtmlColorLabel1)
        Panel9.Controls.Add(ETB_字幕不透明度)
        Panel9.Dock = DockStyle.Top
        Panel9.Location = New Point(20, 253)
        Panel9.Name = "Panel9"
        Panel9.Padding = New Padding(0, 10, 0, 0)
        Panel9.Size = New Size(611, 42)
        Panel9.TabIndex = 27
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSize = True
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.Dock = DockStyle.Fill
        HtmlColorLabel1.Location = New Point(410, 10)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel1.Size = New Size(201, 32)
        HtmlColorLabel1.TabIndex = 15
        HtmlColorLabel1.Text = "不透明度"
        HtmlColorLabel1.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' ETB_字幕不透明度
        ' 
        ETB_字幕不透明度.BackColor = Color.Transparent
        ETB_字幕不透明度.Dock = DockStyle.Left
        ETB_字幕不透明度.LabelColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕不透明度.LabelLineColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕不透明度.LabelLineLength = 16
        ETB_字幕不透明度.Location = New Point(0, 10)
        ETB_字幕不透明度.Margin = New Padding(2, 2, 2, 2)
        ETB_字幕不透明度.Maximum = 255R
        ETB_字幕不透明度.Name = "ETB_字幕不透明度"
        ETB_字幕不透明度.Size = New Size(410, 32)
        ETB_字幕不透明度.TabIndex = 31
        ETB_字幕不透明度.ThumbBorderWidth = 0
        ETB_字幕不透明度.ThumbColor = Color.OliveDrab
        ETB_字幕不透明度.ThumbHeight = 28
        ETB_字幕不透明度.ThumbTextDecimalPlaces = 0
        ETB_字幕不透明度.ThumbTextMode = LakeUI.ExcellentTrackBar.ThumbTextModeEnum.Value
        ETB_字幕不透明度.ThumbWidth = 38
        ETB_字幕不透明度.TrackColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_字幕不透明度.TrackFillColor = Color.Transparent
        ETB_字幕不透明度.Value = 255R
        ' 
        ' Form设置_字幕
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(651, 486)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_字幕"
        Text = "Form设置_字幕"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        Panel7.ResumeLayout(False)
        Panel7.PerformLayout()
        Panel6.ResumeLayout(False)
        Panel5.ResumeLayout(False)
        Panel5.PerformLayout()
        Panel4.ResumeLayout(False)
        Panel4.PerformLayout()
        Panel3.ResumeLayout(False)
        Panel3.PerformLayout()
        Panel2.ResumeLayout(False)
        Panel1.ResumeLayout(False)
        Panel8.ResumeLayout(False)
        Panel9.ResumeLayout(False)
        Panel9.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents HtmlColorLabel2 As LakeUI.HtmlColorLabel
    Friend WithEvents Panel1 As LakeUI.ModernPanel
    Friend WithEvents MB_重置第一行样式和颜色 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl2 As LakeUI.JustEmptyControl
    Friend WithEvents MB_设置第一行字体样式 As LakeUI.ModernButton
    Friend WithEvents Panel2 As LakeUI.ModernPanel
    Friend WithEvents MB_重置第二行样式和颜色 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl1 As LakeUI.JustEmptyControl
    Friend WithEvents MB_设置第二行字体样式 As LakeUI.ModernButton
    Friend WithEvents Panel3 As LakeUI.ModernPanel
    Friend WithEvents HtmlColorLabel4 As LakeUI.HtmlColorLabel
    Friend WithEvents Panel4 As LakeUI.ModernPanel
    Friend WithEvents HtmlColorLabel5 As LakeUI.HtmlColorLabel
    Friend WithEvents Panel5 As LakeUI.ModernPanel
    Friend WithEvents HtmlColorLabel6 As LakeUI.HtmlColorLabel
    Friend WithEvents MCB_底部对齐方式 As LakeUI.ModernComboBox
    Friend WithEvents Panel6 As LakeUI.ModernPanel
    Friend WithEvents MB_重置其他位置样式 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl3 As LakeUI.JustEmptyControl
    Friend WithEvents MB_设置其他位置字体样式 As LakeUI.ModernButton
    Friend WithEvents Panel7 As LakeUI.ModernPanel
    Friend WithEvents HtmlColorLabel8 As LakeUI.HtmlColorLabel
    Friend WithEvents MCB_尺寸缩放方式 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl5 As LakeUI.JustEmptyControl
    Friend WithEvents MB_设置第二行文字颜色 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl4 As LakeUI.JustEmptyControl
    Friend WithEvents MB_设置第一行文字颜色 As LakeUI.ModernButton
    Friend WithEvents ETB_字幕行间距 As LakeUI.ExcellentTrackBar
    Friend WithEvents ETB_字幕底部间距 As LakeUI.ExcellentTrackBar
    Friend WithEvents Panel8 As LakeUI.ModernPanel
    Friend WithEvents MCB_字幕阴影样式 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl6 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_字幕描边样式 As LakeUI.ModernComboBox
    Friend WithEvents Panel9 As LakeUI.ModernPanel
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents ETB_字幕不透明度 As LakeUI.ExcellentTrackBar
End Class
