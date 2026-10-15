<Global.Microsoft.VisualBasic.CompilerServices.DesignerGenerated()> _
Partial Class Form设置_音乐与歌词
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
        MCK_是否启用歌词支持 = New LakeUI.ModernCheckBox()
        MCK_是否渲染封面图毛玻璃背景 = New LakeUI.ModernCheckBox()
        MCK_是否渲染封面图 = New LakeUI.ModernCheckBox()
        ModernPanel1.SuspendLayout()
        SuspendLayout()
        ' 
        ' ModernPanel1
        ' 
        ModernPanel1.BackColor1 = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ModernPanel1.BorderSize = 0
        ModernPanel1.Controls.Add(MCK_是否启用歌词支持)
        ModernPanel1.Controls.Add(MCK_是否渲染封面图毛玻璃背景)
        ModernPanel1.Controls.Add(MCK_是否渲染封面图)
        ModernPanel1.Dock = DockStyle.Fill
        ModernPanel1.Location = New Point(0, 0)
        ModernPanel1.Name = "ModernPanel1"
        ModernPanel1.Padding = New Padding(10, 20, 20, 20)
        ModernPanel1.Size = New Size(598, 433)
        ModernPanel1.TabIndex = 0
        ' 
        ' MCK_是否启用歌词支持
        ' 
        MCK_是否启用歌词支持.AutoSize = True
        MCK_是否启用歌词支持.BackColor = Color.Transparent
        MCK_是否启用歌词支持.BoxBorderRadius = 5
        MCK_是否启用歌词支持.BoxBorderSize = 0
        MCK_是否启用歌词支持.BoxCheckedBackColor = Color.OliveDrab
        MCK_是否启用歌词支持.BoxInnerPadding = 6
        MCK_是否启用歌词支持.BoxSize = 24
        MCK_是否启用歌词支持.BoxTextSpacing = 10
        MCK_是否启用歌词支持.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_是否启用歌词支持.Checked = True
        MCK_是否启用歌词支持.Dock = DockStyle.Top
        MCK_是否启用歌词支持.Location = New Point(10, 88)
        MCK_是否启用歌词支持.Name = "MCK_是否启用歌词支持"
        MCK_是否启用歌词支持.Padding = New Padding(0, 0, 0, 10)
        MCK_是否启用歌词支持.Size = New Size(568, 34)
        MCK_是否启用歌词支持.TabIndex = 31
        MCK_是否启用歌词支持.Text = "启用 LRC 歌词支持"
        ' 
        ' MCK_是否渲染封面图毛玻璃背景
        ' 
        MCK_是否渲染封面图毛玻璃背景.AutoSize = True
        MCK_是否渲染封面图毛玻璃背景.BackColor = Color.Transparent
        MCK_是否渲染封面图毛玻璃背景.BoxBorderRadius = 5
        MCK_是否渲染封面图毛玻璃背景.BoxBorderSize = 0
        MCK_是否渲染封面图毛玻璃背景.BoxCheckedBackColor = Color.OliveDrab
        MCK_是否渲染封面图毛玻璃背景.BoxInnerPadding = 6
        MCK_是否渲染封面图毛玻璃背景.BoxSize = 24
        MCK_是否渲染封面图毛玻璃背景.BoxTextSpacing = 10
        MCK_是否渲染封面图毛玻璃背景.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_是否渲染封面图毛玻璃背景.Checked = True
        MCK_是否渲染封面图毛玻璃背景.Dock = DockStyle.Top
        MCK_是否渲染封面图毛玻璃背景.Location = New Point(10, 54)
        MCK_是否渲染封面图毛玻璃背景.Name = "MCK_是否渲染封面图毛玻璃背景"
        MCK_是否渲染封面图毛玻璃背景.Padding = New Padding(0, 0, 0, 10)
        MCK_是否渲染封面图毛玻璃背景.Size = New Size(568, 34)
        MCK_是否渲染封面图毛玻璃背景.TabIndex = 30
        MCK_是否渲染封面图毛玻璃背景.Text = "渲染封面图毛玻璃背景"
        ' 
        ' MCK_是否渲染封面图
        ' 
        MCK_是否渲染封面图.AutoSize = True
        MCK_是否渲染封面图.BackColor = Color.Transparent
        MCK_是否渲染封面图.BoxBorderRadius = 5
        MCK_是否渲染封面图.BoxBorderSize = 0
        MCK_是否渲染封面图.BoxCheckedBackColor = Color.OliveDrab
        MCK_是否渲染封面图.BoxInnerPadding = 6
        MCK_是否渲染封面图.BoxSize = 24
        MCK_是否渲染封面图.BoxTextSpacing = 10
        MCK_是否渲染封面图.BoxUncheckedBackColor = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        MCK_是否渲染封面图.Checked = True
        MCK_是否渲染封面图.Dock = DockStyle.Top
        MCK_是否渲染封面图.Location = New Point(10, 20)
        MCK_是否渲染封面图.Name = "MCK_是否渲染封面图"
        MCK_是否渲染封面图.Padding = New Padding(0, 0, 0, 10)
        MCK_是否渲染封面图.Size = New Size(568, 34)
        MCK_是否渲染封面图.TabIndex = 29
        MCK_是否渲染封面图.Text = "渲染封面图"
        ' 
        ' Form设置_音乐与歌词
        ' 
        AutoScaleDimensions = New SizeF(96F, 96F)
        AutoScaleMode = AutoScaleMode.Dpi
        BackColor = Color.FromArgb(CByte(24), CByte(24), CByte(24))
        ClientSize = New Size(598, 433)
        Controls.Add(ModernPanel1)
        Font = New Font("Microsoft YaHei UI", 10F)
        ForeColor = Color.Silver
        Name = "Form设置_音乐与歌词"
        Text = "Form设置_音乐与歌词"
        ModernPanel1.ResumeLayout(False)
        ModernPanel1.PerformLayout()
        ResumeLayout(False)
    End Sub

    Friend WithEvents ModernPanel1 As LakeUI.ModernPanel
    Friend WithEvents MCK_是否启用歌词支持 As LakeUI.ModernCheckBox
    Friend WithEvents MCK_是否渲染封面图毛玻璃背景 As LakeUI.ModernCheckBox
    Friend WithEvents MCK_是否渲染封面图 As LakeUI.ModernCheckBox
End Class
