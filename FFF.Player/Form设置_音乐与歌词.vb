Public Class Form设置_音乐与歌词
    Private 正在初始化 As Boolean

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            MCK_是否启用歌词支持.Checked = 设置.实例对象.启用歌词支持
            MCK_是否渲染封面图毛玻璃背景.Checked = 设置.实例对象.渲染封面图毛玻璃背景
            MCK_是否渲染封面图.Checked = 设置.实例对象.渲染封面图
            MCK_是否渲染封面图毛玻璃背景.Enabled = MCK_是否渲染封面图.Checked
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub 选项_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_是否启用歌词支持.CheckedChanged, MCK_是否渲染封面图毛玻璃背景.CheckedChanged, MCK_是否渲染封面图.CheckedChanged
        If 正在初始化 Then Return
        设置.实例对象.启用歌词支持 = MCK_是否启用歌词支持.Checked
        设置.实例对象.渲染封面图 = MCK_是否渲染封面图.Checked
        设置.实例对象.渲染封面图毛玻璃背景 = MCK_是否渲染封面图毛玻璃背景.Checked
        MCK_是否渲染封面图毛玻璃背景.Enabled = MCK_是否渲染封面图.Checked
        Form1.当前主窗体?.应用歌词设置()
    End Sub
End Class
