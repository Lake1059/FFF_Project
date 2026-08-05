Public Class Form设置_弹幕
    Private 正在初始化 As Boolean

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            ETB_弹幕不透明度.Value = 设置.实例对象.弹幕不透明度
            MCB_弹幕描边样式.SelectedIndex = 设置.实例对象.弹幕描边样式
            MCB_弹幕阴影样式.SelectedIndex = 设置.实例对象.弹幕阴影样式
            MCK_是否渲染常规滚动弹幕.Checked = 设置.实例对象.渲染常规滚动弹幕
            MCK_是否渲染顶部弹幕.Checked = 设置.实例对象.渲染顶部弹幕
            MCK_是否渲染底部弹幕.Checked = 设置.实例对象.渲染底部弹幕
            ETB_弹幕最大行数.Value = 设置.实例对象.弹幕最大行数
            ETB_弹幕最大渲染数量.Value = 设置.实例对象.弹幕最大渲染数量
            ETB_弹幕行内前后间距.Value = 设置.实例对象.弹幕行内前后间距
            ETB_弹幕滚动速度.Value = 设置.实例对象.弹幕滚动速度
            MCB_弹幕尺寸缩放方式.SelectedIndex = 设置.实例对象.弹幕尺寸缩放方式
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MB_设置弹幕字体样式_Click(sender As Object, e As EventArgs) Handles MB_设置弹幕字体样式.Click
        If 字体控制.选择字体(设置.实例对象.弹幕字体, 设置.实例对象.弹幕字号,
                         设置.实例对象.弹幕字体样式) Then
            应用更改()
        End If
    End Sub

    Private Sub MB_重置弹幕字体样式_Click(sender As Object, e As EventArgs) Handles MB_重置弹幕字体样式.Click
        设置.实例对象.弹幕字体 = "Microsoft YaHei UI"
        设置.实例对象.弹幕字号 = 36
        设置.实例对象.弹幕字体样式 = CInt(FontStyle.Regular)
        应用更改()
    End Sub

    Private Sub 选项_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_弹幕描边样式.SelectedIndexChanged, MCB_弹幕阴影样式.SelectedIndexChanged, MCB_弹幕尺寸缩放方式.SelectedIndexChanged
        If 正在初始化 Then Return
        If MCB_弹幕描边样式.SelectedIndex >= 0 Then 设置.实例对象.弹幕描边样式 = MCB_弹幕描边样式.SelectedIndex
        If MCB_弹幕阴影样式.SelectedIndex >= 0 Then 设置.实例对象.弹幕阴影样式 = MCB_弹幕阴影样式.SelectedIndex
        If MCB_弹幕尺寸缩放方式.SelectedIndex >= 0 Then 设置.实例对象.弹幕尺寸缩放方式 = MCB_弹幕尺寸缩放方式.SelectedIndex
        应用更改()
    End Sub

    Private Sub 类型_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_是否渲染常规滚动弹幕.CheckedChanged, MCK_是否渲染顶部弹幕.CheckedChanged, MCK_是否渲染底部弹幕.CheckedChanged
        If 正在初始化 Then Return
        设置.实例对象.渲染常规滚动弹幕 = MCK_是否渲染常规滚动弹幕.Checked
        设置.实例对象.渲染顶部弹幕 = MCK_是否渲染顶部弹幕.Checked
        设置.实例对象.渲染底部弹幕 = MCK_是否渲染底部弹幕.Checked
        应用更改()
    End Sub

    Private Sub 数值_ValueChanged(sender As Object, e As EventArgs) Handles ETB_弹幕不透明度.ValueChanged, ETB_弹幕最大行数.ValueChanged, ETB_弹幕最大渲染数量.ValueChanged, ETB_弹幕行内前后间距.ValueChanged, ETB_弹幕滚动速度.ValueChanged
        If 正在初始化 Then Return
        设置.实例对象.弹幕不透明度 = CInt(Math.Round(ETB_弹幕不透明度.Value))
        设置.实例对象.弹幕最大行数 = Math.Max(1, CInt(Math.Round(ETB_弹幕最大行数.Value)))
        设置.实例对象.弹幕最大渲染数量 = Math.Max(1, CInt(Math.Round(ETB_弹幕最大渲染数量.Value)))
        设置.实例对象.弹幕行内前后间距 = CInt(Math.Round(ETB_弹幕行内前后间距.Value))
        设置.实例对象.弹幕滚动速度 = Math.Max(1, CInt(Math.Round(ETB_弹幕滚动速度.Value)))
        应用更改()
    End Sub

    Private Shared Sub 应用更改()
        Form1.当前主窗体?.应用弹幕设置()
    End Sub
End Class
