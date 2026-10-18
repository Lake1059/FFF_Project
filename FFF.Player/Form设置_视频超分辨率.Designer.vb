<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()>
Partial Class Form设置_视频超分辨率
    Inherits System.Windows.Forms.Form

    <System.Diagnostics.DebuggerNonUserCode()>
    Protected Overrides Sub Dispose(ByVal disposing As Boolean)
        Try
            If disposing AndAlso components IsNot Nothing Then
                components.Dispose()
            End If
        Finally
            MyBase.Dispose(disposing)
        End Try
    End Sub

    Private components As System.ComponentModel.IContainer

    <System.Diagnostics.DebuggerStepThrough()>
    Private Sub InitializeComponent()
        ModernPanel1 = New LakeUI.ModernPanel()
        HCL_状态读数 = New LakeUI.HtmlColorLabel()
        MB_调整与读数 = New LakeUI.ModernButton()
        MCK_启用RTX视频超分辨率 = New LakeUI.ModernCheckBox()
        HCL_说明 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        SuspendLayout()
        '
        ' ModernPanel1
        '
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(HCL_状态读数)
        ModernPanel1.Controls.Add(MB_调整与读数)
        ModernPanel1.Controls.Add(MCK_启用RTX视频超分辨率)
        ModernPanel1.Controls.Add(HCL_说明)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(10, 20, 20, 20)
        ModernPanel1.Size = New Size(598, 433)
        ModernPanel1.TabIndex = 0
        '
        ' HCL_说明
        '
        HCL_说明.AutoSize = True
        HCL_说明.BackColor = Color.Transparent
        HCL_说明.Dock = DockStyle.None
        HCL_说明.Location = New Point(10, 20)
        HCL_说明.Name = "HCL_说明"
        HCL_说明.Padding = New Padding(0, 0, 0, 6)
        HCL_说明.Size = New Size(568, 34)
        HCL_说明.TabIndex = 40
        HCL_说明.Text = "RTX 视频超分辨率：由内核逐帧判定是否使用。"
        '
        ' MCK_启用RTX视频超分辨率
        '
        MCK_启用RTX视频超分辨率.AutoSize = True
        MCK_启用RTX视频超分辨率.BackColor = Color.Transparent
        MCK_启用RTX视频超分辨率.BoxBorderRadius = 5
        MCK_启用RTX视频超分辨率.BoxBorderSize = 0
        MCK_启用RTX视频超分辨率.BoxCheckedBackColor = Color.OliveDrab
        MCK_启用RTX视频超分辨率.BoxInnerPadding = 6
        MCK_启用RTX视频超分辨率.BoxSize = 24
        MCK_启用RTX视频超分辨率.BoxTextSpacing = 10
        MCK_启用RTX视频超分辨率.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_启用RTX视频超分辨率.Dock = DockStyle.None
        MCK_启用RTX视频超分辨率.Location = New Point(10, 54)
        MCK_启用RTX视频超分辨率.Name = "MCK_启用RTX视频超分辨率"
        MCK_启用RTX视频超分辨率.Padding = New Padding(0, 0, 0, 10)
        MCK_启用RTX视频超分辨率.Size = New Size(568, 34)
        MCK_启用RTX视频超分辨率.TabIndex = 41
        MCK_启用RTX视频超分辨率.Text = "请求 RTX 视频超分辨率"
        '
        ' MB_调整与读数
        '
        MB_调整与读数.Dock = DockStyle.None
        MB_调整与读数.Location = New Point(10, 88)
        MB_调整与读数.Name = "MB_调整与读数"
        MB_调整与读数.Size = New Size(568, 34)
        MB_调整与读数.TabIndex = 42
        MB_调整与读数.Text = "调整与内核读数"
        '
        ' HCL_状态读数
        '
        HCL_状态读数.AutoSize = True
        HCL_状态读数.BackColor = Color.Transparent
        HCL_状态读数.Dock = DockStyle.None
        HCL_状态读数.Location = New Point(10, 122)
        HCL_状态读数.Name = "HCL_状态读数"
        HCL_状态读数.Padding = New Padding(0, 6, 0, 0)
        HCL_状态读数.Size = New Size(568, 24)
        HCL_状态读数.TabIndex = 43
        HCL_状态读数.Text = "状态：未读取"
        '
        ' Form设置_视频超分辨率
        '
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(598, 433)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_视频超分辨率"
        Text = "Form设置_视频超分辨率"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents HCL_说明 As LakeUI.HtmlColorLabel
    Friend WithEvents MCK_启用RTX视频超分辨率 As LakeUI.ModernCheckBox
    Friend WithEvents MB_调整与读数 As LakeUI.ModernButton
    Friend WithEvents HCL_状态读数 As LakeUI.HtmlColorLabel
End Class
