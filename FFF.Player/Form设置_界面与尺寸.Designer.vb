<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_界面与尺寸
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
        Panel3 = New LakeUI.ModernPanel()
        MCB_视频缩放质量 = New LakeUI.ModernComboBox()
        HtmlColorLabel3 = New LakeUI.HtmlColorLabel()
        Panel1 = New LakeUI.ModernPanel()
        MCB_全局字体 = New LakeUI.ModernComboBox()
        HtmlColorLabel2 = New LakeUI.HtmlColorLabel()
        Panel2 = New LakeUI.ModernPanel()
        MTB_自定义初始画面尺寸高度 = New LakeUI.ModernTextBox()
        JustEmptyControl2 = New LakeUI.JustEmptyControl()
        MTB_自定义初始画面尺寸宽度 = New LakeUI.ModernTextBox()
        JustEmptyControl1 = New LakeUI.JustEmptyControl()
        MCB_初始画面尺寸选项 = New LakeUI.ModernComboBox()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        Panel3.SuspendLayout()
        Panel1.SuspendLayout()
        Panel2.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(Panel3)
        ModernPanel1.Controls.Add(HtmlColorLabel3)
        ModernPanel1.Controls.Add(Panel1)
        ModernPanel1.Controls.Add(HtmlColorLabel2)
        ModernPanel1.Controls.Add(Panel2)
        ModernPanel1.Controls.Add(HtmlColorLabel1)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(20)
        ModernPanel1.Size = New Size(700, 539)
        ModernPanel1.TabIndex = 0
        ' 
        ' Panel3
        ' 
        Panel3.BackColor = Color.Transparent
        Panel3.BackColor1 = Color.Transparent
        Panel3.BorderSize = 0
        Panel3.Controls.Add(MCB_视频缩放质量)
        Panel3.Dock = DockStyle.Top
        Panel3.Location = New Point(20, 213)
        Panel3.Name = "Panel3"
        Panel3.Padding = New Padding(0, 10, 0, 0)
        Panel3.Size = New Size(660, 42)
        Panel3.TabIndex = 19
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
        MCB_视频缩放质量.Name = "MCB_视频缩放质量"
        MCB_视频缩放质量.Padding = New Padding(10, 0, 10, 0)
        MCB_视频缩放质量.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_视频缩放质量.Size = New Size(200, 32)
        MCB_视频缩放质量.TabIndex = 0
        MCB_视频缩放质量.ToolTipGap = -1
        MCB_视频缩放质量.ToolTipMaxWidth = 350
        MCB_视频缩放质量.ToolTipPadding = New Padding(15)
        MCB_视频缩放质量.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' HtmlColorLabel3
        ' 
        HtmlColorLabel3.AutoSize = True
        HtmlColorLabel3.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel3.Dock = DockStyle.Top
        HtmlColorLabel3.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel3.Location = New Point(20, 170)
        HtmlColorLabel3.Margin = New Padding(2)
        HtmlColorLabel3.Name = "HtmlColorLabel3"
        HtmlColorLabel3.Padding = New Padding(0, 20, 0, 0)
        HtmlColorLabel3.Size = New Size(660, 43)
        HtmlColorLabel3.TabIndex = 18
        HtmlColorLabel3.Text = "<span style=""font-size:13; color:Silver"">视频缩放质量</span>"
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.BackColor1 = Color.Transparent
        Panel1.BorderSize = 0
        Panel1.Controls.Add(MCB_全局字体)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(20, 128)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 10, 0, 0)
        Panel1.Size = New Size(660, 42)
        Panel1.TabIndex = 17
        ' 
        ' MCB_全局字体
        ' 
        MCB_全局字体.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_全局字体.BorderRadius = 10
        MCB_全局字体.BorderSize = 0
        MCB_全局字体.Dock = DockStyle.Left
        MCB_全局字体.DropDownBackdropBlurPasses = 2
        MCB_全局字体.DropDownBackdropBlurRadius = 30
        MCB_全局字体.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_全局字体.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_全局字体.DropDownItemHeight = 26
        MCB_全局字体.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_全局字体.DropDownPadding = New Padding(10)
        MCB_全局字体.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_全局字体.DropDownSelectedForeColor = Color.White
        MCB_全局字体.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_全局字体.Location = New Point(0, 10)
        MCB_全局字体.Margin = New Padding(2, 2, 2, 2)
        MCB_全局字体.MaxDropDownItems = 16
        MCB_全局字体.Name = "MCB_全局字体"
        MCB_全局字体.Padding = New Padding(10, 0, 10, 0)
        MCB_全局字体.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_全局字体.Size = New Size(420, 32)
        MCB_全局字体.TabIndex = 0
        MCB_全局字体.ToolTipGap = -1
        MCB_全局字体.ToolTipMaxWidth = 350
        MCB_全局字体.ToolTipPadding = New Padding(15)
        MCB_全局字体.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' HtmlColorLabel2
        ' 
        HtmlColorLabel2.AutoSize = True
        HtmlColorLabel2.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel2.Dock = DockStyle.Top
        HtmlColorLabel2.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel2.Location = New Point(20, 85)
        HtmlColorLabel2.Margin = New Padding(2)
        HtmlColorLabel2.Name = "HtmlColorLabel2"
        HtmlColorLabel2.Padding = New Padding(0, 20, 0, 0)
        HtmlColorLabel2.Size = New Size(660, 43)
        HtmlColorLabel2.TabIndex = 16
        HtmlColorLabel2.Text = "<span style=""font-size:13; color:Silver"">全局字体</span>"
        ' 
        ' Panel2
        ' 
        Panel2.BackColor = Color.Transparent
        Panel2.BackColor1 = Color.Transparent
        Panel2.BorderSize = 0
        Panel2.Controls.Add(MTB_自定义初始画面尺寸高度)
        Panel2.Controls.Add(JustEmptyControl2)
        Panel2.Controls.Add(MTB_自定义初始画面尺寸宽度)
        Panel2.Controls.Add(JustEmptyControl1)
        Panel2.Controls.Add(MCB_初始画面尺寸选项)
        Panel2.Dock = DockStyle.Top
        Panel2.Location = New Point(20, 43)
        Panel2.Name = "Panel2"
        Panel2.Padding = New Padding(0, 10, 0, 0)
        Panel2.Size = New Size(660, 42)
        Panel2.TabIndex = 15
        ' 
        ' MTB_自定义初始画面尺寸高度
        ' 
        MTB_自定义初始画面尺寸高度.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸高度.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸高度.BorderColorFocus = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸高度.BorderRadius = 10
        MTB_自定义初始画面尺寸高度.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸高度.Dock = DockStyle.Left
        MTB_自定义初始画面尺寸高度.Location = New Point(320, 10)
        MTB_自定义初始画面尺寸高度.Margin = New Padding(2)
        MTB_自定义初始画面尺寸高度.Name = "MTB_自定义初始画面尺寸高度"
        MTB_自定义初始画面尺寸高度.Padding = New Padding(10, 0, 10, 0)
        MTB_自定义初始画面尺寸高度.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸高度.Size = New Size(100, 32)
        MTB_自定义初始画面尺寸高度.TabIndex = 18
        MTB_自定义初始画面尺寸高度.WaterText = "高度"
        ' 
        ' JustEmptyControl2
        ' 
        JustEmptyControl2.Dock = DockStyle.Left
        JustEmptyControl2.Location = New Point(310, 10)
        JustEmptyControl2.Name = "JustEmptyControl2"
        JustEmptyControl2.Size = New Size(10, 32)
        JustEmptyControl2.TabIndex = 17
        ' 
        ' MTB_自定义初始画面尺寸宽度
        ' 
        MTB_自定义初始画面尺寸宽度.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸宽度.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸宽度.BorderColorFocus = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸宽度.BorderRadius = 10
        MTB_自定义初始画面尺寸宽度.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸宽度.Dock = DockStyle.Left
        MTB_自定义初始画面尺寸宽度.Location = New Point(210, 10)
        MTB_自定义初始画面尺寸宽度.Margin = New Padding(2)
        MTB_自定义初始画面尺寸宽度.Name = "MTB_自定义初始画面尺寸宽度"
        MTB_自定义初始画面尺寸宽度.Padding = New Padding(10, 0, 10, 0)
        MTB_自定义初始画面尺寸宽度.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义初始画面尺寸宽度.Size = New Size(100, 32)
        MTB_自定义初始画面尺寸宽度.TabIndex = 16
        MTB_自定义初始画面尺寸宽度.WaterText = "宽度"
        ' 
        ' JustEmptyControl1
        ' 
        JustEmptyControl1.Dock = DockStyle.Left
        JustEmptyControl1.Location = New Point(200, 10)
        JustEmptyControl1.Name = "JustEmptyControl1"
        JustEmptyControl1.Size = New Size(10, 32)
        JustEmptyControl1.TabIndex = 15
        ' 
        ' MCB_初始画面尺寸选项
        ' 
        MCB_初始画面尺寸选项.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_初始画面尺寸选项.BorderRadius = 10
        MCB_初始画面尺寸选项.BorderSize = 0
        MCB_初始画面尺寸选项.Dock = DockStyle.Left
        MCB_初始画面尺寸选项.DropDownBackdropBlurPasses = 2
        MCB_初始画面尺寸选项.DropDownBackdropBlurRadius = 30
        MCB_初始画面尺寸选项.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_初始画面尺寸选项.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_初始画面尺寸选项.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_初始画面尺寸选项.DropDownPadding = New Padding(10)
        MCB_初始画面尺寸选项.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_初始画面尺寸选项.DropDownSelectedForeColor = Color.White
        MCB_初始画面尺寸选项.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_初始画面尺寸选项.Items.Add("自定义")
        MCB_初始画面尺寸选项.Items.Add("640x360")
        MCB_初始画面尺寸选项.Items.Add("854x480")
        MCB_初始画面尺寸选项.Items.Add("960x540")
        MCB_初始画面尺寸选项.Items.Add("1024x576")
        MCB_初始画面尺寸选项.Items.Add("1366x768")
        MCB_初始画面尺寸选项.Items.Add("1600x900")
        MCB_初始画面尺寸选项.Items.Add("1920x1080")
        MCB_初始画面尺寸选项.Items.Add("2560x1440")
        MCB_初始画面尺寸选项.Items.Add("3840x2160")
        MCB_初始画面尺寸选项.Location = New Point(0, 10)
        MCB_初始画面尺寸选项.Margin = New Padding(2, 2, 2, 2)
        MCB_初始画面尺寸选项.Name = "MCB_初始画面尺寸选项"
        MCB_初始画面尺寸选项.Padding = New Padding(10, 0, 10, 0)
        MCB_初始画面尺寸选项.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_初始画面尺寸选项.Size = New Size(200, 32)
        MCB_初始画面尺寸选项.TabIndex = 0
        MCB_初始画面尺寸选项.ToolTipGap = -1
        MCB_初始画面尺寸选项.ToolTipMaxWidth = 350
        MCB_初始画面尺寸选项.ToolTipPadding = New Padding(15)
        MCB_初始画面尺寸选项.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSize = True
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.Dock = DockStyle.Top
        HtmlColorLabel1.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel1.Location = New Point(20, 20)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Size = New Size(660, 23)
        HtmlColorLabel1.TabIndex = 14
        HtmlColorLabel1.Text = "<span style=""font-size:13; color:Silver"">初始画面尺寸</span>   每次启动软件后视频渲染区域的尺寸，会跟随 DPI 缩放"
        ' 
        ' Form设置_界面与尺寸
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(700, 539)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_界面与尺寸"
        Text = "Form设置_界面与尺寸"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        Panel3.ResumeLayout(False)
        Panel1.ResumeLayout(False)
        Panel2.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents Panel2 As LakeUI.ModernPanel
    Friend WithEvents MCB_初始画面尺寸选项 As LakeUI.ModernComboBox
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents MTB_自定义初始画面尺寸高度 As LakeUI.ModernTextBox
    Friend WithEvents JustEmptyControl2 As LakeUI.JustEmptyControl
    Friend WithEvents MTB_自定义初始画面尺寸宽度 As LakeUI.ModernTextBox
    Friend WithEvents JustEmptyControl1 As LakeUI.JustEmptyControl
    Friend WithEvents HtmlColorLabel2 As LakeUI.HtmlColorLabel
    Friend WithEvents Panel1 As LakeUI.ModernPanel
    Friend WithEvents MCB_全局字体 As LakeUI.ModernComboBox
    Friend WithEvents Panel3 As LakeUI.ModernPanel
    Friend WithEvents MCB_视频缩放质量 As LakeUI.ModernComboBox
    Friend WithEvents HtmlColorLabel3 As LakeUI.HtmlColorLabel
End Class
