<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form播放列表
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
        Dim ListColumn1 As LakeUI.UltraDetailListView.ListColumn = New LakeUI.UltraDetailListView.ListColumn()
        ModernPanel1 = New LakeUI.ModernPanel()
        UltraDetailListView1 = New LakeUI.UltraDetailListView()
        Panel1 = New LakeUI.ModernPanel()
        MCB_播放模式 = New LakeUI.ModernComboBox()
        JustEmptyControl1 = New LakeUI.JustEmptyControl()
        MB_定位 = New LakeUI.ModernButton()
        JustEmptyControl4 = New LakeUI.JustEmptyControl()
        MB_移除 = New LakeUI.ModernButton()
        JustEmptyControl2 = New LakeUI.JustEmptyControl()
        MB_添加 = New LakeUI.ModernButton()
        JustEmptyControl3 = New LakeUI.JustEmptyControl()
        MB_保存 = New LakeUI.ModernButton()
        JustEmptyControl5 = New LakeUI.JustEmptyControl()
        MB_加载 = New LakeUI.ModernButton()
        ModernPanel1.SuspendLayout()
        Panel1.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor = Color.Transparent
        ModernPanel1.BackColor1 = Color.Transparent
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(UltraDetailListView1)
        ModernPanel1.Controls.Add(Panel1)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(10)
        ModernPanel1.Size = New Size(484, 461)
        ModernPanel1.TabIndex = 0
        ' 
        ' UltraDetailListView1
        ' 
        UltraDetailListView1.AllowDragReorder = True
        UltraDetailListView1.BackgroundColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        UltraDetailListView1.BorderRadius = 10
        UltraDetailListView1.BorderSize = 0
        ListColumn1.Text = "文件名"
        ListColumn1.Width = 400
        ListColumn1.WordWrapHeightFixed = True
        UltraDetailListView1.Columns.Add(ListColumn1)
        UltraDetailListView1.ContentPadding = New Padding(0)
        UltraDetailListView1.Dock = DockStyle.Fill
        UltraDetailListView1.GroupBackColor = Color.FromArgb(CByte(36), CByte(36), CByte(36))
        UltraDetailListView1.GroupForeColor = Color.Gainsboro
        UltraDetailListView1.GroupHeight = 40
        UltraDetailListView1.HeaderBackColor = Color.Transparent
        UltraDetailListView1.HeaderBorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        UltraDetailListView1.HeaderForeColor = Color.DarkGray
        UltraDetailListView1.HeaderVisible = False
        UltraDetailListView1.ItemCornerRadius = 5
        UltraDetailListView1.ItemPadding = New Padding(10, 5, 5, 5)
        UltraDetailListView1.ItemSelectedBackColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        UltraDetailListView1.Location = New Point(10, 52)
        UltraDetailListView1.Margin = New Padding(2, 2, 2, 2)
        UltraDetailListView1.Name = "UltraDetailListView1"
        UltraDetailListView1.ScrollBarThumbColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        UltraDetailListView1.ScrollBarThumbHoverColor = Color.FromArgb(CByte(120), CByte(220), CByte(220), CByte(220))
        UltraDetailListView1.ScrollBarTrackColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        UltraDetailListView1.SelectionRectBorderColor = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        UltraDetailListView1.SelectionRectFillColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        UltraDetailListView1.Size = New Size(464, 399)
        UltraDetailListView1.TabIndex = 21
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.BackColor1 = Color.Transparent
        Panel1.BorderSize = 0
        Panel1.Controls.Add(MCB_播放模式)
        Panel1.Controls.Add(JustEmptyControl1)
        Panel1.Controls.Add(MB_定位)
        Panel1.Controls.Add(JustEmptyControl4)
        Panel1.Controls.Add(MB_移除)
        Panel1.Controls.Add(JustEmptyControl2)
        Panel1.Controls.Add(MB_添加)
        Panel1.Controls.Add(JustEmptyControl3)
        Panel1.Controls.Add(MB_保存)
        Panel1.Controls.Add(JustEmptyControl5)
        Panel1.Controls.Add(MB_加载)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(10, 10)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 0, 0, 10)
        Panel1.Size = New Size(464, 42)
        Panel1.TabIndex = 20
        ' 
        ' MCB_播放模式
        ' 
        MCB_播放模式.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_播放模式.BorderRadius = 10
        MCB_播放模式.BorderSize = 0
        MCB_播放模式.Dock = DockStyle.Fill
        MCB_播放模式.DropDownBackdropBlurPasses = 2
        MCB_播放模式.DropDownBackdropBlurRadius = 30
        MCB_播放模式.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_播放模式.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_播放模式.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_播放模式.DropDownPadding = New Padding(10)
        MCB_播放模式.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_播放模式.DropDownSelectedForeColor = Color.White
        MCB_播放模式.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_播放模式.Items.Add("顺序播放")
        MCB_播放模式.Items.Add("单个循环")
        MCB_播放模式.Items.Add("循环播放")
        MCB_播放模式.Items.Add("随机播放")
        MCB_播放模式.Location = New Point(350, 0)
        MCB_播放模式.Margin = New Padding(2, 2, 2, 2)
        MCB_播放模式.Name = "MCB_播放模式"
        MCB_播放模式.Padding = New Padding(10, 0, 10, 0)
        MCB_播放模式.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_播放模式.Size = New Size(114, 32)
        MCB_播放模式.TabIndex = 22
        MCB_播放模式.ToolTipGap = -1
        MCB_播放模式.ToolTipMaxWidth = 350
        MCB_播放模式.ToolTipPadding = New Padding(15)
        MCB_播放模式.WaterText = "播放模式"
        MCB_播放模式.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' JustEmptyControl1
        ' 
        JustEmptyControl1.Dock = DockStyle.Left
        JustEmptyControl1.Location = New Point(340, 0)
        JustEmptyControl1.Name = "JustEmptyControl1"
        JustEmptyControl1.Size = New Size(10, 32)
        JustEmptyControl1.TabIndex = 23
        ' 
        ' MB_定位
        ' 
        MB_定位.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_定位.BorderRadius = 10
        MB_定位.BorderSize = 0
        MB_定位.Dock = DockStyle.Left
        MB_定位.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_定位.Location = New Point(280, 0)
        MB_定位.Margin = New Padding(2)
        MB_定位.Name = "MB_定位"
        MB_定位.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_定位.Size = New Size(60, 32)
        MB_定位.TabIndex = 24
        MB_定位.Text = "定位"
        ' 
        ' JustEmptyControl4
        ' 
        JustEmptyControl4.Dock = DockStyle.Left
        JustEmptyControl4.Location = New Point(270, 0)
        JustEmptyControl4.Name = "JustEmptyControl4"
        JustEmptyControl4.Size = New Size(10, 32)
        JustEmptyControl4.TabIndex = 21
        ' 
        ' MB_移除
        ' 
        MB_移除.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_移除.BorderRadius = 10
        MB_移除.BorderSize = 0
        MB_移除.Dock = DockStyle.Left
        MB_移除.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_移除.Location = New Point(210, 0)
        MB_移除.Margin = New Padding(2)
        MB_移除.Name = "MB_移除"
        MB_移除.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_移除.Size = New Size(60, 32)
        MB_移除.TabIndex = 20
        MB_移除.Text = "移除"
        ' 
        ' JustEmptyControl2
        ' 
        JustEmptyControl2.Dock = DockStyle.Left
        JustEmptyControl2.Location = New Point(200, 0)
        JustEmptyControl2.Name = "JustEmptyControl2"
        JustEmptyControl2.Size = New Size(10, 32)
        JustEmptyControl2.TabIndex = 19
        ' 
        ' MB_添加
        ' 
        MB_添加.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_添加.BorderRadius = 10
        MB_添加.BorderSize = 0
        MB_添加.Dock = DockStyle.Left
        MB_添加.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_添加.Location = New Point(140, 0)
        MB_添加.Margin = New Padding(2)
        MB_添加.Name = "MB_添加"
        MB_添加.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_添加.Size = New Size(60, 32)
        MB_添加.TabIndex = 17
        MB_添加.Text = "添加"
        ' 
        ' JustEmptyControl3
        ' 
        JustEmptyControl3.Dock = DockStyle.Left
        JustEmptyControl3.Location = New Point(130, 0)
        JustEmptyControl3.Name = "JustEmptyControl3"
        JustEmptyControl3.Size = New Size(10, 32)
        JustEmptyControl3.TabIndex = 26
        ' 
        ' MB_保存
        ' 
        MB_保存.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_保存.BorderRadius = 10
        MB_保存.BorderSize = 0
        MB_保存.Dock = DockStyle.Left
        MB_保存.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_保存.Location = New Point(70, 0)
        MB_保存.Margin = New Padding(2)
        MB_保存.Name = "MB_保存"
        MB_保存.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_保存.Size = New Size(60, 32)
        MB_保存.TabIndex = 25
        MB_保存.Text = "保存"
        ' 
        ' JustEmptyControl5
        ' 
        JustEmptyControl5.Dock = DockStyle.Left
        JustEmptyControl5.Location = New Point(60, 0)
        JustEmptyControl5.Name = "JustEmptyControl5"
        JustEmptyControl5.Size = New Size(10, 32)
        JustEmptyControl5.TabIndex = 28
        ' 
        ' MB_加载
        ' 
        MB_加载.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_加载.BorderRadius = 10
        MB_加载.BorderSize = 0
        MB_加载.Dock = DockStyle.Left
        MB_加载.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_加载.Location = New Point(0, 0)
        MB_加载.Margin = New Padding(2)
        MB_加载.Name = "MB_加载"
        MB_加载.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_加载.Size = New Size(60, 32)
        MB_加载.TabIndex = 27
        MB_加载.Text = "加载"
        ' 
        ' Form播放列表
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(484, 461)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        MaximizeBox = False
        MinimizeBox = False
        MinimumSize = New Size(500, 500)
        Name = "Form播放列表"
        ShowIcon = False
        ShowInTaskbar = False
        Text = "播放列表"
        ModernPanel1.ResumeLayout(False)
        Panel1.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents Panel1 As LakeUI.ModernPanel
    Friend WithEvents JustEmptyControl4 As LakeUI.JustEmptyControl
    Friend WithEvents MB_移除 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl2 As LakeUI.JustEmptyControl
    Friend WithEvents MB_添加 As LakeUI.ModernButton
    Friend WithEvents MCB_播放模式 As LakeUI.ModernComboBox
    Friend WithEvents JustEmptyControl1 As LakeUI.JustEmptyControl
    Friend WithEvents MB_定位 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl3 As LakeUI.JustEmptyControl
    Friend WithEvents MB_保存 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl5 As LakeUI.JustEmptyControl
    Friend WithEvents MB_加载 As LakeUI.ModernButton
    Friend WithEvents UltraDetailListView1 As LakeUI.UltraDetailListView
End Class
