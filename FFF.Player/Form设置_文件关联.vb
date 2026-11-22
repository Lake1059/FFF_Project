Imports LakeUI

Public Class Form设置_文件关联

    Private 正在回显 As Boolean
    Private 保存已排队 As Boolean

    Public Sub New()
        InitializeComponent()
        UltraDetailListView1.Columns.Add(New UltraDetailListView.ListColumn("扩展名"))
        更新列宽()
    End Sub

    Friend Sub 初始化页面()
        Dim 选项 = 文件关联选项.从设置(设置.实例对象)
        正在回显 = True
        UltraDetailListView1.BeginUpdate()
        Try
            If UltraDetailListView1.Groups.Count = 0 Then
                For Each 类别 In [Enum].GetValues(Of 文件关联类别)()
                    Dim 组名 = 类别.ToString()
                    Dim 标题 = If(类别 = 文件关联类别.相机RAW, "相机 RAW", 组名)
                    UltraDetailListView1.Groups.Add(New UltraDetailListView.ListGroup(组名, 标题))
                    For Each 扩展名 In 文件关联管理器.取得扩展名(类别)
                        Dim 行 As New UltraDetailListView.ListItem(New UltraDetailListView.ListSubItem(扩展名)) With {
                            .GroupName = 组名, .Tag = 扩展名, .Checked = 选项.已启用(扩展名)}
                        UltraDetailListView1.Items.Add(行)
                    Next
                Next
            Else
                For Each 项 In UltraDetailListView1.Items
                    项.Checked = 选项.已启用(DirectCast(项.Tag, String))
                Next
            End If
        Finally
            UltraDetailListView1.EndUpdate()
            正在回显 = False
        End Try
    End Sub

    Private Sub 列表尺寸变化(sender As Object, e As EventArgs) Handles UltraDetailListView1.SizeChanged,
            UltraDetailListView1.DpiChangedAfterParent, UltraDetailListView1.HandleCreated,
            UltraDetailListView1.PaddingChanged, UltraDetailListView1.VisibleChanged
        更新列宽()
    End Sub

    Private Sub 更新列宽()
        With UltraDetailListView1
            If .Columns.Count = 0 Then Return
            Dim 比例 = D3D_D2DInterop.GetCurrentDpiScale(UltraDetailListView1)
            Dim 边框 = CInt(.BorderSize * 比例)
            Dim 滚动条内缩 = Math.Max(边框, CInt(.BorderRadius * 比例) \ 2)
            ' 为纵向滚动条和两侧的命中边距留空，折叠分组时列宽也保持稳定。
            Dim 滚动条留白 = CInt(.ScrollBarWidth * 比例) + D3D_ScrollBarRenderer.Margin * 2 + 滚动条内缩 - 边框
            .Columns(0).Width = Math.Max(1, .ClientSize.Width - .Padding.Horizontal - 边框 * 2 - 滚动条留白)
        End With
    End Sub

    Private Sub 列表勾选变化(sender As Object, e As UltraDetailListView.ListItemEventArgs) Handles UltraDetailListView1.ItemCheckedChanged
        If 正在回显 OrElse 保存已排队 OrElse Not UltraDetailListView1.IsHandleCreated Then Return
        ' 等当前输入处理完毕，批量勾选只保存并同步一次。
        保存已排队 = True
        UltraDetailListView1.BeginInvoke(AddressOf 保存并同步关联)
    End Sub

    Private Async Sub 保存并同步关联()
        保存已排队 = False
        If IsDisposed Then Return

        Dim 配置 = 设置.实例对象
        Dim 扩展名 = UltraDetailListView1.CheckedItems.Select(Function(项) DirectCast(项.Tag, String)).ToArray()
        If 扩展名.SequenceEqual(If(配置.关联扩展名, Array.Empty(Of String)()), StringComparer.OrdinalIgnoreCase) Then Return

        配置.关联扩展名 = 扩展名
        设置.退出时保存设置()
        Try
            Await 文件关联管理器.同步全部Async(文件关联选项.从设置(配置))
        Catch ex As Exception
            If Not IsDisposed Then
                Dim owner As Form = If(DirectCast(Form1.当前主窗体, Form), FindForm())
                LakeUI.ExOverlayMsgBox(owner, $"更新文件关联失败：{ex.Message}",
                    MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, "文件关联")
            End If
        End Try
    End Sub
End Class

Friend Class 文件关联列表
    Inherits UltraDetailListView

    Private 首次点击项 As ListItem
    Private 首次点击前已勾选 As Boolean
    Private 双击项 As ListItem
    Private 双击勾选值 As Boolean

    Protected Overrides Function ProcessCmdKey(ByRef msg As Message, keyData As Keys) As Boolean
        If keyData = Keys.Enter OrElse keyData = Keys.Delete Then
            For Each 项 In SelectedItems
                项.Checked = (keyData = Keys.Enter)
            Next
            Return True
        End If
        Return MyBase.ProcessCmdKey(msg, keyData)
    End Function

    Protected Overrides Sub OnMouseDown(e As MouseEventArgs)
        If e.Button = MouseButtons.Left Then
            If CanFocus AndAlso Not Focused Then Focus()
            双击项 = Nothing
            If e.Clicks = 1 Then
                首次点击项 = GetItemAt(e.X, e.Y)
                首次点击前已勾选 = If(首次点击项?.Checked, False)
            End If
        End If
        MyBase.OnMouseDown(e)
    End Sub

    Protected Overrides Sub OnMouseDoubleClick(e As MouseEventArgs)
        MyBase.OnMouseDoubleClick(e)
        If e.Button <> MouseButtons.Left OrElse Not Enabled Then Return
        双击项 = GetItemAt(e.X, e.Y)
        If 双击项 IsNot Nothing Then
            双击勾选值 = Not If(双击项 Is 首次点击项, 首次点击前已勾选, 双击项.Checked)
        End If
    End Sub

    Protected Overrides Sub OnMouseUp(e As MouseEventArgs)
        MyBase.OnMouseUp(e)
        If e.Button = MouseButtons.Left AndAlso 双击项 IsNot Nothing Then
            ' 复选框会处理两次单击。双击结束时以第一次按下前的状态为准，只翻转一次。
            Dim 项 = 双击项
            双击项 = Nothing
            项.Checked = 双击勾选值
        End If
    End Sub
End Class
