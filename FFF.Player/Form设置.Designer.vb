<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置
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
        Dim ModernTabPage1 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage2 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage3 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage4 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage5 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage6 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage7 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage8 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage9 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        ModernTabListControl1 = New LakeUI.ModernTabListControl()
        SuspendLayout()
        ' 
        ' ModernTabListControl1
        ' 
        ModernTabListControl1.ContentBackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernTabListControl1.Dock = DockStyle.Fill
        ModernTabPage1.Text = "FFF.Player"
        ModernTabPage2.Text = "个性化"
        ModernTabPage3.Text = "支持者"
        ModernTabPage4.IsSeparator = True
        ModernTabPage5.Text = "界面与尺寸"
        ModernTabPage6.Text = "HDR"
        ModernTabPage7.Text = "字幕"
        ModernTabPage8.Text = "弹幕"
        ModernTabPage9.Text = "音乐与歌词"
        ModernTabListControl1.Items.Add(ModernTabPage1)
        ModernTabListControl1.Items.Add(ModernTabPage2)
        ModernTabListControl1.Items.Add(ModernTabPage3)
        ModernTabListControl1.Items.Add(ModernTabPage4)
        ModernTabListControl1.Items.Add(ModernTabPage5)
        ModernTabListControl1.Items.Add(ModernTabPage6)
        ModernTabListControl1.Items.Add(ModernTabPage7)
        ModernTabListControl1.Items.Add(ModernTabPage8)
        ModernTabListControl1.Items.Add(ModernTabPage9)
        ModernTabListControl1.Location = New Point(0, 0)
        ModernTabListControl1.Name = "ModernTabListControl1"
        ModernTabListControl1.ScrollBarThumbColor = Color.FromArgb(CByte(40), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.ScrollBarThumbHoverColor = Color.FromArgb(CByte(80), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.ScrollBarTrackColor = Color.FromArgb(CByte(20), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.SeparatorColor = Color.FromArgb(CByte(80), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.Size = New Size(784, 461)
        ModernTabListControl1.TabIndex = 1
        ModernTabListControl1.TabItemHeight = 30
        ModernTabListControl1.TabItemHoverBackColor = Color.FromArgb(CByte(40), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.TabItemSelectedBackColor = Color.FromArgb(CByte(80), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.TabStripWidth = 160
        ' 
        ' Form设置
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(784, 461)
        Controls.Add(ModernTabListControl1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        MaximizeBox = False
        MinimizeBox = False
        MinimumSize = New Size(800, 500)
        Name = "Form设置"
        ShowInTaskbar = False
        Text = "设置"
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernTabListControl1 As LakeUI.ModernTabListControl
End Class
