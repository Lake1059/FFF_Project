Imports System.Reflection

Public NotInheritable Class 字体控制
    Private Sub New()
    End Sub

    Public Shared Sub 更新所有控件字体属性()
        Dim 字体名称 = 设置.实例对象.字体
        设置控件字体(字体名称, Form1.当前主窗体, Nothing, True)
        设置控件字体(字体名称, Form总控台, Nothing, True)
        设置控件字体(字体名称, Form输出设置, Nothing, True)
        设置控件字体(字体名称, Form视频参数, Nothing, True)
        设置控件字体(字体名称, Form音频参数, Nothing, True)
        设置控件字体(字体名称, Form设置, {Form设置.MCB_全局字体}, True)
        设置控件字体(字体名称, Form个性化, Nothing, True)
        设置控件字体(字体名称, Form支持者, Nothing, True)
    End Sub

    Private Shared Sub 设置控件字体(字体名称 As String, 容器 As Control,
                                  Optional 排除控件 As Control() = Nothing,
                                  Optional 包含容器 As Boolean = False)
        If 容器 Is Nothing OrElse String.IsNullOrWhiteSpace(字体名称) Then Return
        If 包含容器 Then 替换字体(容器, 字体名称)
        For Each 控件 As Control In 容器.Controls
            If 排除控件?.Contains(控件) Then Continue For
            替换字体(控件, 字体名称)
            If 控件.HasChildren Then 设置控件字体(字体名称, 控件, 排除控件)
        Next
    End Sub

    Private Shared Sub 替换字体(控件 As Control, 字体名称 As String)
        Try
            Dim 字体属性 = 控件.GetType().GetProperty("Font", BindingFlags.Instance Or BindingFlags.Public Or BindingFlags.NonPublic)
            If 字体属性 Is Nothing OrElse Not 字体属性.CanWrite Then Return
            Dim 当前字体 = 控件.Font
            字体属性.SetValue(控件, New Font(字体名称, 当前字体.Size, 当前字体.Style))
        Catch
        End Try
    End Sub
End Class
