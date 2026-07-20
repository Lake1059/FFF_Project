<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form音频参数
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
        Panel7 = New Panel()
        MCB_采样率 = New LakeUI.ModernComboBox()
        JustEmptyControl11 = New LakeUI.JustEmptyControl()
        MCB_声道数 = New LakeUI.ModernComboBox()
        JustEmptyControl13 = New LakeUI.JustEmptyControl()
        MCB_音频编码器 = New LakeUI.ModernComboBox()
        Panel8 = New Panel()
        HtmlColorLabel6 = New LakeUI.HtmlColorLabel()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        HtmlColorLabel12 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        ModernPanel5.SuspendLayout()
        Panel7.SuspendLayout()
        Panel8.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(ModernPanel5)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(20)
        ModernPanel1.Size = New Size(843, 512)
        ModernPanel1.TabIndex = 0
        ' 
        ' ModernPanel5
        ' 
        ModernPanel5.AutoSize = True
        ModernPanel5.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        ModernPanel5.BorderRadius = 10
        ModernPanel5.BorderSize = 0
        ModernPanel5.Controls.Add(Panel7)
        ModernPanel5.Controls.Add(Panel8)
        ModernPanel5.Dock = DockStyle.Top
        ModernPanel5.Location = New Point(20, 20)
        ModernPanel5.Name = "ModernPanel5"
        ModernPanel5.Padding = New Padding(10)
        ModernPanel5.Size = New Size(803, 87)
        ModernPanel5.TabIndex = 19
        ' 
        ' Panel7
        ' 
        Panel7.BackColor = Color.Transparent
        Panel7.Controls.Add(MCB_采样率)
        Panel7.Controls.Add(JustEmptyControl11)
        Panel7.Controls.Add(MCB_声道数)
        Panel7.Controls.Add(JustEmptyControl13)
        Panel7.Controls.Add(MCB_音频编码器)
        Panel7.Dock = DockStyle.Top
        Panel7.Location = New Point(15, 40)
        Panel7.Name = "Panel7"
        Panel7.Size = New Size(773, 32)
        Panel7.TabIndex = 13
        ' 
        ' MCB_采样率
        ' 
        MCB_采样率.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_采样率.BorderRadius = 10
        MCB_采样率.BorderSize = 0
        MCB_采样率.Dock = DockStyle.Left
        MCB_采样率.DropDownBackdropBlurPasses = 2
        MCB_采样率.DropDownBackdropBlurRadius = 30
        MCB_采样率.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_采样率.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_采样率.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_采样率.DropDownPadding = New Padding(10)
        MCB_采样率.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_采样率.DropDownSelectedForeColor = Color.White
        MCB_采样率.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_采样率.Items.Add("44100")
        MCB_采样率.Items.Add("48000")
        MCB_采样率.Items.Add("96000")
        MCB_采样率.Items.Add("192000")
        MCB_采样率.Location = New Point(340, 0)
        MCB_采样率.Margin = New Padding(2, 2, 2, 2)
        MCB_采样率.Name = "MCB_采样率"
        MCB_采样率.Padding = New Padding(10, 0, 10, 0)
        MCB_采样率.Size = New Size(120, 32)
        MCB_采样率.TabIndex = 19
        MCB_采样率.ToolTipGap = -1
        MCB_采样率.ToolTipMaxWidth = 350
        MCB_采样率.ToolTipPadding = New Padding(15)
        MCB_采样率.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl11
        ' 
        JustEmptyControl11.Dock = DockStyle.Left
        JustEmptyControl11.Location = New Point(330, 0)
        JustEmptyControl11.Name = "JustEmptyControl11"
        JustEmptyControl11.Size = New Size(10, 32)
        JustEmptyControl11.TabIndex = 22
        ' 
        ' MCB_声道数
        ' 
        MCB_声道数.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_声道数.BorderRadius = 10
        MCB_声道数.BorderSize = 0
        MCB_声道数.Dock = DockStyle.Left
        MCB_声道数.DropDownBackdropBlurPasses = 2
        MCB_声道数.DropDownBackdropBlurRadius = 30
        MCB_声道数.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_声道数.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_声道数.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_声道数.DropDownPadding = New Padding(10)
        MCB_声道数.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_声道数.DropDownSelectedForeColor = Color.White
        MCB_声道数.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_声道数.Items.Add("单声道")
        MCB_声道数.Items.Add("立体声")
        MCB_声道数.Items.Add("2.1")
        MCB_声道数.Items.Add("4.0")
        MCB_声道数.Items.Add("5.0")
        MCB_声道数.Items.Add("5.1")
        MCB_声道数.Items.Add("6.1")
        MCB_声道数.Items.Add("7.1")
        MCB_声道数.Location = New Point(210, 0)
        MCB_声道数.Margin = New Padding(2, 2, 2, 2)
        MCB_声道数.Name = "MCB_声道数"
        MCB_声道数.Padding = New Padding(10, 0, 10, 0)
        MCB_声道数.Size = New Size(120, 32)
        MCB_声道数.TabIndex = 18
        MCB_声道数.ToolTipGap = -1
        MCB_声道数.ToolTipMaxWidth = 350
        MCB_声道数.ToolTipPadding = New Padding(15)
        MCB_声道数.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl13
        ' 
        JustEmptyControl13.Dock = DockStyle.Left
        JustEmptyControl13.Location = New Point(200, 0)
        JustEmptyControl13.Name = "JustEmptyControl13"
        JustEmptyControl13.Size = New Size(10, 32)
        JustEmptyControl13.TabIndex = 17
        ' 
        ' MCB_音频编码器
        ' 
        MCB_音频编码器.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频编码器.BorderRadius = 10
        MCB_音频编码器.BorderSize = 0
        MCB_音频编码器.Dock = DockStyle.Left
        MCB_音频编码器.DropDownBackdropBlurPasses = 2
        MCB_音频编码器.DropDownBackdropBlurRadius = 30
        MCB_音频编码器.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_音频编码器.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_音频编码器.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_音频编码器.DropDownPadding = New Padding(10)
        MCB_音频编码器.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频编码器.DropDownSelectedForeColor = Color.White
        MCB_音频编码器.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_音频编码器.Items.Add("AAC 每声道 160K")
        MCB_音频编码器.Items.Add("NMR AAC 每声道 160K")
        MCB_音频编码器.Items.Add("FDK AAC 最高质量")
        MCB_音频编码器.Items.Add("无损 WAV 24bit")
        MCB_音频编码器.Items.Add("无损 WAV 32bit")
        MCB_音频编码器.Items.Add("无损 FLAC")
        MCB_音频编码器.Location = New Point(0, 0)
        MCB_音频编码器.Margin = New Padding(2, 2, 2, 2)
        MCB_音频编码器.Name = "MCB_音频编码器"
        MCB_音频编码器.Padding = New Padding(10, 0, 10, 0)
        MCB_音频编码器.Size = New Size(200, 32)
        MCB_音频编码器.TabIndex = 16
        MCB_音频编码器.ToolTipGap = -1
        MCB_音频编码器.ToolTipMaxWidth = 350
        MCB_音频编码器.ToolTipPadding = New Padding(15)
        MCB_音频编码器.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' Panel8
        ' 
        Panel8.BackColor = Color.Transparent
        Panel8.Controls.Add(HtmlColorLabel6)
        Panel8.Controls.Add(HtmlColorLabel1)
        Panel8.Controls.Add(HtmlColorLabel12)
        Panel8.Dock = DockStyle.Top
        Panel8.Location = New Point(15, 15)
        Panel8.Name = "Panel8"
        Panel8.Size = New Size(773, 25)
        Panel8.TabIndex = 12
        ' 
        ' HtmlColorLabel6
        ' 
        HtmlColorLabel6.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel6.Dock = DockStyle.Left
        HtmlColorLabel6.Location = New Point(340, 0)
        HtmlColorLabel6.Margin = New Padding(2)
        HtmlColorLabel6.Name = "HtmlColorLabel6"
        HtmlColorLabel6.Size = New Size(120, 25)
        HtmlColorLabel6.TabIndex = 9
        HtmlColorLabel6.Text = "采样率"
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.Dock = DockStyle.Left
        HtmlColorLabel1.Location = New Point(210, 0)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Size = New Size(130, 25)
        HtmlColorLabel1.TabIndex = 8
        HtmlColorLabel1.Text = "声道数"
        ' 
        ' HtmlColorLabel12
        ' 
        HtmlColorLabel12.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel12.Dock = DockStyle.Left
        HtmlColorLabel12.Location = New Point(0, 0)
        HtmlColorLabel12.Margin = New Padding(2)
        HtmlColorLabel12.Name = "HtmlColorLabel12"
        HtmlColorLabel12.Size = New Size(210, 25)
        HtmlColorLabel12.TabIndex = 4
        HtmlColorLabel12.Text = "音频编码器"
        ' 
        ' Form音频参数
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(843, 512)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form音频参数"
        Text = "Form音频参数"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ModernPanel5.ResumeLayout(False)
        Panel7.ResumeLayout(False)
        Panel8.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents ModernPanel5 As LakeUI.ModernPanel
    Friend WithEvents Panel7 As Panel
    Friend WithEvents MCB_采样率 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl11 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_声道数 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl13 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_音频编码器 As LakeUI.ModernComboBox
    Friend WithEvents Panel8 As Panel
    Friend WithEvents HtmlColorLabel6 As LakeUI.HtmlColorLabel
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents HtmlColorLabel12 As LakeUI.HtmlColorLabel
End Class
