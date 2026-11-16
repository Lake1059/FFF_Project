<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_播放进度
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
        HCL_状态读数 = New LakeUI.HtmlColorLabel()
        MB_清除播放进度 = New LakeUI.ModernButton()
        HCL_细节说明 = New LakeUI.HtmlColorLabel()
        MCK_记住播放进度 = New LakeUI.ModernCheckBox()
        HCL_说明 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        SuspendLayout()
        '
        ' ModernPanel1
        '
        ModernPanel1.BackColor = Color.Transparent
        ModernPanel1.BackColor1 = Color.Transparent
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(HCL_状态读数)
        ModernPanel1.Controls.Add(MB_清除播放进度)
        ModernPanel1.Controls.Add(HCL_细节说明)
        ModernPanel1.Controls.Add(MCK_记住播放进度)
        ModernPanel1.Controls.Add(HCL_说明)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(15, 20, 20, 20)
        ModernPanel1.Size = New Size(608, 432)
        ModernPanel1.TabIndex = 0
        '
        ' HCL_状态读数
        '
        HCL_状态读数.AutoSize = True
        HCL_状态读数.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_状态读数.BackColor = Color.Transparent
        HCL_状态读数.Dock = DockStyle.Top
        HCL_状态读数.Location = New Point(15, 163)
        HCL_状态读数.Margin = New Padding(2)
        HCL_状态读数.Name = "HCL_状态读数"
        HCL_状态读数.Padding = New Padding(0, 10, 0, 0)
        HCL_状态读数.Size = New Size(573, 27)
        HCL_状态读数.TabIndex = 4
        HCL_状态读数.Text = "当前记录：0 个媒体"
        '
        ' MB_清除播放进度
        '
        MB_清除播放进度.BackColor = Color.Transparent
        MB_清除播放进度.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_清除播放进度.BorderRadius = 10
        MB_清除播放进度.BorderSize = 0
        MB_清除播放进度.Dock = DockStyle.Top
        MB_清除播放进度.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_清除播放进度.Location = New Point(15, 127)
        MB_清除播放进度.Margin = New Padding(2)
        MB_清除播放进度.Name = "MB_清除播放进度"
        MB_清除播放进度.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_清除播放进度.Size = New Size(573, 34)
        MB_清除播放进度.TabIndex = 3
        MB_清除播放进度.Text = "清除已保存的播放进度"
        '
        ' HCL_细节说明
        '
        HCL_细节说明.AutoSize = True
        HCL_细节说明.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_细节说明.BackColor = Color.Transparent
        HCL_细节说明.Dock = DockStyle.Top
        HCL_细节说明.ForeColor = Color.FromArgb(CByte(150), CByte(255), CByte(255), CByte(255))
        HCL_细节说明.Location = New Point(15, 85)
        HCL_细节说明.Margin = New Padding(2)
        HCL_细节说明.Name = "HCL_细节说明"
        HCL_细节说明.Padding = New Padding(0, 0, 0, 8)
        HCL_细节说明.Size = New Size(573, 42)
        HCL_细节说明.TabIndex = 2
        HCL_细节说明.Text = "进度保存在程序目录的 FFF.Player.PlaybackProgress.json，与用户设置分离；删除该文件同样安全。关闭开关后不再读写，退出程序时清空已有记录。"
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
        MCK_记住播放进度.Checked = True
        MCK_记住播放进度.Dock = DockStyle.Top
        MCK_记住播放进度.Location = New Point(15, 51)
        MCK_记住播放进度.Name = "MCK_记住播放进度"
        MCK_记住播放进度.Padding = New Padding(0, 0, 0, 10)
        MCK_记住播放进度.Size = New Size(573, 34)
        MCK_记住播放进度.TabIndex = 1
        MCK_记住播放进度.Text = "记住播放进度（下次打开从上次位置继续）"
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
        HCL_说明.Text = "<span style=""font-size:12; color:Silver"">播放行为</span>   记住每个媒体看到哪里"
        '
        ' Form设置_播放进度
        '
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(608, 432)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_播放进度"
        Text = "Form设置_播放进度"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents HCL_说明 As LakeUI.HtmlColorLabel
    Friend WithEvents MCK_记住播放进度 As LakeUI.ModernCheckBox
    Friend WithEvents HCL_细节说明 As LakeUI.HtmlColorLabel
    Friend WithEvents MB_清除播放进度 As LakeUI.ModernButton
    Friend WithEvents HCL_状态读数 As LakeUI.HtmlColorLabel
End Class
