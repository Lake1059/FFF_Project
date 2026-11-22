<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_播放行为
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
        MB_清除播放进度 = New LakeUI.ModernButton()
        HCL_细节说明 = New LakeUI.HtmlColorLabel()
        MCK_记住播放进度 = New LakeUI.ModernCheckBox()
        ModernPanel2 = New LakeUI.ModernPanel()
        MCB_播放列表打开视频时自动将渲染大小调整为 = New LakeUI.ModernComboBox()
        Panel1 = New LakeUI.ModernPanel()
        MCB_单独打开视频时自动将渲染大小调整为 = New LakeUI.ModernComboBox()
        HCL_说明 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        ModernPanel2.SuspendLayout()
        Panel1.SuspendLayout()
        SuspendLayout()
        '
        ' ModernPanel1
        '
        ModernPanel1.BackColor = Color.Transparent
        ModernPanel1.BackColor1 = Color.Transparent
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(MB_清除播放进度)
        ModernPanel1.Controls.Add(HCL_细节说明)
        ModernPanel1.Controls.Add(MCK_记住播放进度)
        ModernPanel1.Controls.Add(ModernPanel2)
        ModernPanel1.Controls.Add(Panel1)
        ModernPanel1.Controls.Add(HCL_说明)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(15, 20, 20, 20)
        ModernPanel1.Size = New Size(608, 432)
        ModernPanel1.TabIndex = 0
        '
        ' MB_清除播放进度
        '
        MB_清除播放进度.BackColor = Color.Transparent
        MB_清除播放进度.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_清除播放进度.BorderRadius = 10
        MB_清除播放进度.BorderSize = 0
        MB_清除播放进度.Dock = DockStyle.Top
        MB_清除播放进度.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_清除播放进度.Location = New Point(15, 211)
        MB_清除播放进度.Margin = New Padding(2)
        MB_清除播放进度.Name = "MB_清除播放进度"
        MB_清除播放进度.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_清除播放进度.Size = New Size(573, 34)
        MB_清除播放进度.TabIndex = 3
        MB_清除播放进度.Text = "当前没有记录的视频播放进度"
        '
        ' HCL_细节说明
        '
        HCL_细节说明.AutoSize = True
        HCL_细节说明.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_细节说明.BackColor = Color.Transparent
        HCL_细节说明.Dock = DockStyle.Top
        HCL_细节说明.ForeColor = Color.FromArgb(CByte(150), CByte(255), CByte(255), CByte(255))
        HCL_细节说明.Location = New Point(15, 169)
        HCL_细节说明.Margin = New Padding(2)
        HCL_细节说明.Name = "HCL_细节说明"
        HCL_细节说明.Padding = New Padding(0, 0, 0, 8)
        HCL_细节说明.Size = New Size(573, 42)
        HCL_细节说明.TabIndex = 2
        HCL_细节说明.Text = "进度以独立文件保存在本机。关闭开关时立即清空已有记录，之后不再读写。"
        '
        ' MCK_记住播放进度
        '
        MCK_记住播放进度.AutoSize = True
        MCK_记住播放进度.BackColor = Color.Transparent
        MCK_记住播放进度.BoxBorderRadius = 5
        MCK_记住播放进度.BoxBorderSize = 0
        MCK_记住播放进度.BoxCheckedBackColor = Color.OliveDrab
        MCK_记住播放进度.BoxInnerPadding = 6
        MCK_记住播放进度.BoxSize = 24
        MCK_记住播放进度.BoxTextSpacing = 10
        MCK_记住播放进度.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_记住播放进度.Dock = DockStyle.Top
        MCK_记住播放进度.Location = New Point(15, 135)
        MCK_记住播放进度.Name = "MCK_记住播放进度"
        MCK_记住播放进度.Padding = New Padding(0, 0, 0, 10)
        MCK_记住播放进度.Size = New Size(573, 34)
        MCK_记住播放进度.TabIndex = 1
        MCK_记住播放进度.Text = "记住播放进度（下次打开从上次位置继续）"
        '
        ' ModernPanel2
        '
        ModernPanel2.BackColor = Color.Transparent
        ModernPanel2.BackColor1 = Color.Transparent
        ModernPanel2.BorderSize = 0
        ModernPanel2.Controls.Add(MCB_播放列表打开视频时自动将渲染大小调整为)
        ModernPanel2.Dock = DockStyle.Top
        ModernPanel2.Location = New Point(15, 93)
        ModernPanel2.Name = "ModernPanel2"
        ModernPanel2.Padding = New Padding(0, 0, 0, 10)
        ModernPanel2.Size = New Size(573, 42)
        ModernPanel2.TabIndex = 19
        '
        ' MCB_播放列表打开视频时自动将渲染大小调整为
        '
        MCB_播放列表打开视频时自动将渲染大小调整为.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_播放列表打开视频时自动将渲染大小调整为.BorderRadius = 10
        MCB_播放列表打开视频时自动将渲染大小调整为.BorderSize = 0
        MCB_播放列表打开视频时自动将渲染大小调整为.Dock = DockStyle.Fill
        MCB_播放列表打开视频时自动将渲染大小调整为.DropDownBackdropBlurPasses = 2
        MCB_播放列表打开视频时自动将渲染大小调整为.DropDownBackdropBlurRadius = 30
        MCB_播放列表打开视频时自动将渲染大小调整为.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_播放列表打开视频时自动将渲染大小调整为.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_播放列表打开视频时自动将渲染大小调整为.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_播放列表打开视频时自动将渲染大小调整为.DropDownPadding = New Padding(10)
        MCB_播放列表打开视频时自动将渲染大小调整为.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_播放列表打开视频时自动将渲染大小调整为.Items.Add("跟随「界面与尺寸」")
        MCB_播放列表打开视频时自动将渲染大小调整为.Items.Add("保持当前尺寸")
        MCB_播放列表打开视频时自动将渲染大小调整为.Items.Add("初始画面尺寸")
        MCB_播放列表打开视频时自动将渲染大小调整为.Items.Add("内容原始尺寸 1:1")
        MCB_播放列表打开视频时自动将渲染大小调整为.Items.Add("最大化")
        MCB_播放列表打开视频时自动将渲染大小调整为.DropDownSelectedForeColor = Color.White
        MCB_播放列表打开视频时自动将渲染大小调整为.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_播放列表打开视频时自动将渲染大小调整为.Location = New Point(0, 0)
        MCB_播放列表打开视频时自动将渲染大小调整为.Margin = New Padding(2, 2, 2, 2)
        MCB_播放列表打开视频时自动将渲染大小调整为.MaxDropDownItems = 16
        MCB_播放列表打开视频时自动将渲染大小调整为.Name = "MCB_播放列表打开视频时自动将渲染大小调整为"
        MCB_播放列表打开视频时自动将渲染大小调整为.Padding = New Padding(10, 0, 10, 0)
        MCB_播放列表打开视频时自动将渲染大小调整为.PrefixForeColor = Color.Orange
        MCB_播放列表打开视频时自动将渲染大小调整为.PrefixGap = 10
        MCB_播放列表打开视频时自动将渲染大小调整为.PrefixText = "播放列表打开视频时自动将渲染大小调整为"
        MCB_播放列表打开视频时自动将渲染大小调整为.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_播放列表打开视频时自动将渲染大小调整为.Size = New Size(573, 32)
        MCB_播放列表打开视频时自动将渲染大小调整为.TabIndex = 0
        MCB_播放列表打开视频时自动将渲染大小调整为.ToolTipGap = -1
        MCB_播放列表打开视频时自动将渲染大小调整为.ToolTipMaxWidth = 350
        MCB_播放列表打开视频时自动将渲染大小调整为.ToolTipPadding = New Padding(15)
        MCB_播放列表打开视频时自动将渲染大小调整为.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' Panel1
        '
        Panel1.BackColor = Color.Transparent
        Panel1.BackColor1 = Color.Transparent
        Panel1.BorderSize = 0
        Panel1.Controls.Add(MCB_单独打开视频时自动将渲染大小调整为)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(15, 51)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 0, 0, 10)
        Panel1.Size = New Size(573, 42)
        Panel1.TabIndex = 18
        '
        ' MCB_单独打开视频时自动将渲染大小调整为
        '
        MCB_单独打开视频时自动将渲染大小调整为.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_单独打开视频时自动将渲染大小调整为.BorderRadius = 10
        MCB_单独打开视频时自动将渲染大小调整为.BorderSize = 0
        MCB_单独打开视频时自动将渲染大小调整为.Dock = DockStyle.Fill
        MCB_单独打开视频时自动将渲染大小调整为.DropDownBackdropBlurPasses = 2
        MCB_单独打开视频时自动将渲染大小调整为.DropDownBackdropBlurRadius = 30
        MCB_单独打开视频时自动将渲染大小调整为.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_单独打开视频时自动将渲染大小调整为.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_单独打开视频时自动将渲染大小调整为.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_单独打开视频时自动将渲染大小调整为.DropDownPadding = New Padding(10)
        MCB_单独打开视频时自动将渲染大小调整为.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_单独打开视频时自动将渲染大小调整为.Items.Add("跟随「界面与尺寸」")
        MCB_单独打开视频时自动将渲染大小调整为.Items.Add("保持当前尺寸")
        MCB_单独打开视频时自动将渲染大小调整为.Items.Add("初始画面尺寸")
        MCB_单独打开视频时自动将渲染大小调整为.Items.Add("内容原始尺寸 1:1")
        MCB_单独打开视频时自动将渲染大小调整为.Items.Add("最大化")
        MCB_单独打开视频时自动将渲染大小调整为.DropDownSelectedForeColor = Color.White
        MCB_单独打开视频时自动将渲染大小调整为.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_单独打开视频时自动将渲染大小调整为.Location = New Point(0, 0)
        MCB_单独打开视频时自动将渲染大小调整为.Margin = New Padding(2, 2, 2, 2)
        MCB_单独打开视频时自动将渲染大小调整为.MaxDropDownItems = 16
        MCB_单独打开视频时自动将渲染大小调整为.Name = "MCB_单独打开视频时自动将渲染大小调整为"
        MCB_单独打开视频时自动将渲染大小调整为.Padding = New Padding(10, 0, 10, 0)
        MCB_单独打开视频时自动将渲染大小调整为.PrefixForeColor = Color.Orange
        MCB_单独打开视频时自动将渲染大小调整为.PrefixGap = 10
        MCB_单独打开视频时自动将渲染大小调整为.PrefixText = "单独打开视频时自动将渲染大小调整为"
        MCB_单独打开视频时自动将渲染大小调整为.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_单独打开视频时自动将渲染大小调整为.Size = New Size(573, 32)
        MCB_单独打开视频时自动将渲染大小调整为.TabIndex = 0
        MCB_单独打开视频时自动将渲染大小调整为.ToolTipGap = -1
        MCB_单独打开视频时自动将渲染大小调整为.ToolTipMaxWidth = 350
        MCB_单独打开视频时自动将渲染大小调整为.ToolTipPadding = New Padding(15)
        MCB_单独打开视频时自动将渲染大小调整为.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' HCL_说明
        '
        HCL_说明.AutoSize = True
        HCL_说明.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_说明.BackColor = Color.Transparent
        HCL_说明.Dock = DockStyle.Top
        HCL_说明.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HCL_说明.Location = New Point(15, 20)
        HCL_说明.Margin = New Padding(2)
        HCL_说明.Name = "HCL_说明"
        HCL_说明.Padding = New Padding(0, 0, 0, 10)
        HCL_说明.Size = New Size(573, 31)
        HCL_说明.TabIndex = 0
        HCL_说明.Text = "<span style=""font-size:12; color:Silver"">播放行为</span>   让播放器更符合习惯"
        '
        ' Form设置_播放行为
        '
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(608, 432)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_播放行为"
        Text = "Form设置_播放行为"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ModernPanel2.ResumeLayout(False)
        Panel1.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents HCL_说明 As LakeUI.HtmlColorLabel
    Friend WithEvents MCK_记住播放进度 As LakeUI.ModernCheckBox
    Friend WithEvents HCL_细节说明 As LakeUI.HtmlColorLabel
    Friend WithEvents MB_清除播放进度 As LakeUI.ModernButton
    Friend WithEvents ModernPanel2 As LakeUI.ModernPanel
    Friend WithEvents MCB_播放列表打开视频时自动将渲染大小调整为 As LakeUI.ModernComboBox
    Friend WithEvents Panel1 As LakeUI.ModernPanel
    Friend WithEvents MCB_单独打开视频时自动将渲染大小调整为 As LakeUI.ModernComboBox
End Class
