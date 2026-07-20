<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form总控台
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
        ModernPanel5 = New LakeUI.ModernPanel()
        MTB_执行日志 = New LakeUI.ModernTextBox()
        MCK_防误触模式 = New LakeUI.ModernCheckBox()
        Panel1 = New Panel()
        MB_启动或暂停或继续录制 = New LakeUI.ModernButton()
        JustEmptyControl13 = New LakeUI.JustEmptyControl()
        MB_分割 = New LakeUI.ModernButton()
        JustEmptyControl1 = New LakeUI.JustEmptyControl()
        MB_结束 = New LakeUI.ModernButton()
        JustEmptyControl11 = New LakeUI.JustEmptyControl()
        Panel2 = New Panel()
        ModernPanel4 = New LakeUI.ModernPanel()
        JustEmptyControl2 = New LakeUI.JustEmptyControl()
        ModernPanel2 = New LakeUI.ModernPanel()
        Panel3 = New Panel()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        JustEmptyControl15 = New LakeUI.JustEmptyControl()
        EPB_BR = New LakeUI.ExcellentProgressBar()
        JustEmptyControl10 = New LakeUI.JustEmptyControl()
        EPB_BL = New LakeUI.ExcellentProgressBar()
        JustEmptyControl9 = New LakeUI.JustEmptyControl()
        EPB_SR = New LakeUI.ExcellentProgressBar()
        JustEmptyControl7 = New LakeUI.JustEmptyControl()
        EPB_SL = New LakeUI.ExcellentProgressBar()
        JustEmptyControl6 = New LakeUI.JustEmptyControl()
        EPB_LFE = New LakeUI.ExcellentProgressBar()
        JustEmptyControl5 = New LakeUI.JustEmptyControl()
        EPB_C = New LakeUI.ExcellentProgressBar()
        JustEmptyControl4 = New LakeUI.JustEmptyControl()
        EPB_R = New LakeUI.ExcellentProgressBar()
        JustEmptyControl3 = New LakeUI.JustEmptyControl()
        EPB_L = New LakeUI.ExcellentProgressBar()
        JustEmptyControl8 = New LakeUI.JustEmptyControl()
        MCB_音频源 = New LakeUI.ModernComboBox()
        JustEmptyControl12 = New LakeUI.JustEmptyControl()
        MCB_视频源 = New LakeUI.ModernComboBox()
        ModernPanel1.SuspendLayout()
        ModernPanel5.SuspendLayout()
        Panel1.SuspendLayout()
        Panel2.SuspendLayout()
        ModernPanel2.SuspendLayout()
        Panel3.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(ModernPanel5)
        ModernPanel1.Controls.Add(JustEmptyControl11)
        ModernPanel1.Controls.Add(Panel2)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(20)
        ModernPanel1.Size = New Size(831, 506)
        ModernPanel1.TabIndex = 0
        ' 
        ' ModernPanel5
        ' 
        ModernPanel5.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        ModernPanel5.BorderRadius = 10
        ModernPanel5.BorderSize = 0
        ModernPanel5.Controls.Add(MTB_执行日志)
        ModernPanel5.Controls.Add(MCK_防误触模式)
        ModernPanel5.Controls.Add(Panel1)
        ModernPanel5.Dock = DockStyle.Fill
        ModernPanel5.Location = New Point(477, 20)
        ModernPanel5.Name = "ModernPanel5"
        ModernPanel5.Padding = New Padding(10)
        ModernPanel5.Size = New Size(334, 466)
        ModernPanel5.TabIndex = 34
        ' 
        ' MTB_执行日志
        ' 
        MTB_执行日志.BackColor = Color.Transparent
        MTB_执行日志.BackColor1 = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        MTB_执行日志.BorderColor = Color.Transparent
        MTB_执行日志.BorderColorFocus = Color.Empty
        MTB_执行日志.BorderRadius = 10
        MTB_执行日志.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_执行日志.Dock = DockStyle.Fill
        MTB_执行日志.LineHeight = 20
        MTB_执行日志.LineNumberBackColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_执行日志.LineNumberForeColor = Color.Silver
        MTB_执行日志.Location = New Point(15, 15)
        MTB_执行日志.Margin = New Padding(2)
        MTB_执行日志.MultiLine = True
        MTB_执行日志.Name = "MTB_执行日志"
        MTB_执行日志.Padding = New Padding(10, 8, 10, 8)
        MTB_执行日志.ReadOnly = True
        MTB_执行日志.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_执行日志.Size = New Size(304, 332)
        MTB_执行日志.TabIndex = 27
        MTB_执行日志.WaterText = "执行日志"
        MTB_执行日志.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' MCK_防误触模式
        ' 
        MCK_防误触模式.AutoSize = True
        MCK_防误触模式.BackColor = Color.Transparent
        MCK_防误触模式.BoxBorderRadius = 5
        MCK_防误触模式.BoxBorderSize = 0
        MCK_防误触模式.BoxCheckedBackColor = Color.OliveDrab
        MCK_防误触模式.BoxInnerPadding = 6
        MCK_防误触模式.BoxSize = 24
        MCK_防误触模式.BoxTextSpacing = 10
        MCK_防误触模式.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_防误触模式.Dock = DockStyle.Bottom
        MCK_防误触模式.Location = New Point(15, 347)
        MCK_防误触模式.Name = "MCK_防误触模式"
        MCK_防误触模式.Padding = New Padding(0, 10, 0, 10)
        MCK_防误触模式.Size = New Size(304, 44)
        MCK_防误触模式.TabIndex = 26
        MCK_防误触模式.Text = "防误触模式 ：按住以触发按钮"
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.Controls.Add(MB_启动或暂停或继续录制)
        Panel1.Controls.Add(JustEmptyControl13)
        Panel1.Controls.Add(MB_分割)
        Panel1.Controls.Add(JustEmptyControl1)
        Panel1.Controls.Add(MB_结束)
        Panel1.Dock = DockStyle.Bottom
        Panel1.Location = New Point(15, 391)
        Panel1.Name = "Panel1"
        Panel1.Size = New Size(304, 60)
        Panel1.TabIndex = 0
        ' 
        ' MB_启动或暂停或继续录制
        ' 
        MB_启动或暂停或继续录制.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_启动或暂停或继续录制.BorderRadius = 10
        MB_启动或暂停或继续录制.BorderSize = 0
        MB_启动或暂停或继续录制.Dock = DockStyle.Fill
        MB_启动或暂停或继续录制.Font = New Font("Microsoft YaHei UI", 12F)
        MB_启动或暂停或继续录制.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_启动或暂停或继续录制.Location = New Point(0, 0)
        MB_启动或暂停或继续录制.Margin = New Padding(2)
        MB_启动或暂停或继续录制.Name = "MB_启动或暂停或继续录制"
        MB_启动或暂停或继续录制.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_启动或暂停或继续录制.Size = New Size(124, 60)
        MB_启动或暂停或继续录制.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_启动或暂停或继续录制.TabIndex = 12
        MB_启动或暂停或继续录制.Text = "启动录制"
        ' 
        ' JustEmptyControl13
        ' 
        JustEmptyControl13.Dock = DockStyle.Right
        JustEmptyControl13.Location = New Point(124, 0)
        JustEmptyControl13.Name = "JustEmptyControl13"
        JustEmptyControl13.Size = New Size(10, 60)
        JustEmptyControl13.TabIndex = 33
        ' 
        ' MB_分割
        ' 
        MB_分割.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_分割.BorderRadius = 10
        MB_分割.BorderSize = 0
        MB_分割.Dock = DockStyle.Right
        MB_分割.Enabled = False
        MB_分割.Font = New Font("Microsoft YaHei UI", 11F)
        MB_分割.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_分割.Location = New Point(134, 0)
        MB_分割.Margin = New Padding(2)
        MB_分割.Name = "MB_分割"
        MB_分割.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_分割.Size = New Size(80, 60)
        MB_分割.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_分割.TabIndex = 34
        MB_分割.Text = "分割"
        ' 
        ' JustEmptyControl1
        ' 
        JustEmptyControl1.Dock = DockStyle.Right
        JustEmptyControl1.Location = New Point(214, 0)
        JustEmptyControl1.Name = "JustEmptyControl1"
        JustEmptyControl1.Size = New Size(10, 60)
        JustEmptyControl1.TabIndex = 31
        ' 
        ' MB_结束
        ' 
        MB_结束.BackColor1 = Color.FromArgb(CByte(40), CByte(255), CByte(180), CByte(180))
        MB_结束.BorderRadius = 10
        MB_结束.BorderSize = 0
        MB_结束.Dock = DockStyle.Right
        MB_结束.Enabled = False
        MB_结束.Font = New Font("Microsoft YaHei UI", 11F)
        MB_结束.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(255), CByte(180), CByte(180))
        MB_结束.Location = New Point(224, 0)
        MB_结束.Margin = New Padding(2)
        MB_结束.Name = "MB_结束"
        MB_结束.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(255), CByte(180), CByte(180))
        MB_结束.Size = New Size(80, 60)
        MB_结束.SubText = "完成录制"
        MB_结束.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_结束.TabIndex = 32
        MB_结束.Text = "结束"
        ' 
        ' JustEmptyControl11
        ' 
        JustEmptyControl11.Dock = DockStyle.Left
        JustEmptyControl11.Location = New Point(462, 20)
        JustEmptyControl11.Name = "JustEmptyControl11"
        JustEmptyControl11.Size = New Size(15, 466)
        JustEmptyControl11.TabIndex = 33
        ' 
        ' Panel2
        ' 
        Panel2.Controls.Add(ModernPanel4)
        Panel2.Controls.Add(JustEmptyControl2)
        Panel2.Controls.Add(ModernPanel2)
        Panel2.Dock = DockStyle.Left
        Panel2.Location = New Point(20, 20)
        Panel2.Name = "Panel2"
        Panel2.Size = New Size(442, 466)
        Panel2.TabIndex = 1
        ' 
        ' ModernPanel4
        ' 
        ModernPanel4.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        ModernPanel4.BorderRadius = 10
        ModernPanel4.BorderSize = 0
        ModernPanel4.Dock = DockStyle.Fill
        ModernPanel4.Location = New Point(0, 0)
        ModernPanel4.Name = "ModernPanel4"
        ModernPanel4.Padding = New Padding(10)
        ModernPanel4.Size = New Size(442, 232)
        ModernPanel4.TabIndex = 19
        ' 
        ' JustEmptyControl2
        ' 
        JustEmptyControl2.BackColor = Color.Transparent
        JustEmptyControl2.Dock = DockStyle.Bottom
        JustEmptyControl2.Location = New Point(0, 232)
        JustEmptyControl2.Name = "JustEmptyControl2"
        JustEmptyControl2.Size = New Size(442, 15)
        JustEmptyControl2.TabIndex = 18
        ' 
        ' ModernPanel2
        ' 
        ModernPanel2.AutoSize = True
        ModernPanel2.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        ModernPanel2.BorderRadius = 10
        ModernPanel2.BorderSize = 0
        ModernPanel2.Controls.Add(Panel3)
        ModernPanel2.Controls.Add(JustEmptyControl8)
        ModernPanel2.Controls.Add(MCB_音频源)
        ModernPanel2.Controls.Add(JustEmptyControl12)
        ModernPanel2.Controls.Add(MCB_视频源)
        ModernPanel2.Dock = DockStyle.Bottom
        ModernPanel2.Location = New Point(0, 247)
        ModernPanel2.Name = "ModernPanel2"
        ModernPanel2.Padding = New Padding(10)
        ModernPanel2.Size = New Size(442, 219)
        ModernPanel2.TabIndex = 13
        ' 
        ' Panel3
        ' 
        Panel3.BackColor = Color.Transparent
        Panel3.Controls.Add(HtmlColorLabel1)
        Panel3.Controls.Add(JustEmptyControl15)
        Panel3.Controls.Add(EPB_BR)
        Panel3.Controls.Add(JustEmptyControl10)
        Panel3.Controls.Add(EPB_BL)
        Panel3.Controls.Add(JustEmptyControl9)
        Panel3.Controls.Add(EPB_SR)
        Panel3.Controls.Add(JustEmptyControl7)
        Panel3.Controls.Add(EPB_SL)
        Panel3.Controls.Add(JustEmptyControl6)
        Panel3.Controls.Add(EPB_LFE)
        Panel3.Controls.Add(JustEmptyControl5)
        Panel3.Controls.Add(EPB_C)
        Panel3.Controls.Add(JustEmptyControl4)
        Panel3.Controls.Add(EPB_R)
        Panel3.Controls.Add(JustEmptyControl3)
        Panel3.Controls.Add(EPB_L)
        Panel3.Dock = DockStyle.Top
        Panel3.Location = New Point(15, 104)
        Panel3.Name = "Panel3"
        Panel3.Size = New Size(412, 100)
        Panel3.TabIndex = 18
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.BackColor1 = Color.FromArgb(CByte(40), CByte(0), CByte(0), CByte(0))
        HtmlColorLabel1.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        HtmlColorLabel1.BorderRadius = 5
        HtmlColorLabel1.BorderSize = 1
        HtmlColorLabel1.Dock = DockStyle.Fill
        HtmlColorLabel1.Font = New Font("Microsoft YaHei UI", 9F)
        HtmlColorLabel1.Location = New Point(226, 0)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel1.Size = New Size(186, 100)
        HtmlColorLabel1.TabIndex = 34
        HtmlColorLabel1.Text = "总帧数<br>已丢帧<br>重复帧<br>视频体积<br>音频体积"
        HtmlColorLabel1.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' JustEmptyControl15
        ' 
        JustEmptyControl15.Dock = DockStyle.Left
        JustEmptyControl15.Location = New Point(216, 0)
        JustEmptyControl15.Name = "JustEmptyControl15"
        JustEmptyControl15.Size = New Size(10, 100)
        JustEmptyControl15.TabIndex = 35
        ' 
        ' EPB_BR
        ' 
        EPB_BR.BackColor = Color.Transparent
        EPB_BR.BackColor1 = Color.FromArgb(CByte(120), CByte(0), CByte(0), CByte(0))
        EPB_BR.BorderRadius = 5
        EPB_BR.Dock = DockStyle.Left
        EPB_BR.FillColor = Color.FromArgb(CByte(160), CByte(154), CByte(205), CByte(50))
        EPB_BR.FillColor2 = Color.Empty
        EPB_BR.FillGradientColor = Color.DarkRed
        EPB_BR.FillGradientOrientation = Vortice.Direct2D1.Orientation.Default
        EPB_BR.Font = New Font("Segoe UI Variable Small", 7.8F)
        EPB_BR.ForeColor = Color.White
        EPB_BR.Location = New Point(196, 0)
        EPB_BR.Margin = New Padding(0, 0, 0, 5)
        EPB_BR.MarkerColor = Color.Violet
        EPB_BR.MarkerValue = -12
        EPB_BR.Maximum = 0
        EPB_BR.Minimum = -60
        EPB_BR.Name = "EPB_BR"
        EPB_BR.Orientation = LakeUI.ExcellentProgressBar.BarOrientationEnum.Vertical
        EPB_BR.Size = New Size(20, 100)
        EPB_BR.TabIndex = 33
        EPB_BR.Text = "BR"
        EPB_BR.TextAlign = ContentAlignment.BottomCenter
        EPB_BR.TextPadding = New Padding(0, 0, 0, 5)
        EPB_BR.TrackColor = Color.Empty
        EPB_BR.Value = -23
        ' 
        ' JustEmptyControl10
        ' 
        JustEmptyControl10.Dock = DockStyle.Left
        JustEmptyControl10.Location = New Point(188, 0)
        JustEmptyControl10.Name = "JustEmptyControl10"
        JustEmptyControl10.Size = New Size(8, 100)
        JustEmptyControl10.TabIndex = 32
        ' 
        ' EPB_BL
        ' 
        EPB_BL.BackColor = Color.Transparent
        EPB_BL.BackColor1 = Color.FromArgb(CByte(120), CByte(0), CByte(0), CByte(0))
        EPB_BL.BorderRadius = 5
        EPB_BL.Dock = DockStyle.Left
        EPB_BL.FillColor = Color.FromArgb(CByte(160), CByte(154), CByte(205), CByte(50))
        EPB_BL.FillColor2 = Color.Empty
        EPB_BL.FillGradientColor = Color.DarkRed
        EPB_BL.FillGradientOrientation = Vortice.Direct2D1.Orientation.Default
        EPB_BL.Font = New Font("Segoe UI Variable Small", 7.8F)
        EPB_BL.ForeColor = Color.White
        EPB_BL.Location = New Point(168, 0)
        EPB_BL.Margin = New Padding(0, 0, 0, 5)
        EPB_BL.MarkerColor = Color.Violet
        EPB_BL.MarkerValue = -12
        EPB_BL.Maximum = 0
        EPB_BL.Minimum = -60
        EPB_BL.Name = "EPB_BL"
        EPB_BL.Orientation = LakeUI.ExcellentProgressBar.BarOrientationEnum.Vertical
        EPB_BL.Size = New Size(20, 100)
        EPB_BL.TabIndex = 31
        EPB_BL.Text = "BL"
        EPB_BL.TextAlign = ContentAlignment.BottomCenter
        EPB_BL.TextPadding = New Padding(0, 0, 0, 5)
        EPB_BL.TrackColor = Color.Empty
        EPB_BL.Value = -23
        ' 
        ' JustEmptyControl9
        ' 
        JustEmptyControl9.Dock = DockStyle.Left
        JustEmptyControl9.Location = New Point(160, 0)
        JustEmptyControl9.Name = "JustEmptyControl9"
        JustEmptyControl9.Size = New Size(8, 100)
        JustEmptyControl9.TabIndex = 30
        ' 
        ' EPB_SR
        ' 
        EPB_SR.BackColor = Color.Transparent
        EPB_SR.BackColor1 = Color.FromArgb(CByte(120), CByte(0), CByte(0), CByte(0))
        EPB_SR.BorderRadius = 5
        EPB_SR.Dock = DockStyle.Left
        EPB_SR.FillColor = Color.FromArgb(CByte(160), CByte(154), CByte(205), CByte(50))
        EPB_SR.FillColor2 = Color.Empty
        EPB_SR.FillGradientColor = Color.DarkRed
        EPB_SR.FillGradientOrientation = Vortice.Direct2D1.Orientation.Default
        EPB_SR.Font = New Font("Segoe UI Variable Small", 7.8F)
        EPB_SR.ForeColor = Color.White
        EPB_SR.Location = New Point(140, 0)
        EPB_SR.Margin = New Padding(0, 0, 0, 5)
        EPB_SR.MarkerColor = Color.Violet
        EPB_SR.MarkerValue = -12
        EPB_SR.Maximum = 0
        EPB_SR.Minimum = -60
        EPB_SR.Name = "EPB_SR"
        EPB_SR.Orientation = LakeUI.ExcellentProgressBar.BarOrientationEnum.Vertical
        EPB_SR.Size = New Size(20, 100)
        EPB_SR.TabIndex = 29
        EPB_SR.Text = "SR"
        EPB_SR.TextAlign = ContentAlignment.BottomCenter
        EPB_SR.TextPadding = New Padding(0, 0, 0, 5)
        EPB_SR.TrackColor = Color.Empty
        EPB_SR.Value = -23
        ' 
        ' JustEmptyControl7
        ' 
        JustEmptyControl7.Dock = DockStyle.Left
        JustEmptyControl7.Location = New Point(132, 0)
        JustEmptyControl7.Name = "JustEmptyControl7"
        JustEmptyControl7.Size = New Size(8, 100)
        JustEmptyControl7.TabIndex = 28
        ' 
        ' EPB_SL
        ' 
        EPB_SL.BackColor = Color.Transparent
        EPB_SL.BackColor1 = Color.FromArgb(CByte(120), CByte(0), CByte(0), CByte(0))
        EPB_SL.BorderRadius = 5
        EPB_SL.Dock = DockStyle.Left
        EPB_SL.FillColor = Color.FromArgb(CByte(160), CByte(154), CByte(205), CByte(50))
        EPB_SL.FillColor2 = Color.Empty
        EPB_SL.FillGradientColor = Color.DarkRed
        EPB_SL.FillGradientOrientation = Vortice.Direct2D1.Orientation.Default
        EPB_SL.Font = New Font("Segoe UI Variable Small", 7.8F)
        EPB_SL.ForeColor = Color.White
        EPB_SL.Location = New Point(112, 0)
        EPB_SL.Margin = New Padding(0, 0, 0, 5)
        EPB_SL.MarkerColor = Color.Violet
        EPB_SL.MarkerValue = -12
        EPB_SL.Maximum = 0
        EPB_SL.Minimum = -60
        EPB_SL.Name = "EPB_SL"
        EPB_SL.Orientation = LakeUI.ExcellentProgressBar.BarOrientationEnum.Vertical
        EPB_SL.Size = New Size(20, 100)
        EPB_SL.TabIndex = 27
        EPB_SL.Text = "SL"
        EPB_SL.TextAlign = ContentAlignment.BottomCenter
        EPB_SL.TextPadding = New Padding(0, 0, 0, 5)
        EPB_SL.TrackColor = Color.Empty
        EPB_SL.Value = -23
        ' 
        ' JustEmptyControl6
        ' 
        JustEmptyControl6.Dock = DockStyle.Left
        JustEmptyControl6.Location = New Point(104, 0)
        JustEmptyControl6.Name = "JustEmptyControl6"
        JustEmptyControl6.Size = New Size(8, 100)
        JustEmptyControl6.TabIndex = 26
        ' 
        ' EPB_LFE
        ' 
        EPB_LFE.BackColor = Color.Transparent
        EPB_LFE.BackColor1 = Color.FromArgb(CByte(120), CByte(0), CByte(0), CByte(0))
        EPB_LFE.BorderRadius = 5
        EPB_LFE.Dock = DockStyle.Left
        EPB_LFE.FillColor = Color.FromArgb(CByte(160), CByte(154), CByte(205), CByte(50))
        EPB_LFE.FillColor2 = Color.Empty
        EPB_LFE.FillGradientColor = Color.DarkRed
        EPB_LFE.FillGradientOrientation = Vortice.Direct2D1.Orientation.Default
        EPB_LFE.Font = New Font("Segoe UI Variable Small", 7.8F)
        EPB_LFE.ForeColor = Color.White
        EPB_LFE.Location = New Point(84, 0)
        EPB_LFE.Margin = New Padding(0, 0, 0, 5)
        EPB_LFE.MarkerColor = Color.Violet
        EPB_LFE.MarkerValue = -12
        EPB_LFE.Maximum = 0
        EPB_LFE.Minimum = -60
        EPB_LFE.Name = "EPB_LFE"
        EPB_LFE.Orientation = LakeUI.ExcellentProgressBar.BarOrientationEnum.Vertical
        EPB_LFE.Size = New Size(20, 100)
        EPB_LFE.TabIndex = 25
        EPB_LFE.Text = "LFE"
        EPB_LFE.TextAlign = ContentAlignment.BottomCenter
        EPB_LFE.TextPadding = New Padding(0, 0, 0, 5)
        EPB_LFE.TrackColor = Color.Empty
        EPB_LFE.Value = -23
        ' 
        ' JustEmptyControl5
        ' 
        JustEmptyControl5.Dock = DockStyle.Left
        JustEmptyControl5.Location = New Point(76, 0)
        JustEmptyControl5.Name = "JustEmptyControl5"
        JustEmptyControl5.Size = New Size(8, 100)
        JustEmptyControl5.TabIndex = 24
        ' 
        ' EPB_C
        ' 
        EPB_C.BackColor = Color.Transparent
        EPB_C.BackColor1 = Color.FromArgb(CByte(120), CByte(0), CByte(0), CByte(0))
        EPB_C.BorderRadius = 5
        EPB_C.Dock = DockStyle.Left
        EPB_C.FillColor = Color.FromArgb(CByte(160), CByte(154), CByte(205), CByte(50))
        EPB_C.FillColor2 = Color.Empty
        EPB_C.FillGradientColor = Color.DarkRed
        EPB_C.FillGradientOrientation = Vortice.Direct2D1.Orientation.Default
        EPB_C.Font = New Font("Segoe UI Variable Small", 7.8F)
        EPB_C.ForeColor = Color.White
        EPB_C.Location = New Point(56, 0)
        EPB_C.Margin = New Padding(0, 0, 0, 5)
        EPB_C.MarkerColor = Color.Violet
        EPB_C.MarkerValue = -12
        EPB_C.Maximum = 0
        EPB_C.Minimum = -60
        EPB_C.Name = "EPB_C"
        EPB_C.Orientation = LakeUI.ExcellentProgressBar.BarOrientationEnum.Vertical
        EPB_C.Size = New Size(20, 100)
        EPB_C.TabIndex = 23
        EPB_C.Text = "C"
        EPB_C.TextAlign = ContentAlignment.BottomCenter
        EPB_C.TextPadding = New Padding(0, 0, 0, 5)
        EPB_C.TrackColor = Color.Empty
        EPB_C.Value = -23
        ' 
        ' JustEmptyControl4
        ' 
        JustEmptyControl4.Dock = DockStyle.Left
        JustEmptyControl4.Location = New Point(48, 0)
        JustEmptyControl4.Name = "JustEmptyControl4"
        JustEmptyControl4.Size = New Size(8, 100)
        JustEmptyControl4.TabIndex = 22
        ' 
        ' EPB_R
        ' 
        EPB_R.BackColor = Color.Transparent
        EPB_R.BackColor1 = Color.FromArgb(CByte(120), CByte(0), CByte(0), CByte(0))
        EPB_R.BorderRadius = 5
        EPB_R.Dock = DockStyle.Left
        EPB_R.FillColor = Color.FromArgb(CByte(160), CByte(154), CByte(205), CByte(50))
        EPB_R.FillColor2 = Color.Empty
        EPB_R.FillGradientColor = Color.DarkRed
        EPB_R.FillGradientOrientation = Vortice.Direct2D1.Orientation.Default
        EPB_R.Font = New Font("Segoe UI Variable Small", 7.8F)
        EPB_R.ForeColor = Color.White
        EPB_R.Location = New Point(28, 0)
        EPB_R.Margin = New Padding(0, 0, 0, 5)
        EPB_R.MarkerColor = Color.Violet
        EPB_R.MarkerValue = -12
        EPB_R.Maximum = 0
        EPB_R.Minimum = -60
        EPB_R.Name = "EPB_R"
        EPB_R.Orientation = LakeUI.ExcellentProgressBar.BarOrientationEnum.Vertical
        EPB_R.Size = New Size(20, 100)
        EPB_R.TabIndex = 21
        EPB_R.Text = "R"
        EPB_R.TextAlign = ContentAlignment.BottomCenter
        EPB_R.TextPadding = New Padding(0, 0, 0, 5)
        EPB_R.TrackColor = Color.Empty
        EPB_R.Value = -23
        ' 
        ' JustEmptyControl3
        ' 
        JustEmptyControl3.Dock = DockStyle.Left
        JustEmptyControl3.Location = New Point(20, 0)
        JustEmptyControl3.Name = "JustEmptyControl3"
        JustEmptyControl3.Size = New Size(8, 100)
        JustEmptyControl3.TabIndex = 20
        ' 
        ' EPB_L
        ' 
        EPB_L.BackColor = Color.Transparent
        EPB_L.BackColor1 = Color.FromArgb(CByte(120), CByte(0), CByte(0), CByte(0))
        EPB_L.BorderRadius = 5
        EPB_L.Dock = DockStyle.Left
        EPB_L.FillColor = Color.FromArgb(CByte(160), CByte(154), CByte(205), CByte(50))
        EPB_L.FillColor2 = Color.Empty
        EPB_L.FillGradientColor = Color.DarkRed
        EPB_L.FillGradientOrientation = Vortice.Direct2D1.Orientation.Default
        EPB_L.Font = New Font("Segoe UI Variable Small", 7.8F)
        EPB_L.ForeColor = Color.White
        EPB_L.Location = New Point(0, 0)
        EPB_L.Margin = New Padding(0, 0, 0, 5)
        EPB_L.MarkerColor = Color.Violet
        EPB_L.MarkerValue = -12
        EPB_L.Maximum = 0
        EPB_L.Minimum = -60
        EPB_L.Name = "EPB_L"
        EPB_L.Orientation = LakeUI.ExcellentProgressBar.BarOrientationEnum.Vertical
        EPB_L.Size = New Size(20, 100)
        EPB_L.TabIndex = 19
        EPB_L.Text = "L"
        EPB_L.TextAlign = ContentAlignment.BottomCenter
        EPB_L.TextPadding = New Padding(0, 0, 0, 5)
        EPB_L.TrackColor = Color.Empty
        EPB_L.Value = -23
        ' 
        ' JustEmptyControl8
        ' 
        JustEmptyControl8.BackColor = Color.Transparent
        JustEmptyControl8.Dock = DockStyle.Top
        JustEmptyControl8.Location = New Point(15, 89)
        JustEmptyControl8.Name = "JustEmptyControl8"
        JustEmptyControl8.Size = New Size(412, 15)
        JustEmptyControl8.TabIndex = 15
        ' 
        ' MCB_音频源
        ' 
        MCB_音频源.BackColor = Color.Transparent
        MCB_音频源.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频源.BorderRadius = 10
        MCB_音频源.BorderSize = 0
        MCB_音频源.Dock = DockStyle.Top
        MCB_音频源.DropDownBackdropBlurPasses = 2
        MCB_音频源.DropDownBackdropBlurRadius = 30
        MCB_音频源.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_音频源.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_音频源.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_音频源.DropDownPadding = New Padding(10)
        MCB_音频源.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频源.DropDownSelectedForeColor = Color.White
        MCB_音频源.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_音频源.Location = New Point(15, 57)
        MCB_音频源.Margin = New Padding(2, 2, 2, 2)
        MCB_音频源.MaxDropDownItems = 12
        MCB_音频源.Name = "MCB_音频源"
        MCB_音频源.Padding = New Padding(10, 0, 10, 0)
        MCB_音频源.Size = New Size(412, 32)
        MCB_音频源.TabIndex = 16
        MCB_音频源.ToolTipGap = -1
        MCB_音频源.ToolTipMaxWidth = 350
        MCB_音频源.ToolTipPadding = New Padding(15)
        MCB_音频源.WaterText = "选择音频源"
        MCB_音频源.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl12
        ' 
        JustEmptyControl12.BackColor = Color.Transparent
        JustEmptyControl12.Dock = DockStyle.Top
        JustEmptyControl12.Location = New Point(15, 47)
        JustEmptyControl12.Name = "JustEmptyControl12"
        JustEmptyControl12.Size = New Size(412, 10)
        JustEmptyControl12.TabIndex = 19
        ' 
        ' MCB_视频源
        ' 
        MCB_视频源.BackColor = Color.Transparent
        MCB_视频源.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频源.BorderRadius = 10
        MCB_视频源.BorderSize = 0
        MCB_视频源.Dock = DockStyle.Top
        MCB_视频源.DropDownBackdropBlurPasses = 2
        MCB_视频源.DropDownBackdropBlurRadius = 30
        MCB_视频源.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_视频源.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_视频源.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_视频源.DropDownPadding = New Padding(10)
        MCB_视频源.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频源.DropDownSelectedForeColor = Color.White
        MCB_视频源.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_视频源.Location = New Point(15, 15)
        MCB_视频源.Margin = New Padding(2, 2, 2, 2)
        MCB_视频源.MaxDropDownItems = 12
        MCB_视频源.Name = "MCB_视频源"
        MCB_视频源.Padding = New Padding(10, 0, 10, 0)
        MCB_视频源.Size = New Size(412, 32)
        MCB_视频源.TabIndex = 20
        MCB_视频源.ToolTipGap = -1
        MCB_视频源.ToolTipMaxWidth = 350
        MCB_视频源.ToolTipPadding = New Padding(15)
        MCB_视频源.WaterText = "选择视频源"
        MCB_视频源.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' Form总控台
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(831, 506)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form总控台"
        Text = "Form总控台"
        ModernPanel1.ResumeLayout(False)
        ModernPanel5.ResumeLayout(False)
        ModernPanel5.PerformLayout()
        Panel1.ResumeLayout(False)
        Panel2.ResumeLayout(False)
        Panel2.PerformLayout()
        ModernPanel2.ResumeLayout(False)
        Panel3.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents Panel2 As Panel
    Friend WithEvents ModernPanel2 As LakeUI.ModernPanel
    Friend WithEvents JustEmptyControl8 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_音频源 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel4 As LakeUI.ModernPanel
    Friend WithEvents JustEmptyControl2 As LakeUI.JustEmptyControl
    Friend WithEvents Panel3 As Panel
    Friend WithEvents EPB_L As LakeUI.ExcellentProgressBar
    Friend WithEvents EPB_BR As LakeUI.ExcellentProgressBar
    Friend WithEvents JustEmptyControl10 As LakeUI.JustEmptyControl
    Friend WithEvents EPB_BL As LakeUI.ExcellentProgressBar
    Friend WithEvents JustEmptyControl9 As LakeUI.JustEmptyControl
    Friend WithEvents EPB_SR As LakeUI.ExcellentProgressBar
    Friend WithEvents JustEmptyControl7 As LakeUI.JustEmptyControl
    Friend WithEvents EPB_SL As LakeUI.ExcellentProgressBar
    Friend WithEvents JustEmptyControl6 As LakeUI.JustEmptyControl
    Friend WithEvents EPB_LFE As LakeUI.ExcellentProgressBar
    Friend WithEvents JustEmptyControl5 As LakeUI.JustEmptyControl
    Friend WithEvents EPB_C As LakeUI.ExcellentProgressBar
    Friend WithEvents JustEmptyControl4 As LakeUI.JustEmptyControl
    Friend WithEvents EPB_R As LakeUI.ExcellentProgressBar
    Friend WithEvents JustEmptyControl3 As LakeUI.JustEmptyControl
    Friend WithEvents JustEmptyControl11 As LakeUI.JustEmptyControl
    Friend WithEvents ModernPanel5 As LakeUI.ModernPanel
    Friend WithEvents JustEmptyControl12 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_视频源 As LakeUI.ModernComboBox
    Friend WithEvents Panel1 As Panel
    Friend WithEvents MB_结束 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl1 As LakeUI.JustEmptyControl
    Friend WithEvents MB_启动或暂停或继续录制 As LakeUI.ModernButton
    Friend WithEvents MCK_防误触模式 As LakeUI.ModernCheckBox
    Friend WithEvents JustEmptyControl13 As LakeUI.JustEmptyControl
    Friend WithEvents MB_分割 As LakeUI.ModernButton
    Friend WithEvents MTB_执行日志 As LakeUI.ModernTextBox
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents JustEmptyControl15 As LakeUI.JustEmptyControl
End Class
