Imports System.IO

Public Enum 视频打开尺寸策略
    跟随界面设置 = 0
    保持当前尺寸 = 1
    初始画面尺寸 = 2
    内容原始尺寸 = 3
    最大化 = 4
End Enum

Public Class Form设置_播放行为
    Private 正在初始化 As Boolean = True

    Public Sub New()
        InitializeComponent()
        正在初始化 = False
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            MCB_单独打开视频时自动将渲染大小调整为.SelectedIndex = CInt(设置.实例对象.单独打开视频尺寸)
            MCB_播放列表打开视频时自动将渲染大小调整为.SelectedIndex = CInt(设置.实例对象.播放列表打开视频尺寸)
            MCK_记住播放进度.Checked = 设置.实例对象.播放进度记忆
            刷新进度数量()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Friend Sub 刷新进度数量()
        Dim 数量 = 播放进度存储.统计条目数()
        MB_清除播放进度.Text = If(数量 > 0, $"清除已记录的 {数量} 个视频文件的进度", "当前没有记录的视频播放进度")
        MB_清除播放进度.Enabled = 数量 > 0 OrElse File.Exists(播放进度存储.文件路径)
    End Sub

    Private Sub 视频打开尺寸_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_单独打开视频时自动将渲染大小调整为.SelectedIndexChanged,
        MCB_播放列表打开视频时自动将渲染大小调整为.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 选框 = DirectCast(sender, LakeUI.ModernComboBox)
        If 选框.SelectedIndex < 0 Then Return
        Dim 策略 = CType(选框.SelectedIndex, 视频打开尺寸策略)
        If 选框 Is MCB_单独打开视频时自动将渲染大小调整为 Then
            设置.实例对象.单独打开视频尺寸 = 策略
        Else
            设置.实例对象.播放列表打开视频尺寸 = 策略
        End If
    End Sub

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
        If 数量 > 0 OrElse File.Exists(播放进度存储.文件路径) Then
            If LakeUI.ExOverlayMsgBox(Me,
                If(数量 > 0, $"将删除 {数量} 个视频文件的播放进度。", "将删除已保存的播放进度文件。") & vbCrLf &
                "只影响续播位置，不影响任何设置、字幕或播放列表。",
                {"清除", "取消"}, "清除播放进度", MsgBoxStyle.Exclamation, 1) <> 0 Then Return
        End If
        播放进度存储.清空()
        初始化页面()
    End Sub
End Class
