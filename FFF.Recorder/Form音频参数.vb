Public Class Form音频参数
    Private 正在初始化 As Boolean

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            设置索引(MCB_采样率, {44100, 48000, 96000, 192000}.ToList().IndexOf(设置.实例对象.音频采样率), 1)
            设置索引(MCB_声道数, 设置.实例对象.音频声道, 1)
            设置索引(MCB_音频编码器, 设置.实例对象.音频编码器索引)
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MCB_采样率_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_采样率.SelectedIndexChanged
        If Not 正在初始化 AndAlso MCB_采样率.SelectedIndex >= 0 Then 设置.实例对象.音频采样率 = Integer.Parse(MCB_采样率.Text)
    End Sub

    Private Sub MCB_声道数_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_声道数.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.音频声道 = Math.Max(0, MCB_声道数.SelectedIndex)
    End Sub

    Private Sub MCB_音频编码器_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_音频编码器.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.音频编码器索引 = Math.Max(0, MCB_音频编码器.SelectedIndex)
    End Sub

    Private Shared Sub 设置索引(控件 As LakeUI.ModernComboBox, 索引 As Integer, Optional 回退 As Integer = 0)
        If 控件.Items.Count = 0 Then Return
        If 索引 < 0 OrElse 索引 >= 控件.Items.Count Then 索引 = Math.Clamp(回退, 0, 控件.Items.Count - 1)
        控件.SelectedIndex = 索引
    End Sub
End Class
