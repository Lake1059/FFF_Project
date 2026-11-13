Public Class Form设置_关于页面
    Private Const 项目主页 As String = "https://github.com/Lake1059/FFF_Project"
    Private ReadOnly 项目主页按钮 As New LakeUI.ModernButton()

    Public Sub New()
        InitializeComponent()
        项目主页按钮.BackColor1 = Color.FromArgb(CByte(40), CByte(220), CByte(220), CByte(220))
        项目主页按钮.BorderRadius = 10
        项目主页按钮.BorderSize = 0
        项目主页按钮.HoverBackColor1 = Color.FromArgb(CByte(60), CByte(220), CByte(220), CByte(220))
        项目主页按钮.PressedBackColor1 = Color.FromArgb(CByte(80), CByte(220), CByte(220), CByte(220))
        项目主页按钮.Location = New Point(15, 300)
        项目主页按钮.Size = New Size(220, 32)
        项目主页按钮.TabIndex = 22
        项目主页按钮.Text = "项目主页 / GitHub"
        项目主页按钮.ToolTipText = 项目主页
        项目主页按钮.ToolTipShowDelaySeconds = 1R
        项目主页按钮.Anchor = AnchorStyles.Top Or AnchorStyles.Left
        AddHandler 项目主页按钮.Click, AddressOf 打开项目主页
        ModernPanel1.Controls.Add(项目主页按钮)
    End Sub

    Private Sub 打开项目主页(sender As Object, e As EventArgs)
        Try
            Diagnostics.Process.Start(New Diagnostics.ProcessStartInfo(项目主页) With {.UseShellExecute = True})
        Catch ex As Exception
            MessageBox.Show(Me, "无法打开项目主页：" & ex.Message, "项目主页", MessageBoxButtons.OK, MessageBoxIcon.Information)
        End Try
    End Sub
End Class