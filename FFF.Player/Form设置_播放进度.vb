Imports System.IO
Imports System.Net

''' <summary>播放进度记忆开关。进度文件与设置文件分离；关闭后不再读写，
''' 退出程序时清空已有记录（见 Form1_FormClosed），也可以在本页立即清除。</summary>
Public Class Form设置_播放进度
    Private 正在初始化 As Boolean = True

    Public Sub New()
        InitializeComponent()
        正在初始化 = False
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            MCK_记住播放进度.Checked = 设置.实例对象.播放进度记忆
            Dim 数量 = 播放进度存储.统计条目数()
            ' 即使条目数为 0，只要文件存在也允许清除（版本不兼容时 统计条目数 返回 0，
            ' 但磁盘上仍有 v2/v3 文件；用户需要一个显式入口把它删掉）。
            MB_清除播放进度.Enabled = 数量 > 0 OrElse File.Exists(播放进度存储.文件路径)
            HCL_状态读数.Text = 状态文本(数量)
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Shared Function 状态文本(数量 As Integer) As String
        Dim 开关状态 = If(设置.实例对象.播放进度记忆, String.Empty, "（功能已关闭）")
        Dim 路径 = WebUtility.HtmlEncode(播放进度存储.文件路径)
        Return "当前记录：" & 数量.ToString() & " 个媒体" & 开关状态 &
               "<br><span style=""font-size:10; color:#A0A0A0"">" & 路径 & "</span>"
    End Function

    Private Sub 记住播放进度_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_记住播放进度.CheckedChanged
        If 正在初始化 Then Return
        设置.实例对象.播放进度记忆 = MCK_记住播放进度.Checked
        ' 这个开关带隐私语义，立刻持久化，避免用户关掉后异常退出又回到开启状态。
        设置.退出时保存设置()
        If Not MCK_记住播放进度.Checked Then
            ' 关闭即清盘：不等下次退出，立刻删掉已含绝对路径的进度文件。
            ' 正在初始化 守卫已保证进页面绑值时不会误触发这里。
            播放进度存储.清空()
        End If
        初始化页面()
    End Sub

    Private Sub 清除播放进度_Click(sender As Object, e As EventArgs) Handles MB_清除播放进度.Click
        Dim 数量 = 播放进度存储.统计条目数()
        If 数量 > 0 Then
            If LakeUI.ExOverlayMsgBox(Me,
                "将删除全部播放进度记录（" & 数量.ToString() & " 个媒体）。" & vbCrLf &
                "只影响续播位置，不影响任何设置、字幕或播放列表。",
                {"清除", "取消"}, "清除播放进度", MsgBoxStyle.Exclamation, 1) <> 0 Then Return
        End If
        播放进度存储.清空()
        初始化页面()
    End Sub
End Class
