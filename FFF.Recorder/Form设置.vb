Public Class Form设置
    Private 正在初始化 As Boolean

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            MCB_全局字体.Items.Clear()
            Dim fonts = FontFamily.Families.Select(Function(f) f.Name).
                Distinct(StringComparer.CurrentCultureIgnoreCase).
                OrderBy(Function(x) x).
                ToList()
            For Each fontName In fonts
                MCB_全局字体.Items.Add(fontName)
            Next
            If fonts.Contains("微软雅黑") Then
                MCB_全局字体.Font = New Font("微软雅黑", 10)
            ElseIf fonts.Contains("Microsoft YaHei UI") Then
                MCB_全局字体.Font = New Font("Microsoft YaHei UI", 10)
            End If
            Dim fontIndex = fonts.FindIndex(Function(x) String.Equals(x, 设置.实例对象.字体, StringComparison.CurrentCultureIgnoreCase))
            If fontIndex >= 0 Then
                MCB_全局字体.SelectedIndex = fontIndex
            Else
                MCB_全局字体.Text = 设置.实例对象.字体
            End If
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MCB_全局字体_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_全局字体.SelectedIndexChanged
        If 正在初始化 OrElse MCB_全局字体.SelectedIndex < 0 Then Return
        设置.实例对象.字体 = MCB_全局字体.Text
        字体控制.更新所有控件字体属性()
    End Sub

    Private Sub MB_设定开始录制快捷键_Click(sender As Object, e As EventArgs) Handles MB_设定开始录制快捷键.Click
        Form1.当前主窗体.开始捕获快捷键(1)
    End Sub

    Private Sub MB_设定暂停录制快捷键_Click(sender As Object, e As EventArgs) Handles MB_设定暂停录制快捷键.Click
        Form1.当前主窗体.开始捕获快捷键(2)
    End Sub

    Private Sub MB_设定继续录制快捷键_Click(sender As Object, e As EventArgs) Handles MB_设定继续录制快捷键.Click
        Form1.当前主窗体.开始捕获快捷键(3)
    End Sub

    Private Sub MB_设定停止录制快捷键_Click(sender As Object, e As EventArgs) Handles MB_设定停止录制快捷键.Click
        Form1.当前主窗体.开始捕获快捷键(4)
    End Sub

    Private Sub MB_设定切分文件快捷键_Click(sender As Object, e As EventArgs) Handles MB_设定切分文件快捷键.Click
        Form1.当前主窗体.开始捕获快捷键(5)
    End Sub

    Private Sub MB_清除开始录制快捷键_Click(sender As Object, e As EventArgs) Handles MB_清除开始录制快捷键.Click
        Form1.当前主窗体.清除快捷键(1)
    End Sub

    Private Sub MB_清除暂停录制快捷键_Click(sender As Object, e As EventArgs) Handles MB_清除暂停录制快捷键.Click
        Form1.当前主窗体.清除快捷键(2)
    End Sub

    Private Sub MB_清除继续录制快捷键_Click(sender As Object, e As EventArgs) Handles MB_清除继续录制快捷键.Click
        Form1.当前主窗体.清除快捷键(3)
    End Sub

    Private Sub MB_清除停止录制快捷键_Click(sender As Object, e As EventArgs) Handles MB_清除停止录制快捷键.Click
        Form1.当前主窗体.清除快捷键(4)
    End Sub

    Private Sub MB_清除切分文件快捷键_Click(sender As Object, e As EventArgs) Handles MB_清除切分文件快捷键.Click
        Form1.当前主窗体.清除快捷键(5)
    End Sub

    Private Sub Form设置_Load(sender As Object, e As EventArgs) Handles MyBase.Load

    End Sub
End Class
