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
        Dim ModernTabPage1 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage2 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage3 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage4 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage5 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage6 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage7 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage8 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage9 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        Dim ModernTabPage10 As LakeUI.ModernTabListControl.ModernTabPage = New LakeUI.ModernTabListControl.ModernTabPage()
        ModernTabListControl1 = New LakeUI.ModernTabListControl()
        ThisIsYourWindow1 = New LakeUI.ThisIsYourWindow(components)
        SuspendLayout()
        ' 
        ' ModernTabListControl1
        ' 
        ModernTabListControl1.ContentBackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernTabListControl1.Dock = DockStyle.Fill
        ModernTabPage1.IsDescription = True
        ModernTabPage1.Text = "3FR 单源录制器"
        ModernTabPage2.Text = "总控台"
        ModernTabPage3.IsSeparator = True
        ModernTabPage4.Text = "输出设置"
        ModernTabPage5.Text = "视频参数"
        ModernTabPage6.Text = "音频参数"
        ModernTabPage7.IsSeparator = True
        ModernTabPage8.Text = "软件设置"
        ModernTabPage9.Text = "个性化"
        ModernTabPage10.Text = "支持者"
        ModernTabListControl1.Items.Add(ModernTabPage1)
        ModernTabListControl1.Items.Add(ModernTabPage2)
        ModernTabListControl1.Items.Add(ModernTabPage3)
        ModernTabListControl1.Items.Add(ModernTabPage4)
        ModernTabListControl1.Items.Add(ModernTabPage5)
        ModernTabListControl1.Items.Add(ModernTabPage6)
        ModernTabListControl1.Items.Add(ModernTabPage7)
        ModernTabListControl1.Items.Add(ModernTabPage8)
        ModernTabListControl1.Items.Add(ModernTabPage9)
        ModernTabListControl1.Items.Add(ModernTabPage10)
        ModernTabListControl1.Location = New Point(0, 0)
        ModernTabListControl1.Name = "ModernTabListControl1"
        ModernTabListControl1.ScrollBarThumbColor = Color.FromArgb(CByte(40), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.ScrollBarThumbHoverColor = Color.FromArgb(CByte(80), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.ScrollBarTrackColor = Color.FromArgb(CByte(20), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.SeparatorColor = Color.FromArgb(CByte(80), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.Size = New Size(1008, 537)
        ModernTabListControl1.TabIndex = 0
        ModernTabListControl1.TabItemHeight = 32
        ModernTabListControl1.TabItemHoverBackColor = Color.FromArgb(CByte(40), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.TabItemSelectedBackColor = Color.FromArgb(CByte(80), CByte(200), CByte(200), CByte(200))
        ModernTabListControl1.TabStripWidth = 160
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
        ' Form1
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(1008, 537)
        Controls.Add(ModernTabListControl1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        MaximizeBox = False
        Name = "Form1"
        StartPosition = FormStartPosition.CenterParent
        Text = "FFF.Recorder"
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernTabListControl1 As LakeUI.ModernTabListControl
    Friend WithEvents ThisIsYourWindow1 As LakeUI.ThisIsYourWindow

End Class
