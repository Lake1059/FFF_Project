<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_AI字幕
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
        LBL_说明 = New Label()
        LBL_包目录 = New Label()
        MTB_包目录 = New LakeUI.ModernTextBox()
        MB_浏览 = New LakeUI.ModernButton()
        LBL_设备 = New Label()
        MCB_设备 = New LakeUI.ModernComboBox()
        LBL_语言 = New Label()
        MCB_语言 = New LakeUI.ModernComboBox()
        MCK_仅所选语言 = New LakeUI.ModernCheckBox()
        LBL_状态 = New Label()
        MCK_显示高级 = New LakeUI.ModernCheckBox()
        LBL_窗口秒 = New Label()
        MTB_窗口秒 = New LakeUI.ModernTextBox()
        LBL_步长秒 = New Label()
        MTB_步长秒 = New LakeUI.ModernTextBox()
        LBL_设备序号 = New Label()
        MTB_设备序号 = New LakeUI.ModernTextBox()
        LBL_模型档 = New Label()
        MCB_模型档 = New LakeUI.ModernComboBox()
        LBL_Gpu层数 = New Label()
        MTB_Gpu层数 = New LakeUI.ModernTextBox()
        ModernPanel1.SuspendLayout()
        SuspendLayout()
        '
        ' LBL_模型档
        '
        LBL_模型档.AutoSize = False
        LBL_模型档.BackColor = Color.Transparent
        LBL_模型档.ForeColor = Color.Silver
        LBL_模型档.Location = New Point(400, 392)
        LBL_模型档.Name = "LBL_模型档"
        LBL_模型档.Size = New Size(178, 20)
        LBL_模型档.TabIndex = 17
        LBL_模型档.Text = "模型档位"
        LBL_模型档.Visible = False
        '
        ' MCB_模型档
        '
        MCB_模型档.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_模型档.BorderRadius = 10
        MCB_模型档.BorderSize = 0
        MCB_模型档.DropDownBackdropBlurPasses = 2
        MCB_模型档.DropDownBackdropBlurRadius = 30
        MCB_模型档.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_模型档.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_模型档.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_模型档.DropDownPadding = New Padding(10)
        MCB_模型档.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_模型档.DropDownSelectedForeColor = Color.White
        MCB_模型档.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_模型档.Location = New Point(400, 414)
        MCB_模型档.Margin = New Padding(2)
        MCB_模型档.Name = "MCB_模型档"
        MCB_模型档.Padding = New Padding(10, 0, 10, 0)
        MCB_模型档.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_模型档.Size = New Size(178, 32)
        MCB_模型档.TabIndex = 18
        MCB_模型档.ToolTipGap = -1
        MCB_模型档.ToolTipMaxWidth = 350
        MCB_模型档.ToolTipPadding = New Padding(15)
        MCB_模型档.WaterText = "模型档位"
        MCB_模型档.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        MCB_模型档.Visible = False
        '
        ' LBL_Gpu层数
        '
        LBL_Gpu层数.AutoSize = False
        LBL_Gpu层数.BackColor = Color.Transparent
        LBL_Gpu层数.ForeColor = Color.Silver
        LBL_Gpu层数.Location = New Point(10, 450)
        LBL_Gpu层数.Name = "LBL_Gpu层数"
        LBL_Gpu层数.Size = New Size(120, 20)
        LBL_Gpu层数.TabIndex = 19
        LBL_Gpu层数.Text = "GPU 层数"
        LBL_Gpu层数.Visible = False
        '
        ' MTB_Gpu层数
        '
        MTB_Gpu层数.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_Gpu层数.BorderColor = Color.Transparent
        MTB_Gpu层数.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_Gpu层数.BorderRadius = 10
        MTB_Gpu层数.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_Gpu层数.Location = New Point(10, 472)
        MTB_Gpu层数.Margin = New Padding(2)
        MTB_Gpu层数.Name = "MTB_Gpu层数"
        MTB_Gpu层数.Padding = New Padding(10, 0, 10, 0)
        MTB_Gpu层数.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_Gpu层数.Size = New Size(120, 30)
        MTB_Gpu层数.TabIndex = 20
        MTB_Gpu层数.WaterText = "留空=全部"
        MTB_Gpu层数.WaterTextForeColor = Color.DarkGray
        MTB_Gpu层数.Visible = False
        '
        ' ModernPanel1
        '
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(LBL_说明)
        ModernPanel1.Controls.Add(LBL_包目录)
        ModernPanel1.Controls.Add(MTB_包目录)
        ModernPanel1.Controls.Add(MB_浏览)
        ModernPanel1.Controls.Add(LBL_设备)
        ModernPanel1.Controls.Add(MCB_设备)
        ModernPanel1.Controls.Add(LBL_语言)
        ModernPanel1.Controls.Add(MCB_语言)
        ModernPanel1.Controls.Add(MCK_仅所选语言)
        ModernPanel1.Controls.Add(LBL_状态)
        ModernPanel1.Controls.Add(MCK_显示高级)
        ModernPanel1.Controls.Add(LBL_窗口秒)
        ModernPanel1.Controls.Add(MTB_窗口秒)
        ModernPanel1.Controls.Add(LBL_步长秒)
        ModernPanel1.Controls.Add(MTB_步长秒)
        ModernPanel1.Controls.Add(LBL_设备序号)
        ModernPanel1.Controls.Add(MTB_设备序号)
        ModernPanel1.Controls.Add(LBL_模型档)
        ModernPanel1.Controls.Add(MCB_模型档)
        ModernPanel1.Controls.Add(LBL_Gpu层数)
        ModernPanel1.Controls.Add(MTB_Gpu层数)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(10, 20, 20, 20)
        ModernPanel1.Size = New Size(598, 510)
        ModernPanel1.TabIndex = 0
        '
        ' LBL_说明
        '
        LBL_说明.AutoSize = False
        LBL_说明.BackColor = Color.Transparent
        LBL_说明.ForeColor = Color.Gray
        LBL_说明.Location = New Point(10, 20)
        LBL_说明.Name = "LBL_说明"
        LBL_说明.Size = New Size(568, 52)
        LBL_说明.TabIndex = 0
        LBL_说明.Text = "AI 实时字幕以扩展包形式提供：选一个含 ai-subtitles.json 的目录即可启用，" & vbCrLf &
                        "包里自带 worker、模型与运行环境，本体不需要任何别的配置。" & vbCrLf &
                        "目录失效或删掉时功能自动停用，不影响播放。"
        '
        ' LBL_包目录
        '
        LBL_包目录.AutoSize = False
        LBL_包目录.BackColor = Color.Transparent
        LBL_包目录.ForeColor = Color.Silver
        LBL_包目录.Location = New Point(10, 80)
        LBL_包目录.Name = "LBL_包目录"
        LBL_包目录.Size = New Size(568, 20)
        LBL_包目录.TabIndex = 1
        LBL_包目录.Text = "扩展包目录（含 ai-subtitles.json）"
        '
        ' MTB_包目录
        '
        MTB_包目录.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_包目录.BorderColor = Color.Transparent
        MTB_包目录.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_包目录.BorderRadius = 10
        MTB_包目录.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_包目录.Location = New Point(10, 102)
        MTB_包目录.Margin = New Padding(2)
        MTB_包目录.Name = "MTB_包目录"
        MTB_包目录.Padding = New Padding(10, 0, 10, 0)
        MTB_包目录.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_包目录.Size = New Size(458, 30)
        MTB_包目录.TabIndex = 2
        MTB_包目录.WaterText = "D:\...\3FP-AI-Subtitles"
        MTB_包目录.WaterTextForeColor = Color.DarkGray
        '
        ' MB_浏览
        '
        MB_浏览.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MB_浏览.BorderRadius = 10
        MB_浏览.BorderSize = 0
        MB_浏览.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MB_浏览.Location = New Point(476, 102)
        MB_浏览.Margin = New Padding(2)
        MB_浏览.Name = "MB_浏览"
        MB_浏览.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MB_浏览.Size = New Size(100, 32)
        MB_浏览.TabIndex = 3
        MB_浏览.Text = "浏览…"
        '
        ' LBL_设备
        '
        LBL_设备.AutoSize = False
        LBL_设备.BackColor = Color.Transparent
        LBL_设备.ForeColor = Color.Silver
        LBL_设备.Location = New Point(10, 142)
        LBL_设备.Name = "LBL_设备"
        LBL_设备.Size = New Size(568, 20)
        LBL_设备.TabIndex = 4
        LBL_设备.Text = "推理设备（换显卡厂商要换 llama-server 二进制，见扩展包清单的 servers）"
        '
        ' MCB_设备
        '
        MCB_设备.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_设备.BorderRadius = 10
        MCB_设备.BorderSize = 0
        MCB_设备.DropDownBackdropBlurPasses = 2
        MCB_设备.DropDownBackdropBlurRadius = 30
        MCB_设备.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_设备.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_设备.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_设备.DropDownPadding = New Padding(10)
        MCB_设备.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_设备.DropDownSelectedForeColor = Color.White
        MCB_设备.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_设备.Location = New Point(10, 164)
        MCB_设备.Margin = New Padding(2)
        MCB_设备.Name = "MCB_设备"
        MCB_设备.Padding = New Padding(10, 0, 10, 0)
        MCB_设备.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_设备.Size = New Size(280, 32)
        MCB_设备.TabIndex = 5
        MCB_设备.ToolTipGap = -1
        MCB_设备.ToolTipMaxWidth = 350
        MCB_设备.ToolTipPadding = New Padding(15)
        MCB_设备.WaterText = "推理设备"
        MCB_设备.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' LBL_语言
        '
        LBL_语言.AutoSize = False
        LBL_语言.BackColor = Color.Transparent
        LBL_语言.ForeColor = Color.Silver
        LBL_语言.Location = New Point(10, 206)
        LBL_语言.Name = "LBL_语言"
        LBL_语言.Size = New Size(568, 20)
        LBL_语言.TabIndex = 6
        LBL_语言.Text = "字幕语言（只是给模型的提示，不会翻译；要「只出这个语言」请勾右框）"
        '
        ' MCB_语言
        '
        MCB_语言.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_语言.BorderRadius = 10
        MCB_语言.BorderSize = 0
        MCB_语言.DropDownBackdropBlurPasses = 2
        MCB_语言.DropDownBackdropBlurRadius = 30
        MCB_语言.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_语言.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_语言.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_语言.DropDownPadding = New Padding(10)
        MCB_语言.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_语言.DropDownSelectedForeColor = Color.White
        MCB_语言.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_语言.Location = New Point(10, 228)
        MCB_语言.Margin = New Padding(2)
        MCB_语言.Name = "MCB_语言"
        MCB_语言.Padding = New Padding(10, 0, 10, 0)
        MCB_语言.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_语言.Size = New Size(280, 32)
        MCB_语言.TabIndex = 7
        MCB_语言.ToolTipGap = -1
        MCB_语言.ToolTipMaxWidth = 350
        MCB_语言.ToolTipPadding = New Padding(15)
        MCB_语言.WaterText = "字幕语言"
        MCB_语言.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        '
        ' MCK_仅所选语言
        '
        MCK_仅所选语言.AutoSize = True
        MCK_仅所选语言.BackColor = Color.Transparent
        MCK_仅所选语言.BoxBorderRadius = 5
        MCK_仅所选语言.BoxBorderSize = 0
        MCK_仅所选语言.BoxCheckedBackColor = Color.OliveDrab
        MCK_仅所选语言.BoxInnerPadding = 6
        MCK_仅所选语言.BoxSize = 24
        MCK_仅所选语言.BoxTextSpacing = 10
        MCK_仅所选语言.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_仅所选语言.Location = New Point(310, 232)
        MCK_仅所选语言.Name = "MCK_仅所选语言"
        MCK_仅所选语言.Size = New Size(268, 24)
        MCK_仅所选语言.TabIndex = 8
        MCK_仅所选语言.Text = "只显示所选语言（不符的窗不上屏）"
        '
        ' LBL_状态
        '
        LBL_状态.AutoSize = False
        LBL_状态.BackColor = Color.Transparent
        LBL_状态.ForeColor = Color.Gray
        LBL_状态.Location = New Point(10, 270)
        LBL_状态.Name = "LBL_状态"
        ' 三行：包与窗口/步长、推理设备与语言、"这个包没带该后端二进制"的提醒。
        LBL_状态.Size = New Size(568, 70)
        LBL_状态.TabIndex = 9
        LBL_状态.Text = "未启用。选择一个扩展包目录（含 ai-subtitles.json）即可启用。"
        '
        ' MCK_显示高级
        '
        MCK_显示高级.AutoSize = True
        MCK_显示高级.BackColor = Color.Transparent
        MCK_显示高级.BoxBorderRadius = 5
        MCK_显示高级.BoxBorderSize = 0
        MCK_显示高级.BoxCheckedBackColor = Color.OliveDrab
        MCK_显示高级.BoxInnerPadding = 6
        MCK_显示高级.BoxSize = 24
        MCK_显示高级.BoxTextSpacing = 10
        MCK_显示高级.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_显示高级.Location = New Point(10, 348)
        MCK_显示高级.Name = "MCK_显示高级"
        MCK_显示高级.Padding = New Padding(0, 0, 0, 10)
        MCK_显示高级.Size = New Size(568, 34)
        MCK_显示高级.TabIndex = 10
        MCK_显示高级.Text = "高级选项"
        '
        ' LBL_窗口秒
        '
        LBL_窗口秒.AutoSize = False
        LBL_窗口秒.BackColor = Color.Transparent
        LBL_窗口秒.ForeColor = Color.Silver
        LBL_窗口秒.Location = New Point(10, 392)
        LBL_窗口秒.Name = "LBL_窗口秒"
        LBL_窗口秒.Size = New Size(120, 20)
        LBL_窗口秒.TabIndex = 11
        LBL_窗口秒.Text = "窗口（秒）"
        LBL_窗口秒.Visible = False
        '
        ' MTB_窗口秒
        '
        MTB_窗口秒.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_窗口秒.BorderColor = Color.Transparent
        MTB_窗口秒.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_窗口秒.BorderRadius = 10
        MTB_窗口秒.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_窗口秒.Location = New Point(10, 414)
        MTB_窗口秒.Margin = New Padding(2)
        MTB_窗口秒.Name = "MTB_窗口秒"
        MTB_窗口秒.Padding = New Padding(10, 0, 10, 0)
        MTB_窗口秒.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_窗口秒.Size = New Size(120, 30)
        MTB_窗口秒.TabIndex = 12
        MTB_窗口秒.Visible = False
        '
        ' LBL_步长秒
        '
        LBL_步长秒.AutoSize = False
        LBL_步长秒.BackColor = Color.Transparent
        LBL_步长秒.ForeColor = Color.Silver
        LBL_步长秒.Location = New Point(140, 392)
        LBL_步长秒.Name = "LBL_步长秒"
        LBL_步长秒.Size = New Size(120, 20)
        LBL_步长秒.TabIndex = 13
        LBL_步长秒.Text = "步长（秒）"
        LBL_步长秒.Visible = False
        '
        ' MTB_步长秒
        '
        MTB_步长秒.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_步长秒.BorderColor = Color.Transparent
        MTB_步长秒.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_步长秒.BorderRadius = 10
        MTB_步长秒.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_步长秒.Location = New Point(140, 414)
        MTB_步长秒.Margin = New Padding(2)
        MTB_步长秒.Name = "MTB_步长秒"
        MTB_步长秒.Padding = New Padding(10, 0, 10, 0)
        MTB_步长秒.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_步长秒.Size = New Size(120, 30)
        MTB_步长秒.TabIndex = 14
        MTB_步长秒.Visible = False
        '
        ' LBL_设备序号
        '
        LBL_设备序号.AutoSize = False
        LBL_设备序号.BackColor = Color.Transparent
        LBL_设备序号.ForeColor = Color.Silver
        LBL_设备序号.Location = New Point(270, 392)
        LBL_设备序号.Name = "LBL_设备序号"
        LBL_设备序号.Size = New Size(120, 20)
        LBL_设备序号.TabIndex = 15
        LBL_设备序号.Text = "设备序号"
        LBL_设备序号.Visible = False
        '
        ' MTB_设备序号
        '
        MTB_设备序号.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_设备序号.BorderColor = Color.Transparent
        MTB_设备序号.BorderColorFocus = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        MTB_设备序号.BorderRadius = 10
        MTB_设备序号.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_设备序号.Location = New Point(270, 414)
        MTB_设备序号.Margin = New Padding(2)
        MTB_设备序号.Name = "MTB_设备序号"
        MTB_设备序号.Padding = New Padding(10, 0, 10, 0)
        MTB_设备序号.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_设备序号.Size = New Size(120, 30)
        MTB_设备序号.TabIndex = 16
        MTB_设备序号.WaterText = "0"
        MTB_设备序号.WaterTextForeColor = Color.DarkGray
        MTB_设备序号.Visible = False
        '
        ' Form设置_AI字幕
        '
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(598, 510)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_AI字幕"
        Text = "Form设置_AI字幕"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents LBL_说明 As Label
    Friend WithEvents LBL_包目录 As Label
    Friend WithEvents MTB_包目录 As LakeUI.ModernTextBox
    Friend WithEvents MB_浏览 As LakeUI.ModernButton
    Friend WithEvents LBL_设备 As Label
    Friend WithEvents MCB_设备 As LakeUI.ModernComboBox
    Friend WithEvents LBL_语言 As Label
    Friend WithEvents MCB_语言 As LakeUI.ModernComboBox
    Friend WithEvents MCK_仅所选语言 As LakeUI.ModernCheckBox
    Friend WithEvents LBL_状态 As Label
    Friend WithEvents MCK_显示高级 As LakeUI.ModernCheckBox
    Friend WithEvents LBL_窗口秒 As Label
    Friend WithEvents MTB_窗口秒 As LakeUI.ModernTextBox
    Friend WithEvents LBL_步长秒 As Label
    Friend WithEvents MTB_步长秒 As LakeUI.ModernTextBox
    Friend WithEvents LBL_设备序号 As Label
    Friend WithEvents MTB_设备序号 As LakeUI.ModernTextBox
    Friend WithEvents LBL_模型档 As Label
    Friend WithEvents MCB_模型档 As LakeUI.ModernComboBox
    Friend WithEvents LBL_Gpu层数 As Label
    Friend WithEvents MTB_Gpu层数 As LakeUI.ModernTextBox
End Class
