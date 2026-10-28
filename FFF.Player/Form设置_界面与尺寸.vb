Public Class Form设置_界面与尺寸
    Private 正在初始化 As Boolean

    Public Sub New()
        正在初始化 = True
        Try
            InitializeComponent()
        Finally
            正在初始化 = False
        End Try
        更新自定义尺寸控件状态()
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            MCB_全局字体.Items.Clear()
            Dim fonts = FontFamily.Families.Select(Function(f) f.Name).
                Distinct(StringComparer.CurrentCultureIgnoreCase).OrderBy(Function(x) x).ToList()
            For Each fontName In fonts
                MCB_全局字体.Items.Add(fontName)
            Next
            Dim fontIndex = fonts.FindIndex(Function(x) String.Equals(x, 设置.实例对象.字体,
                                                       StringComparison.CurrentCultureIgnoreCase))
            If fontIndex >= 0 Then
                MCB_全局字体.SelectedIndex = fontIndex
            Else
                MCB_全局字体.Text = 设置.实例对象.字体
            End If
            MCB_初始画面尺寸选项.SelectedIndex = 设置.实例对象.初始画面尺寸选项
            MTB_自定义初始画面尺寸宽度.Text = 设置.实例对象.自定义初始画面宽度.ToString()
            MTB_自定义初始画面尺寸高度.Text = 设置.实例对象.自定义初始画面高度.ToString()
            更新自定义尺寸控件状态()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MCB_全局字体_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_全局字体.SelectedIndexChanged
        If 正在初始化 OrElse MCB_全局字体.SelectedIndex < 0 Then Return
        设置.实例对象.字体 = MCB_全局字体.Text
        字体控制.更新所有控件字体属性()
    End Sub

    Private Sub MCB_初始画面尺寸选项_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_初始画面尺寸选项.SelectedIndexChanged
        If 正在初始化 OrElse MCB_初始画面尺寸选项.SelectedIndex < 0 Then Return
        设置.实例对象.初始画面尺寸选项 = MCB_初始画面尺寸选项.SelectedIndex
        更新自定义尺寸控件状态()
    End Sub

    Private Sub 自定义尺寸_TextChanged(sender As Object, e As EventArgs) Handles MTB_自定义初始画面尺寸宽度.TextChanged, MTB_自定义初始画面尺寸高度.TextChanged
        If 正在初始化 Then Return
        Dim width, height As Integer
        If Integer.TryParse(MTB_自定义初始画面尺寸宽度.Text, width) AndAlso width >= 320 AndAlso width <= 7680 Then
            设置.实例对象.自定义初始画面宽度 = width
        End If
        If Integer.TryParse(MTB_自定义初始画面尺寸高度.Text, height) AndAlso height >= 180 AndAlso height <= 4320 Then
            设置.实例对象.自定义初始画面高度 = height
        End If
    End Sub

    Private Sub 更新自定义尺寸控件状态()
        Dim 显示自定义尺寸 = MCB_初始画面尺寸选项.SelectedIndex = 0
        Dim 停靠顺序 As Control() = {
            MTB_自定义初始画面尺寸高度,
            JustEmptyControl2,
            MTB_自定义初始画面尺寸宽度,
            JustEmptyControl1,
            MCB_初始画面尺寸选项
        }
        Dim 自定义尺寸控件 As Control() = {
            JustEmptyControl1,
            MTB_自定义初始画面尺寸宽度,
            JustEmptyControl2,
            MTB_自定义初始画面尺寸高度
        }

        Panel2.SuspendLayout()
        Try
            For Each 控件 In 自定义尺寸控件
                控件.Visible = 显示自定义尺寸
            Next
            ' LakeUI 控件显隐会改变 Z 顺序；显隐后需连同 JEC 间隔恢复 DockStyle.Left 的反向停靠顺序。
            For index = 0 To 停靠顺序.Length - 1
                Panel2.Controls.SetChildIndex(停靠顺序(index), index)
            Next
        Finally
            Panel2.ResumeLayout(True)
        End Try
    End Sub
End Class
