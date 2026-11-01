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
        ModernPanel1 = New LakeUI.ModernPanel()
        ModernPanel5 = New LakeUI.ModernPanel()
        MCB_空间音频 = New LakeUI.ModernComboBox()
        ModernPanel4 = New LakeUI.ModernPanel()
        MCB_视频缩放质量 = New LakeUI.ModernComboBox()
        ModernPanel3 = New LakeUI.ModernPanel()
        MCB_音频缓冲区策略 = New LakeUI.ModernComboBox()
        ModernPanel2 = New LakeUI.ModernPanel()
        MCB_视频缓冲区策略 = New LakeUI.ModernComboBox()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        Panel1 = New LakeUI.ModernPanel()
        MCB_GPU解码设备 = New LakeUI.ModernComboBox()
        ModernPanel1.SuspendLayout()
        ModernPanel5.SuspendLayout()
        ModernPanel4.SuspendLayout()
        ModernPanel3.SuspendLayout()
        ModernPanel2.SuspendLayout()
        Panel1.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(ModernPanel5)
        ModernPanel1.Controls.Add(ModernPanel4)
        ModernPanel1.Controls.Add(ModernPanel3)
        ModernPanel1.Controls.Add(ModernPanel2)
        ModernPanel1.Controls.Add(HtmlColorLabel1)
        ModernPanel1.Controls.Add(Panel1)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(15, 20, 20, 20)
        ModernPanel1.Size = New Size(652, 493)
        ModernPanel1.TabIndex = 1
        ' 
        ' ModernPanel5
        ' 
        ModernPanel5.BackColor = Color.Transparent
        ModernPanel5.BackColor1 = Color.Transparent
        ModernPanel5.BorderSize = 0
        ModernPanel5.Controls.Add(MCB_空间音频)
        ModernPanel5.Dock = DockStyle.Top
        ModernPanel5.Location = New Point(15, 215)
        ModernPanel5.Name = "ModernPanel5"
        ModernPanel5.Padding = New Padding(0, 10, 0, 0)
        ModernPanel5.Size = New Size(617, 42)
        ModernPanel5.TabIndex = 22
        ' 
        ' MCB_空间音频
        ' 
        MCB_空间音频.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_空间音频.BorderRadius = 10
        MCB_空间音频.BorderSize = 0
        MCB_空间音频.Dock = DockStyle.Left
        MCB_空间音频.DropDownBackdropBlurPasses = 2
        MCB_空间音频.DropDownBackdropBlurRadius = 30
        MCB_空间音频.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_空间音频.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_空间音频.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_空间音频.DropDownPadding = New Padding(10)
        MCB_空间音频.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_空间音频.DropDownSelectedForeColor = Color.White
        MCB_空间音频.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_空间音频.Items.Add("自动启用")
        MCB_空间音频.Items.Add("不使用")
        MCB_空间音频.Location = New Point(0, 10)
        MCB_空间音频.Margin = New Padding(2, 2, 2, 2)
        MCB_空间音频.MaxDropDownItems = 16
        MCB_空间音频.Name = "MCB_空间音频"
        MCB_空间音频.Padding = New Padding(10, 0, 10, 0)
        MCB_空间音频.PrefixForeColor = Color.YellowGreen
        MCB_空间音频.PrefixGap = 10
        MCB_空间音频.PrefixText = "空间音频"
        MCB_空间音频.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_空间音频.Size = New Size(250, 32)
        MCB_空间音频.TabIndex = 0
        MCB_空间音频.ToolTipGap = -1
        MCB_空间音频.ToolTipMaxWidth = 350
        MCB_空间音频.ToolTipPadding = New Padding(15)
        MCB_空间音频.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' ModernPanel4
        ' 
        ModernPanel4.BackColor = Color.Transparent
        ModernPanel4.BackColor1 = Color.Transparent
        ModernPanel4.BorderSize = 0
        ModernPanel4.Controls.Add(MCB_视频缩放质量)
        ModernPanel4.Dock = DockStyle.Top
        ModernPanel4.Location = New Point(15, 173)
        ModernPanel4.Name = "ModernPanel4"
        ModernPanel4.Padding = New Padding(0, 10, 0, 0)
        ModernPanel4.Size = New Size(617, 42)
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
        MCB_视频缩放质量.Items.Add("高质量 Lanczos4")
        MCB_视频缩放质量.Items.Add("Jinc anti alias")
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
        ' ModernPanel3
        ' 
        ModernPanel3.BackColor = Color.Transparent
        ModernPanel3.BackColor1 = Color.Transparent
        ModernPanel3.BorderSize = 0
        ModernPanel3.Controls.Add(MCB_音频缓冲区策略)
        ModernPanel3.Dock = DockStyle.Top
        ModernPanel3.Location = New Point(15, 131)
        ModernPanel3.Name = "ModernPanel3"
        ModernPanel3.Padding = New Padding(0, 10, 0, 0)
        ModernPanel3.Size = New Size(617, 42)
        ModernPanel3.TabIndex = 20
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
        MCB_音频缓冲区策略.Items.Add("(默认) 基于开发机性能的均衡保守策略")
        MCB_音频缓冲区策略.Items.Add("在低性能设备上使用更多内存以改善流畅度")
        MCB_音频缓冲区策略.Items.Add("在高性能设备上使用更少内存进行压力测试")
        MCB_音频缓冲区策略.Location = New Point(0, 10)
        MCB_音频缓冲区策略.Margin = New Padding(2, 2, 2, 2)
        MCB_音频缓冲区策略.MaxDropDownItems = 16
        MCB_音频缓冲区策略.Name = "MCB_音频缓冲区策略"
        MCB_音频缓冲区策略.Padding = New Padding(10, 0, 10, 0)
        MCB_音频缓冲区策略.PrefixForeColor = Color.YellowGreen
        MCB_音频缓冲区策略.PrefixGap = 10
        MCB_音频缓冲区策略.PrefixText = "音频缓冲区策略"
        MCB_音频缓冲区策略.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频缓冲区策略.Size = New Size(617, 32)
        MCB_音频缓冲区策略.TabIndex = 0
        MCB_音频缓冲区策略.ToolTipGap = -1
        MCB_音频缓冲区策略.ToolTipMaxWidth = 350
        MCB_音频缓冲区策略.ToolTipPadding = New Padding(15)
        MCB_音频缓冲区策略.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' ModernPanel2
        ' 
        ModernPanel2.BackColor = Color.Transparent
        ModernPanel2.BackColor1 = Color.Transparent
        ModernPanel2.BorderSize = 0
        ModernPanel2.Controls.Add(MCB_视频缓冲区策略)
        ModernPanel2.Dock = DockStyle.Top
        ModernPanel2.Location = New Point(15, 89)
        ModernPanel2.Name = "ModernPanel2"
        ModernPanel2.Padding = New Padding(0, 10, 0, 0)
        ModernPanel2.Size = New Size(617, 42)
        ModernPanel2.TabIndex = 19
        ' 
        ' MCB_视频缓冲区策略
        ' 
        MCB_视频缓冲区策略.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.BorderRadius = 10
        MCB_视频缓冲区策略.BorderSize = 0
        MCB_视频缓冲区策略.Dock = DockStyle.Fill
        MCB_视频缓冲区策略.DropDownBackdropBlurPasses = 2
        MCB_视频缓冲区策略.DropDownBackdropBlurRadius = 30
        MCB_视频缓冲区策略.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_视频缓冲区策略.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_视频缓冲区策略.DropDownPadding = New Padding(10)
        MCB_视频缓冲区策略.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.DropDownSelectedForeColor = Color.White
        MCB_视频缓冲区策略.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.Items.Add("(默认) 基于开发机性能的均衡保守策略")
        MCB_视频缓冲区策略.Items.Add("在低性能设备上使用更多内存以改善流畅度")
        MCB_视频缓冲区策略.Items.Add("在高性能设备上使用更少内存进行压力测试")
        MCB_视频缓冲区策略.Location = New Point(0, 10)
        MCB_视频缓冲区策略.Margin = New Padding(2, 2, 2, 2)
        MCB_视频缓冲区策略.MaxDropDownItems = 16
        MCB_视频缓冲区策略.Name = "MCB_视频缓冲区策略"
        MCB_视频缓冲区策略.Padding = New Padding(10, 0, 10, 0)
        MCB_视频缓冲区策略.PrefixForeColor = Color.CornflowerBlue
        MCB_视频缓冲区策略.PrefixGap = 10
        MCB_视频缓冲区策略.PrefixText = "视频缓冲区策略"
        MCB_视频缓冲区策略.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缓冲区策略.Size = New Size(617, 32)
        MCB_视频缓冲区策略.TabIndex = 0
        MCB_视频缓冲区策略.ToolTipGap = -1
        MCB_视频缓冲区策略.ToolTipMaxWidth = 350
        MCB_视频缓冲区策略.ToolTipPadding = New Padding(15)
        MCB_视频缓冲区策略.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSize = True
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.Dock = DockStyle.Top
        HtmlColorLabel1.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel1.Location = New Point(15, 62)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Padding = New Padding(0, 0, 0, 10)
        HtmlColorLabel1.Size = New Size(617, 27)
        HtmlColorLabel1.TabIndex = 18
        HtmlColorLabel1.Text = "渲染呈现 和 LakeUI 总是使用主显示器的 GPU"
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.BackColor1 = Color.Transparent
        Panel1.BorderSize = 0
        Panel1.Controls.Add(MCB_GPU解码设备)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(15, 20)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 0, 0, 10)
        Panel1.Size = New Size(617, 42)
        Panel1.TabIndex = 17
        ' 
        ' MCB_GPU解码设备
        ' 
        MCB_GPU解码设备.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_GPU解码设备.BorderRadius = 10
        MCB_GPU解码设备.BorderSize = 0
        MCB_GPU解码设备.Dock = DockStyle.Fill
        MCB_GPU解码设备.DropDownBackdropBlurPasses = 2
        MCB_GPU解码设备.DropDownBackdropBlurRadius = 30
        MCB_GPU解码设备.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_GPU解码设备.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_GPU解码设备.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_GPU解码设备.DropDownPadding = New Padding(10)
        MCB_GPU解码设备.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_GPU解码设备.DropDownSelectedForeColor = Color.White
        MCB_GPU解码设备.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_GPU解码设备.Location = New Point(0, 0)
        MCB_GPU解码设备.Margin = New Padding(2, 2, 2, 2)
        MCB_GPU解码设备.MaxDropDownItems = 16
        MCB_GPU解码设备.Name = "MCB_GPU解码设备"
        MCB_GPU解码设备.Padding = New Padding(10, 0, 10, 0)
        MCB_GPU解码设备.PrefixForeColor = Color.Orange
        MCB_GPU解码设备.PrefixGap = 10
        MCB_GPU解码设备.PrefixText = "GPU 解码设备"
        MCB_GPU解码设备.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_GPU解码设备.Size = New Size(617, 32)
        MCB_GPU解码设备.TabIndex = 0
        MCB_GPU解码设备.ToolTipGap = -1
        MCB_GPU解码设备.ToolTipMaxWidth = 350
        MCB_GPU解码设备.ToolTipPadding = New Padding(15)
        MCB_GPU解码设备.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' Form设置_设备与性能
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(652, 493)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_设备与性能"
        Text = "Form设置_设备与性能"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ModernPanel5.ResumeLayout(False)
        ModernPanel4.ResumeLayout(False)
        ModernPanel3.ResumeLayout(False)
        ModernPanel2.ResumeLayout(False)
        Panel1.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents Panel1 As LakeUI.ModernPanel
    Friend WithEvents MCB_GPU解码设备 As LakeUI.ModernComboBox
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents ModernPanel2 As LakeUI.ModernPanel
    Friend WithEvents MCB_视频缓冲区策略 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel3 As LakeUI.ModernPanel
    Friend WithEvents MCB_音频缓冲区策略 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel4 As LakeUI.ModernPanel
    Friend WithEvents MCB_视频缩放质量 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel5 As LakeUI.ModernPanel
    Friend WithEvents MCB_空间音频 As LakeUI.ModernComboBox
End Class
