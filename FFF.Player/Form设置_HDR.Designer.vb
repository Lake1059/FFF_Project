<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_HDR
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
        Panel1 = New LakeUI.ModernPanel()
        ETB_HDR映射SDR亮度 = New LakeUI.ExcellentTrackBar()
        HtmlColorLabel2 = New LakeUI.HtmlColorLabel()
        Panel2 = New LakeUI.ModernPanel()
        MTB_自定义真实HDR峰值亮度 = New LakeUI.ModernTextBox()
        JustEmptyControl1 = New LakeUI.JustEmptyControl()
        MCB_真实HDR峰值亮度选项 = New LakeUI.ModernComboBox()
        HtmlColorLabel1 = New LakeUI.HtmlColorLabel()
        ModernPanel1.SuspendLayout()
        Panel1.SuspendLayout()
        Panel2.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(Panel1)
        ModernPanel1.Controls.Add(HtmlColorLabel2)
        ModernPanel1.Controls.Add(Panel2)
        ModernPanel1.Controls.Add(HtmlColorLabel1)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(20)
        ModernPanel1.Size = New Size(697, 494)
        ModernPanel1.TabIndex = 0
        ' 
        ' Panel1
        ' 
        Panel1.BackColor = Color.Transparent
        Panel1.BackColor1 = Color.Transparent
        Panel1.BorderSize = 0
        Panel1.Controls.Add(ETB_HDR映射SDR亮度)
        Panel1.Dock = DockStyle.Top
        Panel1.Location = New Point(20, 128)
        Panel1.Name = "Panel1"
        Panel1.Padding = New Padding(0, 10, 0, 0)
        Panel1.Size = New Size(657, 42)
        Panel1.TabIndex = 19
        ' 
        ' ETB_HDR映射SDR亮度
        ' 
        ETB_HDR映射SDR亮度.BackColor = Color.Transparent
        ETB_HDR映射SDR亮度.Dock = DockStyle.Fill
        ETB_HDR映射SDR亮度.LabelColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_HDR映射SDR亮度.LabelLineColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_HDR映射SDR亮度.LabelLineLength = 16
        ETB_HDR映射SDR亮度.Location = New Point(0, 10)
        ETB_HDR映射SDR亮度.Margin = New Padding(2, 2, 2, 2)
        ETB_HDR映射SDR亮度.Maximum = 500R
        ETB_HDR映射SDR亮度.Minimum = 1R
        ETB_HDR映射SDR亮度.Name = "ETB_HDR映射SDR亮度"
        ETB_HDR映射SDR亮度.Size = New Size(657, 32)
        ETB_HDR映射SDR亮度.TabIndex = 30
        ETB_HDR映射SDR亮度.ThumbBorderWidth = 0
        ETB_HDR映射SDR亮度.ThumbColor = Color.OliveDrab
        ETB_HDR映射SDR亮度.ThumbHeight = 28
        ETB_HDR映射SDR亮度.ThumbTextDecimalPlaces = 0
        ETB_HDR映射SDR亮度.ThumbTextMode = LakeUI.ExcellentTrackBar.ThumbTextModeEnum.Value
        ETB_HDR映射SDR亮度.ThumbWidth = 38
        ETB_HDR映射SDR亮度.TrackColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ETB_HDR映射SDR亮度.TrackFillColor = Color.Transparent
        ETB_HDR映射SDR亮度.Value = 250R
        ' 
        ' HtmlColorLabel2
        ' 
        HtmlColorLabel2.AutoSize = True
        HtmlColorLabel2.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel2.Dock = DockStyle.Top
        HtmlColorLabel2.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel2.Location = New Point(20, 85)
        HtmlColorLabel2.Margin = New Padding(2)
        HtmlColorLabel2.Name = "HtmlColorLabel2"
        HtmlColorLabel2.Padding = New Padding(0, 20, 0, 0)
        HtmlColorLabel2.Size = New Size(657, 43)
        HtmlColorLabel2.TabIndex = 18
        HtmlColorLabel2.Text = "<span style=""font-size:13; color:Silver"">映射 SDR 参考亮度</span>   仅影响 HDR 片源的映射 SDR 模式"
        ' 
        ' Panel2
        ' 
        Panel2.BackColor = Color.Transparent
        Panel2.BackColor1 = Color.Transparent
        Panel2.BorderSize = 0
        Panel2.Controls.Add(MTB_自定义真实HDR峰值亮度)
        Panel2.Controls.Add(JustEmptyControl1)
        Panel2.Controls.Add(MCB_真实HDR峰值亮度选项)
        Panel2.Dock = DockStyle.Top
        Panel2.Location = New Point(20, 43)
        Panel2.Name = "Panel2"
        Panel2.Padding = New Padding(0, 10, 0, 0)
        Panel2.Size = New Size(657, 42)
        Panel2.TabIndex = 17
        ' 
        ' MTB_自定义真实HDR峰值亮度
        ' 
        MTB_自定义真实HDR峰值亮度.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义真实HDR峰值亮度.BorderColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义真实HDR峰值亮度.BorderColorFocus = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MTB_自定义真实HDR峰值亮度.BorderRadius = 10
        MTB_自定义真实HDR峰值亮度.CaretColor = Color.FromArgb(CByte(220), CByte(220), CByte(220))
        MTB_自定义真实HDR峰值亮度.Dock = DockStyle.Left
        MTB_自定义真实HDR峰值亮度.Location = New Point(210, 10)
        MTB_自定义真实HDR峰值亮度.Margin = New Padding(2)
        MTB_自定义真实HDR峰值亮度.Name = "MTB_自定义真实HDR峰值亮度"
        MTB_自定义真实HDR峰值亮度.Padding = New Padding(10, 0, 10, 0)
        MTB_自定义真实HDR峰值亮度.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MTB_自定义真实HDR峰值亮度.Size = New Size(100, 32)
        MTB_自定义真实HDR峰值亮度.TabIndex = 16
        MTB_自定义真实HDR峰值亮度.WaterText = "峰值亮度"
        ' 
        ' JustEmptyControl1
        ' 
        JustEmptyControl1.Dock = DockStyle.Left
        JustEmptyControl1.Location = New Point(200, 10)
        JustEmptyControl1.Name = "JustEmptyControl1"
        JustEmptyControl1.Size = New Size(10, 32)
        JustEmptyControl1.TabIndex = 15
        ' 
        ' MCB_真实HDR峰值亮度选项
        ' 
        MCB_真实HDR峰值亮度选项.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_真实HDR峰值亮度选项.BorderRadius = 10
        MCB_真实HDR峰值亮度选项.BorderSize = 0
        MCB_真实HDR峰值亮度选项.Dock = DockStyle.Left
        MCB_真实HDR峰值亮度选项.DropDownBackdropBlurPasses = 2
        MCB_真实HDR峰值亮度选项.DropDownBackdropBlurRadius = 30
        MCB_真实HDR峰值亮度选项.DropDownBackdropMode = LakeUI.PopupBackdropMode.Auto
        MCB_真实HDR峰值亮度选项.DropDownHoverColor = Color.FromArgb(CByte(20), CByte(220), CByte(220), CByte(220))
        MCB_真实HDR峰值亮度选项.DropDownMode = LakeUI.ModernComboBox.DropDownDisplayMode.Overlay
        MCB_真实HDR峰值亮度选项.DropDownPadding = New Padding(10)
        MCB_真实HDR峰值亮度选项.DropDownSelectedColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_真实HDR峰值亮度选项.DropDownSelectedForeColor = Color.White
        MCB_真实HDR峰值亮度选项.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCB_真实HDR峰值亮度选项.Items.Add("跟随显示器最大亮度")
        MCB_真实HDR峰值亮度选项.Items.Add("自定义")
        MCB_真实HDR峰值亮度选项.Items.Add("400nit")
        MCB_真实HDR峰值亮度选项.Items.Add("500nit")
        MCB_真实HDR峰值亮度选项.Items.Add("600nit")
        MCB_真实HDR峰值亮度选项.Items.Add("700nit")
        MCB_真实HDR峰值亮度选项.Items.Add("800nit")
        MCB_真实HDR峰值亮度选项.Items.Add("900nit")
        MCB_真实HDR峰值亮度选项.Items.Add("1000nit")
        MCB_真实HDR峰值亮度选项.Items.Add("2000nit")
        MCB_真实HDR峰值亮度选项.Location = New Point(0, 10)
        MCB_真实HDR峰值亮度选项.Margin = New Padding(2, 2, 2, 2)
        MCB_真实HDR峰值亮度选项.Name = "MCB_真实HDR峰值亮度选项"
        MCB_真实HDR峰值亮度选项.Padding = New Padding(10, 0, 10, 0)
        MCB_真实HDR峰值亮度选项.SelectionColor = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        MCB_真实HDR峰值亮度选项.Size = New Size(200, 32)
        MCB_真实HDR峰值亮度选项.TabIndex = 0
        MCB_真实HDR峰值亮度选项.ToolTipGap = -1
        MCB_真实HDR峰值亮度选项.ToolTipMaxWidth = 350
        MCB_真实HDR峰值亮度选项.ToolTipPadding = New Padding(15)
        MCB_真实HDR峰值亮度选项.WaterTextForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        ' 
        ' HtmlColorLabel1
        ' 
        HtmlColorLabel1.AutoSize = True
        HtmlColorLabel1.AutoSizeMode = AutoSizeMode.GrowAndShrink
        HtmlColorLabel1.Dock = DockStyle.Top
        HtmlColorLabel1.ForeColor = Color.FromArgb(CByte(120), CByte(255), CByte(255), CByte(255))
        HtmlColorLabel1.Location = New Point(20, 20)
        HtmlColorLabel1.Margin = New Padding(2)
        HtmlColorLabel1.Name = "HtmlColorLabel1"
        HtmlColorLabel1.Size = New Size(657, 23)
        HtmlColorLabel1.TabIndex = 16
        HtmlColorLabel1.Text = "<span style=""font-size:13; color:Silver"">真实 HDR 峰值亮度</span>   仅影响 HDR 片源的真实 HDR 模式"
        ' 
        ' Form设置_HDR
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(697, 494)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_HDR"
        Text = "Form设置_HDR"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        Panel1.ResumeLayout(False)
        Panel2.ResumeLayout(False)
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents Panel2 As LakeUI.ModernPanel
    Friend WithEvents MTB_自定义真实HDR峰值亮度 As LakeUI.ModernTextBox
    Friend WithEvents JustEmptyControl1 As LakeUI.JustEmptyControl
    Friend WithEvents MCB_真实HDR峰值亮度选项 As LakeUI.ModernComboBox
    Friend WithEvents HtmlColorLabel1 As LakeUI.HtmlColorLabel
    Friend WithEvents Panel1 As LakeUI.ModernPanel
    Friend WithEvents HtmlColorLabel2 As LakeUI.HtmlColorLabel
    Friend WithEvents ETB_HDR映射SDR亮度 As LakeUI.ExcellentTrackBar
End Class
