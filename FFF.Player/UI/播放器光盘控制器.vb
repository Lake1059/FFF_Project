Imports System.IO

Friend NotInheritable Class 播放器光盘控制器
    Implements IDisposable

    Private ReadOnly 主窗体 As Form
    Private ReadOnly 画面 As 播放器画面控件
    Private ReadOnly 播放器 As 播放器控制器
    Private ReadOnly 菜单 As LakeUI.ModernContextMenu
    Private ReadOnly 刷新计时器 As New Timer With {.Interval = 100}
    Private 状态 As New 光盘状态()
    Private 上次鼠标提交 As Long
    Private 菜单项已显示 As Boolean
    <CodeAnalysis.SuppressMessage("Style", "IDE0044:添加只读修饰符", Justification:="<挂起>")>
    Private 光驱项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private 扫描中 As Integer

    Friend Sub New(窗体 As Form, 视频 As 播放器画面控件, 控制器 As 播放器控制器, 标题菜单 As LakeUI.ModernContextMenu)
        主窗体 = 窗体 : 画面 = 视频 : 播放器 = 控制器 : 菜单 = 标题菜单
        根菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem("光盘菜单页面")
        叠加层项 = New LakeUI.ModernContextMenu.ModernMenuItem("光盘菜单叠加层")
        绑定命令(根菜单项, 光盘命令.根菜单)
        绑定命令(叠加层项, 光盘命令.弹出菜单)
        光驱项 = New LakeUI.ModernContextMenu.ModernMenuItem("打开可用光驱") With {
            .SubMenu = 创建光驱子菜单()
        }
        菜单.Items.Add(光驱项)
        AddHandler 刷新计时器.Tick, AddressOf 刷新
        AddHandler 画面.光盘鼠标移动, AddressOf 鼠标移动
        AddHandler 画面.光盘鼠标确认, AddressOf 鼠标确认
        刷新计时器.Start()
    End Sub

    Private Function 创建光驱子菜单() As LakeUI.ModernContextMenu
        Dim 子菜单 As New LakeUI.ModernContextMenu With {
            .AnimationFPS = 120,
            .BackColor = 菜单.BackColor,
            .BackColor1 = 菜单.BackColor1,
            .BorderSize = 菜单.BorderSize,
            .BackdropBlurRadius = 菜单.BackdropBlurRadius,
            .BackdropMode = 菜单.BackdropMode,
            .BackdropTintColor = 菜单.BackdropTintColor,
            .HoverBackColor = 菜单.HoverBackColor,
            .HoverRadius = 菜单.HoverRadius,
            .ItemHeight = 菜单.ItemHeight,
            .ItemPadding = 菜单.ItemPadding,
            .MenuFont = 菜单.MenuFont,
            .MenuPadding = 菜单.MenuPadding,
            .PressedBackColor = 菜单.PressedBackColor,
            .SeparatorColor = 菜单.SeparatorColor,
            .SeparatorHeight = 菜单.SeparatorHeight,
            .IconSize = 0
        }
        子菜单.Items.Add(New LakeUI.ModernContextMenu.ModernMenuItem("正在扫描可用光驱") With {.IsDescription = True})
        Return 子菜单
    End Function

    Friend Sub 请求扫描光驱()
        If Threading.Interlocked.Exchange(扫描中, 1) <> 0 Then Return
        Task.Run(Function() DriveInfo.GetDrives().Where(Function(d) d.DriveType = DriveType.CDRom AndAlso d.IsReady).Select(Function(d) d.RootDirectory.FullName).ToArray()).ContinueWith(
            Sub(t)
                Threading.Interlocked.Exchange(扫描中, 0)
                If t.IsFaulted OrElse t.Result.Length = 0 Then Return
                主窗体.BeginInvoke(Sub()
                    光驱项.SubMenu.Items.Clear()
                    For Each 项路径 In t.Result
                        Dim 项 As New LakeUI.ModernContextMenu.ModernMenuItem(项路径)
                        AddHandler 项.Click, Sub() 播放器.打开媒体(项路径)
                        光驱项.SubMenu.Items.Add(项)
                    Next
                End Sub)
            End Sub, TaskScheduler.FromCurrentSynchronizationContext())
    End Sub

    <CodeAnalysis.SuppressMessage("Style", "IDE0044:添加只读修饰符", Justification:="<挂起>")>
    Private 根菜单项 As LakeUI.ModernContextMenu.ModernMenuItem
    <CodeAnalysis.SuppressMessage("Style", "IDE0044:添加只读修饰符", Justification:="<挂起>")>
    Private 叠加层项 As LakeUI.ModernContextMenu.ModernMenuItem

    Private Sub 绑定命令(项 As LakeUI.ModernContextMenu.ModernMenuItem, 命令 As 光盘命令)
        Dim 操作 As Action = Sub()
                                 播放器.光盘导航(命令)
                             End Sub
        AddHandler 项.Click, Sub(sender As Object, e As EventArgs)
                                  操作()
                              End Sub
    End Sub

    Private Sub 刷新(sender As Object, e As EventArgs)
        状态 = 播放器.读取光盘状态()
        Dim 快照 = 播放器.安全读取快照()
        If 快照 Is Nothing OrElse 快照.状态 = 播放状态.失败 OrElse 快照.状态 = 播放状态.正在打开 Then 状态 = New 光盘状态()
        画面.光盘交互已启用 = 状态.已打开 AndAlso 状态.菜单可见
        If 状态.已打开 <> 菜单项已显示 Then
            菜单项已显示 = 状态.已打开
            If 菜单项已显示 Then
                If Not 菜单.Items.Contains(根菜单项) Then 菜单.Items.Add(根菜单项)
                If Not 菜单.Items.Contains(叠加层项) Then 菜单.Items.Add(叠加层项)
            Else
                菜单.Items.Remove(根菜单项)
                菜单.Items.Remove(叠加层项)
            End If
        End If
    End Sub

    Friend Function 处理按键(键 As Keys) As Boolean
        If Not 状态.已打开 Then Return False
        If 键 = (Keys.Control Or Keys.M) Then
            播放器.光盘导航(光盘命令.根菜单) : Return True
        End If
        If 键 = (Keys.Control Or Keys.Shift Or Keys.M) Then
            播放器.光盘导航(光盘命令.弹出菜单) : Return True
        End If
        If Not 状态.菜单可见 Then Return False
        Select Case 键
            Case Keys.Up : 播放器.光盘导航(光盘命令.上)
            Case Keys.Down : 播放器.光盘导航(光盘命令.下)
            Case Keys.Left : 播放器.光盘导航(光盘命令.左)
            Case Keys.Right : 播放器.光盘导航(光盘命令.右)
            Case Keys.Enter : 播放器.光盘导航(光盘命令.确认)
            Case Keys.Escape, Keys.Back : 播放器.光盘导航(光盘命令.返回)
            Case Else : Return False
        End Select
        Return True
    End Function

    Private Sub 鼠标移动(sender As Object, e As MouseEventArgs)
        Dim 当前 = Environment.TickCount64
        If 当前 - 上次鼠标提交 < 16 Then Return
        上次鼠标提交 = 当前
        发送鼠标(sender, 光盘命令.鼠标移动, e)
    End Sub
    Private Sub 鼠标确认(sender As Object, e As MouseEventArgs)
        发送鼠标(sender, 光盘命令.鼠标确认, e)
    End Sub
    Private Sub 发送鼠标(sender As Object, 命令 As 光盘命令, e As MouseEventArgs)
        Dim 输出 = TryCast(sender, Control)
        If 输出 Is Nothing OrElse 状态.画布宽度 <= 0 OrElse 状态.画布高度 <= 0 OrElse 状态.显示比例 <= 0 Then Return
        Dim 宽 = CDbl(输出.ClientSize.Width), 高 = CDbl(输出.ClientSize.Height)
        If 宽 / Math.Max(1, 高) > 状态.显示比例 Then 宽 = 高 * 状态.显示比例 Else 高 = 宽 / 状态.显示比例
        Dim 左 = (输出.ClientSize.Width - 宽) / 2, 上 = (输出.ClientSize.Height - 高) / 2
        If 宽 <= 0 OrElse 高 <= 0 OrElse e.X < 左 OrElse e.Y < 上 OrElse e.X >= 左 + 宽 OrElse e.Y >= 上 + 高 Then Return
        播放器.光盘导航(命令, CInt(Math.Floor((e.X - 左) * 状态.画布宽度 / 宽)), CInt(Math.Floor((e.Y - 上) * 状态.画布高度 / 高)))
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        刷新计时器.Stop() : 刷新计时器.Dispose()
        RemoveHandler 画面.光盘鼠标移动, AddressOf 鼠标移动
        RemoveHandler 画面.光盘鼠标确认, AddressOf 鼠标确认
    End Sub
End Class
