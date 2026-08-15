Public Class Form设置_文件关联

    Private 正在初始化 As Boolean

    Public Sub New()
        正在初始化 = True
        Try
            InitializeComponent()
            填写扩展名说明()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            MCK_关联常见视频.Checked = 设置.实例对象.关联常见视频
            MCK_关联不常见视频.Checked = 设置.实例对象.关联不常见视频
            MCK_关联老旧视频.Checked = 设置.实例对象.关联老旧视频
            MCK_关联常见音频.Checked = 设置.实例对象.关联常见音频
            MCK_关联不常见音频.Checked = 设置.实例对象.关联不常见音频
            MCK_关联老旧音频.Checked = 设置.实例对象.关联老旧音频
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub 填写扩展名说明()
        MCK_关联常见视频.SubText = 取得扩展名说明(文件关联类别.常见视频)
        MCK_关联不常见视频.SubText = 取得扩展名说明(文件关联类别.不常见视频)
        MCK_关联老旧视频.SubText = 取得扩展名说明(文件关联类别.老旧视频)
        MCK_关联常见音频.SubText = 取得扩展名说明(文件关联类别.常见音频)
        MCK_关联不常见音频.SubText = 取得扩展名说明(文件关联类别.不常见音频)
        MCK_关联老旧音频.SubText = 取得扩展名说明(文件关联类别.老旧音频)
    End Sub

    Private Shared Function 取得扩展名说明(类别 As 文件关联类别) As String
        Return String.Join("  ", 文件关联管理器.取得扩展名(类别))
    End Function

    Private Async Sub 关联_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_关联常见视频.CheckedChanged,
        MCK_关联不常见视频.CheckedChanged,
        MCK_关联老旧视频.CheckedChanged, MCK_关联常见音频.CheckedChanged,
        MCK_关联不常见音频.CheckedChanged, MCK_关联老旧音频.CheckedChanged

        If 正在初始化 Then Return
        Dim checkBox = DirectCast(sender, LakeUI.ModernCheckBox)
        If ReferenceEquals(checkBox, MCK_关联常见视频) Then
            设置.实例对象.关联常见视频 = checkBox.Checked
        ElseIf ReferenceEquals(checkBox, MCK_关联不常见视频) Then
            设置.实例对象.关联不常见视频 = checkBox.Checked
        ElseIf ReferenceEquals(checkBox, MCK_关联老旧视频) Then
            设置.实例对象.关联老旧视频 = checkBox.Checked
        ElseIf ReferenceEquals(checkBox, MCK_关联常见音频) Then
            设置.实例对象.关联常见音频 = checkBox.Checked
        ElseIf ReferenceEquals(checkBox, MCK_关联不常见音频) Then
            设置.实例对象.关联不常见音频 = checkBox.Checked
        ElseIf ReferenceEquals(checkBox, MCK_关联老旧音频) Then
            设置.实例对象.关联老旧音频 = checkBox.Checked
        Else
            Return
        End If

        设置.退出时保存设置()
        Try
            Await 文件关联管理器.同步全部Async(文件关联选项.从设置(设置.实例对象))
        Catch ex As Exception
            If Not IsDisposed Then
                Dim owner = If(Form1.当前主窗体 IsNot Nothing,
                               DirectCast(Form1.当前主窗体, Form), FindForm())
                LakeUI.ExOverlayMsgBox(owner, $"更新文件关联失败：{ex.Message}",
                    MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, "文件关联")
            End If
        End Try
    End Sub

    Private Sub Form设置_文件关联_Load(sender As Object, e As EventArgs) Handles MyBase.Load

    End Sub
End Class
