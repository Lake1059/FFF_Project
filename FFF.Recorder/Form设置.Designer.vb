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
        ModernPanel1 = New LakeUI.ModernPanel()
        ModernPanel2 = New LakeUI.ModernPanel()
        Panel5 = New Panel()
        MB_清除切分文件快捷键 = New LakeUI.ModernButton()
        HtmlColorLabel7 = New LakeUI.HtmlColorLabel()
        MB_设定切分文件快捷键 = New LakeUI.ModernButton()
        Panel4 = New Panel()
        MB_清除停止录制快捷键 = New LakeUI.ModernButton()
        HtmlColorLabel6 = New LakeUI.HtmlColorLabel()
        MB_设定停止录制快捷键 = New LakeUI.ModernButton()
        Panel3 = New Panel()
        MB_清除继续录制快捷键 = New LakeUI.ModernButton()
        HtmlColorLabel5 = New LakeUI.HtmlColorLabel()
        MB_设定继续录制快捷键 = New LakeUI.ModernButton()
        Panel2 = New Panel()
        MB_清除暂停录制快捷键 = New LakeUI.ModernButton()
        HtmlColorLabel4 = New LakeUI.HtmlColorLabel()
        MB_设定暂停录制快捷键 = New LakeUI.ModernButton()
        Panel1 = New Panel()
        MB_清除开始录制快捷键 = New LakeUI.ModernButton()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        MB_设定开始录制快捷键 = New LakeUI.ModernButton()
        HtmlColorLabel3 = New LakeUI.HtmlColorLabel()
        JustEmptyControl4 = New LakeUI.JustEmptyControl()
        ModernPanel5 = New LakeUI.ModernPanel()
        Panel7 = New Panel()
        MCB_全局字体 = New LakeUI.ModernComboBox()
        HtmlColorLabel2 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        ModernPanel2.SuspendLayout()
        Panel5.SuspendLayout()
        Panel4.SuspendLayout()
        Panel3.SuspendLayout()
        Panel2.SuspendLayout()
        Panel1.SuspendLayout()
        ModernPanel5.SuspendLayout()
        Panel7.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(ModernPanel2)
        ModernPanel1.Controls.Add(JustEmptyControl4)
        ModernPanel1.Controls.Add(ModernPanel5)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(20)
        ModernPanel1.Size = New Size(800, 450)
        ModernPanel1.TabIndex = 0
        ' 
        ' ModernPanel2
        ' 
        ModernPanel2.AutoSize = True
        ModernPanel2.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        ModernPanel2.BorderRadius = 10
        ModernPanel2.BorderSize = 0
        ModernPanel2.Controls.Add(Panel5)
        ModernPanel2.Controls.Add(Panel4)
        ModernPanel2.Controls.Add(Panel3)
        ModernPanel2.Controls.Add(Panel2)
        ModernPanel2.Controls.Add(Panel1)
        ModernPanel2.Controls.Add(HtmlColorLabel3)
        ModernPanel2.Dock = DockStyle.Top
        ModernPanel2.Location = New Point(20, 132)
        ModernPanel2.Name = "ModernPanel2"
        ModernPanel2.Padding = New Padding(10)
        ModernPanel2.Size = New Size(760, 260)
        ModernPanel2.TabIndex = 23
        ' 
        ' Panel5
        ' 
        Panel5.BackColor = Color.Transparent
        Panel5.Controls.Add(MB_清除切分文件快捷键)
        Panel5.Controls.Add(HtmlColorLabel7)
        Panel5.Controls.Add(MB_设定切分文件快捷键)
        Panel5.Dock = DockStyle.Top
        Panel5.Location = New Point(15, 213)
        Panel5.Name = "Panel5"
        Panel5.Size = New Size(730, 32)
        Panel5.TabIndex = 22
        ' 
        ' MB_清除切分文件快捷键
        ' 
        MB_清除切分文件快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_清除切分文件快捷键.BorderRadius = 10
        MB_清除切分文件快捷键.BorderSize = 0
        MB_清除切分文件快捷键.Dock = DockStyle.Left
        MB_清除切分文件快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_清除切分文件快捷键.Location = New Point(300, 0)
        MB_清除切分文件快捷键.Margin = New Padding(2)
        MB_清除切分文件快捷键.Name = "MB_清除切分文件快捷键"
        MB_清除切分文件快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_清除切分文件快捷键.Size = New Size(40, 32)
        MB_清除切分文件快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_清除切分文件快捷键.TabIndex = 19
        MB_清除切分文件快捷键.Text = "❌️"
        ' 
        ' HtmlColorLabel7
        ' 
        HtmlColorLabel7.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel7.BackColor = Color.Transparent
        HtmlColorLabel7.Dock = DockStyle.Left
        HtmlColorLabel7.Location = New Point(100, 0)
        HtmlColorLabel7.Margin = New Padding(2)
        HtmlColorLabel7.Name = "HtmlColorLabel7"
        HtmlColorLabel7.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel7.Size = New Size(200, 32)
        HtmlColorLabel7.TabIndex = 18
        HtmlColorLabel7.Text = "切分文件"
        HtmlColorLabel7.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' MB_设定切分文件快捷键
        ' 
        MB_设定切分文件快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设定切分文件快捷键.BorderRadius = 10
        MB_设定切分文件快捷键.BorderSize = 0
        MB_设定切分文件快捷键.Dock = DockStyle.Left
        MB_设定切分文件快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设定切分文件快捷键.Location = New Point(0, 0)
        MB_设定切分文件快捷键.Margin = New Padding(2)
        MB_设定切分文件快捷键.Name = "MB_设定切分文件快捷键"
        MB_设定切分文件快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设定切分文件快捷键.Size = New Size(100, 32)
        MB_设定切分文件快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_设定切分文件快捷键.TabIndex = 13
        MB_设定切分文件快捷键.Text = "未设定"
        ' 
        ' Panel4
        ' 
        Panel4.BackColor = Color.Transparent
        Panel4.Controls.Add(MB_清除停止录制快捷键)
        Panel4.Controls.Add(HtmlColorLabel6)
        Panel4.Controls.Add(MB_设定停止录制快捷键)
        Panel4.Dock = DockStyle.Top
        Panel4.Location = New Point(15, 171)
        Panel4.Name = "Panel4"
        Panel4.Padding = New Padding(0, 0, 0, 10)
        Panel4.Size = New Size(730, 42)
        Panel4.TabIndex = 21
        ' 
        ' MB_清除停止录制快捷键
        ' 
        MB_清除停止录制快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_清除停止录制快捷键.BorderRadius = 10
        MB_清除停止录制快捷键.BorderSize = 0
        MB_清除停止录制快捷键.Dock = DockStyle.Left
        MB_清除停止录制快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_清除停止录制快捷键.Location = New Point(300, 0)
        MB_清除停止录制快捷键.Margin = New Padding(2)
        MB_清除停止录制快捷键.Name = "MB_清除停止录制快捷键"
        MB_清除停止录制快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_清除停止录制快捷键.Size = New Size(40, 32)
        MB_清除停止录制快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_清除停止录制快捷键.TabIndex = 19
        MB_清除停止录制快捷键.Text = "❌️"
        ' 
        ' HtmlColorLabel6
        ' 
        HtmlColorLabel6.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel6.BackColor = Color.Transparent
        HtmlColorLabel6.Dock = DockStyle.Left
        HtmlColorLabel6.Location = New Point(100, 0)
        HtmlColorLabel6.Margin = New Padding(2)
        HtmlColorLabel6.Name = "HtmlColorLabel6"
        HtmlColorLabel6.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel6.Size = New Size(200, 32)
        HtmlColorLabel6.TabIndex = 18
        HtmlColorLabel6.Text = "停止录制"
        HtmlColorLabel6.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' MB_设定停止录制快捷键
        ' 
        MB_设定停止录制快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设定停止录制快捷键.BorderRadius = 10
        MB_设定停止录制快捷键.BorderSize = 0
        MB_设定停止录制快捷键.Dock = DockStyle.Left
        MB_设定停止录制快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设定停止录制快捷键.Location = New Point(0, 0)
        MB_设定停止录制快捷键.Margin = New Padding(2)
        MB_设定停止录制快捷键.Name = "MB_设定停止录制快捷键"
        MB_设定停止录制快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设定停止录制快捷键.Size = New Size(100, 32)
        MB_设定停止录制快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_设定停止录制快捷键.TabIndex = 13
        MB_设定停止录制快捷键.Text = "未设定"
        ' 
        ' Panel3
        ' 
        Panel3.BackColor = Color.Transparent
        Panel3.Controls.Add(MB_清除继续录制快捷键)
        Panel3.Controls.Add(HtmlColorLabel5)
        Panel3.Controls.Add(MB_设定继续录制快捷键)
        Panel3.Dock = DockStyle.Top
        Panel3.Location = New Point(15, 129)
        Panel3.Name = "Panel3"
        Panel3.Padding = New Padding(0, 0, 0, 10)
        Panel3.Size = New Size(730, 42)
        Panel3.TabIndex = 20
        ' 
        ' MB_清除继续录制快捷键
        ' 
        MB_清除继续录制快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_清除继续录制快捷键.BorderRadius = 10
        MB_清除继续录制快捷键.BorderSize = 0
        MB_清除继续录制快捷键.Dock = DockStyle.Left
        MB_清除继续录制快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_清除继续录制快捷键.Location = New Point(300, 0)
        MB_清除继续录制快捷键.Margin = New Padding(2)
        MB_清除继续录制快捷键.Name = "MB_清除继续录制快捷键"
        MB_清除继续录制快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_清除继续录制快捷键.Size = New Size(40, 32)
        MB_清除继续录制快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_清除继续录制快捷键.TabIndex = 19
        MB_清除继续录制快捷键.Text = "❌️"
        ' 
        ' HtmlColorLabel5
        ' 
        HtmlColorLabel5.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel5.BackColor = Color.Transparent
        HtmlColorLabel5.Dock = DockStyle.Left
        HtmlColorLabel5.Location = New Point(100, 0)
        HtmlColorLabel5.Margin = New Padding(2)
        HtmlColorLabel5.Name = "HtmlColorLabel5"
        HtmlColorLabel5.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel5.Size = New Size(200, 32)
        HtmlColorLabel5.TabIndex = 18
        HtmlColorLabel5.Text = "继续录制"
        HtmlColorLabel5.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' MB_设定继续录制快捷键
        ' 
        MB_设定继续录制快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设定继续录制快捷键.BorderRadius = 10
        MB_设定继续录制快捷键.BorderSize = 0
        MB_设定继续录制快捷键.Dock = DockStyle.Left
        MB_设定继续录制快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设定继续录制快捷键.Location = New Point(0, 0)
        MB_设定继续录制快捷键.Margin = New Padding(2)
        MB_设定继续录制快捷键.Name = "MB_设定继续录制快捷键"
        MB_设定继续录制快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设定继续录制快捷键.Size = New Size(100, 32)
        MB_设定继续录制快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_设定继续录制快捷键.TabIndex = 13
        MB_设定继续录制快捷键.Text = "未设定"
        ' 
        ' Panel2
        ' 
        Panel2.BackColor = Color.Transparent
        Panel2.Controls.Add(MB_清除暂停录制快捷键)
        Panel2.Controls.Add(HtmlColorLabel4)
        Panel2.Controls.Add(MB_设定暂停录制快捷键)
        Panel2.Dock = DockStyle.Top
        Panel2.Location = New Point(15, 87)
        Panel2.Name = "Panel2"
        Panel2.Padding = New Padding(0, 0, 0, 10)
        Panel2.Size = New Size(730, 42)
        Panel2.TabIndex = 19
        ' 
        ' MB_清除暂停录制快捷键
        ' 
        MB_清除暂停录制快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_清除暂停录制快捷键.BorderRadius = 10
        MB_清除暂停录制快捷键.BorderSize = 0
        MB_清除暂停录制快捷键.Dock = DockStyle.Left
        MB_清除暂停录制快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_清除暂停录制快捷键.Location = New Point(300, 0)
        MB_清除暂停录制快捷键.Margin = New Padding(2)
        MB_清除暂停录制快捷键.Name = "MB_清除暂停录制快捷键"
        MB_清除暂停录制快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_清除暂停录制快捷键.Size = New Size(40, 32)
        MB_清除暂停录制快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_清除暂停录制快捷键.TabIndex = 19
        MB_清除暂停录制快捷键.Text = "❌️"
        ' 
        ' HtmlColorLabel4
        ' 
        HtmlColorLabel4.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel4.BackColor = Color.Transparent
        HtmlColorLabel4.Dock = DockStyle.Left
        HtmlColorLabel4.Location = New Point(100, 0)
        HtmlColorLabel4.Margin = New Padding(2)
        HtmlColorLabel4.Name = "HtmlColorLabel4"
        HtmlColorLabel4.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel4.Size = New Size(200, 32)
        HtmlColorLabel4.TabIndex = 18
        HtmlColorLabel4.Text = "暂停录制"
        HtmlColorLabel4.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' MB_设定暂停录制快捷键
        ' 
        MB_设定暂停录制快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设定暂停录制快捷键.BorderRadius = 10
        MB_设定暂停录制快捷键.BorderSize = 0
        MB_设定暂停录制快捷键.Dock = DockStyle.Left
        MB_设定暂停录制快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设定暂停录制快捷键.Location = New Point(0, 0)
        MB_设定暂停录制快捷键.Margin = New Padding(2)
        MB_设定暂停录制快捷键.Name = "MB_设定暂停录制快捷键"
        MB_设定暂停录制快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设定暂停录制快捷键.Size = New Size(100, 32)
        MB_设定暂停录制快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_设定暂停录制快捷键.TabIndex = 13
        MB_设定暂停录制快捷键.Text = "未设定"
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.Controls.Add(MB_清除开始录制快捷键)
        Panel1.Controls.Add(HtmlColorLabel1)
        Panel1.Controls.Add(MB_设定开始录制快捷键)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(15, 45)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 0, 0, 10)
        Panel1.Size = New Size(730, 42)
        Panel1.TabIndex = 13
        ' 
        ' MB_清除开始录制快捷键
        ' 
        MB_清除开始录制快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_清除开始录制快捷键.BorderRadius = 10
        MB_清除开始录制快捷键.BorderSize = 0
        MB_清除开始录制快捷键.Dock = DockStyle.Left
        MB_清除开始录制快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_清除开始录制快捷键.Location = New Point(300, 0)
        MB_清除开始录制快捷键.Margin = New Padding(2)
        MB_清除开始录制快捷键.Name = "MB_清除开始录制快捷键"
        MB_清除开始录制快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_清除开始录制快捷键.Size = New Size(40, 32)
        MB_清除开始录制快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_清除开始录制快捷键.TabIndex = 19
        MB_清除开始录制快捷键.Text = "❌️"
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.BackColor = Color.Transparent
        HtmlColorLabel1.Dock = DockStyle.Left
        HtmlColorLabel1.Location = New Point(100, 0)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Padding = New Padding(10, 0, 0, 0)
        HtmlColorLabel1.Size = New Size(200, 32)
        HtmlColorLabel1.TabIndex = 18
        HtmlColorLabel1.Text = "开始录制"
        HtmlColorLabel1.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.MiddleLeft
        ' 
        ' MB_设定开始录制快捷键
        ' 
        MB_设定开始录制快捷键.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_设定开始录制快捷键.BorderRadius = 10
        MB_设定开始录制快捷键.BorderSize = 0
        MB_设定开始录制快捷键.Dock = DockStyle.Left
        MB_设定开始录制快捷键.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_设定开始录制快捷键.Location = New Point(0, 0)
        MB_设定开始录制快捷键.Margin = New Padding(2)
        MB_设定开始录制快捷键.Name = "MB_设定开始录制快捷键"
        MB_设定开始录制快捷键.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_设定开始录制快捷键.Size = New Size(100, 32)
        MB_设定开始录制快捷键.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_设定开始录制快捷键.TabIndex = 13
        MB_设定开始录制快捷键.Text = "未设定"
        ' 
        ' HtmlColorLabel3
        ' 
        HtmlColorLabel3.AutoSize = True
        HtmlColorLabel3.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel3.BackColor = Color.Transparent
        HtmlColorLabel3.Dock = DockStyle.Top
        HtmlColorLabel3.Location = New Point(15, 15)
        HtmlColorLabel3.Margin = New Padding(2)
        HtmlColorLabel3.Name = "HtmlColorLabel3"
        HtmlColorLabel3.Padding = New Padding(0, 0, 0, 10)
        HtmlColorLabel3.Size = New Size(730, 30)
        HtmlColorLabel3.TabIndex = 18
        HtmlColorLabel3.Text = "快捷键设置"
        ' 
        ' JustEmptyControl4
        ' 
        JustEmptyControl4.Dock = DockStyle.Top
        JustEmptyControl4.Location = New Point(20, 112)
        JustEmptyControl4.Name = "JustEmptyControl4"
        JustEmptyControl4.Size = New Size(760, 20)
        JustEmptyControl4.TabIndex = 22
        ' 
        ' ModernPanel5
        ' 
        ModernPanel5.AutoSize = True
        ModernPanel5.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        ModernPanel5.BorderRadius = 10
        ModernPanel5.BorderSize = 0
        ModernPanel5.Controls.Add(Panel7)
        ModernPanel5.Controls.Add(HtmlColorLabel2)
        ModernPanel5.Dock = DockStyle.Top
        ModernPanel5.Location = New Point(20, 20)
        ModernPanel5.Name = "ModernPanel5"
        ModernPanel5.Padding = New Padding(10)
        ModernPanel5.Size = New Size(760, 92)
        ModernPanel5.TabIndex = 21
        ' 
        ' Panel7
        ' 
        Panel7.BackColor = Color.Transparent
        Panel7.Controls.Add(MCB_全局字体)
        Panel7.Dock = DockStyle.Top
        Panel7.Location = New Point(15, 45)
        Panel7.Name = "Panel7"
        Panel7.Size = New Size(730, 32)
        Panel7.TabIndex = 13
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
        MCB_全局字体.DropDownItemHeight = 24
        MCB_全局字体.DropDownPadding = New Padding(10)
        MCB_全局字体.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_全局字体.DropDownSelectedForeColor = Color.White
        MCB_全局字体.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_全局字体.Location = New Point(0, 0)
        MCB_全局字体.Margin = New Padding(2, 2, 2, 2)
        MCB_全局字体.MaxDropDownItems = 16
        MCB_全局字体.Name = "MCB_全局字体"
        MCB_全局字体.Padding = New Padding(10, 0, 10, 0)
        MCB_全局字体.Size = New Size(300, 32)
        MCB_全局字体.TabIndex = 16
        MCB_全局字体.ToolTipGap = -1
        MCB_全局字体.ToolTipMaxWidth = 350
        MCB_全局字体.ToolTipPadding = New Padding(15)
        MCB_全局字体.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
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
        HtmlColorLabel2.Padding = New Padding(0, 0, 0, 10)
        HtmlColorLabel2.Size = New Size(730, 30)
        HtmlColorLabel2.TabIndex = 17
        HtmlColorLabel2.Text = "全局字体"
        ' 
        ' Form设置
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(800, 450)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置"
        Text = "Form设置"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ModernPanel2.ResumeLayout(False)
        ModernPanel2.PerformLayout()
        Panel5.ResumeLayout(False)
        Panel4.ResumeLayout(False)
        Panel3.ResumeLayout(False)
        Panel2.ResumeLayout(False)
        Panel1.ResumeLayout(False)
        ModernPanel5.ResumeLayout(False)
        ModernPanel5.PerformLayout()
        Panel7.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents ModernPanel5 As LakeUI.ModernPanel
    Friend WithEvents Panel7 As Panel
    Friend WithEvents MCB_全局字体 As LakeUI.ModernComboBox
    Friend WithEvents HtmlColorLabel2 As LakeUI.HtmlColorLabel
    Friend WithEvents JustEmptyControl4 As LakeUI.JustEmptyControl
    Friend WithEvents ModernPanel2 As LakeUI.ModernPanel
    Friend WithEvents Panel1 As Panel
    Friend WithEvents MB_设定开始录制快捷键 As LakeUI.ModernButton
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents MB_清除开始录制快捷键 As LakeUI.ModernButton
    Friend WithEvents HtmlColorLabel3 As LakeUI.HtmlColorLabel
    Friend WithEvents Panel5 As Panel
    Friend WithEvents MB_清除切分文件快捷键 As LakeUI.ModernButton
    Friend WithEvents HtmlColorLabel7 As LakeUI.HtmlColorLabel
    Friend WithEvents MB_设定切分文件快捷键 As LakeUI.ModernButton
    Friend WithEvents Panel4 As Panel
    Friend WithEvents MB_清除停止录制快捷键 As LakeUI.ModernButton
    Friend WithEvents HtmlColorLabel6 As LakeUI.HtmlColorLabel
    Friend WithEvents MB_设定停止录制快捷键 As LakeUI.ModernButton
    Friend WithEvents Panel3 As Panel
    Friend WithEvents MB_清除继续录制快捷键 As LakeUI.ModernButton
    Friend WithEvents HtmlColorLabel5 As LakeUI.HtmlColorLabel
    Friend WithEvents MB_设定继续录制快捷键 As LakeUI.ModernButton
    Friend WithEvents Panel2 As Panel
    Friend WithEvents MB_清除暂停录制快捷键 As LakeUI.ModernButton
    Friend WithEvents HtmlColorLabel4 As LakeUI.HtmlColorLabel
    Friend WithEvents MB_设定暂停录制快捷键 As LakeUI.ModernButton
End Class
