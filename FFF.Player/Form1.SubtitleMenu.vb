Imports System.IO
Imports System.Threading

Partial Public Class Form1
    Private ReadOnly 字幕菜单 As New LakeUI.ModernContextMenu With {.CornerRadius = 8, .HoverRadius = 5}
    Private 字幕显示 As Boolean = True
    Private 当前次字幕 As 外部字幕轨道
    Private 次字幕加载取消 As CancellationTokenSource
    Private 次字幕媒体路径 As String = ""
    Private 次字幕挂载路径 As String = ""
    Private 次字幕挂载索引 As Integer = -1
    Private 次字幕挂载轨 As 媒体流信息()

    Private Sub 初始化字幕菜单()
        视频菜单.Items.Add(New LakeUI.ModernContextMenu.ModernMenuItem With {.Text = "字幕设置", .SubMenu = 字幕菜单})
    End Sub

    Private Sub 更新字幕菜单()
        For Each 项 In 字幕菜单.Items
            项.SubMenu?.Dispose()
        Next
        字幕菜单.Items.Clear()
        Dim 媒体路径 = 播放控制器.当前媒体路径
        Dim 候选字幕 As IReadOnlyList(Of 外部字幕候选) = If(String.IsNullOrWhiteSpace(媒体路径),
            Array.Empty(Of 外部字幕候选)(), 外部字幕自动加载器.扫描同名字幕(媒体路径))
        ' 媒体信息是一次完整的原生往返 + JSON 反序列化，与槽无关，提出循环外只取一次。
        Dim 信息 = 播放控制器.安全读取媒体信息()
        For Each 槽 In {0, 1}
            Dim 插槽 = 槽
            Dim 轨道菜单 As New LakeUI.ModernContextMenu With {.CornerRadius = 8, .HoverRadius = 5}
            Dim 当前 = If(槽 = 0, 播放控制器.当前字幕, Volatile.Read(当前次字幕))
            添加字幕选择项(轨道菜单, "无", 当前 Is Nothing, Sub()
                                                                If 插槽 = 0 Then 播放控制器.关闭字幕() Else 清除次字幕(False)
                                                            End Sub)
            Dim 挂载路径 = If(槽 = 0, 播放控制器.已导入字幕?.路径, 次字幕挂载路径)
            Dim 挂载轨 = If(槽 = 0, 播放控制器.已导入字幕?.容器字幕轨, 次字幕挂载轨)
            Dim 挂载索引 = If(槽 = 0, If(播放控制器.已导入字幕?.流索引, -1), 次字幕挂载索引)
            Dim 已选挂载 = 当前 IsNot Nothing AndAlso Not 当前.是内嵌 AndAlso String.Equals(当前.路径, 挂载路径, StringComparison.OrdinalIgnoreCase)
            If Not String.IsNullOrEmpty(挂载路径) Then
                添加字幕选择项(轨道菜单, "挂载：" & Path.GetFileName(挂载路径), 已选挂载, Sub() 选择字幕文件(插槽, 挂载路径, 挂载索引))
            End If
            For Each 候选 In 候选字幕
                Dim 文件 = 候选.路径, 索引 = 候选.轨索引
                If String.Equals(挂载路径, 文件, StringComparison.OrdinalIgnoreCase) Then Continue For
                添加字幕选择项(轨道菜单, Path.GetFileName(文件), False, Sub() 选择字幕文件(插槽, 文件, 索引))
            Next
            If 挂载轨 IsNot Nothing Then
                For Each 轨 In 挂载轨
                    Dim 索引 = 轨.索引
                    Dim 语言 = 字幕元数据(轨, "language", "")
                    添加字幕选择项(轨道菜单, $"MKS 轨 {索引 + 1} · {轨.编码} · {语言}", 已选挂载 AndAlso 当前.流索引 = 索引, Sub() 选择字幕文件(插槽, 挂载路径, 索引))
                Next
            End If
            If 信息 IsNot Nothing Then
                Dim 序号 = 0
                For Each 轨 In 信息.流.Where(Function(x) x.类型 = "subtitle")
                    Dim 索引 = 轨.索引
                    Dim 名称 = 字幕元数据(轨, "title", 轨.编码)
                    Dim 语言 = 字幕元数据(轨, "language-ietf", 字幕元数据(轨, "language", "und"))
                    Dim 标记 As New List(Of String)
                    If 轨.是默认流 Then 标记.Add("Default")
                    If 轨.是强制流 Then 标记.Add("Forced")
                    Dim 文本 = $"Track 0:s:{序号} - {名称} - {语言} - {If(标记.Count = 0, "None", String.Join("+", 标记))}"
                    添加字幕选择项(轨道菜单, 文本, 当前 IsNot Nothing AndAlso 当前.是内嵌 AndAlso 当前.流索引 = 索引,
                        Sub()
                            If 插槽 = 0 Then
                                播放控制器.选择内嵌字幕(索引)
                            Else
                                Dim 忽略 = 加载次字幕Async(播放控制器.当前媒体路径, 索引, True)
                            End If
                        End Sub)
                    序号 += 1
                Next
            End If
            字幕菜单.Items.Add(New LakeUI.ModernContextMenu.ModernMenuItem With {.Text = If(槽 = 0, "选择字幕 · 底部", "选择次字幕 · 顶部"), .SubMenu = 轨道菜单})
        Next
        添加字幕选择项(字幕菜单, "显示字幕", 字幕显示, Sub()
                                                          字幕显示 = Not 字幕显示
                                                          设置.实例对象.显示字幕 = 字幕显示
                                                          字幕图层呈现器?.使图层失效()
                                                      End Sub)
        添加功能菜单(字幕菜单, "切换首 / 次字幕", AddressOf 交换主次字幕)
        添加功能菜单(字幕菜单, "挂载字幕文件…", Sub()
                                                     Using 对话框 As New OpenFileDialog With {.Title = "挂载字幕", .Filter = "Subtitles (*.ass;*.ssa;*.srt;*.sup;*.mks)|*.ass;*.ssa;*.srt;*.sup;*.mks"}
                                                         If 对话框.ShowDialog(Me) = DialogResult.OK Then 播放控制器.替换字幕(对话框.FileName)
                                                     End Using
                                                 End Sub)
        添加功能菜单(字幕菜单, "卸载挂载字幕，恢复内置选择", Sub()
                                                                    清除次字幕()
                                                                    播放控制器.卸载挂载字幕()
                                                                End Sub)
        添加功能菜单(字幕菜单, "字幕样式设置…(SRT)", Sub()
                                                              If 设置窗口 Is Nothing OrElse 设置窗口.IsDisposed Then 设置窗口 = New Form设置()
                                                              设置窗口.显示字幕设置()
                                                          End Sub)
    End Sub

    Private Sub 添加字幕选择项(菜单 As LakeUI.ModernContextMenu, 文本 As String, 已选 As Boolean, 操作 As Action)
        添加功能菜单(菜单, 文本, 操作)
        菜单.Items.Last().Checked = 已选
    End Sub

    Private Shared Function 字幕元数据(轨 As 媒体流信息, 名称 As String, 默认值 As String) As String
        Dim 值 As String = Nothing
        Return If(轨.元数据 IsNot Nothing AndAlso 轨.元数据.TryGetValue(名称, 值) AndAlso Not String.IsNullOrEmpty(值), 值, 默认值)
    End Function

    Private Sub 选择字幕文件(槽 As Integer, 路径 As String, Optional 索引 As Integer = -1)
        If 槽 = 0 Then
            播放控制器.选择外部字幕(路径, 索引)
        Else
            Dim 忽略 = 加载次字幕Async(路径, 索引, False)
        End If
    End Sub

    Private Async Function 加载次字幕Async(路径 As String, 索引 As Integer, 内嵌 As Boolean) As Task
        Dim 取消 As New CancellationTokenSource()
        Dim 旧取消 = Interlocked.Exchange(次字幕加载取消, 取消)
        旧取消?.Cancel()
        Dim 媒体 = 播放控制器.当前媒体路径
        Dim 轨道 As 外部字幕轨道 = Nothing
        Try
            If String.IsNullOrWhiteSpace(媒体) Then Return
            If 内嵌 Then
                ' 菜单构建与点击之间媒体可能已关闭：媒体信息或目标流取不到时静默放弃。
                Dim 快照 = 播放控制器.安全读取媒体信息()
                Dim 流 = 快照?.流?.FirstOrDefault(Function(x) x.索引 = 索引)
                If 流 Is Nothing Then Return
                轨道 = Await Task.Run(Function() 外部字幕自动加载器.加载内嵌字幕(媒体, 流, 取消.Token), 取消.Token)
            Else
                轨道 = Await 外部字幕自动加载器.加载字幕Async(路径, 媒体, 取消.Token, 索引)
            End If
            If 取消.IsCancellationRequested OrElse Not String.Equals(媒体, 播放控制器.当前媒体路径, StringComparison.OrdinalIgnoreCase) Then Return
            轨道.SRT生成器?.设置样式(设置.实例对象.创建SRT字幕样式())
            次字幕媒体路径 = 媒体
            If Not 内嵌 Then
                次字幕挂载路径 = 路径
                次字幕挂载索引 = 轨道.流索引
                次字幕挂载轨 = 轨道.容器字幕轨
            End If
            Dim 旧轨道 = Interlocked.Exchange(当前次字幕, 轨道)
            轨道 = Nothing
            旧轨道?.释放()
            字幕图层呈现器?.使图层失效()
        Catch ex As OperationCanceledException
        Catch ex As Exception
            If Not IsDisposed Then LakeUI.ExOverlayMsgBox(Me, ex.Message, MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, "字幕加载失败")
        Finally
            轨道?.释放()
            Interlocked.CompareExchange(次字幕加载取消, Nothing, 取消)
            取消.Dispose()
        End Try
    End Function

    Private Sub 清除次字幕(Optional 卸载 As Boolean = True)
        Interlocked.Exchange(次字幕加载取消, Nothing)?.Cancel()
        Interlocked.Exchange(当前次字幕, Nothing)?.释放()
        If 卸载 Then
            次字幕挂载路径 = ""
            次字幕挂载索引 = -1
            次字幕挂载轨 = Nothing
        End If
        字幕图层呈现器?.使图层失效()
    End Sub

    Private Sub 交换主次字幕()
        Dim 主 = 播放控制器.当前字幕, 次 = Volatile.Read(当前次字幕)
        If 次 Is Nothing Then
            播放控制器.关闭字幕()
        ElseIf 次.是内嵌 Then
            播放控制器.选择内嵌字幕(次.流索引)
        Else
            播放控制器.选择外部字幕(次.路径, 次.流索引)
        End If
        If 主 Is Nothing Then
            清除次字幕()
        Else
            Dim 忽略 = 加载次字幕Async(主.路径, 主.流索引, 主.是内嵌)
        End If
    End Sub
End Class
