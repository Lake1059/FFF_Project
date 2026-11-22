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
        components = New System.ComponentModel.Container()
        MCM_VSR调整 = New LakeUI.ModernContextMenu()
        components.Add(MCM_VSR调整)
        MCM_VSR读数 = New LakeUI.ModernContextMenu()
        components.Add(MCM_VSR读数)
        MI_VSR切换 = New LakeUI.ModernContextMenu.ModernMenuItem()
        MI_VSR重探 = New LakeUI.ModernContextMenu.ModernMenuItem()
        MI_VSR实时状态 = New LakeUI.ModernContextMenu.ModernMenuItem()
        Dim MI_VSR读数子菜单 As New LakeUI.ModernContextMenu.ModernMenuItem()
        Dim MI_VSR驱动说明 As New LakeUI.ModernContextMenu.ModernMenuItem()
        Dim MI_VSR使用条件 As New LakeUI.ModernContextMenu.ModernMenuItem()
        Dim MI_VSR效果说明 As New LakeUI.ModernContextMenu.ModernMenuItem()
        MCM_HDR调整 = New LakeUI.ModernContextMenu()
        components.Add(MCM_HDR调整)
        MCM_HDR读数 = New LakeUI.ModernContextMenu()
        components.Add(MCM_HDR读数)
        MI_HDR切换 = New LakeUI.ModernContextMenu.ModernMenuItem()
        MI_HDR重探 = New LakeUI.ModernContextMenu.ModernMenuItem()
        MI_HDR实时状态 = New LakeUI.ModernContextMenu.ModernMenuItem()
        Dim MI_HDR读数子菜单 As New LakeUI.ModernContextMenu.ModernMenuItem()
        Dim MI_HDR驱动说明 As New LakeUI.ModernContextMenu.ModernMenuItem()
        Dim MI_HDR使用条件 As New LakeUI.ModernContextMenu.ModernMenuItem()
        Dim MI_HDR效果说明 As New LakeUI.ModernContextMenu.ModernMenuItem()
        ModernPanel1 = New LakeUI.ModernPanel()
        HCL_自动HDR使用条件 = New LakeUI.HtmlColorLabel()
        MB_自动HDR调整与读数 = New LakeUI.ModernButton()
        MCK_启用视频自动HDR = New LakeUI.ModernCheckBox()
        HCL_自动HDR说明 = New LakeUI.HtmlColorLabel()
        HCL_超分使用条件 = New LakeUI.HtmlColorLabel()
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
        ModernPanel1.Controls.Add(HCL_自动HDR使用条件)
        ModernPanel1.Controls.Add(MB_自动HDR调整与读数)
        ModernPanel1.Controls.Add(MCK_启用视频自动HDR)
        ModernPanel1.Controls.Add(HCL_自动HDR说明)
        ModernPanel1.Controls.Add(HCL_超分使用条件)
        ModernPanel1.Controls.Add(MB_调整与读数)
        ModernPanel1.Controls.Add(MCK_启用RTX视频超分辨率)
        ModernPanel1.Controls.Add(HCL_说明)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(15, 20, 20, 20)
        ModernPanel1.Size = New Size(608, 432)
        ModernPanel1.TabIndex = 0
        ' 
        ' HCL_自动HDR使用条件
        ' 
        HCL_自动HDR使用条件.AutoSize = True
        HCL_自动HDR使用条件.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_自动HDR使用条件.BackColor = Color.Transparent
        HCL_自动HDR使用条件.Dock = DockStyle.Top
        HCL_自动HDR使用条件.Location = New Point(15, 265)
        HCL_自动HDR使用条件.Margin = New Padding(2)
        HCL_自动HDR使用条件.Name = "HCL_自动HDR使用条件"
        HCL_自动HDR使用条件.Padding = New Padding(0, 10, 0, 0)
        HCL_自动HDR使用条件.Size = New Size(573, 27)
        HCL_自动HDR使用条件.TabIndex = 53
        HCL_自动HDR使用条件.Text = "自动 HDR 适用于 SDR 视频，需要 10 位 HDR 输出。"
        ' 
        ' MB_自动HDR调整与读数
        ' 
        MB_自动HDR调整与读数.BackColor = Color.Transparent
        MB_自动HDR调整与读数.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_自动HDR调整与读数.BorderRadius = 10
        MB_自动HDR调整与读数.BorderSize = 0
        MB_自动HDR调整与读数.Dock = DockStyle.Top
        MB_自动HDR调整与读数.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_自动HDR调整与读数.Location = New Point(15, 231)
        MB_自动HDR调整与读数.Margin = New Padding(2)
        MB_自动HDR调整与读数.Name = "MB_自动HDR调整与读数"
        MB_自动HDR调整与读数.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_自动HDR调整与读数.Size = New Size(573, 34)
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
        MCK_启用视频自动HDR.Location = New Point(15, 197)
        MCK_启用视频自动HDR.Name = "MCK_启用视频自动HDR"
        MCK_启用视频自动HDR.Padding = New Padding(0, 0, 0, 10)
        MCK_启用视频自动HDR.Size = New Size(573, 34)
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
        HCL_自动HDR说明.Location = New Point(15, 146)
        HCL_自动HDR说明.Margin = New Padding(2)
        HCL_自动HDR说明.Name = "HCL_自动HDR说明"
        HCL_自动HDR说明.Padding = New Padding(0, 20, 0, 10)
        HCL_自动HDR说明.Size = New Size(573, 51)
        HCL_自动HDR说明.TabIndex = 50
        HCL_自动HDR说明.Text = "<span style=""font-size:12; color:Silver"">RTX 视频增强：HDR</span>   在 NVIDIA APP 中调整详细设置"
        ' 
        ' HCL_超分使用条件
        ' 
        HCL_超分使用条件.AutoSize = True
        HCL_超分使用条件.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_超分使用条件.BackColor = Color.Transparent
        HCL_超分使用条件.Dock = DockStyle.Top
        HCL_超分使用条件.Location = New Point(15, 119)
        HCL_超分使用条件.Margin = New Padding(2)
        HCL_超分使用条件.Name = "HCL_超分使用条件"
        HCL_超分使用条件.Padding = New Padding(0, 10, 0, 0)
        HCL_超分使用条件.Size = New Size(573, 27)
        HCL_超分使用条件.TabIndex = 43
        HCL_超分使用条件.Text = "超分辨率是否生效取决于源视频尺寸、像素格式和驱动支持。"
        ' 
        ' MB_调整与读数
        ' 
        MB_调整与读数.BackColor = Color.Transparent
        MB_调整与读数.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_调整与读数.BorderRadius = 10
        MB_调整与读数.BorderSize = 0
        MB_调整与读数.Dock = DockStyle.Top
        MB_调整与读数.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_调整与读数.Location = New Point(15, 85)
        MB_调整与读数.Margin = New Padding(2)
        MB_调整与读数.Name = "MB_调整与读数"
        MB_调整与读数.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_调整与读数.Size = New Size(573, 34)
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
        MCK_启用RTX视频超分辨率.Location = New Point(15, 51)
        MCK_启用RTX视频超分辨率.Name = "MCK_启用RTX视频超分辨率"
        MCK_启用RTX视频超分辨率.Padding = New Padding(0, 0, 0, 10)
        MCK_启用RTX视频超分辨率.Size = New Size(573, 34)
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
        HCL_说明.Location = New Point(15, 20)
        HCL_说明.Margin = New Padding(2)
        HCL_说明.Name = "HCL_说明"
        HCL_说明.Padding = New Padding(0, 0, 0, 10)
        HCL_说明.Size = New Size(573, 31)
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
        MCM_VSR调整.IconSize = 0
        MCM_VSR调整.MenuFont = New Font("Microsoft YaHei UI", 10F)
        MCM_VSR读数.IconSize = 0
        MCM_VSR读数.MenuFont = New Font("Microsoft YaHei UI", 10F)
        MI_VSR切换.Text = "请求超分辨率"
        MI_VSR重探.Text = "清除驱动拒绝状态并重新探测"
        MI_VSR实时状态.Text = "状态：会话未就绪"
        MI_VSR读数子菜单.SubMenu = MCM_VSR读数
        MI_VSR读数子菜单.Text = "内核读数与使用条件"
        MI_VSR驱动说明.Text = "画质档位与驱动总开关由 NVIDIA App / 控制面板管理"
        MI_VSR使用条件.Text = "条件：源高度 360–1440、NV12/P010、非隔行、最高 10 位、解码器表面"
        MI_VSR效果说明.Text = "驱动接受请求不代表画面一定已增强"
        MI_VSR实时状态.IsDescription = True
        MCM_VSR读数.Items.Add(MI_VSR实时状态)
        MI_VSR驱动说明.IsDescription = True
        MCM_VSR读数.Items.Add(MI_VSR驱动说明)
        MI_VSR使用条件.IsDescription = True
        MCM_VSR读数.Items.Add(MI_VSR使用条件)
        MI_VSR效果说明.IsDescription = True
        MCM_VSR读数.Items.Add(MI_VSR效果说明)
        MCM_VSR调整.Items.Add(MI_VSR切换)
        MCM_VSR调整.Items.Add(MI_VSR重探)
        MCM_VSR调整.Items.Add(MI_VSR读数子菜单)

        MCM_HDR调整.IconSize = 0
        MCM_HDR调整.MenuFont = New Font("Microsoft YaHei UI", 10F)
        MCM_HDR读数.IconSize = 0
        MCM_HDR读数.MenuFont = New Font("Microsoft YaHei UI", 10F)
        MI_HDR切换.Text = "请求自动 HDR"
        MI_HDR重探.Text = "清除驱动拒绝状态并重新探测"
        MI_HDR实时状态.Text = "状态：会话未就绪"
        MI_HDR读数子菜单.SubMenu = MCM_HDR读数
        MI_HDR读数子菜单.Text = "内核读数与使用条件"
        MI_HDR驱动说明.Text = "画质档位与驱动总开关由 NVIDIA App / 控制面板管理"
        MI_HDR使用条件.Text = "条件：NVIDIA 适配器、SDR 源、10 位 HDR 输出、非隔行"
        MI_HDR效果说明.Text = "对比模式与自动 HDR 互斥，实际转换由驱动决定"
        MI_HDR实时状态.IsDescription = True
        MCM_HDR读数.Items.Add(MI_HDR实时状态)
        MI_HDR驱动说明.IsDescription = True
        MCM_HDR读数.Items.Add(MI_HDR驱动说明)
        MI_HDR使用条件.IsDescription = True
        MCM_HDR读数.Items.Add(MI_HDR使用条件)
        MI_HDR效果说明.IsDescription = True
        MCM_HDR读数.Items.Add(MI_HDR效果说明)
        MCM_HDR调整.Items.Add(MI_HDR切换)
        MCM_HDR调整.Items.Add(MI_HDR重探)
        MCM_HDR调整.Items.Add(MI_HDR读数子菜单)
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents HCL_说明 As LakeUI.HtmlColorLabel
    Friend WithEvents MCK_启用RTX视频超分辨率 As LakeUI.ModernCheckBox
    Friend WithEvents MB_调整与读数 As LakeUI.ModernButton
    Friend WithEvents HCL_超分使用条件 As LakeUI.HtmlColorLabel
    Friend WithEvents HCL_自动HDR说明 As LakeUI.HtmlColorLabel
    Friend WithEvents MCK_启用视频自动HDR As LakeUI.ModernCheckBox
    Friend WithEvents MB_自动HDR调整与读数 As LakeUI.ModernButton
    Friend WithEvents HCL_自动HDR使用条件 As LakeUI.HtmlColorLabel
    Friend WithEvents MCM_VSR调整 As LakeUI.ModernContextMenu
    Friend WithEvents MCM_VSR读数 As LakeUI.ModernContextMenu
    Friend WithEvents MI_VSR切换 As LakeUI.ModernContextMenu.ModernMenuItem
    Friend WithEvents MI_VSR重探 As LakeUI.ModernContextMenu.ModernMenuItem
    Friend WithEvents MI_VSR实时状态 As LakeUI.ModernContextMenu.ModernMenuItem
    Friend WithEvents MCM_HDR调整 As LakeUI.ModernContextMenu
    Friend WithEvents MCM_HDR读数 As LakeUI.ModernContextMenu
    Friend WithEvents MI_HDR切换 As LakeUI.ModernContextMenu.ModernMenuItem
    Friend WithEvents MI_HDR重探 As LakeUI.ModernContextMenu.ModernMenuItem
    Friend WithEvents MI_HDR实时状态 As LakeUI.ModernContextMenu.ModernMenuItem
End Class
