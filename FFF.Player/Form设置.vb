Public Class Form设置
    Private ReadOnly 关于页面 As New Form设置_关于页面()
    Private ReadOnly 个性化页面 As New Form设置_个性化()
    Private ReadOnly 支持者页面 As New Form设置_支持者()
    Private ReadOnly 界面与尺寸页面 As New Form设置_界面与尺寸()
    Private ReadOnly 设备与性能页面 As New Form设置_设备与性能()
    Private ReadOnly HDR页面 As New Form设置_HDR()
    Private ReadOnly 字幕页面 As New Form设置_字幕()
    Private ReadOnly 弹幕页面 As New Form设置_弹幕()
    Private ReadOnly 音乐与歌词页面 As New Form设置_音乐与歌词()
    Private ReadOnly 文件关联页面 As New Form设置_文件关联()
    Private ReadOnly 视频超分辨率页面 As New Form设置_视频超分辨率()
    Private ReadOnly 页面注册 As (页面 As Form, 初始化 As Action)()

    Public Sub New()
        InitializeComponent()
        ' 导航、初始化、字体和背景共用这一份注册表；分隔项不占页面位置。
        页面注册 = {
            (关于页面, Nothing),
            (个性化页面, AddressOf 个性化页面.初始化页面),
            (支持者页面, AddressOf 支持者页面.初始化页面),
            (界面与尺寸页面, AddressOf 界面与尺寸页面.初始化页面),
            (设备与性能页面, AddressOf 设备与性能页面.初始化页面),
            (HDR页面, AddressOf HDR页面.初始化页面),
            (字幕页面, AddressOf 字幕页面.初始化页面),
            (弹幕页面, AddressOf 弹幕页面.初始化页面),
            (音乐与歌词页面, AddressOf 音乐与歌词页面.初始化页面),
            (文件关联页面, AddressOf 文件关联页面.初始化页面),
            (视频超分辨率页面, AddressOf 视频超分辨率页面.初始化页面)
        }
        Dim 导航项 = ModernTabListControl1.Items.Where(Function(x) Not x.IsSeparator).ToArray()
        If 导航项.Length <> 页面注册.Length Then Throw New InvalidOperationException("设置导航与页面注册数量不一致。")
        For index = 0 To 页面注册.Length - 1
            导航项(index).BoundControl = 页面注册(index).页面
        Next
    End Sub

    Private Sub Form设置_Load(sender As Object, e As EventArgs) Handles Me.Load
        Dim 主窗体 = Form1.当前主窗体
        If 主窗体 IsNot Nothing Then
            主窗体.ThisIsYourWindow1.Attach(Me)
            Icon = 主窗体.Icon
        End If
        If 主窗体 IsNot Nothing Then Location = Me.居中于(主窗体.Bounds)
        应用玻璃背景(SP_UnLock AndAlso 设置.实例对象.SP_毛玻璃模式 > 0)
        应用字体(设置.实例对象.字体)
    End Sub

    Friend Sub 显示字幕设置()
        显示窗口()
        BeginInvoke(Sub() ModernTabListControl1.SelectedIndex = 6)
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
        For Each 注册 In 页面注册
            注册.初始化?.Invoke()
        Next
    End Sub

    Friend Sub 应用字体(fontName As String)
        字体控制.设置控件字体(fontName, Me, Nothing, True)
        For Each 注册 In 页面注册
            Dim 页面 = 注册.页面
            Dim 排除 = If(ReferenceEquals(页面, 界面与尺寸页面),
                        New Control() {界面与尺寸页面.MCB_全局字体}, Nothing)
            字体控制.设置控件字体(fontName, 页面, 排除, True)
        Next
    End Sub

    Friend Sub 应用玻璃背景(启用 As Boolean)
        Dim 背景颜色 = If(启用, Color.Transparent, Color.FromArgb(24, 24, 24))
        ModernTabListControl1.BackColor = 背景颜色
        ModernTabListControl1.TabStripBackColor = 背景颜色
        ModernTabListControl1.ContentBackColor = 背景颜色
        For Each 注册 In 页面注册
            Dim 根面板 = 注册.页面.Controls.Cast(Of Control)().OfType(Of LakeUI.ModernPanel)().
                FirstOrDefault(Function(x) x.Dock = DockStyle.Fill)
            If 根面板 Is Nothing Then Continue For
            根面板.BackColor = 背景颜色
            根面板.BackColor1 = 背景颜色
            根面板.BackgroundSource = If(启用, Me, Nothing)
        Next
    End Sub

    Private Sub Form设置_FormClosing(sender As Object, e As FormClosingEventArgs) Handles Me.FormClosing
        If e.CloseReason = CloseReason.UserClosing Then
            e.Cancel = True
            Hide()
        End If
    End Sub

    Private Sub Form设置_Disposed(sender As Object, e As EventArgs) Handles Me.Disposed
        If 页面注册 Is Nothing Then Return
        For Each 注册 In 页面注册
            If Not 注册.页面.IsDisposed Then 注册.页面.Dispose()
        Next
    End Sub
End Class
