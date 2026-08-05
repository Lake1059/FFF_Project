Public Class Form设置
    Private ReadOnly 关于页面 As New Form设置_关于页面()
    Private ReadOnly 个性化页面 As New Form设置_个性化()
    Private ReadOnly 支持者页面 As New Form设置_支持者()
    Private ReadOnly 界面与尺寸页面 As New Form设置_界面与尺寸()
    Private ReadOnly HDR页面 As New Form设置_HDR()
    Private ReadOnly 字幕页面 As New Form设置_字幕()
    Private ReadOnly 弹幕页面 As New Form设置_弹幕()
    Private ReadOnly 音乐与歌词页面 As New Form设置_音乐与歌词()

    Private Sub Form设置_Load(sender As Object, e As EventArgs) Handles Me.Load
        Form1.ThisIsYourWindow1.Attach(Me)
        Dim 主窗体 = Form1.当前主窗体
        If 主窗体 IsNot Nothing Then
            Icon = 主窗体.Icon
        End If
        ModernTabListControl1.Items(0).BoundControl = 关于页面
        ModernTabListControl1.Items(1).BoundControl = 个性化页面
        ModernTabListControl1.Items(2).BoundControl = 支持者页面
        ModernTabListControl1.Items(4).BoundControl = 界面与尺寸页面
        ModernTabListControl1.Items(5).BoundControl = HDR页面
        ModernTabListControl1.Items(6).BoundControl = 字幕页面
        ModernTabListControl1.Items(7).BoundControl = 弹幕页面
        ModernTabListControl1.Items(8).BoundControl = 音乐与歌词页面
        If 主窗体 IsNot Nothing Then Location = Me.居中于(主窗体.Bounds)
        应用字体(设置.实例对象.字体)
    End Sub

    Friend Sub 显示窗口()
        If Visible Then
            初始化所有页面()
            Activate()
            BringToFront()
            Return
        End If
        If IsHandleCreated Then 初始化所有页面()
        Show()
    End Sub

    Private Sub Form设置_Shown(sender As Object, e As EventArgs) Handles Me.Shown
        初始化所有页面()
        ModernTabListControl1.SelectedIndex = 0
    End Sub

    Private Sub 初始化所有页面()
        界面与尺寸页面.初始化页面()
        HDR页面.初始化页面()
        字幕页面.初始化页面()
        弹幕页面.初始化页面()
        音乐与歌词页面.初始化页面()
        个性化页面.初始化页面()
        支持者页面.初始化页面()
    End Sub

    Friend Sub 应用字体(fontName As String)
        字体控制.设置控件字体(fontName, Me, Nothing, True)
        For Each 页面 In {关于页面, 个性化页面, 支持者页面, 界面与尺寸页面,
                         HDR页面, 字幕页面, 弹幕页面, 音乐与歌词页面}
            Dim 排除 = If(ReferenceEquals(页面, 界面与尺寸页面),
                        New Control() {界面与尺寸页面.MCB_全局字体}, Nothing)
            字体控制.设置控件字体(fontName, 页面, 排除, True)
        Next
    End Sub

    Private Sub Form设置_FormClosing(sender As Object, e As FormClosingEventArgs) Handles Me.FormClosing
        If e.CloseReason = CloseReason.UserClosing Then
            e.Cancel = True
            Hide()
        End If
    End Sub
End Class
