Imports LakeUI

Public Class Form设置_字幕
    Private 正在初始化 As Boolean

    Public Sub New()
        正在初始化 = True
        Try
            InitializeComponent()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            ETB_字幕不透明度.Value = 设置.实例对象.字幕不透明度
            MCB_字幕描边样式.SelectedIndex = 设置.实例对象.字幕描边样式
            MCB_字幕阴影样式.SelectedIndex = 设置.实例对象.字幕阴影样式
            ETB_字幕行间距.Value = 设置.实例对象.字幕行间距
            ETB_字幕底部间距.Value = 设置.实例对象.字幕底部间距
            MCB_底部对齐方式.SelectedIndex = 设置.实例对象.字幕底部对齐方式
            MCB_尺寸缩放方式.SelectedIndex = 设置.实例对象.字幕尺寸缩放方式
            更新颜色预览()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MB_设置第一行字体样式_Click(sender As Object, e As EventArgs) Handles MB_设置第一行字体样式.Click
        If 字体控制.选择字体(设置.实例对象.字幕第一行字体, 设置.实例对象.字幕第一行字号,
                         设置.实例对象.字幕第一行样式) Then 应用更改()
    End Sub

    Private Sub MB_设置第二行字体样式_Click(sender As Object, e As EventArgs) Handles MB_设置第二行字体样式.Click
        If 字体控制.选择字体(设置.实例对象.字幕第二行字体, 设置.实例对象.字幕第二行字号,
                         设置.实例对象.字幕第二行样式) Then 应用更改()
    End Sub

    Private Sub MB_设置其他位置字体样式_Click(sender As Object, e As EventArgs) Handles MB_设置其他位置字体样式.Click
        If 字体控制.选择字体(设置.实例对象.字幕其他行字体, 设置.实例对象.字幕其他行字号,
                         设置.实例对象.字幕其他行样式) Then 应用更改()
    End Sub

    Private Sub MB_设置第一行文字颜色_Click(sender As Object, e As EventArgs) Handles MB_设置第一行文字颜色.Click
        Dim color = 从ARGB(设置.实例对象.字幕第一行颜色ARGB)
        If 选择颜色(color) Then
            设置.实例对象.字幕第一行颜色ARGB = 到ARGB(color)
            更新颜色预览()
            应用更改()
        End If
    End Sub

    Private Sub MB_设置第二行文字颜色_Click(sender As Object, e As EventArgs) Handles MB_设置第二行文字颜色.Click
        Dim color = 从ARGB(设置.实例对象.字幕第二行颜色ARGB)
        If 选择颜色(color) Then
            设置.实例对象.字幕第二行颜色ARGB = 到ARGB(color)
            更新颜色预览()
            应用更改()
        End If
    End Sub

    Private Sub MB_重置第一行样式和颜色_Click(sender As Object, e As EventArgs) Handles MB_重置第一行样式和颜色.Click
        设置.实例对象.字幕第一行字体 = "Microsoft YaHei UI"
        设置.实例对象.字幕第一行字号 = 48
        设置.实例对象.字幕第一行样式 = CInt(FontStyle.Regular)
        设置.实例对象.字幕第一行颜色ARGB = &HFFFFFFFFUI
        更新颜色预览()
        应用更改()
    End Sub

    Private Sub MB_重置第二行样式和颜色_Click(sender As Object, e As EventArgs) Handles MB_重置第二行样式和颜色.Click
        设置.实例对象.字幕第二行字体 = "Microsoft YaHei UI"
        设置.实例对象.字幕第二行字号 = 48
        设置.实例对象.字幕第二行样式 = CInt(FontStyle.Regular)
        设置.实例对象.字幕第二行颜色ARGB = &HFFFFFFFFUI
        更新颜色预览()
        应用更改()
    End Sub

    Private Sub MB_重置其他位置样式_Click(sender As Object, e As EventArgs) Handles MB_重置其他位置样式.Click
        设置.实例对象.字幕其他行字体 = "Microsoft YaHei UI"
        设置.实例对象.字幕其他行字号 = 48
        设置.实例对象.字幕其他行样式 = CInt(FontStyle.Regular)
        应用更改()
    End Sub

    Private Sub ETB_字幕行间距_ValueChanged(sender As Object, e As EventArgs) Handles ETB_字幕行间距.ValueChanged
        If 正在初始化 Then Return
        设置.实例对象.字幕行间距 = CInt(Math.Round(ETB_字幕行间距.Value))
        应用更改()
    End Sub

    Private Sub ETB_字幕不透明度_ValueChanged(sender As Object, e As EventArgs) Handles ETB_字幕不透明度.ValueChanged
        If 正在初始化 Then Return
        设置.实例对象.字幕不透明度 = CInt(Math.Round(ETB_字幕不透明度.Value))
        应用更改()
    End Sub

    Private Sub ETB_字幕底部间距_ValueChanged(sender As Object, e As EventArgs) Handles ETB_字幕底部间距.ValueChanged
        If 正在初始化 Then Return
        设置.实例对象.字幕底部间距 = CInt(Math.Round(ETB_字幕底部间距.Value))
        应用更改()
    End Sub

    Private Sub MCB_底部对齐方式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_底部对齐方式.SelectedIndexChanged
        If 正在初始化 OrElse MCB_底部对齐方式.SelectedIndex < 0 Then Return
        设置.实例对象.字幕底部对齐方式 = MCB_底部对齐方式.SelectedIndex
        应用更改()
    End Sub

    Private Sub MCB_尺寸缩放方式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_尺寸缩放方式.SelectedIndexChanged
        If 正在初始化 OrElse MCB_尺寸缩放方式.SelectedIndex < 0 Then Return
        设置.实例对象.字幕尺寸缩放方式 = MCB_尺寸缩放方式.SelectedIndex
        应用更改()
    End Sub

    Private Sub 字幕效果_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_字幕描边样式.SelectedIndexChanged, MCB_字幕阴影样式.SelectedIndexChanged
        If 正在初始化 Then Return
        If MCB_字幕描边样式.SelectedIndex >= 0 Then 设置.实例对象.字幕描边样式 = MCB_字幕描边样式.SelectedIndex
        If MCB_字幕阴影样式.SelectedIndex >= 0 Then 设置.实例对象.字幕阴影样式 = MCB_字幕阴影样式.SelectedIndex
        应用更改()
    End Sub

    Private Shared Function 选择颜色(ByRef color As Color) As Boolean
        Using dialog As New ModernColorDialog With {.SelectedColor = color, .Icon = Form1.当前主窗体?.Icon}
            Form1.当前主窗体?.ThisIsYourWindow1.Attach(dialog)
            If dialog.ShowDialog(Form1.当前主窗体) <> DialogResult.OK Then Return False
            color = dialog.SelectedColor
            Return True
        End Using
    End Function

    Private Sub 更新颜色预览()
        MB_设置第一行文字颜色.ForeColor = 从ARGB(设置.实例对象.字幕第一行颜色ARGB)
        MB_设置第二行文字颜色.ForeColor = 从ARGB(设置.实例对象.字幕第二行颜色ARGB)
    End Sub

    Private Shared Function 从ARGB(value As UInteger) As Color
        Return Color.FromArgb(BitConverter.ToInt32(BitConverter.GetBytes(value), 0))
    End Function

    Private Shared Function 到ARGB(value As Color) As UInteger
        Return BitConverter.ToUInt32(BitConverter.GetBytes(value.ToArgb()), 0)
    End Function

    Private Shared Sub 应用更改()
        Form1.当前主窗体?.应用字幕设置()
    End Sub
End Class
