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
        HCL_自动HDR状态读数 = New LakeUI.HtmlColorLabel()
        MB_自动HDR调整与读数 = New LakeUI.ModernButton()
        MCK_启用视频自动HDR = New LakeUI.ModernCheckBox()
        HCL_自动HDR说明 = New LakeUI.HtmlColorLabel()
        HCL_状态读数 = New LakeUI.HtmlColorLabel()
        MB_调整与读数 = New LakeUI.ModernButton()
        MCK_启用RTX视频超分辨率 = New LakeUI.ModernCheckBox()
        HCL_说明 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor = Color.Transparent
        ModernPanel1.BackColor1 = Color.Transparent
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(HCL_自动HDR状态读数)
        ModernPanel1.Controls.Add(MB_自动HDR调整与读数)
        ModernPanel1.Controls.Add(MCK_启用视频自动HDR)
        ModernPanel1.Controls.Add(HCL_自动HDR说明)
        ModernPanel1.Controls.Add(HCL_状态读数)
        ModernPanel1.Controls.Add(MB_调整与读数)
        ModernPanel1.Controls.Add(MCK_启用RTX视频超分辨率)
        ModernPanel1.Controls.Add(HCL_说明)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(20)
        ModernPanel1.Size = New Size(608, 432)
        ModernPanel1.TabIndex = 0
        ' 
        ' HCL_自动HDR状态读数
        ' 
        HCL_自动HDR状态读数.AutoSize = True
        HCL_自动HDR状态读数.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_自动HDR状态读数.BackColor = Color.Transparent
        HCL_自动HDR状态读数.Dock = DockStyle.Top
        HCL_自动HDR状态读数.Location = New Point(20, 265)
        HCL_自动HDR状态读数.Margin = New Padding(2)
        HCL_自动HDR状态读数.Name = "HCL_自动HDR状态读数"
        HCL_自动HDR状态读数.Padding = New Padding(0, 10, 0, 0)
        HCL_自动HDR状态读数.Size = New Size(568, 27)
        HCL_自动HDR状态读数.TabIndex = 53
        HCL_自动HDR状态读数.Text = "状态：未读取"
        ' 
        ' MB_自动HDR调整与读数
        ' 
        MB_自动HDR调整与读数.BackColor = Color.Transparent
        MB_自动HDR调整与读数.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_自动HDR调整与读数.BorderRadius = 10
        MB_自动HDR调整与读数.BorderSize = 0
        MB_自动HDR调整与读数.Dock = DockStyle.Top
        MB_自动HDR调整与读数.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_自动HDR调整与读数.Location = New Point(20, 231)
        MB_自动HDR调整与读数.Margin = New Padding(2)
        MB_自动HDR调整与读数.Name = "MB_自动HDR调整与读数"
        MB_自动HDR调整与读数.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_自动HDR调整与读数.Size = New Size(568, 34)
        MB_自动HDR调整与读数.TabIndex = 52
        MB_自动HDR调整与读数.Text = "调整与内核读数"
        ' 
        ' MCK_启用视频自动HDR
        ' 
        MCK_启用视频自动HDR.AutoSize = True
        MCK_启用视频自动HDR.BackColor = Color.Transparent
        MCK_启用视频自动HDR.BoxBorderRadius = 5
        MCK_启用视频自动HDR.BoxBorderSize = 0
        MCK_启用视频自动HDR.BoxCheckedBackColor = Color.OliveDrab
        MCK_启用视频自动HDR.BoxInnerPadding = 6
        MCK_启用视频自动HDR.BoxSize = 24
        MCK_启用视频自动HDR.BoxTextSpacing = 10
        MCK_启用视频自动HDR.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_启用视频自动HDR.Dock = DockStyle.Top
        MCK_启用视频自动HDR.Location = New Point(20, 197)
        MCK_启用视频自动HDR.Name = "MCK_启用视频自动HDR"
        MCK_启用视频自动HDR.Padding = New Padding(0, 0, 0, 10)
        MCK_启用视频自动HDR.Size = New Size(568, 34)
        MCK_启用视频自动HDR.TabIndex = 51
        MCK_启用视频自动HDR.Text = "请求自动 HDR"
        ' 
        ' HCL_自动HDR说明
        ' 
        HCL_自动HDR说明.AutoSize = True
        HCL_自动HDR说明.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_自动HDR说明.BackColor = Color.Transparent
        HCL_自动HDR说明.Dock = DockStyle.Top
        HCL_自动HDR说明.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HCL_自动HDR说明.Location = New Point(20, 146)
        HCL_自动HDR说明.Margin = New Padding(2)
        HCL_自动HDR说明.Name = "HCL_自动HDR说明"
        HCL_自动HDR说明.Padding = New Padding(0, 20, 0, 10)
        HCL_自动HDR说明.Size = New Size(568, 51)
        HCL_自动HDR说明.TabIndex = 50
        HCL_自动HDR说明.Text = "<span style=""font-size:12; color:Silver"">RTX 视频增强：HDR</span>   在 NVIDIA APP 中调整详细设置"
        ' 
        ' HCL_状态读数
        ' 
        HCL_状态读数.AutoSize = True
        HCL_状态读数.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_状态读数.BackColor = Color.Transparent
        HCL_状态读数.Dock = DockStyle.Top
        HCL_状态读数.Location = New Point(20, 119)
        HCL_状态读数.Margin = New Padding(2)
        HCL_状态读数.Name = "HCL_状态读数"
        HCL_状态读数.Padding = New Padding(0, 10, 0, 0)
        HCL_状态读数.Size = New Size(568, 27)
        HCL_状态读数.TabIndex = 43
        HCL_状态读数.Text = "状态：未读取"
        ' 
        ' MB_调整与读数
        ' 
        MB_调整与读数.BackColor = Color.Transparent
        MB_调整与读数.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_调整与读数.BorderRadius = 10
        MB_调整与读数.BorderSize = 0
        MB_调整与读数.Dock = DockStyle.Top
        MB_调整与读数.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_调整与读数.Location = New Point(20, 85)
        MB_调整与读数.Margin = New Padding(2)
        MB_调整与读数.Name = "MB_调整与读数"
        MB_调整与读数.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_调整与读数.Size = New Size(568, 34)
        MB_调整与读数.TabIndex = 42
        MB_调整与读数.Text = "调整与内核读数"
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
        MCK_启用RTX视频超分辨率.Dock = DockStyle.Top
        MCK_启用RTX视频超分辨率.Location = New Point(20, 51)
        MCK_启用RTX视频超分辨率.Name = "MCK_启用RTX视频超分辨率"
        MCK_启用RTX视频超分辨率.Padding = New Padding(0, 0, 0, 10)
        MCK_启用RTX视频超分辨率.Size = New Size(568, 34)
        MCK_启用RTX视频超分辨率.TabIndex = 41
        MCK_启用RTX视频超分辨率.Text = "请求 RTX 视频超分辨率"
        ' 
        ' HCL_说明
        ' 
        HCL_说明.AutoSize = True
        HCL_说明.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_说明.BackColor = Color.Transparent
        HCL_说明.Dock = DockStyle.Top
        HCL_说明.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HCL_说明.Location = New Point(20, 20)
        HCL_说明.Margin = New Padding(2)
        HCL_说明.Name = "HCL_说明"
        HCL_说明.Padding = New Padding(0, 0, 0, 10)
        HCL_说明.Size = New Size(568, 31)
        HCL_说明.TabIndex = 40
        HCL_说明.Text = "<span style=""font-size:12; color:Silver"">RTX 视频增强：超分辨率</span>   在 NVIDIA APP 中调整详细设置"
        ' 
        ' Form设置_视频超分辨率
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(608, 432)
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
    Friend WithEvents HCL_自动HDR说明 As LakeUI.HtmlColorLabel
    Friend WithEvents MCK_启用视频自动HDR As LakeUI.ModernCheckBox
    Friend WithEvents MB_自动HDR调整与读数 As LakeUI.ModernButton
    Friend WithEvents HCL_自动HDR状态读数 As LakeUI.HtmlColorLabel
End Class
