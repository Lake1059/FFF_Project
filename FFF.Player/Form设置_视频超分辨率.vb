Imports System.Net
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
            ' 说明、排版和富文本样式由设计器维护；这里只更新动态读数。
            HCL_状态读数.Text = 状态文本(主窗体?.读取视频超分辨率状态())
            HCL_自动HDR状态读数.Text = 状态文本(主窗体?.读取视频自动HDR状态())
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Shared Function 状态文本(读数 As String) As String
        Return "状态：" & WebUtility.HtmlEncode(If(读数, "未读取（会话未就绪）"))
    End Function

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
        Dim 自动HDR = ReferenceEquals(sender, MB_自动HDR调整与读数)
        Dim 按钮 = DirectCast(sender, Control)
        Dim 菜单 As New ModernContextMenu With {.MenuFont = Font, .IconSize = 0}
        Dim 开启项 As New ModernContextMenu.ModernMenuItem(If(自动HDR, "请求自动 HDR", "请求超分辨率")) With {
            .Checked = If(自动HDR, 主窗体.视频自动HDR已启用(), 主窗体.视频超分辨率已启用()),
            .ToggleCheckOnClick = False
        }
        AddHandler 开启项.Click, Sub()
                                  If 自动HDR Then
                                      主窗体.应用视频自动HDR设置(Not 主窗体.视频自动HDR已启用())
                                  Else
                                      主窗体.应用视频超分辨率设置(Not 主窗体.视频超分辨率已启用())
                                  End If
                                  初始化页面()
                              End Sub
        菜单.Items.Add(开启项)
        Dim 重探项 As New ModernContextMenu.ModernMenuItem("清除驱动拒绝状态并重新探测")
        AddHandler 重探项.Click, Sub()
                                  If 自动HDR Then
                                      主窗体.重新探测视频自动HDR()
                                  Else
                                      主窗体.重新探测视频超分辨率()
                                  End If
                                  初始化页面()
                              End Sub
        菜单.Items.Add(重探项)
        Dim 读数菜单 As New ModernContextMenu With {.MenuFont = Font, .IconSize = 0}
        添加描述行(读数菜单, "状态：" & If(If(自动HDR, 主窗体.读取视频自动HDR状态(), 主窗体.读取视频超分辨率状态()), "会话未就绪"))
        添加描述行(读数菜单, "画质档位与驱动总开关由 NVIDIA App / 控制面板管理")
        If 自动HDR Then
            添加描述行(读数菜单, "条件：NVIDIA 适配器、SDR 源、10 位 HDR 输出、非隔行")
            添加描述行(读数菜单, "对比模式与自动 HDR 互斥；实际转换由驱动决定")
        Else
            添加描述行(读数菜单, "条件：源高度 360–1440、NV12/P010、非隔行、最高 10 位、解码器表面")
            添加描述行(读数菜单, "驱动接受请求不代表画面一定已增强")
        End If
        菜单.Items.Add(New ModernContextMenu.ModernMenuItem("内核读数与使用条件") With {.SubMenu = 读数菜单})
        菜单.Show(按钮, New Point(0, 按钮.Height))
    End Sub

    Private Shared Sub 添加描述行(菜单 As ModernContextMenu, 文本 As String)
        菜单.Items.Add(New ModernContextMenu.ModernMenuItem(文本) With {.IsDescription = True})
    End Sub

End Class
