Imports LakeUI

Public Class Form设置_视频超分辨率
    Private 正在初始化 As Boolean = True

    Public Sub New()
        InitializeComponent()
        正在初始化 = False
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            Dim 主窗体 = Form1.当前主窗体
            MCK_启用RTX视频超分辨率.Enabled = 主窗体 IsNot Nothing AndAlso 主窗体.视频超分辨率可用()
            MCK_启用视频自动HDR.Enabled = 主窗体 IsNot Nothing AndAlso 主窗体.视频自动HDR可用()
            MB_调整与读数.Enabled = MCK_启用RTX视频超分辨率.Enabled
            MB_自动HDR调整与读数.Enabled = MCK_启用视频自动HDR.Enabled
            MCK_启用RTX视频超分辨率.Checked = 主窗体 IsNot Nothing AndAlso 主窗体.视频超分辨率已启用()
            MCK_启用视频自动HDR.Checked = 主窗体 IsNot Nothing AndAlso 主窗体.视频自动HDR已启用()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub 超分辨率开关_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_启用RTX视频超分辨率.CheckedChanged
        If 正在初始化 Then Return
        Form1.当前主窗体?.应用视频超分辨率设置(MCK_启用RTX视频超分辨率.Checked)
        初始化页面()
    End Sub

    Private Sub 自动HDR开关_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_启用视频自动HDR.CheckedChanged
        If 正在初始化 Then Return
        Form1.当前主窗体?.应用视频自动HDR设置(MCK_启用视频自动HDR.Checked)
        初始化页面()
    End Sub

    Private Sub 调整与读数_Click(sender As Object, e As EventArgs) Handles MB_调整与读数.Click, MB_自动HDR调整与读数.Click
        Dim 主窗体 = Form1.当前主窗体
        If 主窗体 Is Nothing Then Return
        Dim 自动HDR = sender Is MB_自动HDR调整与读数
        Dim 按钮 = DirectCast(sender, Control)
        Dim 菜单 = If(自动HDR, MCM_HDR调整, MCM_VSR调整)
        Dim 子菜单 = If(自动HDR, MCM_HDR读数, MCM_VSR读数)
        Dim 切换项 = If(自动HDR, MI_HDR切换, MI_VSR切换)
        Dim 读数项 = If(自动HDR, MI_HDR实时状态, MI_VSR实时状态)
        菜单.MenuFont = Font
        子菜单.MenuFont = Font
        切换项.Checked = If(自动HDR, 主窗体.视频自动HDR已启用(), 主窗体.视频超分辨率已启用())
        读数项.Text = "状态：" & If(If(自动HDR, 主窗体.读取视频自动HDR状态(), 主窗体.读取视频超分辨率状态()), "会话未就绪")
        菜单.Show(按钮, New Point(0, 按钮.Height))
    End Sub

    Private Sub 切换菜单_Click(sender As Object, e As EventArgs) Handles MI_HDR切换.Click, MI_VSR切换.Click
        Dim 主窗体 = Form1.当前主窗体
        If 主窗体 Is Nothing Then Return
        If sender Is MI_HDR切换 Then
            主窗体.应用视频自动HDR设置(Not 主窗体.视频自动HDR已启用())
        Else
            主窗体.应用视频超分辨率设置(Not 主窗体.视频超分辨率已启用())
        End If
        初始化页面()
    End Sub

    Private Sub 重探菜单_Click(sender As Object, e As EventArgs) Handles MI_HDR重探.Click, MI_VSR重探.Click
        If sender Is MI_HDR重探 Then
            Form1.当前主窗体?.重新探测视频自动HDR()
        Else
            Form1.当前主窗体?.重新探测视频超分辨率()
        End If
        初始化页面()
    End Sub
End Class
