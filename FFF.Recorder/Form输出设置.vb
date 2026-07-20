Public Class Form输出设置
    Private 正在初始化 As Boolean

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            If 设置.实例对象.输出目录 = Application.StartupPath Then
                设置索引(MCB_输出位置, 0)
            ElseIf 设置.实例对象.输出目录 = Environment.GetFolderPath(Environment.SpecialFolder.MyVideos) Then
                设置索引(MCB_输出位置, 1)
            ElseIf 设置.实例对象.输出目录 = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory) Then
                设置索引(MCB_输出位置, 2)
            Else
                MCB_输出位置.SelectedIndex = -1
                MCB_输出位置.Text = 设置.实例对象.输出目录
            End If
            设置索引(MCB_自动命名方式, 设置.实例对象.自动命名方式)
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MCB_输出位置_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_输出位置.SelectedIndexChanged
        If 正在初始化 Then Return
        Select Case MCB_输出位置.SelectedIndex
            Case 0
                设置.实例对象.输出目录 = Application.StartupPath
            Case 1
                设置.实例对象.输出目录 = Environment.GetFolderPath(Environment.SpecialFolder.MyVideos)
            Case 2
                设置.实例对象.输出目录 = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory)
            Case 3
                Using 选择目录 As New FolderBrowserDialog()
                    If 选择目录.ShowDialog(Form1.当前主窗体) = DialogResult.OK Then 设置.实例对象.输出目录 = 选择目录.SelectedPath
                End Using
        End Select
    End Sub

    Private Sub MCB_自动命名方式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_自动命名方式.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.自动命名方式 = Math.Max(0, MCB_自动命名方式.SelectedIndex)
    End Sub

    Private Shared Sub 设置索引(控件 As LakeUI.ModernComboBox, 索引 As Integer, Optional 回退 As Integer = 0)
        If 控件.Items.Count = 0 Then Return
        If 索引 < 0 OrElse 索引 >= 控件.Items.Count Then 索引 = Math.Clamp(回退, 0, 控件.Items.Count - 1)
        控件.SelectedIndex = 索引
    End Sub

    Private Sub Form输出设置_Load(sender As Object, e As EventArgs) Handles MyBase.Load

    End Sub
End Class
