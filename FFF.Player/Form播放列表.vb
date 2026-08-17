Public Class Form播放列表

    <Runtime.InteropServices.DllImport("user32.dll")>
    Private Shared Function GetDpiForWindow(hwnd As IntPtr) As UInteger
    End Function

    Private 列表数据 As 播放列表
    Private 播放请求 As Action(Of Integer)
    Private 当前媒体路径提供器 As Func(Of String)
    Private 显示项目 As New List(Of 播放列表项)()
    Private 正在刷新 As Boolean

    Friend Sub 连接(数据 As 播放列表, 请求播放 As Action(Of Integer), 当前媒体路径 As Func(Of String))
        ArgumentNullException.ThrowIfNull(数据)
        ArgumentNullException.ThrowIfNull(请求播放)
        ArgumentNullException.ThrowIfNull(当前媒体路径)
        If 列表数据 IsNot Nothing Then RemoveHandler 列表数据.列表变化, AddressOf 列表数据_列表变化
        列表数据 = 数据
        播放请求 = 请求播放
        当前媒体路径提供器 = 当前媒体路径
        AddHandler 列表数据.列表变化, AddressOf 列表数据_列表变化
        刷新列表()
    End Sub

    Friend Sub 显示窗口(宿主 As Form)
        更新出现位置(宿主)
        If Not Visible Then
            If 宿主 Is Nothing Then Show() Else Show(宿主)
            更新出现位置(宿主)
        Else
            Activate()
            BringToFront()
        End If
    End Sub

    Friend Sub 应用字体(fontName As String)
        字体控制.设置控件字体(fontName, Me, Nothing, True)
    End Sub

    Private Sub Form播放列表_Load(sender As Object, e As EventArgs) Handles Me.Load
        Form1.ThisIsYourWindow1.Attach(Me)
        AllowDrop = True
        ModernPanel1.AllowDrop = True
        Panel1.AllowDrop = True
        UltraDetailListView1.AllowDrop = True
        UltraDetailListView1.AllowDragReorder = True
        UltraDetailListView1.AllowColumnResize = False
        更新列表列宽()
        MCB_播放模式.SelectedIndex = 播放模式到选项(If(列表数据?.播放模式, 列表播放模式.顺序播放))
        应用字体(设置.实例对象.字体)
    End Sub

    Private Sub UltraDetailListView1_SizeChanged(sender As Object, e As EventArgs) Handles UltraDetailListView1.SizeChanged
        更新列表列宽()
    End Sub

    Private Sub Form播放列表_DpiChanged(sender As Object, e As DpiChangedEventArgs) Handles Me.DpiChanged
        If IsHandleCreated Then BeginInvoke(AddressOf 更新列表列宽)
    End Sub

    Private Sub 更新列表列宽()
        If UltraDetailListView1.Columns.Count = 0 OrElse UltraDetailListView1.ClientSize.Width <= 0 Then Return
        Dim DPI = Math.Max(1, UltraDetailListView1.DeviceDpi)
        If UltraDetailListView1.IsHandleCreated Then
            Dim 窗口DPI = CInt(GetDpiForWindow(UltraDetailListView1.Handle))
            If 窗口DPI > 0 Then DPI = 窗口DPI
        End If
        UltraDetailListView1.Columns(0).Width = 计算列表列宽(
            UltraDetailListView1.ClientSize.Width, UltraDetailListView1.Padding.Horizontal,
            UltraDetailListView1.BorderSize, UltraDetailListView1.BorderRadius, DPI)
    End Sub

    Friend Shared Function 计算列表列宽(客户区宽度 As Integer, 水平内边距 As Integer,
                                  边框宽度 As Integer, 圆角半径 As Integer, DPI As Integer) As Integer
        If DPI <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(DPI))
        Dim DPI比例 = CSng(DPI) / 96.0F
        Dim 边界内缩 = CInt(Math.Max(边框宽度 * DPI比例, 圆角半径 * DPI比例 / 2.0F))
        Return Math.Max(1, 客户区宽度 - 水平内边距 - 边界内缩 * 2)
    End Function

    Private Sub 列表数据_列表变化(sender As Object, e As EventArgs)
        If IsDisposed Then Return
        If InvokeRequired Then
            BeginInvoke(Sub() 列表数据_列表变化(sender, e))
            Return
        End If
        Dim 新项目 = 列表数据.取得项目().ToList()
        If 显示项目.Count = 新项目.Count AndAlso
            显示项目.Select(Function(x) x.路径).SequenceEqual(
                新项目.Select(Function(x) x.路径), StringComparer.OrdinalIgnoreCase) Then
            更新正在播放项()
            Return
        End If
        刷新列表(新项目)
    End Sub

    Private Sub 刷新列表(Optional 新项目 As List(Of 播放列表项) = Nothing)
        If 列表数据 Is Nothing Then Return
        Dim 原选中项目 = TryCast(UltraDetailListView1.SelectedItem?.Tag, 播放列表项)
        Dim 原选中路径 = 原选中项目?.路径
        正在刷新 = True
        Try
            显示项目 = If(新项目, 列表数据.取得项目().ToList())
            Dim 正在播放路径 = 取得当前媒体路径()
            UltraDetailListView1.BeginUpdate()
            Try
                UltraDetailListView1.Items.Clear()
                For Each 项目 In 显示项目
                    Dim 文字项 As New LakeUI.UltraDetailListView.ListSubItem(IO.Path.GetFileName(项目.路径)) With {
                        .ForeColor = If(路径相同(项目.路径, 正在播放路径), Color.YellowGreen, Color.Empty)
                    }
                    UltraDetailListView1.Items.Add(New LakeUI.UltraDetailListView.ListItem(文字项) With {.Tag = 项目})
                Next
            Finally
                UltraDetailListView1.EndUpdate()
            End Try
            If 原选中路径 IsNot Nothing Then
                Dim 索引 = 显示项目.FindIndex(Function(x) String.Equals(x.路径, 原选中路径, StringComparison.OrdinalIgnoreCase))
                UltraDetailListView1.SelectedIndex = 索引
                If 索引 >= 0 Then UltraDetailListView1.EnsureVisible(索引)
            End If
        Finally
            正在刷新 = False
        End Try
    End Sub

    Friend Sub 更新正在播放项()
        If 正在刷新 OrElse IsDisposed Then Return
        Dim 正在播放路径 = 取得当前媒体路径()
        For Each 行 In UltraDetailListView1.Items
            Dim 项目 = TryCast(行.Tag, 播放列表项)
            If 行.SubItems.Count > 0 Then
                行.SubItems(0).ForeColor = If(项目 IsNot Nothing AndAlso 路径相同(项目.路径, 正在播放路径),
                                              Color.YellowGreen, Color.Empty)
            End If
        Next
        UltraDetailListView1.RefreshItems()
    End Sub

    Private Sub MB_加载_Click(sender As Object, e As EventArgs) Handles MB_加载.Click
        Using 对话框 As New OpenFileDialog With {
            .CheckFileExists = True,
            .Filter = "M3U8 播放列表|*.m3u8",
            .RestoreDirectory = True,
            .Title = "加载播放列表"
        }
            If 对话框.ShowDialog(Me) <> DialogResult.OK Then Return
            Try
                列表数据.导入M3U8(对话框.FileName)
            Catch ex As Exception
                显示错误("无法加载播放列表", ex.Message)
            End Try
        End Using
    End Sub

    Private Sub MB_保存_Click(sender As Object, e As EventArgs) Handles MB_保存.Click
        Using 对话框 As New SaveFileDialog With {
            .AddExtension = True,
            .DefaultExt = "m3u8",
            .Filter = "M3U8 播放列表|*.m3u8",
            .OverwritePrompt = True,
            .RestoreDirectory = True,
            .Title = "保存播放列表"
        }
            If 对话框.ShowDialog(Me) <> DialogResult.OK Then Return
            Try
                列表数据.导出M3U8(对话框.FileName)
            Catch ex As Exception
                显示错误("无法保存播放列表", ex.Message)
            End Try
        End Using
    End Sub

    Private Sub MB_添加_Click(sender As Object, e As EventArgs) Handles MB_添加.Click
        Using 对话框 As New OpenFileDialog With {
            .CheckFileExists = True,
            .Filter = "所有文件|*.*",
            .Multiselect = True,
            .RestoreDirectory = True,
            .Title = "添加媒体文件"
        }
            If 对话框.ShowDialog(Me) <> DialogResult.OK Then Return
            Try
                For Each 路径 In 对话框.FileNames
                    列表数据.添加(路径)
                Next
            Catch ex As Exception
                显示错误("无法添加媒体", ex.Message)
            End Try
        End Using
    End Sub

    Private Sub MB_移除_Click(sender As Object, e As EventArgs) Handles MB_移除.Click
        Dim 索引集合 = UltraDetailListView1.SelectedItems.
            Select(Function(x) 显示项目.IndexOf(TryCast(x.Tag, 播放列表项))).
            Where(Function(x) x >= 0).OrderByDescending(Function(x) x).ToArray()
        For Each 索引 In 索引集合
            If 索引 >= 0 AndAlso 索引 < 列表数据.数量 Then 列表数据.移除(索引)
        Next
    End Sub

    Private Sub MB_定位_Click(sender As Object, e As EventArgs) Handles MB_定位.Click
        Dim 当前路径 = 取得当前媒体路径()
        Dim 索引 = 显示项目.FindIndex(Function(x) 路径相同(x.路径, 当前路径))
        If 索引 < 0 OrElse 索引 >= UltraDetailListView1.Items.Count Then Return
        UltraDetailListView1.SelectedIndex = 索引
        UltraDetailListView1.EnsureVisible(索引)
    End Sub

    Private Sub UltraDetailListView1_ItemDoubleClick(sender As Object, e As LakeUI.UltraDetailListView.ListItemEventArgs) Handles UltraDetailListView1.ItemDoubleClick
        Dim 项目 = TryCast(e.Item?.Tag, 播放列表项)
        Dim 索引 = If(项目 Is Nothing, -1, 显示项目.IndexOf(项目))
        If 索引 >= 0 Then 播放请求(索引)
    End Sub

    Private Sub UltraDetailListView1_ItemOrderChanged(sender As Object, e As EventArgs) Handles UltraDetailListView1.ItemOrderChanged
        If 正在刷新 Then Return
        Dim 新顺序 = UltraDetailListView1.Items.Select(Function(x) TryCast(x.Tag, 播放列表项)).
            Where(Function(x) x IsNot Nothing).ToList()
        If 新顺序.Count <> 显示项目.Count Then Return
        显示项目 = 新顺序
        列表数据.按路径顺序重排(显示项目.Select(Function(x) x.路径))
    End Sub

    Private Sub UltraDetailListView1_AfterLabelEdit(sender As Object, e As LakeUI.UltraDetailListView.LabelEditEventArgs) Handles UltraDetailListView1.AfterLabelEdit
        e.CancelEdit = True
    End Sub

    Private Sub 播放列表_DragEnter(sender As Object, e As DragEventArgs) Handles Me.DragEnter, ModernPanel1.DragEnter, Panel1.DragEnter, UltraDetailListView1.DragEnter
        e.Effect = If(e.Data IsNot Nothing AndAlso e.Data.GetDataPresent(DataFormats.FileDrop),
                      DragDropEffects.Copy, DragDropEffects.None)
    End Sub

    Private Sub 播放列表_DragDrop(sender As Object, e As DragEventArgs) Handles Me.DragDrop, ModernPanel1.DragDrop, Panel1.DragDrop, UltraDetailListView1.DragDrop
        Dim 路径 = TryCast(e.Data?.GetData(DataFormats.FileDrop), String())
        If 路径 Is Nothing OrElse 路径.Length = 0 Then Return
        Try
            For Each 媒体路径 In 枚举拖入媒体(路径)
                列表数据.添加(媒体路径)
            Next
        Catch ex As Exception
            显示错误("无法添加媒体", ex.Message)
        End Try
    End Sub

    Friend Shared Iterator Function 枚举拖入媒体(路径 As IEnumerable(Of String)) As IEnumerable(Of String)
        For Each 值 In 路径
            If IO.File.Exists(值) Then
                If 播放列表.是支持的媒体文件(值) Then Yield IO.Path.GetFullPath(值)
            ElseIf IO.Directory.Exists(值) Then
                For Each 文件 In IO.Directory.EnumerateFiles(值, "*", IO.SearchOption.TopDirectoryOnly).
                    Where(AddressOf 播放列表.是支持的媒体文件).
                    OrderBy(Function(x) IO.Path.GetFileName(x), StringComparer.CurrentCultureIgnoreCase)
                    Yield 文件
                Next
            End If
        Next
    End Function

    Private Function 取得当前媒体路径() As String
        Try
            Return If(当前媒体路径提供器?.Invoke(), String.Empty)
        Catch ex As ObjectDisposedException
            Return String.Empty
        End Try
    End Function

    Private Shared Function 路径相同(左 As String, 右 As String) As Boolean
        Return Not String.IsNullOrEmpty(左) AndAlso Not String.IsNullOrEmpty(右) AndAlso
            String.Equals(左, 右, StringComparison.OrdinalIgnoreCase)
    End Function

    Private Sub 更新出现位置(宿主 As Form)
        If 宿主 Is Nothing OrElse 宿主.IsDisposed Then Return
        StartPosition = FormStartPosition.Manual
        If 宿主.WindowState = FormWindowState.Maximized OrElse Form1.ThisIsYourWindow1.IsFullScreen(宿主) Then
            Bounds = 计算居中边界(宿主.Bounds, Size)
        Else
            Bounds = 计算贴靠边界(宿主.Bounds, Width)
        End If
    End Sub

    Friend Shared Function 计算贴靠边界(宿主边界 As Rectangle, 窗口宽度 As Integer) As Rectangle
        If 宿主边界.Width <= 0 OrElse 宿主边界.Height <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(宿主边界))
        If 窗口宽度 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(窗口宽度))
        Return New Rectangle(宿主边界.Right, 宿主边界.Top, 窗口宽度, 宿主边界.Height)
    End Function

    Friend Shared Function 计算居中边界(宿主边界 As Rectangle, 窗口大小 As Size) As Rectangle
        If 宿主边界.Width <= 0 OrElse 宿主边界.Height <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(宿主边界))
        If 窗口大小.Width <= 0 OrElse 窗口大小.Height <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(窗口大小))
        Return New Rectangle(宿主边界.Left + (宿主边界.Width - 窗口大小.Width) \ 2,
                             宿主边界.Top + (宿主边界.Height - 窗口大小.Height) \ 2,
                             窗口大小.Width, 窗口大小.Height)
    End Function

    Private Sub MCB_播放模式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_播放模式.SelectedIndexChanged
        If 正在刷新 OrElse 列表数据 Is Nothing Then Return
        Select Case MCB_播放模式.SelectedIndex
            Case 0 : 列表数据.播放模式 = 列表播放模式.顺序播放
            Case 1 : 列表数据.播放模式 = 列表播放模式.单项循环
            Case 2 : 列表数据.播放模式 = 列表播放模式.列表循环
            Case 3 : 列表数据.播放模式 = 列表播放模式.随机播放
        End Select
    End Sub

    Private Shared Function 播放模式到选项(模式 As 列表播放模式) As Integer
        Select Case 模式
            Case 列表播放模式.单项循环 : Return 1
            Case 列表播放模式.列表循环 : Return 2
            Case 列表播放模式.随机播放 : Return 3
            Case Else : Return 0
        End Select
    End Function

    Private Sub 显示错误(标题 As String, 内容 As String)
        LakeUI.ExOverlayMsgBox(Me, 内容, MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, 标题)
    End Sub

    Private Sub Form播放列表_FormClosing(sender As Object, e As FormClosingEventArgs) Handles Me.FormClosing
        If e.CloseReason = CloseReason.UserClosing Then
            e.Cancel = True
            Hide()
        End If
    End Sub

    Private Sub Form播放列表_FormClosed(sender As Object, e As FormClosedEventArgs) Handles Me.FormClosed
        If 列表数据 IsNot Nothing Then RemoveHandler 列表数据.列表变化, AddressOf 列表数据_列表变化
    End Sub
End Class
