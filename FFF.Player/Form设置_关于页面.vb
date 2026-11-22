Public Class Form设置_关于页面
    Private Const 项目主页 As String = "https://github.com/Lake1059/FFF_Project"

    Private Sub 打开项目主页(sender As Object, e As EventArgs) Handles MB_项目主页.Click
        Try
            Diagnostics.Process.Start(New Diagnostics.ProcessStartInfo(项目主页) With {.UseShellExecute = True})
        Catch ex As Exception
            MessageBox.Show(Me, "无法打开项目主页：" & ex.Message, "项目主页", MessageBoxButtons.OK, MessageBoxIcon.Information)
        End Try
    End Sub
End Class