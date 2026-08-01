Public Class Form设置
    Private Sub Form设置_Load(sender As Object, e As EventArgs) Handles Me.Load
        Form1.ThisIsYourWindow1.Attach(Me)
        Me.Location = Me.居中于(Form1.Bounds)
    End Sub

    Private Sub Form设置_Shown(sender As Object, e As EventArgs) Handles Me.Shown

    End Sub

    Private Sub Form设置_SizeChanged(sender As Object, e As EventArgs) Handles Me.SizeChanged

    End Sub

    Private Sub Form设置_FormClosing(sender As Object, e As FormClosingEventArgs) Handles Me.FormClosing

    End Sub
End Class