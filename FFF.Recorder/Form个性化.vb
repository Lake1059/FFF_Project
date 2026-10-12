Imports System.IO
Imports LakeUI

Public Class Form个性化
    Private 正在初始化 As Boolean

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            If SP_UnLock Then
                HtmlColorLabel1.Text = "感谢您支持 FFF.Recorder Supporter Pack"
                Panel4.Visible = False
            Else
                HtmlColorLabel1.Text = "购买 FFF.Recorder Supporter Pack 以解锁个性化设置"
                Panel4.Visible = True
            End If

            MCB_边框宽度.SelectedIndex = 设置.实例对象.SP_边框宽度
            MCB_毛玻璃模式.SelectedIndex = 设置.实例对象.SP_毛玻璃模式
            MCB_背景来源.SelectedIndex = 设置.实例对象.SP_毛玻璃背景来源
            MCB_噪点颗粒.SelectedIndex = 设置.实例对象.SP_毛玻璃噪点颗粒
            更新玻璃附属控件状态()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MB_前往购买_Click(sender As Object, e As EventArgs) Handles MB_前往购买.Click
        Process.Start(New ProcessStartInfo With {
            .FileName = "https://ifdian.net/item/ed2719d4841211f1a67352540025c377",
            .UseShellExecute = True
        })
    End Sub

    Private Sub MB_窗口标题文字_Click(sender As Object, e As EventArgs) Handles MB_窗口标题文字.Click
        If Not SP_UnLock Then Return
        设置.实例对象.SP_窗口标题文字 = ExInputBox(Form1.当前主窗体,
                                                "自定义窗口标题文本，清空即表示还原",
                                                "窗口标题文字",
                                                设置.实例对象.SP_窗口标题文字)
        设置.应用SP个性化设置()
    End Sub

    Private Sub MB_图标_Click(sender As Object, e As EventArgs) Handles MB_图标.Click
        If Not SP_UnLock Then Return
        Select Case ExMsgBox($"图标会显示在窗口左上角，分辨率不宜过高。{vbCrLf & vbCrLf}设置之后如果要取消，请删除当前目录下的 {Path.GetFileName(设置.自定义图标路径)} 并重启软件。",
                            New List(Of ExMsgBoxButton) From {
                                New ExMsgBoxButton("选择图标", True),
                                New ExMsgBoxButton("取消")
                            }, , MsgBoxStyle.Information, Form1.当前主窗体)
            Case 0
                Using dialog As New OpenFileDialog With {
                    .Multiselect = False,
                    .Filter = "支持的图片|*.png;*.jpg;*.jpeg;*.gif"
                }
                    If dialog.ShowDialog(Form1.当前主窗体) <> DialogResult.OK Then Return
                    Try
                        File.Copy(dialog.FileName, 设置.自定义图标路径, True)
                        设置.加载SP自定义图标()
                    Catch ex As Exception
                        ExMsgBox(Form1.当前主窗体, "加载自定义图标失败：" & ex.Message, MsgBoxStyle.Critical)
                    End Try
                End Using
        End Select
    End Sub

    Private Sub MB_窗口边框颜色_Click(sender As Object, e As EventArgs) Handles MB_窗口边框颜色.Click
        If Not SP_UnLock Then Return
        Using dialog As New ModernColorDialog With {
            .SelectedColor = Form1.当前主窗体.ThisIsYourWindow1.BorderColor,
            .Icon = Form1.当前主窗体.Icon
        }
            Form1.当前主窗体.ThisIsYourWindow1.Attach(dialog)
            If dialog.ShowDialog(Form1.当前主窗体) <> DialogResult.OK Then Return
            设置.实例对象.SP_窗口边框颜色_A = dialog.SelectedColor.A
            设置.实例对象.SP_窗口边框颜色_R = dialog.SelectedColor.R
            设置.实例对象.SP_窗口边框颜色_G = dialog.SelectedColor.G
            设置.实例对象.SP_窗口边框颜色_B = dialog.SelectedColor.B
        End Using
        设置.应用SP个性化设置()
    End Sub

    Private Sub MB_分层阴影颜色_Click(sender As Object, e As EventArgs) Handles MB_分层阴影颜色.Click
        If Not SP_UnLock Then Return
        Using dialog As New ModernColorDialog With {
            .SelectedColor = Form1.当前主窗体.ThisIsYourWindow1.LayerShadowColor,
            .Icon = Form1.当前主窗体.Icon
        }
            Form1.当前主窗体.ThisIsYourWindow1.Attach(dialog)
            If dialog.ShowDialog(Form1.当前主窗体) <> DialogResult.OK Then Return
            设置.实例对象.SP_分层阴影颜色_A = dialog.SelectedColor.A
            设置.实例对象.SP_分层阴影颜色_R = dialog.SelectedColor.R
            设置.实例对象.SP_分层阴影颜色_G = dialog.SelectedColor.G
            设置.实例对象.SP_分层阴影颜色_B = dialog.SelectedColor.B
        End Using
        设置.应用SP个性化设置()
    End Sub

    Private Sub MCB_边框宽度_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_边框宽度.SelectedIndexChanged
        If 正在初始化 OrElse Not SP_UnLock OrElse MCB_边框宽度.SelectedIndex < 0 Then Return
        设置.实例对象.SP_边框宽度 = MCB_边框宽度.SelectedIndex
        设置.应用SP个性化设置()
    End Sub

    Private Sub MCB_毛玻璃模式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_毛玻璃模式.SelectedIndexChanged
        If 正在初始化 OrElse Not SP_UnLock OrElse MCB_毛玻璃模式.SelectedIndex < 0 Then Return
        设置.实例对象.SP_毛玻璃模式 = MCB_毛玻璃模式.SelectedIndex
        If MCB_毛玻璃模式.SelectedIndex = 0 Then
            正在初始化 = True
            Try
                设置.实例对象.SP_毛玻璃背景来源 = -1
                设置.实例对象.SP_毛玻璃噪点颗粒 = -1
                MCB_背景来源.SelectedIndex = -1
                MCB_噪点颗粒.SelectedIndex = -1
            Finally
                正在初始化 = False
            End Try
        End If
        更新玻璃附属控件状态()
        设置.应用SP个性化设置()
    End Sub

    Private Sub MCB_背景来源_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_背景来源.SelectedIndexChanged
        If 正在初始化 OrElse Not SP_UnLock Then Return
        设置.实例对象.SP_毛玻璃背景来源 = MCB_背景来源.SelectedIndex
        设置.应用SP个性化设置()
    End Sub

    Private Sub MCB_噪点颗粒_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_噪点颗粒.SelectedIndexChanged
        If 正在初始化 OrElse Not SP_UnLock Then Return
        设置.实例对象.SP_毛玻璃噪点颗粒 = MCB_噪点颗粒.SelectedIndex
        设置.应用SP个性化设置()
    End Sub

    Private Sub MB_选择背景图_Click(sender As Object, e As EventArgs) Handles MB_选择背景图.Click
        If Not SP_UnLock Then Return
        Select Case ExMsgBox($"要使用背景图，必须启用玻璃背景并将背景来源设为背景图。{vbCrLf & vbCrLf}设置之后如果要取消，请删除当前目录下的 {Path.GetFileName(设置.自定义背景图路径)} 并重启软件。",
                            New List(Of ExMsgBoxButton) From {
                                New ExMsgBoxButton("选择图片", True),
                                New ExMsgBoxButton("取消")
                            }, , MsgBoxStyle.Information, Form1.当前主窗体)
            Case 0
                Using dialog As New OpenFileDialog With {
                    .Multiselect = False,
                    .Filter = "支持的图片|*.png;*.jpg;*.jpeg"
                }
                    If dialog.ShowDialog(Form1.当前主窗体) <> DialogResult.OK Then Return
                    Try
                        File.Copy(dialog.FileName, 设置.自定义背景图路径, True)
                        If 设置.实例对象.SP_毛玻璃模式 > 0 AndAlso 设置.实例对象.SP_毛玻璃背景来源 = 0 Then
                            设置.加载SP自定义背景图()
                        End If
                    Catch ex As Exception
                        ExMsgBox(Form1.当前主窗体, "加载自定义背景图失败：" & ex.Message, MsgBoxStyle.Critical)
                    End Try
                End Using
        End Select
    End Sub

    Private Sub 更新玻璃附属控件状态()
        Dim enabled = MCB_毛玻璃模式.SelectedIndex > 0
        MCB_背景来源.Enabled = enabled
        MCB_噪点颗粒.Enabled = enabled
        MB_选择背景图.Enabled = enabled
    End Sub

    Private Sub Form个性化_Load(sender As Object, e As EventArgs) Handles MyBase.Load
    End Sub
End Class
