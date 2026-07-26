<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()>
Partial Class Form1
    Inherits System.Windows.Forms.Form

    'Form overrides dispose to clean up the component list.
    <System.Diagnostics.DebuggerNonUserCode()>
    Protected Overrides Sub Dispose(disposing As Boolean)
        Try
            If disposing AndAlso components IsNot Nothing Then
                components.Dispose()
            End If
        Finally
            MyBase.Dispose(disposing)
        End Try
    End Sub

    'Required by the Windows Form Designer
    Private components As System.ComponentModel.IContainer

    'NOTE: The following procedure is required by the Windows Form Designer
    'It can be modified using the Windows Form Designer.
    'Do not modify it using the code editor.
    <System.Diagnostics.DebuggerStepThrough()>
    Private Sub InitializeComponent()
        components = New ComponentModel.Container()
        ThisIsYourWindow1 = New LakeUI.ThisIsYourWindow(components)
        Panel1 = New Panel()
        MB_1 = New LakeUI.ModernButton()
        ModernPanel1 = New LakeUI.ModernPanel()
        Panel3 = New Panel()
        Panel4 = New Panel()
        MB_当前声道数显示 = New LakeUI.ModernButton()
        JEC_当前声道数显示前面的空白占位 = New LakeUI.JustEmptyControl()
        MB_当前音频编码显示 = New LakeUI.ModernButton()
        JEC_当前音频编码显示前面的空白占位 = New LakeUI.JustEmptyControl()
        MB_当前视频编码显示 = New LakeUI.ModernButton()
        JEC_当前视频编码显示前面的空白占位 = New LakeUI.JustEmptyControl()
        MB_HDR模式 = New LakeUI.ModernButton()
        JEC_HDR选项前面的空白占位 = New LakeUI.JustEmptyControl()
        MB_软件解码或硬件解码 = New LakeUI.ModernButton()
        JustEmptyControl19 = New LakeUI.JustEmptyControl()
        MB_选择流 = New LakeUI.ModernButton()
        JustEmptyControl18 = New LakeUI.JustEmptyControl()
        MB_查看当前媒体信息 = New LakeUI.ModernButton()
        JustEmptyControl13 = New LakeUI.JustEmptyControl()
        MB_剪辑区间模式 = New LakeUI.ModernButton()
        JustEmptyControl12 = New LakeUI.JustEmptyControl()
        MB_播放列表 = New LakeUI.ModernButton()
        JustEmptyControl11 = New LakeUI.JustEmptyControl()
        MB_软件设置 = New LakeUI.ModernButton()
        JustEmptyControl10 = New LakeUI.JustEmptyControl()
        HCL_时间戳显示 = New LakeUI.HtmlColorLabel()
        JustEmptyControl9 = New LakeUI.JustEmptyControl()
        MB_打开文件 = New LakeUI.ModernButton()
        JustEmptyControl8 = New LakeUI.JustEmptyControl()
        MB_快进或下一个 = New LakeUI.ModernButton()
        JustEmptyControl7 = New LakeUI.JustEmptyControl()
        MB_倒退或上一个 = New LakeUI.ModernButton()
        JustEmptyControl6 = New LakeUI.JustEmptyControl()
        MB_停止 = New LakeUI.ModernButton()
        JustEmptyControl5 = New LakeUI.JustEmptyControl()
        MB_播放和暂停 = New LakeUI.ModernButton()
        JustEmptyControl3 = New LakeUI.JustEmptyControl()
        Panel2 = New Panel()
        JustEmptyControl4 = New LakeUI.JustEmptyControl()
        ETB_媒体进度条 = New LakeUI.ExcellentTrackBar()
        ETB_音量条 = New LakeUI.ExcellentTrackBar()
        JustEmptyControl1 = New LakeUI.JustEmptyControl()
        MP_剪辑区间操作容器 = New LakeUI.ModernPanel()
        JustEmptyControl2 = New LakeUI.JustEmptyControl()
        MP_DX视频容器 = New Panel()
        Panel1.SuspendLayout()
        ModernPanel1.SuspendLayout()
        Panel3.SuspendLayout()
        Panel4.SuspendLayout()
        Panel2.SuspendLayout()
        MP_剪辑区间操作容器.SuspendLayout()
        MP_DX视频容器.SuspendLayout()
        SuspendLayout()
        '
        ' ThisIsYourWindow1
        '
        ThisIsYourWindow1.BackdropNoiseScale = 0.5F
        ThisIsYourWindow1.BackdropTintColor = Color.FromArgb(CByte(160), CByte(0), CByte(0), CByte(0))
        ThisIsYourWindow1.BackdropTintInactiveColor = Color.FromArgb(CByte(160), CByte(0), CByte(0), CByte(0))
        ThisIsYourWindow1.BorderColor = Color.Gray
        ThisIsYourWindow1.BorderInactiveColor = Color.Gray
        ThisIsYourWindow1.ButtonCornerRadius = 5
        ThisIsYourWindow1.ButtonGlyphLineWidth = 2F
        ThisIsYourWindow1.ButtonPadding = New Padding(0, 5, 5, 5)
        ThisIsYourWindow1.ButtonWidth = 40
        ThisIsYourWindow1.CaptionBackColor = Color.FromArgb(CByte(48), CByte(48), CByte(48))
        ThisIsYourWindow1.CaptionButtonGlyphColor = Color.FromArgb(CByte(200), CByte(200), CByte(200))
        ThisIsYourWindow1.CaptionButtonHoverBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        ThisIsYourWindow1.CaptionButtonPressedBackColor = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        ThisIsYourWindow1.CaptionControl = Panel1
        ThisIsYourWindow1.CaptionHeight = 42
        ThisIsYourWindow1.CaptionInactiveBackColor = Color.FromArgb(CByte(48), CByte(48), CByte(48))
        ThisIsYourWindow1.CloseButtonGlyphColor = Color.FromArgb(CByte(200), CByte(200), CByte(200))
        ThisIsYourWindow1.IconPaddingLeft = 10
        ThisIsYourWindow1.IconSize = 26
        ThisIsYourWindow1.LayerShadowResizeFullArea = True
        ThisIsYourWindow1.ShadowMode = LakeUI.ThisIsYourWindow.ShadowModeEnum.Layer
        ThisIsYourWindow1.TitleAlign = LakeUI.ThisIsYourWindow.TitleAlignEnum.Center
        ThisIsYourWindow1.TitleForeColor = Color.Silver
        ThisIsYourWindow1.TitleInactiveForeColor = Color.DarkGray
        '
        ' Panel1
        '
        Panel1.BackColor = Color.Transparent
        Panel1.Controls.Add(MB_1)
        Panel1.Location = New Point(42, 48)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 4, 0, 4)
        Panel1.Size = New Size(100, 42)
        Panel1.TabIndex = 1
        ' MB_1
        MB_1.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_1.BorderRadius = 10
        MB_1.BorderSize = 0
        MB_1.Dock = DockStyle.Fill
        MB_1.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_1.Location = New Point(0, 4)
        MB_1.Margin = New Padding(2)
        MB_1.Name = "MB_1"
        MB_1.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_1.Size = New Size(100, 34)
        MB_1.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_1.TabIndex = 12
        MB_1.Text = "FFF.Player"
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(Panel3)
        ModernPanel1.Controls.Add(JustEmptyControl3)
        ModernPanel1.Controls.Add(Panel2)
        ModernPanel1.Controls.Add(JustEmptyControl1)
        ModernPanel1.Dock = DockStyle.Bottom
        ModernPanel1.Location = New Point(0, 446)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Size = New Size(844, 65)
        ModernPanel1.TabIndex = 0
        ' 
        ' Panel3
        ' 
        Panel3.BackColor = Color.Transparent
        Panel3.Controls.Add(Panel4)
        Panel3.Controls.Add(JustEmptyControl19)
        Panel3.Controls.Add(MB_选择流)
        Panel3.Controls.Add(JustEmptyControl18)
        Panel3.Controls.Add(MB_查看当前媒体信息)
        Panel3.Controls.Add(JustEmptyControl13)
        Panel3.Controls.Add(MB_剪辑区间模式)
        Panel3.Controls.Add(JustEmptyControl12)
        Panel3.Controls.Add(MB_播放列表)
        Panel3.Controls.Add(JustEmptyControl11)
        Panel3.Controls.Add(MB_软件设置)
        Panel3.Controls.Add(JustEmptyControl10)
        Panel3.Controls.Add(HCL_时间戳显示)
        Panel3.Controls.Add(JustEmptyControl9)
        Panel3.Controls.Add(MB_打开文件)
        Panel3.Controls.Add(JustEmptyControl8)
        Panel3.Controls.Add(MB_快进或下一个)
        Panel3.Controls.Add(JustEmptyControl7)
        Panel3.Controls.Add(MB_倒退或上一个)
        Panel3.Controls.Add(JustEmptyControl6)
        Panel3.Controls.Add(MB_停止)
        Panel3.Controls.Add(JustEmptyControl5)
        Panel3.Controls.Add(MB_播放和暂停)
        Panel3.Dock = DockStyle.Fill
        Panel3.Location = New Point(0, 25)
        Panel3.Name = "Panel3"
        Panel3.Size = New Size(844, 40)
        Panel3.TabIndex = 33
        ' 
        ' Panel4
        ' 
        Panel4.Controls.Add(MB_当前声道数显示)
        Panel4.Controls.Add(JEC_当前声道数显示前面的空白占位)
        Panel4.Controls.Add(MB_当前音频编码显示)
        Panel4.Controls.Add(JEC_当前音频编码显示前面的空白占位)
        Panel4.Controls.Add(MB_当前视频编码显示)
        Panel4.Controls.Add(JEC_当前视频编码显示前面的空白占位)
        Panel4.Controls.Add(MB_HDR模式)
        Panel4.Controls.Add(JEC_HDR选项前面的空白占位)
        Panel4.Controls.Add(MB_软件解码或硬件解码)
        Panel4.Dock = DockStyle.Fill
        Panel4.Location = New Point(362, 0)
        Panel4.Name = "Panel4"
        Panel4.Padding = New Padding(10, 8, 10, 8)
        Panel4.Size = New Size(272, 40)
        Panel4.TabIndex = 51
        ' 
        ' MB_当前声道数显示
        ' 
        MB_当前声道数显示.BackColor = Color.Transparent
        MB_当前声道数显示.BackColor1 = Color.FromArgb(CByte(40), CByte(0), CByte(0), CByte(0))
        MB_当前声道数显示.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_当前声道数显示.BorderRadius = 5
        MB_当前声道数显示.Dock = DockStyle.Left
        MB_当前声道数显示.Font = New Font("Microsoft YaHei UI", 9F)
        MB_当前声道数显示.Location = New Point(190, 8)
        MB_当前声道数显示.Margin = New Padding(2)
        MB_当前声道数显示.Name = "MB_当前声道数显示"
        MB_当前声道数显示.Size = New Size(40, 24)
        MB_当前声道数显示.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_当前声道数显示.TabIndex = 40
        MB_当前声道数显示.Text = "声道"
        ' 
        ' JEC_当前声道数显示前面的空白占位
        ' 
        JEC_当前声道数显示前面的空白占位.Dock = DockStyle.Left
        JEC_当前声道数显示前面的空白占位.Location = New Point(185, 8)
        JEC_当前声道数显示前面的空白占位.Name = "JEC_当前声道数显示前面的空白占位"
        JEC_当前声道数显示前面的空白占位.Size = New Size(5, 24)
        JEC_当前声道数显示前面的空白占位.TabIndex = 39
        ' 
        ' MB_当前音频编码显示
        ' 
        MB_当前音频编码显示.BackColor = Color.Transparent
        MB_当前音频编码显示.BackColor1 = Color.FromArgb(CByte(40), CByte(0), CByte(0), CByte(0))
        MB_当前音频编码显示.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_当前音频编码显示.BorderRadius = 5
        MB_当前音频编码显示.Dock = DockStyle.Left
        MB_当前音频编码显示.Font = New Font("Microsoft YaHei UI", 9F)
        MB_当前音频编码显示.Location = New Point(145, 8)
        MB_当前音频编码显示.Margin = New Padding(2)
        MB_当前音频编码显示.Name = "MB_当前音频编码显示"
        MB_当前音频编码显示.Size = New Size(40, 24)
        MB_当前音频编码显示.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_当前音频编码显示.TabIndex = 38
        MB_当前音频编码显示.Text = "音频"
        ' 
        ' JEC_当前音频编码显示前面的空白占位
        ' 
        JEC_当前音频编码显示前面的空白占位.Dock = DockStyle.Left
        JEC_当前音频编码显示前面的空白占位.Location = New Point(140, 8)
        JEC_当前音频编码显示前面的空白占位.Name = "JEC_当前音频编码显示前面的空白占位"
        JEC_当前音频编码显示前面的空白占位.Size = New Size(5, 24)
        JEC_当前音频编码显示前面的空白占位.TabIndex = 37
        ' 
        ' MB_当前视频编码显示
        ' 
        MB_当前视频编码显示.BackColor = Color.Transparent
        MB_当前视频编码显示.BackColor1 = Color.FromArgb(CByte(40), CByte(0), CByte(0), CByte(0))
        MB_当前视频编码显示.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_当前视频编码显示.BorderRadius = 5
        MB_当前视频编码显示.Dock = DockStyle.Left
        MB_当前视频编码显示.Font = New Font("Microsoft YaHei UI", 9F)
        MB_当前视频编码显示.Location = New Point(100, 8)
        MB_当前视频编码显示.Margin = New Padding(2)
        MB_当前视频编码显示.Name = "MB_当前视频编码显示"
        MB_当前视频编码显示.Size = New Size(40, 24)
        MB_当前视频编码显示.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_当前视频编码显示.TabIndex = 36
        MB_当前视频编码显示.Text = "视频"
        ' 
        ' JEC_当前视频编码显示前面的空白占位
        ' 
        JEC_当前视频编码显示前面的空白占位.Dock = DockStyle.Left
        JEC_当前视频编码显示前面的空白占位.Location = New Point(95, 8)
        JEC_当前视频编码显示前面的空白占位.Name = "JEC_当前视频编码显示前面的空白占位"
        JEC_当前视频编码显示前面的空白占位.Size = New Size(5, 24)
        JEC_当前视频编码显示前面的空白占位.TabIndex = 42
        ' 
        ' MB_HDR模式
        ' 
        MB_HDR模式.BackColor = Color.Transparent
        MB_HDR模式.BackColor1 = Color.FromArgb(CByte(40), CByte(0), CByte(0), CByte(0))
        MB_HDR模式.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_HDR模式.BorderRadius = 5
        MB_HDR模式.Dock = DockStyle.Left
        MB_HDR模式.Font = New Font("Microsoft YaHei UI", 9F)
        MB_HDR模式.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_HDR模式.Location = New Point(55, 8)
        MB_HDR模式.Margin = New Padding(2)
        MB_HDR模式.Name = "MB_HDR模式"
        MB_HDR模式.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_HDR模式.Size = New Size(40, 24)
        MB_HDR模式.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_HDR模式.TabIndex = 41
        MB_HDR模式.Text = "色彩"
        ' 
        ' JEC_HDR选项前面的空白占位
        ' 
        JEC_HDR选项前面的空白占位.Dock = DockStyle.Left
        JEC_HDR选项前面的空白占位.Location = New Point(50, 8)
        JEC_HDR选项前面的空白占位.Name = "JEC_HDR选项前面的空白占位"
        JEC_HDR选项前面的空白占位.Size = New Size(5, 24)
        JEC_HDR选项前面的空白占位.TabIndex = 35
        ' 
        ' MB_软件解码或硬件解码
        ' 
        MB_软件解码或硬件解码.BackColor = Color.Transparent
        MB_软件解码或硬件解码.BackColor1 = Color.FromArgb(CByte(40), CByte(0), CByte(0), CByte(0))
        MB_软件解码或硬件解码.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_软件解码或硬件解码.BorderRadius = 5
        MB_软件解码或硬件解码.Dock = DockStyle.Left
        MB_软件解码或硬件解码.Font = New Font("Microsoft YaHei UI", 9F)
        MB_软件解码或硬件解码.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_软件解码或硬件解码.Location = New Point(10, 8)
        MB_软件解码或硬件解码.Margin = New Padding(2)
        MB_软件解码或硬件解码.Name = "MB_软件解码或硬件解码"
        MB_软件解码或硬件解码.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_软件解码或硬件解码.Size = New Size(40, 24)
        MB_软件解码或硬件解码.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_软件解码或硬件解码.TabIndex = 34
        MB_软件解码或硬件解码.Text = "解码"
        ' 
        ' JustEmptyControl19
        ' 
        JustEmptyControl19.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl19.Dock = DockStyle.Right
        JustEmptyControl19.Location = New Point(634, 0)
        JustEmptyControl19.Name = "JustEmptyControl19"
        JustEmptyControl19.Size = New Size(2, 40)
        JustEmptyControl19.TabIndex = 55
        ' 
        ' MB_选择流
        ' 
        MB_选择流.BackColor = Color.Transparent
        MB_选择流.BackColor1 = Color.Transparent
        MB_选择流.BorderSize = 0
        MB_选择流.Dock = DockStyle.Right
        MB_选择流.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_选择流.Location = New Point(636, 0)
        MB_选择流.Margin = New Padding(2)
        MB_选择流.Name = "MB_选择流"
        MB_选择流.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_选择流.Size = New Size(40, 40)
        MB_选择流.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_选择流.TabIndex = 54
        MB_选择流.Text = "📀"
        ' 
        ' JustEmptyControl18
        ' 
        JustEmptyControl18.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl18.Dock = DockStyle.Right
        JustEmptyControl18.Location = New Point(676, 0)
        JustEmptyControl18.Name = "JustEmptyControl18"
        JustEmptyControl18.Size = New Size(2, 40)
        JustEmptyControl18.TabIndex = 53
        ' 
        ' MB_查看当前媒体信息
        ' 
        MB_查看当前媒体信息.BackColor = Color.Transparent
        MB_查看当前媒体信息.BackColor1 = Color.Transparent
        MB_查看当前媒体信息.BorderSize = 0
        MB_查看当前媒体信息.Dock = DockStyle.Right
        MB_查看当前媒体信息.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_查看当前媒体信息.Location = New Point(678, 0)
        MB_查看当前媒体信息.Margin = New Padding(2)
        MB_查看当前媒体信息.Name = "MB_查看当前媒体信息"
        MB_查看当前媒体信息.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_查看当前媒体信息.Size = New Size(40, 40)
        MB_查看当前媒体信息.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_查看当前媒体信息.TabIndex = 52
        MB_查看当前媒体信息.Text = "📄"
        ' 
        ' JustEmptyControl13
        ' 
        JustEmptyControl13.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl13.Dock = DockStyle.Right
        JustEmptyControl13.Location = New Point(718, 0)
        JustEmptyControl13.Name = "JustEmptyControl13"
        JustEmptyControl13.Size = New Size(2, 40)
        JustEmptyControl13.TabIndex = 50
        ' 
        ' MB_剪辑区间模式
        ' 
        MB_剪辑区间模式.BackColor = Color.Transparent
        MB_剪辑区间模式.BackColor1 = Color.Transparent
        MB_剪辑区间模式.BorderSize = 0
        MB_剪辑区间模式.Dock = DockStyle.Right
        MB_剪辑区间模式.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_剪辑区间模式.Location = New Point(720, 0)
        MB_剪辑区间模式.Margin = New Padding(2)
        MB_剪辑区间模式.Name = "MB_剪辑区间模式"
        MB_剪辑区间模式.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_剪辑区间模式.Size = New Size(40, 40)
        MB_剪辑区间模式.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_剪辑区间模式.TabIndex = 49
        MB_剪辑区间模式.Text = "✂️"
        ' 
        ' JustEmptyControl12
        ' 
        JustEmptyControl12.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl12.Dock = DockStyle.Right
        JustEmptyControl12.Location = New Point(760, 0)
        JustEmptyControl12.Name = "JustEmptyControl12"
        JustEmptyControl12.Size = New Size(2, 40)
        JustEmptyControl12.TabIndex = 48
        ' 
        ' MB_播放列表
        ' 
        MB_播放列表.BackColor = Color.Transparent
        MB_播放列表.BackColor1 = Color.Transparent
        MB_播放列表.BorderSize = 0
        MB_播放列表.Dock = DockStyle.Right
        MB_播放列表.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_播放列表.Location = New Point(762, 0)
        MB_播放列表.Margin = New Padding(2)
        MB_播放列表.Name = "MB_播放列表"
        MB_播放列表.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_播放列表.Size = New Size(40, 40)
        MB_播放列表.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_播放列表.TabIndex = 47
        MB_播放列表.Text = "💽"
        ' 
        ' JustEmptyControl11
        ' 
        JustEmptyControl11.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl11.Dock = DockStyle.Right
        JustEmptyControl11.Location = New Point(802, 0)
        JustEmptyControl11.Name = "JustEmptyControl11"
        JustEmptyControl11.Size = New Size(2, 40)
        JustEmptyControl11.TabIndex = 46
        ' 
        ' MB_软件设置
        ' 
        MB_软件设置.BackColor = Color.Transparent
        MB_软件设置.BackColor1 = Color.Transparent
        MB_软件设置.BorderSize = 0
        MB_软件设置.Dock = DockStyle.Right
        MB_软件设置.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_软件设置.Location = New Point(804, 0)
        MB_软件设置.Margin = New Padding(2)
        MB_软件设置.Name = "MB_软件设置"
        MB_软件设置.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_软件设置.Size = New Size(40, 40)
        MB_软件设置.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_软件设置.TabIndex = 45
        MB_软件设置.Text = "⚙️"
        ' 
        ' JustEmptyControl10
        ' 
        JustEmptyControl10.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl10.Dock = DockStyle.Left
        JustEmptyControl10.Location = New Point(360, 0)
        JustEmptyControl10.Name = "JustEmptyControl10"
        JustEmptyControl10.Size = New Size(2, 40)
        JustEmptyControl10.TabIndex = 44
        ' 
        ' HCL_时间戳显示
        ' 
        HCL_时间戳显示.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HCL_时间戳显示.Dock = DockStyle.Left
        HCL_时间戳显示.Location = New Point(210, 0)
        HCL_时间戳显示.Margin = New Padding(2)
        HCL_时间戳显示.Name = "HCL_时间戳显示"
        HCL_时间戳显示.Padding = New Padding(10, 0, 10, 0)
        HCL_时间戳显示.Size = New Size(150, 40)
        HCL_时间戳显示.TabIndex = 43
        HCL_时间戳显示.Text = "00:00:00 / 00:00:00"
        HCL_时间戳显示.TextAlign = LakeUI.HtmlColorLabel.TextAlignEnum.Center
        ' 
        ' JustEmptyControl9
        ' 
        JustEmptyControl9.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl9.Dock = DockStyle.Left
        JustEmptyControl9.Location = New Point(208, 0)
        JustEmptyControl9.Name = "JustEmptyControl9"
        JustEmptyControl9.Size = New Size(2, 40)
        JustEmptyControl9.TabIndex = 42
        ' 
        ' MB_打开文件
        ' 
        MB_打开文件.BackColor = Color.Transparent
        MB_打开文件.BackColor1 = Color.Transparent
        MB_打开文件.BorderSize = 0
        MB_打开文件.Dock = DockStyle.Left
        MB_打开文件.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_打开文件.Location = New Point(168, 0)
        MB_打开文件.Margin = New Padding(2)
        MB_打开文件.Name = "MB_打开文件"
        MB_打开文件.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_打开文件.Size = New Size(40, 40)
        MB_打开文件.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_打开文件.TabIndex = 41
        MB_打开文件.Text = "⏏️"
        ' 
        ' JustEmptyControl8
        ' 
        JustEmptyControl8.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl8.Dock = DockStyle.Left
        JustEmptyControl8.Location = New Point(166, 0)
        JustEmptyControl8.Name = "JustEmptyControl8"
        JustEmptyControl8.Size = New Size(2, 40)
        JustEmptyControl8.TabIndex = 40
        ' 
        ' MB_快进或下一个
        ' 
        MB_快进或下一个.BackColor = Color.Transparent
        MB_快进或下一个.BackColor1 = Color.Transparent
        MB_快进或下一个.BorderSize = 0
        MB_快进或下一个.Dock = DockStyle.Left
        MB_快进或下一个.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_快进或下一个.Location = New Point(126, 0)
        MB_快进或下一个.Margin = New Padding(2)
        MB_快进或下一个.Name = "MB_快进或下一个"
        MB_快进或下一个.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_快进或下一个.Size = New Size(40, 40)
        MB_快进或下一个.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_快进或下一个.TabIndex = 39
        MB_快进或下一个.Text = "⏩"
        ' 
        ' JustEmptyControl7
        ' 
        JustEmptyControl7.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl7.Dock = DockStyle.Left
        JustEmptyControl7.Location = New Point(124, 0)
        JustEmptyControl7.Name = "JustEmptyControl7"
        JustEmptyControl7.Size = New Size(2, 40)
        JustEmptyControl7.TabIndex = 38
        ' 
        ' MB_倒退或上一个
        ' 
        MB_倒退或上一个.BackColor = Color.Transparent
        MB_倒退或上一个.BackColor1 = Color.Transparent
        MB_倒退或上一个.BorderSize = 0
        MB_倒退或上一个.Dock = DockStyle.Left
        MB_倒退或上一个.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_倒退或上一个.Location = New Point(84, 0)
        MB_倒退或上一个.Margin = New Padding(2)
        MB_倒退或上一个.Name = "MB_倒退或上一个"
        MB_倒退或上一个.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_倒退或上一个.Size = New Size(40, 40)
        MB_倒退或上一个.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_倒退或上一个.TabIndex = 37
        MB_倒退或上一个.Text = "⏪"
        ' 
        ' JustEmptyControl6
        ' 
        JustEmptyControl6.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl6.Dock = DockStyle.Left
        JustEmptyControl6.Location = New Point(82, 0)
        JustEmptyControl6.Name = "JustEmptyControl6"
        JustEmptyControl6.Size = New Size(2, 40)
        JustEmptyControl6.TabIndex = 36
        ' 
        ' MB_停止
        ' 
        MB_停止.BackColor = Color.Transparent
        MB_停止.BackColor1 = Color.Transparent
        MB_停止.BorderSize = 0
        MB_停止.Dock = DockStyle.Left
        MB_停止.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_停止.Location = New Point(42, 0)
        MB_停止.Margin = New Padding(2)
        MB_停止.Name = "MB_停止"
        MB_停止.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_停止.Size = New Size(40, 40)
        MB_停止.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_停止.TabIndex = 35
        MB_停止.Text = "🔳"
        ' 
        ' JustEmptyControl5
        ' 
        JustEmptyControl5.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl5.Dock = DockStyle.Left
        JustEmptyControl5.Location = New Point(40, 0)
        JustEmptyControl5.Name = "JustEmptyControl5"
        JustEmptyControl5.Size = New Size(2, 40)
        JustEmptyControl5.TabIndex = 34
        ' 
        ' MB_播放和暂停
        ' 
        MB_播放和暂停.BackColor = Color.Transparent
        MB_播放和暂停.BackColor1 = Color.Transparent
        MB_播放和暂停.BorderSize = 0
        MB_播放和暂停.Dock = DockStyle.Left
        MB_播放和暂停.HoverBackColor1 = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MB_播放和暂停.Location = New Point(0, 0)
        MB_播放和暂停.Margin = New Padding(2)
        MB_播放和暂停.Name = "MB_播放和暂停"
        MB_播放和暂停.PressedBackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_播放和暂停.Size = New Size(40, 40)
        MB_播放和暂停.SubTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MB_播放和暂停.TabIndex = 33
        MB_播放和暂停.Text = "▶️"
        ' 
        ' JustEmptyControl3
        ' 
        JustEmptyControl3.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl3.Dock = DockStyle.Top
        JustEmptyControl3.Location = New Point(0, 23)
        JustEmptyControl3.Name = "JustEmptyControl3"
        JustEmptyControl3.Size = New Size(844, 2)
        JustEmptyControl3.TabIndex = 30
        ' 
        ' Panel2
        ' 
        Panel2.BackColor = Color.Transparent
        Panel2.Controls.Add(JustEmptyControl4)
        Panel2.Controls.Add(ETB_媒体进度条)
        Panel2.Controls.Add(ETB_音量条)
        Panel2.Dock = DockStyle.Top
        Panel2.Location = New Point(0, 1)
        Panel2.Name = "Panel2"
        Panel2.Size = New Size(844, 22)
        Panel2.TabIndex = 31
        ' 
        ' JustEmptyControl4
        ' 
        JustEmptyControl4.BackColor = Color.FromArgb(CByte(80), CByte(0), CByte(0), CByte(0))
        JustEmptyControl4.Dock = DockStyle.Right
        JustEmptyControl4.Location = New Point(732, 0)
        JustEmptyControl4.Name = "JustEmptyControl4"
        JustEmptyControl4.Size = New Size(2, 22)
        JustEmptyControl4.TabIndex = 32
        ' 
        ' ETB_媒体进度条
        ' 
        ETB_媒体进度条.BackColor = Color.Transparent
        ETB_媒体进度条.Dock = DockStyle.Fill
        ETB_媒体进度条.LabelColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_媒体进度条.LabelLineColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_媒体进度条.LabelLineLength = 16
        ETB_媒体进度条.Location = New Point(0, 0)
        ETB_媒体进度条.Margin = New Padding(2, 2, 2, 2)
        ETB_媒体进度条.Name = "ETB_媒体进度条"
        ETB_媒体进度条.Padding = New Padding(8, 0, 8, 0)
        ETB_媒体进度条.Size = New Size(734, 22)
        ETB_媒体进度条.TabIndex = 30
        ETB_媒体进度条.ThumbBorderWidth = 0
        ETB_媒体进度条.ThumbColor = Color.FromArgb(CByte(220), CByte(255), CByte(255), CByte(255))
        ETB_媒体进度条.ThumbHeight = 12
        ETB_媒体进度条.ThumbRadius = 6
        ETB_媒体进度条.ThumbTextDecimalPlaces = 0
        ETB_媒体进度条.ThumbWidth = 12
        ETB_媒体进度条.TrackColor = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        ETB_媒体进度条.TrackFillColor = Color.OliveDrab
        ETB_媒体进度条.TrackThickness = 4
        ETB_媒体进度条.Value = 100R
        ' 
        ' ETB_音量条
        ' 
        ETB_音量条.BackColor = Color.Transparent
        ETB_音量条.Dock = DockStyle.Right
        ETB_音量条.LabelColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_音量条.LabelLineColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_音量条.LabelLineLength = 16
        ETB_音量条.Location = New Point(734, 0)
        ETB_音量条.Margin = New Padding(2, 2, 2, 2)
        ETB_音量条.Name = "ETB_音量条"
        ETB_音量条.Padding = New Padding(8, 0, 8, 0)
        ETB_音量条.Size = New Size(110, 22)
        ETB_音量条.TabIndex = 31
        ETB_音量条.ThumbBorderWidth = 0
        ETB_音量条.ThumbColor = Color.FromArgb(CByte(220), CByte(255), CByte(255), CByte(255))
        ETB_音量条.ThumbHeight = 12
        ETB_音量条.ThumbRadius = 6
        ETB_音量条.ThumbTextDecimalPlaces = 0
        ETB_音量条.ThumbWidth = 12
        ETB_音量条.TrackColor = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        ETB_音量条.TrackFillColor = Color.MediumSlateBlue
        ETB_音量条.TrackThickness = 4
        ETB_音量条.Value = 100R
        ' 
        ' JustEmptyControl1
        ' 
        JustEmptyControl1.BackColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        JustEmptyControl1.Dock = DockStyle.Top
        JustEmptyControl1.Location = New Point(0, 0)
        JustEmptyControl1.Name = "JustEmptyControl1"
        JustEmptyControl1.Size = New Size(844, 1)
        JustEmptyControl1.TabIndex = 1
        ' 
        ' MP_剪辑区间操作容器
        ' 
        MP_剪辑区间操作容器.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MP_剪辑区间操作容器.BorderSize = 0
        MP_剪辑区间操作容器.Controls.Add(JustEmptyControl2)
        MP_剪辑区间操作容器.Dock = DockStyle.Bottom
        MP_剪辑区间操作容器.Location = New Point(0, 415)
        MP_剪辑区间操作容器.Name = "MP_剪辑区间操作容器"
        MP_剪辑区间操作容器.Size = New Size(844, 31)
        MP_剪辑区间操作容器.TabIndex = 1
        MP_剪辑区间操作容器.Visible = False
        ' 
        ' JustEmptyControl2
        ' 
        JustEmptyControl2.BackColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        JustEmptyControl2.Dock = DockStyle.Top
        JustEmptyControl2.Location = New Point(0, 0)
        JustEmptyControl2.Name = "JustEmptyControl2"
        JustEmptyControl2.Size = New Size(844, 1)
        JustEmptyControl2.TabIndex = 0
        ' 
        ' MP_DX视频容器
        ' 
        MP_DX视频容器.Controls.Add(Panel1)
        MP_DX视频容器.Dock = DockStyle.Fill
        MP_DX视频容器.Location = New Point(0, 0)
        MP_DX视频容器.Name = "MP_DX视频容器"
        MP_DX视频容器.Size = New Size(844, 415)
        MP_DX视频容器.TabIndex = 2
        ' 
        ' Form1
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.Black
        ClientSize = New Size(844, 511)
        Controls.Add(MP_DX视频容器)
        Controls.Add(MP_剪辑区间操作容器)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form1"
        StartPosition = FormStartPosition.CenterScreen
        Text = "FFF.Player"
        Panel1.ResumeLayout(False)
        ModernPanel1.ResumeLayout(False)
        Panel3.ResumeLayout(False)
        Panel4.ResumeLayout(False)
        Panel2.ResumeLayout(False)
        MP_剪辑区间操作容器.ResumeLayout(False)
        MP_DX视频容器.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ThisIsYourWindow1 As LakeUI.ThisIsYourWindow
    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents MP_剪辑区间操作容器 As LakeUI.ModernPanel
    Friend WithEvents JustEmptyControl2 As LakeUI.JustEmptyControl
    Friend WithEvents JustEmptyControl1 As LakeUI.JustEmptyControl
    Friend WithEvents MP_DX视频容器 As Panel
    Friend WithEvents JustEmptyControl3 As LakeUI.JustEmptyControl
    Friend WithEvents Panel2 As Panel
    Friend WithEvents ETB_媒体进度条 As LakeUI.ExcellentTrackBar
    Friend WithEvents ETB_音量条 As LakeUI.ExcellentTrackBar
    Friend WithEvents JustEmptyControl4 As LakeUI.JustEmptyControl
    Friend WithEvents Panel3 As Panel
    Friend WithEvents MB_播放和暂停 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl5 As LakeUI.JustEmptyControl
    Friend WithEvents JustEmptyControl9 As LakeUI.JustEmptyControl
    Friend WithEvents MB_打开文件 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl8 As LakeUI.JustEmptyControl
    Friend WithEvents MB_快进或下一个 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl7 As LakeUI.JustEmptyControl
    Friend WithEvents MB_倒退或上一个 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl6 As LakeUI.JustEmptyControl
    Friend WithEvents MB_停止 As LakeUI.ModernButton
    Friend WithEvents HCL_时间戳显示 As LakeUI.HtmlColorLabel
    Friend WithEvents JustEmptyControl10 As LakeUI.JustEmptyControl
    Friend WithEvents JustEmptyControl11 As LakeUI.JustEmptyControl
    Friend WithEvents MB_软件设置 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl13 As LakeUI.JustEmptyControl
    Friend WithEvents MB_剪辑区间模式 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl12 As LakeUI.JustEmptyControl
    Friend WithEvents MB_播放列表 As LakeUI.ModernButton
    Friend WithEvents Panel4 As Panel
    Friend WithEvents MB_软件解码或硬件解码 As LakeUI.ModernButton
    Friend WithEvents MB_当前声道数显示 As LakeUI.ModernButton
    Friend WithEvents JEC_当前声道数显示前面的空白占位 As LakeUI.JustEmptyControl
    Friend WithEvents MB_当前音频编码显示 As LakeUI.ModernButton
    Friend WithEvents JEC_当前音频编码显示前面的空白占位 As LakeUI.JustEmptyControl
    Friend WithEvents MB_当前视频编码显示 As LakeUI.ModernButton
    Friend WithEvents JEC_HDR选项前面的空白占位 As LakeUI.JustEmptyControl
    Friend WithEvents JEC_当前视频编码显示前面的空白占位 As LakeUI.JustEmptyControl
    Friend WithEvents MB_HDR模式 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl18 As LakeUI.JustEmptyControl
    Friend WithEvents MB_查看当前媒体信息 As LakeUI.ModernButton
    Friend WithEvents JustEmptyControl19 As LakeUI.JustEmptyControl
    Friend WithEvents MB_选择流 As LakeUI.ModernButton
    Friend WithEvents Panel1 As Panel
    Friend WithEvents MB_1 As LakeUI.ModernButton

End Class
