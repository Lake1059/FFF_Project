Imports LakeUI

Public Class Form设置_支持者
    Private Shared ReadOnly 付费支持者列表 As New List(Of String) From {"Avec"}
    Private Shared ReadOnly 赠送支持者列表 As New List(Of String) From {
        "格里芬指挥官|#39C5BB", "陆耀YSNX462 (FFBOX最严厉的父亲)|#66FF66",
        "Celery (酒吧点蛋炒饭的)|#21AEFF", "哈哈6662333 (坏点子大师/""网""管)|#FF9633",
        "哈基曼波|#FF96DE", "ZOGMOS (终末诗)|#72565F", "Uyanide (I use arch btw)|#89B4FA",
        "Simlalsy (压片的)|#E3E0F9", "Dominic (AWJ神力)|#FF9D9F"}
    Private 已初始化 As Boolean

    Friend Sub 初始化页面()
        If 已初始化 Then Return
        已初始化 = True
        显示列表(True, True)
    End Sub

    Private Sub 显示列表(显示付费 As Boolean, 显示赠送 As Boolean)
        MemberWall1.Items.Clear()
        If 显示付费 Then 添加列表(付费支持者列表)
        If 显示赠送 Then 添加列表(赠送支持者列表)
        MemberWall1.Redraw()
    End Sub

    Private Sub 添加列表(items As IEnumerable(Of String))
        For Each entry In items
            Dim data = entry.Split("|"c)
            Dim color As Color = If(data.Length > 1 AndAlso data(1).StartsWith("#"c),
                                    ColorTranslator.FromHtml(data(1)), Color.White)
            color = Color.FromArgb(If(data.Length > 1, 200, 160), color.R, color.G, color.B)
            Dim brightness = color.R * 0.299 + color.G * 0.587 + color.B * 0.114
            Dim item As New MemberWall.MemberItem With {
                .Text = data(0), .BackColor = color,
                .ForeColor = If(brightness >= 128, Color.Black, Color.Silver)}
            If data(0).Contains("ZOGMOS") Then item.BorderColor = Color.FromArgb(200, 255, 255, 255) : item.BorderSize = 2
            MemberWall1.Items.Add(item)
        Next
    End Sub

    Private Sub ModernButton1_Click(sender As Object, e As EventArgs) Handles ModernButton1.Click
        显示列表(True, True)
    End Sub

    Private Sub ModernButton2_Click(sender As Object, e As EventArgs) Handles ModernButton2.Click
        显示列表(True, False)
    End Sub

    Private Sub ModernButton3_Click(sender As Object, e As EventArgs) Handles ModernButton3.Click
        显示列表(False, True)
    End Sub

    Private Sub ModernButton4_Click(sender As Object, e As EventArgs) Handles ModernButton4.Click
        显示列表(False, False)
    End Sub

    Private Sub ModernButton5_Click(sender As Object, e As EventArgs) Handles ModernButton5.Click
        MemberWall1.Search(ModernTextBox1.Text)
    End Sub

    Private Sub ModernTextBox1_KeyDown(sender As Object, e As KeyEventArgs) Handles ModernTextBox1.KeyDown
        If e.KeyCode = Keys.Enter Then MemberWall1.Search(ModernTextBox1.Text)
    End Sub
End Class
