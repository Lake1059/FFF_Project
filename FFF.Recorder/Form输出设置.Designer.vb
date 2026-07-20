<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form输出设置
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
        Panel1 = New Panel()
        MCB_自动命名方式 = New LakeUI.ModernComboBox()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        Panel7 = New Panel()
        MCB_输出位置 = New LakeUI.ModernComboBox()
        HtmlColorLabel2 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        ModernPanel5.SuspendLayout()
        Panel1.SuspendLayout()
        Panel7.SuspendLayout()
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
        ModernPanel1.Size = New Size(800, 450)
        ModernPanel1.TabIndex = 0
        ' 
        ' ModernPanel5
        ' 
        ModernPanel5.AutoSize = True
        ModernPanel5.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        ModernPanel5.BorderRadius = 10
        ModernPanel5.BorderSize = 0
        ModernPanel5.Controls.Add(Panel1)
        ModernPanel5.Controls.Add(HtmlColorLabel1)
        ModernPanel5.Controls.Add(Panel7)
        ModernPanel5.Controls.Add(HtmlColorLabel2)
        ModernPanel5.Dock = DockStyle.Top
        ModernPanel5.Location = New Point(20, 20)
        ModernPanel5.Name = "ModernPanel5"
        ModernPanel5.Padding = New Padding(10)
        ModernPanel5.Size = New Size(760, 159)
        ModernPanel5.TabIndex = 20
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.Controls.Add(MCB_自动命名方式)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(15, 112)
        Panel1.Name = "Panel1"
        Panel1.Size = New Size(730, 32)
        Panel1.TabIndex = 15
        ' 
        ' MCB_自动命名方式
        ' 
        MCB_自动命名方式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_自动命名方式.BorderRadius = 10
        MCB_自动命名方式.BorderSize = 0
        MCB_自动命名方式.Dock = DockStyle.Left
        MCB_自动命名方式.DropDownBackdropBlurPasses = 2
        MCB_自动命名方式.DropDownBackdropBlurRadius = 30
        MCB_自动命名方式.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_自动命名方式.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_自动命名方式.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_自动命名方式.DropDownPadding = New Padding(10)
        MCB_自动命名方式.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_自动命名方式.DropDownSelectedForeColor = Color.White
        MCB_自动命名方式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_自动命名方式.Items.Add("自动递增时间戳")
        MCB_自动命名方式.Items.Add("自动递增序号")
        MCB_自动命名方式.Location = New Point(0, 0)
        MCB_自动命名方式.Margin = New Padding(2, 2, 2, 2)
        MCB_自动命名方式.Name = "MCB_自动命名方式"
        MCB_自动命名方式.Padding = New Padding(10, 0, 10, 0)
        MCB_自动命名方式.Size = New Size(200, 32)
        MCB_自动命名方式.TabIndex = 16
        MCB_自动命名方式.ToolTipGap = -1
        MCB_自动命名方式.ToolTipMaxWidth = 350
        MCB_自动命名方式.ToolTipPadding = New Padding(15)
        MCB_自动命名方式.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSize = True
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.BackColor = Color.Transparent
        HtmlColorLabel1.Dock = DockStyle.Top
        HtmlColorLabel1.Location = New Point(15, 72)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Padding = New Padding(0, 15, 0, 5)
        HtmlColorLabel1.Size = New Size(730, 40)
        HtmlColorLabel1.TabIndex = 16
        HtmlColorLabel1.Text = "自动命名方式"
        ' 
        ' Panel7
        ' 
        Panel7.BackColor = Color.Transparent
        Panel7.Controls.Add(MCB_输出位置)
        Panel7.Dock = DockStyle.Top
        Panel7.Location = New Point(15, 40)
        Panel7.Name = "Panel7"
        Panel7.Size = New Size(730, 32)
        Panel7.TabIndex = 13
        ' 
        ' MCB_输出位置
        ' 
        MCB_输出位置.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_输出位置.BorderRadius = 10
        MCB_输出位置.BorderSize = 0
        MCB_输出位置.Dock = DockStyle.Fill
        MCB_输出位置.DropDownBackdropBlurPasses = 2
        MCB_输出位置.DropDownBackdropBlurRadius = 30
        MCB_输出位置.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_输出位置.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_输出位置.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_输出位置.DropDownPadding = New Padding(10)
        MCB_输出位置.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_输出位置.DropDownSelectedForeColor = Color.White
        MCB_输出位置.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_输出位置.Items.Add("当前目录")
        MCB_输出位置.Items.Add("用户视频文件夹")
        MCB_输出位置.Items.Add("用户桌面")
        MCB_输出位置.Items.Add("浏览 ...")
        MCB_输出位置.Location = New Point(0, 0)
        MCB_输出位置.Margin = New Padding(2, 2, 2, 2)
        MCB_输出位置.Name = "MCB_输出位置"
        MCB_输出位置.Padding = New Padding(10, 0, 10, 0)
        MCB_输出位置.Size = New Size(730, 32)
        MCB_输出位置.TabIndex = 16
        MCB_输出位置.ToolTipGap = -1
        MCB_输出位置.ToolTipMaxWidth = 350
        MCB_输出位置.ToolTipPadding = New Padding(15)
        MCB_输出位置.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' HtmlColorLabel2
        ' 
        HtmlColorLabel2.AutoSize = True
        HtmlColorLabel2.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel2.BackColor = Color.Transparent
        HtmlColorLabel2.Dock = DockStyle.Top
        HtmlColorLabel2.Location = New Point(15, 15)
        HtmlColorLabel2.Margin = New Padding(2)
        HtmlColorLabel2.Name = "HtmlColorLabel2"
        HtmlColorLabel2.Padding = New Padding(0, 0, 0, 5)
        HtmlColorLabel2.Size = New Size(730, 25)
        HtmlColorLabel2.TabIndex = 17
        HtmlColorLabel2.Text = "录制文件存放位置"
        ' 
        ' Form输出设置
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(800, 450)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form输出设置"
        Text = "Form输出设置"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ModernPanel5.ResumeLayout(False)
        ModernPanel5.PerformLayout()
        Panel1.ResumeLayout(False)
        Panel7.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents ModernPanel5 As LakeUI.ModernPanel
    Friend WithEvents Panel7 As Panel
    Friend WithEvents MCB_输出位置 As LakeUI.ModernComboBox
    Friend WithEvents Panel1 As Panel
    Friend WithEvents MCB_自动命名方式 As LakeUI.ModernComboBox
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents HtmlColorLabel2 As LakeUI.HtmlColorLabel
End Class
