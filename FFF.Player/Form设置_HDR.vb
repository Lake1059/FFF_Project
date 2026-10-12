Public Class Form设置_HDR
    Private Shared ReadOnly 预设峰值 As Integer() = {0, 0, 400, 500, 600, 700, 800, 900, 1000, 2000}
    Private 正在初始化 As Boolean

    Public Sub New()
        正在初始化 = True
        Try
            InitializeComponent()
        Finally
            正在初始化 = False
        End Try
        更新自定义控件状态()
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            MCB_真实HDR峰值亮度选项.SelectedIndex = 设置.实例对象.HDR峰值亮度选项
            MTB_自定义真实HDR峰值亮度.Text = If(设置.实例对象.HDR峰值亮度 > 0,
                                               设置.实例对象.HDR峰值亮度.ToString(), String.Empty)
            ETB_HDR映射SDR亮度.Value = 设置.实例对象.HDR映射SDR参考亮度
            更新自定义控件状态()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MCB_真实HDR峰值亮度选项_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_真实HDR峰值亮度选项.SelectedIndexChanged
        If 正在初始化 OrElse MCB_真实HDR峰值亮度选项.SelectedIndex < 0 Then Return
        Dim index = MCB_真实HDR峰值亮度选项.SelectedIndex
        设置.实例对象.HDR峰值亮度选项 = index
        If index <> 1 Then 设置.实例对象.HDR峰值亮度 = 预设峰值(index)
        更新自定义控件状态
        Form1.当前主窗体?.应用HDR峰值设置
    End Sub

    Private Sub MTB_自定义真实HDR峰值亮度_TextChanged(sender As Object, e As EventArgs) Handles MTB_自定义真实HDR峰值亮度.TextChanged
        If 正在初始化 OrElse MCB_真实HDR峰值亮度选项.SelectedIndex <> 1 Then Return
        Dim value As Integer
        If Integer.TryParse(MTB_自定义真实HDR峰值亮度.Text, value) AndAlso value >= 100 AndAlso value <= 10000 Then
            设置.实例对象.HDR峰值亮度 = value
            Form1.当前主窗体?.应用HDR峰值设置
        End If
    End Sub

    Private Sub ETB_HDR映射SDR亮度_ValueChanged(sender As Object, e As EventArgs) Handles ETB_HDR映射SDR亮度.ValueChanged
        If 正在初始化 Then Return
        设置.实例对象.HDR映射SDR参考亮度 = CInt(Math.Round(ETB_HDR映射SDR亮度.Value))
        Form1.当前主窗体?.应用SDR亮度设置()
    End Sub

    Private Sub 更新自定义控件状态()
        Dim 显示自定义峰值 = MCB_真实HDR峰值亮度选项.SelectedIndex = 1
        MTB_自定义真实HDR峰值亮度.Visible = 显示自定义峰值
        JustEmptyControl1.Visible = 显示自定义峰值
    End Sub
End Class
