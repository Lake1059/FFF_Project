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
    Private ReadOnly AI字幕页面 As New Form设置_AI字幕()
    Private ReadOnly 播放行为页面 As New Form设置_播放行为()
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
            (播放行为页面, AddressOf 播放行为页面.初始化页面),
            (HDR页面, AddressOf HDR页面.初始化页面),
            (字幕页面, AddressOf 字幕页面.初始化页面),
            (弹幕页面, AddressOf 弹幕页面.初始化页面),
            (音乐与歌词页面, AddressOf 音乐与歌词页面.初始化页面),
            (文件关联页面, AddressOf 文件关联页面.初始化页面),
            (视频超分辨率页面, AddressOf 视频超分辨率页面.初始化页面),
            (AI字幕页面, AddressOf AI字幕页面.初始化页面)
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

    Friend ReadOnly Property 正在显示设备页 As Boolean
        Get
            Return Visible AndAlso 设备与性能页面.Visible
        End Get
    End Property

    Friend Sub 刷新实时设置页(端点 As 蓝牙音频信息)
        If Not Visible Then Return
        If 正在显示设备页 Then 设备与性能页面.刷新蓝牙设备显示(端点)
        If 播放行为页面.Visible Then 播放行为页面.刷新进度数量()
    End Sub

    Private 首次显示完成 As Boolean
    Private 待选中页索引 As Integer = -1

    Friend Sub 显示窗口()
        待选中页索引 = -1
        打开并定位()
    End Sub

    Private Sub 打开并定位()
        If 首次显示完成 Then 初始化所有页面()
        If Not Visible Then Show()
        ' 首次打开由 Shown 消费目标页面，后续从隐藏状态恢复时在这里定位。
        If 首次显示完成 Then 应用待选中页()
        Activate()
        BringToFront()
    End Sub

    ''' <summary>打开设置窗口并选中 AI 字幕页。</summary>
    Friend Sub 显示Ai字幕页()
        待选中页索引 = ModernTabListControl1.Items.ToList().FindIndex(Function(项) 项.BoundControl Is AI字幕页面)
        打开并定位()
    End Sub

    Private Sub 应用待选中页()
        If 待选中页索引 < 0 Then Return
        ModernTabListControl1.SelectedIndex = 待选中页索引
        待选中页索引 = -1
    End Sub

    Private Sub Form设置_Shown(sender As Object, e As EventArgs) Handles Me.Shown
        初始化所有页面()
        首次显示完成 = True
        If 待选中页索引 >= 0 Then
            应用待选中页()
        Else
            ModernTabListControl1.SelectedIndex = 0
        End If
    End Sub

    Private Sub 初始化所有页面()
        For Each 注册 In 页面注册
            注册.初始化?.Invoke()
        Next
    End Sub

    ' 调整停靠在左侧的等宽列，最右列由 DockStyle.Fill 占满剩余空间。
    Friend Shared Sub 均分设置列(行 As LakeUI.ModernPanel, 固定宽度 As Integer, ParamArray 左侧列 As Control())
        Dim 可用宽度 = Math.Max(0, 行.DisplayRectangle.Width - 固定宽度)
        Dim 列数 = 左侧列.Length + 1
        Dim 基础列宽 = 可用宽度 \ 列数
        Dim 余量 = 可用宽度 Mod 列数
        For 索引 = 0 To 左侧列.Length - 1
            Dim 列宽 = 基础列宽 + If(索引 < 余量, 1, 0)
            If 左侧列(索引).Width <> 列宽 Then 左侧列(索引).Width = 列宽
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
