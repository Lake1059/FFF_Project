Imports System.Reflection

Public NotInheritable Class 字体控制
    Private Sub New()
    End Sub

    Public Shared Sub 更新所有控件字体属性()
        Dim 字体名称 = 设置.实例对象.字体
        设置控件字体(字体名称, Form1.当前主窗体, Nothing, True)
        Form1.当前主窗体?.设置窗口应用字体(字体名称)
    End Sub

    Friend Shared Function 选择字体(ByRef 字体名称 As String, ByRef 字号 As Single,
                              ByRef 字体样式 As Integer) As Boolean
        Try
            ' 设置文件中的字号单位固定为 point；DirectWrite 命令中的 fontSize
            ' 由字幕/弹幕布局另行缩放为画布 DIP，不能把两个边界的数值混存。
            ' 显式指定单位并用 SizeInPoints 回写，避免对话框返回 Pixel 字体时把 48pt 保存成约 64。
            Using 初始字体 As New Font(字体名称, 字号, CType(字体样式, FontStyle), GraphicsUnit.Point)
                Dim 选中字体 As Font = Nothing
                Try
                    Using 对话框 As New LakeUI.ModernFontDialog With {.SelectedFont = 初始字体}
                        Dim 主窗体 = Form1.当前主窗体
                        主窗体?.ThisIsYourWindow1.Attach(对话框)
                        If 对话框.ShowDialog(主窗体) <> DialogResult.OK Then Return False
                        选中字体 = 对话框.SelectedFont
                        写入选择结果(选中字体, 字体名称, 字号, 字体样式)
                        Return True
                    End Using
                Finally
                    If 选中字体 IsNot Nothing AndAlso Not ReferenceEquals(选中字体, 初始字体) Then
                        选中字体.Dispose()
                    End If
                End Try
            End Using
        Catch
            Return False
        End Try
    End Function

    Friend Shared Sub 写入选择结果(选中字体 As Font, ByRef 字体名称 As String,
                              ByRef 字号 As Single, ByRef 字体样式 As Integer)
        ArgumentNullException.ThrowIfNull(选中字体)
        字体名称 = If(String.IsNullOrWhiteSpace(选中字体.Name),
                  选中字体.FontFamily.Name, 选中字体.Name)
        ' Font.Size 使用 Font.Unit，ModernFontDialog 返回 Pixel 字体时 48pt 会约为
        ' 64px。持久化边界只能读取 SizeInPoints，否则每次打开对话框都会再次放大。
        字号 = Math.Clamp(选中字体.SizeInPoints, 8.0F, 200.0F)
        字体样式 = CInt(选中字体.Style)
    End Sub

    Friend Shared Sub 设置控件字体(字体名称 As String, 容器 As Control,
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
            Dim 新字体 As New Font(字体名称, 当前字体.Size, 当前字体.Style, 当前字体.Unit)
            字体属性.SetValue(控件, 新字体)
        Catch
        End Try
    End Sub
End Class
