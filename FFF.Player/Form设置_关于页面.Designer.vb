<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_关于页面
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
        MB_项目主页 = New LakeUI.ModernButton()
        HtmlColorLabel3 = New LakeUI.HtmlColorLabel()
        HtmlColorLabel2 = New LakeUI.HtmlColorLabel()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        HtmlColorLabel4 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(HtmlColorLabel4)
        ModernPanel1.Controls.Add(MB_项目主页)
        ModernPanel1.Controls.Add(HtmlColorLabel3)
        ModernPanel1.Controls.Add(HtmlColorLabel2)
        ModernPanel1.Controls.Add(HtmlColorLabel1)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(15, 20, 20, 20)
        ModernPanel1.ScrollBarMode = LakeUI.ModernPanel.ScrollMode.Vertical
        ModernPanel1.Size = New Size(580, 450)
        ModernPanel1.TabIndex = 0
        ' 
        ' MB_项目主页
        ' 
        MB_项目主页.BackColor = Color.Transparent
        MB_项目主页.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_项目主页.BorderRadius = 10
        MB_项目主页.BorderSize = 0
        MB_项目主页.Dock = DockStyle.Top
        MB_项目主页.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_项目主页.Location = New Point(15, 289)
        MB_项目主页.Margin = New Padding(2)
        MB_项目主页.Name = "MB_项目主页"
        MB_项目主页.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_项目主页.Size = New Size(545, 32)
        MB_项目主页.TabIndex = 22
        MB_项目主页.Text = "项目主页 / GitHub"
        MB_项目主页.ToolTipShowDelaySeconds = 1R
        MB_项目主页.ToolTipText = "打开浏览器查看项目主页"
        ' 
        ' HtmlColorLabel3
        ' 
        HtmlColorLabel3.AutoSize = True
        HtmlColorLabel3.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel3.Dock = DockStyle.Top
        HtmlColorLabel3.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel3.LineSpacing = 3
        HtmlColorLabel3.Location = New Point(15, 186)
        HtmlColorLabel3.Margin = New Padding(2)
        HtmlColorLabel3.Name = "HtmlColorLabel3"
        HtmlColorLabel3.Padding = New Padding(0, 20, 0, 20)
        HtmlColorLabel3.Size = New Size(545, 103)
        HtmlColorLabel3.TabIndex = 21
        HtmlColorLabel3.Text = "<span style=""font-size:13; color:Silver"">技术声明</span><br>3FP 不是套壳播放器，其渲染管线完全独立搭建<br>不会支持任何外部渲染器和特效插件"
        ' 
        ' HtmlColorLabel2
        ' 
        HtmlColorLabel2.AutoSize = True
        HtmlColorLabel2.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel2.Dock = DockStyle.Top
        HtmlColorLabel2.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel2.LineSpacing = 3
        HtmlColorLabel2.Location = New Point(15, 83)
        HtmlColorLabel2.Margin = New Padding(2)
        HtmlColorLabel2.Name = "HtmlColorLabel2"
        HtmlColorLabel2.Padding = New Padding(0, 20, 0, 0)
        HtmlColorLabel2.Size = New Size(545, 103)
        HtmlColorLabel2.TabIndex = 20
        HtmlColorLabel2.Text = "<span style=""font-size:13; color:Silver"">核心用途和发展方向</span><br>3FP 最初只是为了向 3FUI 提供帧级精确的剪辑区间时间戳<br>后续开发不是为了对标市面上任何播放器产品，而是完全技术测试方向<br>因此不要提任何舒适度的需求，也不要幻想任何花里胡哨的功能"
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSize = True
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.Dock = DockStyle.Top
        HtmlColorLabel1.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel1.LineSpacing = 3
        HtmlColorLabel1.Location = New Point(15, 20)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Size = New Size(545, 63)
        HtmlColorLabel1.TabIndex = 19
        HtmlColorLabel1.Text = "<span style=""font-size:13; color:Silver"">3FP 极简本地播放器</span><br>确保将 Shared FFmpeg 的所有 dll 放在程序目录或环境变量中才可正常播放<br>理论上编码是否支持取决于该版本的 FFmpeg 能否支持"
        ' 
        ' HtmlColorLabel4
        ' 
        HtmlColorLabel4.AutoSize = True
        HtmlColorLabel4.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel4.Dock = DockStyle.Top
        HtmlColorLabel4.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel4.LineSpacing = 3
        HtmlColorLabel4.Location = New Point(15, 321)
        HtmlColorLabel4.Margin = New Padding(2)
        HtmlColorLabel4.Name = "HtmlColorLabel4"
        HtmlColorLabel4.Padding = New Padding(0, 5, 0, 0)
        HtmlColorLabel4.Size = New Size(545, 22)
        HtmlColorLabel4.TabIndex = 23
        HtmlColorLabel4.Text = "此产品没有检查更新功能，请定期访问仓库来获取新版本"
        ' 
        ' Form设置_关于页面
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(580, 450)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = SystemColors.Control
        Name = "Form设置_关于页面"
        Text = "Form设置_关于页面"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents MB_项目主页 As LakeUI.ModernButton
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents HtmlColorLabel3 As LakeUI.HtmlColorLabel
    Friend WithEvents HtmlColorLabel2 As LakeUI.HtmlColorLabel
    Friend WithEvents HtmlColorLabel4 As LakeUI.HtmlColorLabel
End Class
