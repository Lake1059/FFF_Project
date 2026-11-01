Imports System.Globalization

Partial Public Class Form1
    Private ReadOnly 视频菜单 As New LakeUI.ModernContextMenu()
    Private 倍速弹窗 As Form
    Private 还原画面按钮 As LakeUI.ModernButton
    Private ReadOnly 均衡增益(9) As Single
    Private 均衡已启用 As Boolean
    Private 当前倍速 As Double = 1
    Private 当前缩放算法 As UInteger
    Private 视频缩放 As Single = 1
    Private 缩放滚轮余量 As Integer
    Private 倍速按钮 As LakeUI.ModernButton
    Private 倍速弹窗停用时刻 As Integer

    Private Sub 添加功能菜单(菜单 As LakeUI.ModernContextMenu, 文本 As String, 操作 As Action)
        Dim 项 As New LakeUI.ModernContextMenu.ModernMenuItem With {.Text = 文本}
        AddHandler 项.Click, Sub()
                                ' 菜单点击与窗体关闭存在竞态：BeginInvoke 及其回调都可能落在关闭之后。
                                If IsDisposed OrElse Disposing Then Return
                                Try
                                    BeginInvoke(Sub()
                                                    If IsDisposed OrElse Disposing Then Return
                                                    Try
                                                        操作()
                                                    Catch ex As Exception
                                                        LakeUI.ExOverlayMsgBox(Me, ex.Message, MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, "操作失败")
                                                    End Try
                                                End Sub)
                                Catch ex As InvalidOperationException
                                End Try
                            End Sub
        菜单.Items.Add(项)
    End Sub

    Private Shared Sub 释放菜单树(菜单 As LakeUI.ModernContextMenu)
        For Each 项 In 菜单.Items
            If 项.SubMenu IsNot Nothing Then 释放菜单树(项.SubMenu)
        Next
        菜单.Dispose()
    End Sub

    Private Sub 初始化VSP功能()
        当前倍速 = If(Double.IsFinite(设置.实例对象.播放倍速), Math.Clamp(设置.实例对象.播放倍速, 0.1, 16), 1)
        当前缩放算法 = ({3UI, 0UI, 2UI, 1UI})(Math.Clamp(设置.实例对象.视频缩放质量选项, 0, 3))
        字幕显示 = 设置.实例对象.显示字幕
        画面控件.视频滚轮缩放 = 设置.实例对象.滚轮放大视频
        视频菜单.CornerRadius = 8
        视频菜单.HoverRadius = 5
        视频菜单.BackdropMode = LakeUI.ModernContextMenu.BackdropModeEnum.Auto
        添加功能菜单(视频菜单, "打开文件…", Sub() MB_打开文件_Click(MB_打开文件, EventArgs.Empty))
        Dim 缩放菜单 As New LakeUI.ModernContextMenu With {.CornerRadius = 8, .HoverRadius = 5}
        For Each 项 In {("Hermite", 0), ("Lanczos3", 1), ("Lanczos4", 2), ("Jinc", 3)}
            Dim 选项 = 项.Item2
            添加功能菜单(缩放菜单, 项.Item1, Sub()
                                                 设置.实例对象.视频缩放质量选项 = 选项
                                                 应用视频缩放设置()
                                                 信息图层呈现器?.显示操作信息(项.Item1, &HFF69DF8BUI)
                                             End Sub)
        Next
        视频菜单.Items.Add(New LakeUI.ModernContextMenu.ModernMenuItem With {.Text = "缩放模式", .SubMenu = 缩放菜单})
        Dim 滚轮菜单 As New LakeUI.ModernContextMenu With {.CornerRadius = 8, .HoverRadius = 5}
        添加功能菜单(滚轮菜单, "音量调节", Sub()
                                                   画面控件.视频滚轮缩放 = False
                                                   设置.实例对象.滚轮放大视频 = False
                                               End Sub)
        添加功能菜单(滚轮菜单, "放大视频", Sub()
                                                   画面控件.视频滚轮缩放 = True
                                                   设置.实例对象.滚轮放大视频 = True
                                               End Sub)
        视频菜单.Items.Add(New LakeUI.ModernContextMenu.ModernMenuItem With {.Text = "鼠标滚轮", .SubMenu = 滚轮菜单})
        初始化字幕菜单()
        Dim 截图 As New LakeUI.ModernContextMenu With {.CornerRadius = 8, .HoverRadius = 5}
        添加功能菜单(截图, "原始画面…", Sub() 画面菜单控制器.保存到指定路径(True))
        添加功能菜单(截图, "实际渲染画面…", Sub() 画面菜单控制器.保存到指定路径(False))
        视频菜单.Items.Add(New LakeUI.ModernContextMenu.ModernMenuItem With {.Text = "截图到指定路径", .SubMenu = 截图})
        添加功能菜单(视频菜单, "软件设置", Sub() MB_软件设置_Click(MB_软件设置, EventArgs.Empty))
        AddHandler 画面控件.画面右键, Sub(sender, e)
                                                For i = 0 To 1
                                                    滚轮菜单.Items(i).Checked = (i = If(画面控件.视频滚轮缩放, 1, 0))
                                                Next
                                                For i = 0 To 3
                                                    缩放菜单.Items(i).Checked = ({3UI, 0UI, 2UI, 1UI})(i) = 当前缩放算法
                                                Next
                                                更新字幕菜单()
                                                应用视频菜单样式(视频菜单)
                                                视频菜单.Show(画面控件, 画面控件.PointToClient(Cursor.Position))
                                            End Sub
        AddHandler 画面控件.视频缩放滚轮, Sub(sender, e)
                                                缩放滚轮余量 += e.Delta
                                                Dim 格数 = 缩放滚轮余量 \ 120
                                                缩放滚轮余量 -= 格数 * 120
                                                Dim 新缩放 = CSng(Math.Clamp(视频缩放 * Math.Pow(1.25, 格数), 0.25, 16))
                                                Dim 结果 As Single
                                                Dim 锚点 = 画面控件.PointToClient(Cursor.Position)
                                                If 播放控制器.光标锚定缩放(新缩放 / 视频缩放, Math.Clamp(锚点.X / CSng(Math.Max(1, 画面控件.Width)), 0, 1), Math.Clamp(锚点.Y / CSng(Math.Max(1, 画面控件.Height)), 0, 1), 结果) Then
                                                    视频缩放 = 结果
                                                    播放控制器.重绘画面()
                                                    ' 缩放未生效（如旧内核缺 ZoomViewAt）时不提示，避免"没缩放却报百分比"的误导。
                                                    信息图层呈现器?.显示操作信息($"视频缩放 {视频缩放:P0}", &HFF69DF8BUI)
                                                End If
                                                还原画面按钮.Visible = 视频缩放 <> 1
                                                还原画面按钮.BringToFront()
                                            End Sub
        Dim 比例 = DeviceDpi / 96.0F
        还原画面按钮 = New LakeUI.ModernButton With {.Text = "还原画面", .Bounds = New Rectangle(CInt(8 * 比例), CInt(8 * 比例), CInt(100 * 比例), CInt(30 * 比例)), .BorderRadius = 5, .Visible = False, .Font = MB_软件解码或硬件解码.Font, .BackColor1 = MB_软件解码或硬件解码.BackColor1}
        画面控件.Controls.Add(还原画面按钮)
        AddHandler 还原画面按钮.Click, Sub()
                                            视频缩放 = 1
                                            播放控制器.设置视图变换(1, 0, 0)
                                            播放控制器.重绘画面()
                                            还原画面按钮.Hide()
                                        End Sub
        Dim 参考 = MB_软件解码或硬件解码
        倍速按钮 = New LakeUI.ModernButton With {.Text = 当前倍速.ToString("0.##", CultureInfo.InvariantCulture) & "×", .Dock = DockStyle.Left, .Size = 参考.Size, .Font = 参考.Font, .ForeColor = 参考.ForeColor, .BackColor = 参考.BackColor, .BackColor1 = 参考.BackColor1, .BorderColor = 参考.BorderColor, .BorderSize = 参考.BorderSize, .BorderRadius = 参考.BorderRadius, .HoverBackColor1 = 参考.HoverBackColor1, .PressedBackColor1 = 参考.PressedBackColor1, .Padding = 参考.Padding, .Margin = 参考.Margin, .SubTextForeColor = 参考.SubTextForeColor}
        AddHandler 倍速按钮.Click, Sub() 显示倍速展开()
        AddHandler 倍速按钮.TextChanged, Sub() 更新倍速按钮尺寸()
        AddHandler 倍速按钮.FontChanged, Sub() 更新倍速按钮尺寸()
        AddHandler 倍速按钮.DpiChangedAfterParent, Sub() 更新倍速按钮尺寸()
        AddHandler 参考.FontChanged, Sub() 倍速按钮.Font = 参考.Font
        AddHandler 参考.SizeChanged, Sub() 更新倍速按钮尺寸()
        Dim 间隔 As New LakeUI.JustEmptyControl With {.Dock = DockStyle.Left, .Width = JEC_HDR选项前面的空白占位.Width}
        AddHandler JEC_HDR选项前面的空白占位.SizeChanged, Sub() 间隔.Width = JEC_HDR选项前面的空白占位.Width
        Panel4.SuspendLayout()
        Panel4.Controls.Add(间隔)
        Panel4.Controls.SetChildIndex(间隔, 0)
        Panel4.Controls.Add(倍速按钮)
        Panel4.Controls.SetChildIndex(倍速按钮, 0)
        Panel4.ResumeLayout()
        更新倍速按钮尺寸()
        AddHandler 播放控制器.媒体已打开, Sub()
                                               视频缩放 = 1
                                               还原画面按钮.Hide()
                                               If 次字幕媒体路径 <> 播放控制器.当前媒体路径 Then 清除次字幕()
                                               缩放滚轮余量 = 0
                                               应用视频缩放设置()
                                               播放控制器.设置均衡器(均衡已启用, 均衡增益)
                                               If 当前倍速 <> 1 Then 播放控制器.设置播放倍速(当前倍速)
                                           End Sub
        AddHandler FormClosed, Sub()
                                   ' ModernContextMenu.Dispose 不递归子菜单，先收叶子再收根。
                                   释放菜单树(视频菜单)
                                   倍速弹窗?.Dispose()
                                   清除次字幕()
                               End Sub
    End Sub

    Friend Sub 应用视频缩放设置()
        当前缩放算法 = ({3UI, 0UI, 2UI, 1UI})(Math.Clamp(设置.实例对象.视频缩放质量选项, 0, 3))
        播放控制器?.设置缩放算法(当前缩放算法)
    End Sub

    Private Sub 更新倍速按钮尺寸()
        播放器界面呈现器.更新自适应宽度(倍速按钮)
        倍速按钮.Width = Math.Max(倍速按钮.Width, MB_软件解码或硬件解码.Width)
    End Sub

    Private Sub 应用视频菜单样式(菜单 As LakeUI.ModernContextMenu)
        Dim 参考 = MCM_标题栏菜单
        If Not 菜单.MenuFont.Equals(参考.MenuFont) Then
            Dim 旧字体 = 菜单.MenuFont
            菜单.MenuFont = DirectCast(参考.MenuFont.Clone(), Font)
            旧字体.Dispose()
        End If
        菜单.MenuForeColor = 参考.MenuForeColor
        菜单.BackdropMode = 参考.BackdropMode
        菜单.BackdropTintColor = 参考.BackdropTintColor
        菜单.BackdropBlurRadius = 参考.BackdropBlurRadius
        菜单.BackdropNoiseOpacity = 参考.BackdropNoiseOpacity
        菜单.BorderSize = 参考.BorderSize
        菜单.HoverBackColor = 参考.HoverBackColor
        菜单.PressedBackColor = 参考.PressedBackColor
        菜单.ItemHeight = 参考.ItemHeight
        菜单.ItemPadding = 参考.ItemPadding
        菜单.MenuPadding = 参考.MenuPadding
        For Each 项 In 菜单.Items
            If 项.SubMenu IsNot Nothing Then 应用视频菜单样式(项.SubMenu)
        Next
    End Sub

    Private Sub 应用倍速(值 As Double)
        If Math.Abs(当前倍速 - 值) < 0.000000001 Then Return
        播放控制器.设置播放倍速(值)
        当前倍速 = 值
        设置.实例对象.播放倍速 = 值
        倍速按钮.Text = 值.ToString("0.##", CultureInfo.InvariantCulture) & "×"
    End Sub

    Private Sub 显示均衡器()
        Using 窗口 As New Form With {.Text = "音频均衡器", .Size = New Size(680, 350), .StartPosition = FormStartPosition.CenterParent, .BackColor = BackColor, .ForeColor = ForeColor, .Font = Font, .MinimizeBox = False, .MaximizeBox = False}
            Dim 开关 As New CheckBox With {.Text = "启用均衡器", .Checked = 均衡已启用, .Dock = DockStyle.Top, .Height = 32}
            Dim 网格 As New TableLayoutPanel With {.Dock = DockStyle.Fill, .ColumnCount = 10, .RowCount = 3}
            Dim 重置 As New LakeUI.ModernButton With {.Text = "重置为平直", .Dock = DockStyle.Bottom, .Height = 32}
            Dim 滑块(9) As TrackBar
            Dim 频率 = {"60", "170", "310", "600", "1k", "3k", "6k", "12k", "14k", "16k"}
            For i = 0 To 9
                Dim 索引 = i
                网格.ColumnStyles.Add(New ColumnStyle(SizeType.Percent, 10))
                滑块(i) = New TrackBar With {.Minimum = -12, .Maximum = 12, .Value = CInt(均衡增益(i)), .Orientation = Orientation.Vertical, .Dock = DockStyle.Fill, .TickFrequency = 3}
                Dim 增益标签 As New Label With {.Text = $"{均衡增益(i):+0;-0;0} dB", .Dock = DockStyle.Fill, .TextAlign = ContentAlignment.MiddleCenter}
                网格.Controls.Add(New Label With {.Text = 频率(i) & " Hz", .Dock = DockStyle.Fill, .TextAlign = ContentAlignment.MiddleCenter}, i, 0)
                网格.Controls.Add(滑块(i), i, 1) : 网格.Controls.Add(增益标签, i, 2)
                AddHandler 滑块(i).ValueChanged, Sub()
                                                   均衡增益(索引) = 滑块(索引).Value
                                                   增益标签.Text = $"{均衡增益(索引):+0;-0;0} dB"
                                                   播放控制器.设置均衡器(均衡已启用, 均衡增益)
                                               End Sub
            Next
            网格.RowStyles.Add(New RowStyle(SizeType.Absolute, 25))
            网格.RowStyles.Add(New RowStyle(SizeType.Percent, 100))
            网格.RowStyles.Add(New RowStyle(SizeType.Absolute, 25))
            AddHandler 开关.CheckedChanged, Sub()
                                               均衡已启用 = 开关.Checked
                                               播放控制器.设置均衡器(均衡已启用, 均衡增益)
                                           End Sub
            AddHandler 重置.Click, Sub()
                                       For Each 控件 In 滑块
                                           控件.Value = 0
                                       Next
                                   End Sub
            窗口.Controls.Add(网格) : 窗口.Controls.Add(开关) : 窗口.Controls.Add(重置)
            窗口.ShowDialog(Me)
        End Using
    End Sub
End Class
