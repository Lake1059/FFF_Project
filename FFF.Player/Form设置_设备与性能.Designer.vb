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
        ModernPanel6 = New LakeUI.ModernPanel()
        MCB_音频直通 = New LakeUI.ModernComboBox()
        HtmlColorLabel2 = New LakeUI.HtmlColorLabel()
        ModernPanel7 = New LakeUI.ModernPanel()
        MCB_LakeUI抗锯齿 = New LakeUI.ModernComboBox()
        ModernPanel8 = New LakeUI.ModernPanel()
        MCB_LakeUI文字渲染 = New LakeUI.ModernComboBox()
        ModernPanel9 = New LakeUI.ModernPanel()
        MCB_LakeUI超采样 = New LakeUI.ModernComboBox()
        ModernPanel10 = New LakeUI.ModernPanel()
        MCB_LakeUI_GPU缓存 = New LakeUI.ModernComboBox()
        ModernPanel11 = New LakeUI.ModernPanel()
        MCB_LakeUI_画刷缓存 = New LakeUI.ModernComboBox()
        ModernPanel12 = New LakeUI.ModernPanel()
        MCB_LakeUI_字体缓存 = New LakeUI.ModernComboBox()
        ModernPanel13 = New LakeUI.ModernPanel()
        MCB_LakeUI_CPU缓存 = New LakeUI.ModernComboBox()
        ModernPanel14 = New LakeUI.ModernPanel()
        MCB_LakeUI_背景脏区 = New LakeUI.ModernComboBox()
        ModernPanel15 = New LakeUI.ModernPanel()
        MCB_LakeUI_SSAA分桶 = New LakeUI.ModernComboBox()
        ModernPanel16 = New LakeUI.ModernPanel()
        MCB_LakeUI动画帧率 = New LakeUI.ModernComboBox()
        HtmlColorLabel3 = New LakeUI.HtmlColorLabel()
        ModernPanel17 = New LakeUI.ModernPanel()
        MCB_LakeUI缓存清理 = New LakeUI.ModernComboBox()
        ModernPanel1 = New LakeUI.ModernPanel()
        ModernPanel5 = New LakeUI.ModernPanel()
        MCB_音频效果 = New LakeUI.ModernComboBox()
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
        ModernPanel6.SuspendLayout()
        ModernPanel7.SuspendLayout()
        ModernPanel8.SuspendLayout()
        ModernPanel9.SuspendLayout()
        ModernPanel10.SuspendLayout()
        ModernPanel11.SuspendLayout()
        ModernPanel12.SuspendLayout()
        ModernPanel13.SuspendLayout()
        ModernPanel14.SuspendLayout()
        ModernPanel15.SuspendLayout()
        ModernPanel16.SuspendLayout()
        ModernPanel17.SuspendLayout()
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
        ' Dock=Top 的停靠次序与 Controls.Add 相反（后加者靠上）：ModernPanel17（清理）
        ' 最先加，因此排在最底。2026-11-11 新增的 LakeUI 图形性能组（说明标签 +
        ' 十个下拉 + 清理说明 + 清理下拉）整体位于既有「音频直通」之下。
        ModernPanel1.Controls.Add(ModernPanel17)
        ModernPanel1.Controls.Add(HtmlColorLabel3)
        ModernPanel1.Controls.Add(ModernPanel16)
        ModernPanel1.Controls.Add(ModernPanel15)
        ModernPanel1.Controls.Add(ModernPanel14)
        ModernPanel1.Controls.Add(ModernPanel13)
        ModernPanel1.Controls.Add(ModernPanel12)
        ModernPanel1.Controls.Add(ModernPanel11)
        ModernPanel1.Controls.Add(ModernPanel10)
        ModernPanel1.Controls.Add(ModernPanel9)
        ModernPanel1.Controls.Add(ModernPanel8)
        ModernPanel1.Controls.Add(ModernPanel7)
        ModernPanel1.Controls.Add(HtmlColorLabel2)
        ModernPanel1.Controls.Add(ModernPanel6)
        ModernPanel1.Controls.Add(ModernPanel5)
        ModernPanel1.Controls.Add(ModernPanel4)
        ModernPanel1.Controls.Add(ModernPanel3)
        ModernPanel1.Controls.Add(ModernPanel2)
        ModernPanel1.Controls.Add(HtmlColorLabel1)
        ModernPanel1.Controls.Add(Panel1)
        ModernPanel1.Padding = New Padding(15, 20, 20, 20)
        ' 这一页有 835 逻辑像素高，而设置窗口客户区只有 493 —— 与 Form设置_AI字幕
        ' 同款处理：根面板不能用 Dock.Fill（Dock 的控件不参与 AutoScroll 滚动，
        ' 会被钉在边上），改成绝对定位 + 保留设计高度，窗体 AutoScroll 出滚动条；
        ' 宽度跟随容器（见 Form设置_设备与性能.vb 的 对齐面板宽度）。
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Size = New Size(652, 835)
        ModernPanel1.TabIndex = 1
        ' 
        ' ModernPanel5
        ' 
        ModernPanel5.BackColor = Color.Transparent
        ModernPanel5.BackColor1 = Color.Transparent
        ModernPanel5.BorderSize = 0
        ModernPanel5.Controls.Add(MCB_音频效果)
        ModernPanel5.Dock = DockStyle.Top
        ModernPanel5.Location = New Point(15, 215)
        ModernPanel5.Name = "ModernPanel5"
        ModernPanel5.Padding = New Padding(0, 10, 0, 0)
        ModernPanel5.Size = New Size(617, 42)
        ModernPanel5.TabIndex = 22
        ' 
        ' MCB_音频效果
        ' 
        MCB_音频效果.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.BorderRadius = 10
        MCB_音频效果.BorderSize = 0
        MCB_音频效果.Dock = DockStyle.Left
        MCB_音频效果.DropDownBackdropBlurPasses = 2
        MCB_音频效果.DropDownBackdropBlurRadius = 30
        MCB_音频效果.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_音频效果.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_音频效果.DropDownPadding = New Padding(10)
        MCB_音频效果.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.DropDownSelectedForeColor = Color.White
        MCB_音频效果.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.Items.Add("无")
        MCB_音频效果.Items.Add("自动 HRTF 空间音频")
        MCB_音频效果.Items.Add("单声道音频")
        MCB_音频效果.Location = New Point(0, 10)
        MCB_音频效果.Margin = New Padding(2, 2, 2, 2)
        MCB_音频效果.MaxDropDownItems = 16
        MCB_音频效果.Name = "MCB_音频效果"
        MCB_音频效果.Padding = New Padding(10, 0, 10, 0)
        MCB_音频效果.PrefixForeColor = Color.YellowGreen
        MCB_音频效果.PrefixGap = 10
        MCB_音频效果.PrefixText = "音频效果"
        MCB_音频效果.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频效果.Size = New Size(300, 32)
        MCB_音频效果.TabIndex = 0
        MCB_音频效果.ToolTipGap = -1
        MCB_音频效果.ToolTipMaxWidth = 350
        MCB_音频效果.ToolTipPadding = New Padding(15)
        MCB_音频效果.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' ModernPanel6
        ' 
        ModernPanel6.BackColor = Color.Transparent
        ModernPanel6.BackColor1 = Color.Transparent
        ModernPanel6.BorderSize = 0
        ModernPanel6.Controls.Add(MCB_音频直通)
        ModernPanel6.Dock = DockStyle.Top
        ModernPanel6.Location = New Point(15, 257)
        ModernPanel6.Name = "ModernPanel6"
        ModernPanel6.Padding = New Padding(0, 10, 0, 0)
        ModernPanel6.Size = New Size(617, 42)
        ModernPanel6.TabIndex = 23
        ' 
        ' MCB_音频直通
        ' 
        MCB_音频直通.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.BorderRadius = 10
        MCB_音频直通.BorderSize = 0
        MCB_音频直通.Dock = DockStyle.Left
        MCB_音频直通.DropDownBackdropBlurPasses = 2
        MCB_音频直通.DropDownBackdropBlurRadius = 30
        MCB_音频直通.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_音频直通.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_音频直通.DropDownPadding = New Padding(10)
        MCB_音频直通.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.DropDownSelectedForeColor = Color.White
        MCB_音频直通.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.Items.Add("自动（支持时源码直通）")
        MCB_音频直通.Items.Add("强制（失败时显式提示）")
        MCB_音频直通.Items.Add("关")
        MCB_音频直通.Location = New Point(0, 10)
        MCB_音频直通.Margin = New Padding(2, 2, 2, 2)
        MCB_音频直通.MaxDropDownItems = 16
        MCB_音频直通.Name = "MCB_音频直通"
        MCB_音频直通.Padding = New Padding(10, 0, 10, 0)
        MCB_音频直通.PrefixForeColor = Color.YellowGreen
        MCB_音频直通.PrefixGap = 10
        MCB_音频直通.PrefixText = "音频源码直通"
        MCB_音频直通.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_音频直通.Size = New Size(300, 32)
        MCB_音频直通.TabIndex = 0
        MCB_音频直通.ToolTipGap = -1
        MCB_音频直通.ToolTipMaxWidth = 350
        MCB_音频直通.ToolTipPadding = New Padding(15)
        MCB_音频直通.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' HtmlColorLabel2
        '
        HtmlColorLabel2.AutoSize = True
        HtmlColorLabel2.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel2.Dock = DockStyle.Top
        HtmlColorLabel2.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel2.Location = New Point(15, 299)
        HtmlColorLabel2.Margin = New Padding(2)
        HtmlColorLabel2.Name = "HtmlColorLabel2"
        HtmlColorLabel2.Padding = New Padding(0, 0, 0, 10)
        HtmlColorLabel2.Size = New Size(617, 27)
        HtmlColorLabel2.TabIndex = 31
        HtmlColorLabel2.Text = "LakeUI 呈现引擎（作用于整个进程的界面渲染）：以下选项改后即时生效，无需重启；两个缓存预算按进程总量约束"
        '
        ' ModernPanel7 / MCB_LakeUI抗锯齿
        '
        ModernPanel7.BackColor = Color.Transparent
        ModernPanel7.BackColor1 = Color.Transparent
        ModernPanel7.BorderSize = 0
        ModernPanel7.Controls.Add(MCB_LakeUI抗锯齿)
        ModernPanel7.Dock = DockStyle.Top
        ModernPanel7.Location = New Point(15, 326)
        ModernPanel7.Name = "ModernPanel7"
        ModernPanel7.Padding = New Padding(0, 10, 0, 0)
        ModernPanel7.Size = New Size(617, 42)
        ModernPanel7.TabIndex = 24
        MCB_LakeUI抗锯齿.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI抗锯齿.BorderRadius = 10
        MCB_LakeUI抗锯齿.BorderSize = 0
        MCB_LakeUI抗锯齿.Dock = DockStyle.Left
        MCB_LakeUI抗锯齿.DropDownBackdropBlurPasses = 2
        MCB_LakeUI抗锯齿.DropDownBackdropBlurRadius = 30
        MCB_LakeUI抗锯齿.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI抗锯齿.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI抗锯齿.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI抗锯齿.DropDownPadding = New Padding(10)
        MCB_LakeUI抗锯齿.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI抗锯齿.DropDownSelectedForeColor = Color.White
        MCB_LakeUI抗锯齿.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI抗锯齿.Items.Add("启用（默认）")
        MCB_LakeUI抗锯齿.Items.Add("禁用（Aliased，最快）")
        MCB_LakeUI抗锯齿.Location = New Point(0, 10)
        MCB_LakeUI抗锯齿.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI抗锯齿.MaxDropDownItems = 16
        MCB_LakeUI抗锯齿.Name = "MCB_LakeUI抗锯齿"
        MCB_LakeUI抗锯齿.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI抗锯齿.PrefixForeColor = Color.Gold
        MCB_LakeUI抗锯齿.PrefixGap = 10
        MCB_LakeUI抗锯齿.PrefixText = "LakeUI 抗锯齿"
        MCB_LakeUI抗锯齿.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI抗锯齿.Size = New Size(300, 32)
        MCB_LakeUI抗锯齿.TabIndex = 0
        MCB_LakeUI抗锯齿.ToolTipGap = -1
        MCB_LakeUI抗锯齿.ToolTipMaxWidth = 350
        MCB_LakeUI抗锯齿.ToolTipPadding = New Padding(15)
        MCB_LakeUI抗锯齿.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel8 / MCB_LakeUI文字渲染
        '
        ModernPanel8.BackColor = Color.Transparent
        ModernPanel8.BackColor1 = Color.Transparent
        ModernPanel8.BorderSize = 0
        ModernPanel8.Controls.Add(MCB_LakeUI文字渲染)
        ModernPanel8.Dock = DockStyle.Top
        ModernPanel8.Location = New Point(15, 368)
        ModernPanel8.Name = "ModernPanel8"
        ModernPanel8.Padding = New Padding(0, 10, 0, 0)
        ModernPanel8.Size = New Size(617, 42)
        ModernPanel8.TabIndex = 25
        MCB_LakeUI文字渲染.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI文字渲染.BorderRadius = 10
        MCB_LakeUI文字渲染.BorderSize = 0
        MCB_LakeUI文字渲染.Dock = DockStyle.Left
        MCB_LakeUI文字渲染.DropDownBackdropBlurPasses = 2
        MCB_LakeUI文字渲染.DropDownBackdropBlurRadius = 30
        MCB_LakeUI文字渲染.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI文字渲染.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI文字渲染.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI文字渲染.DropDownPadding = New Padding(10)
        MCB_LakeUI文字渲染.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI文字渲染.DropDownSelectedForeColor = Color.White
        MCB_LakeUI文字渲染.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI文字渲染.Items.Add("ClearType")
        MCB_LakeUI文字渲染.Items.Add("灰度")
        MCB_LakeUI文字渲染.Items.Add("无抗锯齿（最快）")
        MCB_LakeUI文字渲染.Items.Add("矢量几何 Outline（当前默认）")
        MCB_LakeUI文字渲染.Location = New Point(0, 10)
        MCB_LakeUI文字渲染.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI文字渲染.MaxDropDownItems = 16
        MCB_LakeUI文字渲染.Name = "MCB_LakeUI文字渲染"
        MCB_LakeUI文字渲染.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI文字渲染.PrefixForeColor = Color.Gold
        MCB_LakeUI文字渲染.PrefixGap = 10
        MCB_LakeUI文字渲染.PrefixText = "LakeUI 文字渲染"
        MCB_LakeUI文字渲染.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI文字渲染.Size = New Size(300, 32)
        MCB_LakeUI文字渲染.TabIndex = 0
        MCB_LakeUI文字渲染.ToolTipGap = -1
        MCB_LakeUI文字渲染.ToolTipMaxWidth = 350
        MCB_LakeUI文字渲染.ToolTipPadding = New Padding(15)
        MCB_LakeUI文字渲染.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel9 / MCB_LakeUI超采样
        '
        ModernPanel9.BackColor = Color.Transparent
        ModernPanel9.BackColor1 = Color.Transparent
        ModernPanel9.BorderSize = 0
        ModernPanel9.Controls.Add(MCB_LakeUI超采样)
        ModernPanel9.Dock = DockStyle.Top
        ModernPanel9.Location = New Point(15, 410)
        ModernPanel9.Name = "ModernPanel9"
        ModernPanel9.Padding = New Padding(0, 10, 0, 0)
        ModernPanel9.Size = New Size(617, 42)
        ModernPanel9.TabIndex = 26
        MCB_LakeUI超采样.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI超采样.BorderRadius = 10
        MCB_LakeUI超采样.BorderSize = 0
        MCB_LakeUI超采样.Dock = DockStyle.Left
        MCB_LakeUI超采样.DropDownBackdropBlurPasses = 2
        MCB_LakeUI超采样.DropDownBackdropBlurRadius = 30
        MCB_LakeUI超采样.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI超采样.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI超采样.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI超采样.DropDownPadding = New Padding(10)
        MCB_LakeUI超采样.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI超采样.DropDownSelectedForeColor = Color.White
        MCB_LakeUI超采样.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI超采样.Items.Add("关（默认）")
        MCB_LakeUI超采样.Items.Add("x2")
        MCB_LakeUI超采样.Items.Add("x3")
        MCB_LakeUI超采样.Items.Add("x4")
        MCB_LakeUI超采样.Location = New Point(0, 10)
        MCB_LakeUI超采样.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI超采样.MaxDropDownItems = 16
        MCB_LakeUI超采样.Name = "MCB_LakeUI超采样"
        MCB_LakeUI超采样.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI超采样.PrefixForeColor = Color.Gold
        MCB_LakeUI超采样.PrefixGap = 10
        MCB_LakeUI超采样.PrefixText = "LakeUI 超采样"
        MCB_LakeUI超采样.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI超采样.Size = New Size(300, 32)
        MCB_LakeUI超采样.TabIndex = 0
        MCB_LakeUI超采样.ToolTipGap = -1
        MCB_LakeUI超采样.ToolTipMaxWidth = 350
        MCB_LakeUI超采样.ToolTipPadding = New Padding(15)
        MCB_LakeUI超采样.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel10 / MCB_LakeUI_GPU缓存
        '
        ModernPanel10.BackColor = Color.Transparent
        ModernPanel10.BackColor1 = Color.Transparent
        ModernPanel10.BorderSize = 0
        ModernPanel10.Controls.Add(MCB_LakeUI_GPU缓存)
        ModernPanel10.Dock = DockStyle.Top
        ModernPanel10.Location = New Point(15, 452)
        ModernPanel10.Name = "ModernPanel10"
        ModernPanel10.Padding = New Padding(0, 10, 0, 0)
        ModernPanel10.Size = New Size(617, 42)
        ModernPanel10.TabIndex = 27
        MCB_LakeUI_GPU缓存.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_GPU缓存.BorderRadius = 10
        MCB_LakeUI_GPU缓存.BorderSize = 0
        MCB_LakeUI_GPU缓存.Dock = DockStyle.Left
        MCB_LakeUI_GPU缓存.DropDownBackdropBlurPasses = 2
        MCB_LakeUI_GPU缓存.DropDownBackdropBlurRadius = 30
        MCB_LakeUI_GPU缓存.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI_GPU缓存.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_GPU缓存.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI_GPU缓存.DropDownPadding = New Padding(10)
        MCB_LakeUI_GPU缓存.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_GPU缓存.DropDownSelectedForeColor = Color.White
        MCB_LakeUI_GPU缓存.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_GPU缓存.Items.Add("禁用")
        MCB_LakeUI_GPU缓存.Items.Add("64 MiB")
        MCB_LakeUI_GPU缓存.Items.Add("128 MiB（默认）")
        MCB_LakeUI_GPU缓存.Items.Add("256 MiB")
        MCB_LakeUI_GPU缓存.Items.Add("512 MiB")
        MCB_LakeUI_GPU缓存.Items.Add("1 GiB")
        MCB_LakeUI_GPU缓存.Items.Add("2 GiB")
        MCB_LakeUI_GPU缓存.Items.Add("4 GiB")
        MCB_LakeUI_GPU缓存.Items.Add("8 GiB")
        MCB_LakeUI_GPU缓存.Location = New Point(0, 10)
        MCB_LakeUI_GPU缓存.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI_GPU缓存.MaxDropDownItems = 16
        MCB_LakeUI_GPU缓存.Name = "MCB_LakeUI_GPU缓存"
        MCB_LakeUI_GPU缓存.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI_GPU缓存.PrefixForeColor = Color.Gold
        MCB_LakeUI_GPU缓存.PrefixGap = 10
        MCB_LakeUI_GPU缓存.PrefixText = "GPU 缓存预算"
        MCB_LakeUI_GPU缓存.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_GPU缓存.Size = New Size(300, 32)
        MCB_LakeUI_GPU缓存.TabIndex = 0
        MCB_LakeUI_GPU缓存.ToolTipGap = -1
        MCB_LakeUI_GPU缓存.ToolTipMaxWidth = 350
        MCB_LakeUI_GPU缓存.ToolTipPadding = New Padding(15)
        MCB_LakeUI_GPU缓存.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel11 / MCB_LakeUI_画刷缓存
        '
        ModernPanel11.BackColor = Color.Transparent
        ModernPanel11.BackColor1 = Color.Transparent
        ModernPanel11.BorderSize = 0
        ModernPanel11.Controls.Add(MCB_LakeUI_画刷缓存)
        ModernPanel11.Dock = DockStyle.Top
        ModernPanel11.Location = New Point(15, 494)
        ModernPanel11.Name = "ModernPanel11"
        ModernPanel11.Padding = New Padding(0, 10, 0, 0)
        ModernPanel11.Size = New Size(617, 42)
        ModernPanel11.TabIndex = 28
        MCB_LakeUI_画刷缓存.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_画刷缓存.BorderRadius = 10
        MCB_LakeUI_画刷缓存.BorderSize = 0
        MCB_LakeUI_画刷缓存.Dock = DockStyle.Left
        MCB_LakeUI_画刷缓存.DropDownBackdropBlurPasses = 2
        MCB_LakeUI_画刷缓存.DropDownBackdropBlurRadius = 30
        MCB_LakeUI_画刷缓存.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI_画刷缓存.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_画刷缓存.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI_画刷缓存.DropDownPadding = New Padding(10)
        MCB_LakeUI_画刷缓存.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_画刷缓存.DropDownSelectedForeColor = Color.White
        MCB_LakeUI_画刷缓存.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_画刷缓存.Items.Add("禁用")
        MCB_LakeUI_画刷缓存.Items.Add("8")
        MCB_LakeUI_画刷缓存.Items.Add("16")
        MCB_LakeUI_画刷缓存.Items.Add("32")
        MCB_LakeUI_画刷缓存.Items.Add("64")
        MCB_LakeUI_画刷缓存.Items.Add("128")
        MCB_LakeUI_画刷缓存.Items.Add("256（默认）")
        MCB_LakeUI_画刷缓存.Items.Add("512")
        MCB_LakeUI_画刷缓存.Items.Add("1024")
        MCB_LakeUI_画刷缓存.Location = New Point(0, 10)
        MCB_LakeUI_画刷缓存.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI_画刷缓存.MaxDropDownItems = 16
        MCB_LakeUI_画刷缓存.Name = "MCB_LakeUI_画刷缓存"
        MCB_LakeUI_画刷缓存.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI_画刷缓存.PrefixForeColor = Color.Gold
        MCB_LakeUI_画刷缓存.PrefixGap = 10
        MCB_LakeUI_画刷缓存.PrefixText = "画刷缓存条目"
        MCB_LakeUI_画刷缓存.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_画刷缓存.Size = New Size(300, 32)
        MCB_LakeUI_画刷缓存.TabIndex = 0
        MCB_LakeUI_画刷缓存.ToolTipGap = -1
        MCB_LakeUI_画刷缓存.ToolTipMaxWidth = 350
        MCB_LakeUI_画刷缓存.ToolTipPadding = New Padding(15)
        MCB_LakeUI_画刷缓存.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel12 / MCB_LakeUI_字体缓存
        '
        ModernPanel12.BackColor = Color.Transparent
        ModernPanel12.BackColor1 = Color.Transparent
        ModernPanel12.BorderSize = 0
        ModernPanel12.Controls.Add(MCB_LakeUI_字体缓存)
        ModernPanel12.Dock = DockStyle.Top
        ModernPanel12.Location = New Point(15, 536)
        ModernPanel12.Name = "ModernPanel12"
        ModernPanel12.Padding = New Padding(0, 10, 0, 0)
        ModernPanel12.Size = New Size(617, 42)
        ModernPanel12.TabIndex = 29
        MCB_LakeUI_字体缓存.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_字体缓存.BorderRadius = 10
        MCB_LakeUI_字体缓存.BorderSize = 0
        MCB_LakeUI_字体缓存.Dock = DockStyle.Left
        MCB_LakeUI_字体缓存.DropDownBackdropBlurPasses = 2
        MCB_LakeUI_字体缓存.DropDownBackdropBlurRadius = 30
        MCB_LakeUI_字体缓存.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI_字体缓存.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_字体缓存.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI_字体缓存.DropDownPadding = New Padding(10)
        MCB_LakeUI_字体缓存.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_字体缓存.DropDownSelectedForeColor = Color.White
        MCB_LakeUI_字体缓存.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_字体缓存.Items.Add("禁用")
        MCB_LakeUI_字体缓存.Items.Add("32")
        MCB_LakeUI_字体缓存.Items.Add("64")
        MCB_LakeUI_字体缓存.Items.Add("128（默认）")
        MCB_LakeUI_字体缓存.Items.Add("256")
        MCB_LakeUI_字体缓存.Items.Add("512")
        MCB_LakeUI_字体缓存.Items.Add("1024")
        MCB_LakeUI_字体缓存.Location = New Point(0, 10)
        MCB_LakeUI_字体缓存.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI_字体缓存.MaxDropDownItems = 16
        MCB_LakeUI_字体缓存.Name = "MCB_LakeUI_字体缓存"
        MCB_LakeUI_字体缓存.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI_字体缓存.PrefixForeColor = Color.Gold
        MCB_LakeUI_字体缓存.PrefixGap = 10
        MCB_LakeUI_字体缓存.PrefixText = "字体缓存条目"
        MCB_LakeUI_字体缓存.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_字体缓存.Size = New Size(300, 32)
        MCB_LakeUI_字体缓存.TabIndex = 0
        MCB_LakeUI_字体缓存.ToolTipGap = -1
        MCB_LakeUI_字体缓存.ToolTipMaxWidth = 350
        MCB_LakeUI_字体缓存.ToolTipPadding = New Padding(15)
        MCB_LakeUI_字体缓存.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel13 / MCB_LakeUI_CPU缓存
        '
        ModernPanel13.BackColor = Color.Transparent
        ModernPanel13.BackColor1 = Color.Transparent
        ModernPanel13.BorderSize = 0
        ModernPanel13.Controls.Add(MCB_LakeUI_CPU缓存)
        ModernPanel13.Dock = DockStyle.Top
        ModernPanel13.Location = New Point(15, 578)
        ModernPanel13.Name = "ModernPanel13"
        ModernPanel13.Padding = New Padding(0, 10, 0, 0)
        ModernPanel13.Size = New Size(617, 42)
        ModernPanel13.TabIndex = 30
        MCB_LakeUI_CPU缓存.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_CPU缓存.BorderRadius = 10
        MCB_LakeUI_CPU缓存.BorderSize = 0
        MCB_LakeUI_CPU缓存.Dock = DockStyle.Left
        MCB_LakeUI_CPU缓存.DropDownBackdropBlurPasses = 2
        MCB_LakeUI_CPU缓存.DropDownBackdropBlurRadius = 30
        MCB_LakeUI_CPU缓存.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI_CPU缓存.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_CPU缓存.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI_CPU缓存.DropDownPadding = New Padding(10)
        MCB_LakeUI_CPU缓存.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_CPU缓存.DropDownSelectedForeColor = Color.White
        MCB_LakeUI_CPU缓存.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_CPU缓存.Items.Add("禁用")
        MCB_LakeUI_CPU缓存.Items.Add("32 MiB")
        MCB_LakeUI_CPU缓存.Items.Add("64 MiB")
        MCB_LakeUI_CPU缓存.Items.Add("96 MiB（默认）")
        MCB_LakeUI_CPU缓存.Items.Add("128 MiB")
        MCB_LakeUI_CPU缓存.Items.Add("256 MiB")
        MCB_LakeUI_CPU缓存.Items.Add("512 MiB")
        MCB_LakeUI_CPU缓存.Items.Add("1 GiB")
        MCB_LakeUI_CPU缓存.Items.Add("2 GiB")
        MCB_LakeUI_CPU缓存.Location = New Point(0, 10)
        MCB_LakeUI_CPU缓存.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI_CPU缓存.MaxDropDownItems = 16
        MCB_LakeUI_CPU缓存.Name = "MCB_LakeUI_CPU缓存"
        MCB_LakeUI_CPU缓存.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI_CPU缓存.PrefixForeColor = Color.Gold
        MCB_LakeUI_CPU缓存.PrefixGap = 10
        MCB_LakeUI_CPU缓存.PrefixText = "CPU 缓存预算"
        MCB_LakeUI_CPU缓存.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_CPU缓存.Size = New Size(300, 32)
        MCB_LakeUI_CPU缓存.TabIndex = 0
        MCB_LakeUI_CPU缓存.ToolTipGap = -1
        MCB_LakeUI_CPU缓存.ToolTipMaxWidth = 350
        MCB_LakeUI_CPU缓存.ToolTipPadding = New Padding(15)
        MCB_LakeUI_CPU缓存.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel14 / MCB_LakeUI_背景脏区
        '
        ModernPanel14.BackColor = Color.Transparent
        ModernPanel14.BackColor1 = Color.Transparent
        ModernPanel14.BorderSize = 0
        ModernPanel14.Controls.Add(MCB_LakeUI_背景脏区)
        ModernPanel14.Dock = DockStyle.Top
        ModernPanel14.Location = New Point(15, 620)
        ModernPanel14.Name = "ModernPanel14"
        ModernPanel14.Padding = New Padding(0, 10, 0, 0)
        ModernPanel14.Size = New Size(617, 42)
        ModernPanel14.TabIndex = 32
        MCB_LakeUI_背景脏区.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_背景脏区.BorderRadius = 10
        MCB_LakeUI_背景脏区.BorderSize = 0
        MCB_LakeUI_背景脏区.Dock = DockStyle.Left
        MCB_LakeUI_背景脏区.DropDownBackdropBlurPasses = 2
        MCB_LakeUI_背景脏区.DropDownBackdropBlurRadius = 30
        MCB_LakeUI_背景脏区.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI_背景脏区.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_背景脏区.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI_背景脏区.DropDownPadding = New Padding(10)
        MCB_LakeUI_背景脏区.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_背景脏区.DropDownSelectedForeColor = Color.White
        MCB_LakeUI_背景脏区.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_背景脏区.Items.Add("4 区 / 40%")
        MCB_LakeUI_背景脏区.Items.Add("4 区 / 60%")
        MCB_LakeUI_背景脏区.Items.Add("4 区 / 80%")
        MCB_LakeUI_背景脏区.Items.Add("8 区 / 40%")
        MCB_LakeUI_背景脏区.Items.Add("8 区 / 60%（默认）")
        MCB_LakeUI_背景脏区.Items.Add("8 区 / 80%")
        MCB_LakeUI_背景脏区.Items.Add("16 区 / 40%")
        MCB_LakeUI_背景脏区.Items.Add("16 区 / 60%")
        MCB_LakeUI_背景脏区.Items.Add("16 区 / 80%")
        MCB_LakeUI_背景脏区.Location = New Point(0, 10)
        MCB_LakeUI_背景脏区.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI_背景脏区.MaxDropDownItems = 16
        MCB_LakeUI_背景脏区.Name = "MCB_LakeUI_背景脏区"
        MCB_LakeUI_背景脏区.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI_背景脏区.PrefixForeColor = Color.Gold
        MCB_LakeUI_背景脏区.PrefixGap = 10
        MCB_LakeUI_背景脏区.PrefixText = "背景脏区策略"
        MCB_LakeUI_背景脏区.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_背景脏区.Size = New Size(300, 32)
        MCB_LakeUI_背景脏区.TabIndex = 0
        MCB_LakeUI_背景脏区.ToolTipGap = -1
        MCB_LakeUI_背景脏区.ToolTipMaxWidth = 350
        MCB_LakeUI_背景脏区.ToolTipPadding = New Padding(15)
        MCB_LakeUI_背景脏区.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel15 / MCB_LakeUI_SSAA分桶
        '
        ModernPanel15.BackColor = Color.Transparent
        ModernPanel15.BackColor1 = Color.Transparent
        ModernPanel15.BorderSize = 0
        ModernPanel15.Controls.Add(MCB_LakeUI_SSAA分桶)
        ModernPanel15.Dock = DockStyle.Top
        ModernPanel15.Location = New Point(15, 662)
        ModernPanel15.Name = "ModernPanel15"
        ModernPanel15.Padding = New Padding(0, 10, 0, 0)
        ModernPanel15.Size = New Size(617, 42)
        ModernPanel15.TabIndex = 33
        MCB_LakeUI_SSAA分桶.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_SSAA分桶.BorderRadius = 10
        MCB_LakeUI_SSAA分桶.BorderSize = 0
        MCB_LakeUI_SSAA分桶.Dock = DockStyle.Left
        MCB_LakeUI_SSAA分桶.DropDownBackdropBlurPasses = 2
        MCB_LakeUI_SSAA分桶.DropDownBackdropBlurRadius = 30
        MCB_LakeUI_SSAA分桶.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI_SSAA分桶.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_SSAA分桶.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI_SSAA分桶.DropDownPadding = New Padding(10)
        MCB_LakeUI_SSAA分桶.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_SSAA分桶.DropDownSelectedForeColor = Color.White
        MCB_LakeUI_SSAA分桶.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_SSAA分桶.Items.Add("32 px")
        MCB_LakeUI_SSAA分桶.Items.Add("64 px（默认）")
        MCB_LakeUI_SSAA分桶.Items.Add("96 px")
        MCB_LakeUI_SSAA分桶.Items.Add("128 px")
        MCB_LakeUI_SSAA分桶.Items.Add("192 px")
        MCB_LakeUI_SSAA分桶.Items.Add("256 px")
        MCB_LakeUI_SSAA分桶.Items.Add("384 px")
        MCB_LakeUI_SSAA分桶.Items.Add("512 px")
        MCB_LakeUI_SSAA分桶.Items.Add("1024 px")
        MCB_LakeUI_SSAA分桶.Location = New Point(0, 10)
        MCB_LakeUI_SSAA分桶.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI_SSAA分桶.MaxDropDownItems = 16
        MCB_LakeUI_SSAA分桶.Name = "MCB_LakeUI_SSAA分桶"
        MCB_LakeUI_SSAA分桶.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI_SSAA分桶.PrefixForeColor = Color.Gold
        MCB_LakeUI_SSAA分桶.PrefixGap = 10
        MCB_LakeUI_SSAA分桶.PrefixText = "SSAA 分桶粒度"
        MCB_LakeUI_SSAA分桶.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI_SSAA分桶.Size = New Size(300, 32)
        MCB_LakeUI_SSAA分桶.TabIndex = 0
        MCB_LakeUI_SSAA分桶.ToolTipGap = -1
        MCB_LakeUI_SSAA分桶.ToolTipMaxWidth = 350
        MCB_LakeUI_SSAA分桶.ToolTipPadding = New Padding(15)
        MCB_LakeUI_SSAA分桶.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' ModernPanel16 / MCB_LakeUI动画帧率
        '
        ModernPanel16.BackColor = Color.Transparent
        ModernPanel16.BackColor1 = Color.Transparent
        ModernPanel16.BorderSize = 0
        ModernPanel16.Controls.Add(MCB_LakeUI动画帧率)
        ModernPanel16.Dock = DockStyle.Top
        ModernPanel16.Location = New Point(15, 704)
        ModernPanel16.Name = "ModernPanel16"
        ModernPanel16.Padding = New Padding(0, 10, 0, 0)
        ModernPanel16.Size = New Size(617, 42)
        ModernPanel16.TabIndex = 34
        MCB_LakeUI动画帧率.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI动画帧率.BorderRadius = 10
        MCB_LakeUI动画帧率.BorderSize = 0
        MCB_LakeUI动画帧率.Dock = DockStyle.Left
        MCB_LakeUI动画帧率.DropDownBackdropBlurPasses = 2
        MCB_LakeUI动画帧率.DropDownBackdropBlurRadius = 30
        MCB_LakeUI动画帧率.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI动画帧率.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI动画帧率.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI动画帧率.DropDownPadding = New Padding(10)
        MCB_LakeUI动画帧率.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI动画帧率.DropDownSelectedForeColor = Color.White
        MCB_LakeUI动画帧率.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI动画帧率.Items.Add("0（不限制）")
        MCB_LakeUI动画帧率.Items.Add("15")
        MCB_LakeUI动画帧率.Items.Add("30")
        MCB_LakeUI动画帧率.Items.Add("45")
        MCB_LakeUI动画帧率.Items.Add("50")
        MCB_LakeUI动画帧率.Items.Add("60（默认）")
        MCB_LakeUI动画帧率.Items.Add("90")
        MCB_LakeUI动画帧率.Items.Add("120")
        MCB_LakeUI动画帧率.Location = New Point(0, 10)
        MCB_LakeUI动画帧率.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI动画帧率.MaxDropDownItems = 16
        MCB_LakeUI动画帧率.Name = "MCB_LakeUI动画帧率"
        MCB_LakeUI动画帧率.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI动画帧率.PrefixForeColor = Color.Gold
        MCB_LakeUI动画帧率.PrefixGap = 10
        MCB_LakeUI动画帧率.PrefixText = "界面动画帧率"
        MCB_LakeUI动画帧率.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI动画帧率.Size = New Size(300, 32)
        MCB_LakeUI动画帧率.TabIndex = 0
        MCB_LakeUI动画帧率.ToolTipGap = -1
        MCB_LakeUI动画帧率.ToolTipMaxWidth = 350
        MCB_LakeUI动画帧率.ToolTipPadding = New Padding(15)
        MCB_LakeUI动画帧率.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' HtmlColorLabel3
        '
        HtmlColorLabel3.AutoSize = True
        HtmlColorLabel3.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel3.Dock = DockStyle.Top
        HtmlColorLabel3.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel3.Location = New Point(15, 746)
        HtmlColorLabel3.Margin = New Padding(2)
        HtmlColorLabel3.Name = "HtmlColorLabel3"
        HtmlColorLabel3.Padding = New Padding(0, 0, 0, 10)
        HtmlColorLabel3.Size = New Size(617, 27)
        HtmlColorLabel3.TabIndex = 35
        HtmlColorLabel3.Text = "清理为即时动作，执行完自动弹回；「D2D 修剪到缓存预算」可日常常备，后面两档留给低内存设备救急"
        '
        ' ModernPanel17 / MCB_LakeUI缓存清理
        '
        ModernPanel17.BackColor = Color.Transparent
        ModernPanel17.BackColor1 = Color.Transparent
        ModernPanel17.BorderSize = 0
        ModernPanel17.Controls.Add(MCB_LakeUI缓存清理)
        ModernPanel17.Dock = DockStyle.Top
        ModernPanel17.Location = New Point(15, 773)
        ModernPanel17.Name = "ModernPanel17"
        ModernPanel17.Padding = New Padding(0, 10, 0, 0)
        ModernPanel17.Size = New Size(617, 42)
        ModernPanel17.TabIndex = 36
        MCB_LakeUI缓存清理.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI缓存清理.BorderRadius = 10
        MCB_LakeUI缓存清理.BorderSize = 0
        MCB_LakeUI缓存清理.Dock = DockStyle.Left
        MCB_LakeUI缓存清理.DropDownBackdropBlurPasses = 2
        MCB_LakeUI缓存清理.DropDownBackdropBlurRadius = 30
        MCB_LakeUI缓存清理.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_LakeUI缓存清理.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI缓存清理.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_LakeUI缓存清理.DropDownPadding = New Padding(10)
        MCB_LakeUI缓存清理.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI缓存清理.DropDownSelectedForeColor = Color.White
        MCB_LakeUI缓存清理.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI缓存清理.Items.Add("GC 压缩回收")
        MCB_LakeUI缓存清理.Items.Add("GC + 清空工作集")
        MCB_LakeUI缓存清理.Items.Add("D2D 修剪到缓存预算")
        MCB_LakeUI缓存清理.Items.Add("D2D 释放全部缓存")
        MCB_LakeUI缓存清理.Items.Add("D2D 全释放（含设备重建）")
        MCB_LakeUI缓存清理.Location = New Point(0, 10)
        MCB_LakeUI缓存清理.Margin = New Padding(2, 2, 2, 2)
        MCB_LakeUI缓存清理.MaxDropDownItems = 16
        MCB_LakeUI缓存清理.Name = "MCB_LakeUI缓存清理"
        MCB_LakeUI缓存清理.Padding = New Padding(10, 0, 10, 0)
        MCB_LakeUI缓存清理.PrefixForeColor = Color.Gold
        MCB_LakeUI缓存清理.PrefixGap = 10
        MCB_LakeUI缓存清理.PrefixText = "图形缓存清理"
        MCB_LakeUI缓存清理.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_LakeUI缓存清理.Size = New Size(300, 32)
        MCB_LakeUI缓存清理.TabIndex = 0
        MCB_LakeUI缓存清理.ToolTipGap = -1
        MCB_LakeUI缓存清理.ToolTipMaxWidth = 350
        MCB_LakeUI缓存清理.ToolTipPadding = New Padding(15)
        MCB_LakeUI缓存清理.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
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
        ' 页面设计高度 835 超出设置窗口客户区 493，靠窗体滚动条露出下半页
        ' （根面板为此改成了绝对定位，见 ModernPanel1 段的注释）。
        AutoScroll = True
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(652, 493)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_设备与性能"
        Text = "Form设置_设备与性能"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ModernPanel6.ResumeLayout(False)
        ModernPanel7.ResumeLayout(False)
        ModernPanel8.ResumeLayout(False)
        ModernPanel9.ResumeLayout(False)
        ModernPanel10.ResumeLayout(False)
        ModernPanel11.ResumeLayout(False)
        ModernPanel12.ResumeLayout(False)
        ModernPanel13.ResumeLayout(False)
        ModernPanel14.ResumeLayout(False)
        ModernPanel15.ResumeLayout(False)
        ModernPanel16.ResumeLayout(False)
        ModernPanel17.ResumeLayout(False)
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
    Friend WithEvents MCB_音频效果 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel6 As LakeUI.ModernPanel
    Friend WithEvents MCB_音频直通 As LakeUI.ModernComboBox
    Friend WithEvents HtmlColorLabel2 As LakeUI.HtmlColorLabel
    Friend WithEvents ModernPanel7 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI抗锯齿 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel8 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI文字渲染 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel9 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI超采样 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel10 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI_GPU缓存 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel11 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI_画刷缓存 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel12 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI_字体缓存 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel13 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI_CPU缓存 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel14 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI_背景脏区 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel15 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI_SSAA分桶 As LakeUI.ModernComboBox
    Friend WithEvents ModernPanel16 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI动画帧率 As LakeUI.ModernComboBox
    Friend WithEvents HtmlColorLabel3 As LakeUI.HtmlColorLabel
    Friend WithEvents ModernPanel17 As LakeUI.ModernPanel
    Friend WithEvents MCB_LakeUI缓存清理 As LakeUI.ModernComboBox
End Class
