Imports System
Imports System.Collections.Generic
Imports System.Drawing
Imports System.IO
Imports System.Linq
Imports System.Reflection
Imports System.Text.Json
Imports System.Text.RegularExpressions
Imports System.Windows.Forms
Imports Microsoft.Win32
Imports FFF.Player
Imports LakeUI

''' <summary>相机 RAW 关联/打开白名单回归。
''' 跑法：dotnet FFF.Player.Tests.dll --raw-file-association-regression
''' （注册表往返必须用 dotnet 宿主；未签名的 apphost 会被系统拒绝写 HKCU）
''' 判据：RAW 分组、播放列表媒体/图片白名单、RAW 识别必须共用 Raw格式.扩展名列表；
''' 任何一处漏项都会让“设置页可勾选但双击打不开”或“能打开但图片模式不认”重新出现。</summary>
Friend Module 文件关联回归

    Friend Function 运行() As Integer
        Dim 失败 = 0
        Dim 关联扩展名 = 文件关联管理器.取得扩展名(文件关联类别.相机RAW)

        ' .x3f 只在 LibRaw 带 USE_X3FTOOLS 时才允许出现在关联承诺里。
        ' 这里同时把“构建机拿错 DLL”从运行期的笼统 open=-2 变成测试期明确失败。
        If Not Raw格式.X3F可用 Then
            失败 += 1
            Console.WriteLine("FAIL  随包 libraw.dll 未启用 X3F 支持（需要 USE_X3FTOOLS）；不能把 .x3f 当普通 RAW 承诺")
        ElseIf Not 关联扩展名.Contains(".x3f") Then
            失败 += 1
            Console.WriteLine("FAIL  LibRaw 已启用 X3F，但 RAW 关联分组缺少 .x3f")
        End If

        If Not 播放列表.相机RAW格式筛选器.Contains("*.dng") Then
            失败 += 1
            Console.WriteLine("FAIL  RAW 打开对话框筛选器未包含 *.dng")
        End If

        运行设置页(失败)
        运行列表交互(失败)

        If 关联扩展名.Count <> Raw格式.扩展名列表.Length Then
            失败 += 1
            Console.WriteLine($"FAIL  RAW 关联分组数量：期望 {Raw格式.扩展名列表.Length}，实际 {关联扩展名.Count}")
        End If

        For Each 扩展名 In Raw格式.扩展名列表
            If Not 关联扩展名.Contains(扩展名) Then
                失败 += 1
                Console.WriteLine($"FAIL  RAW 关联分组缺少 {扩展名}")
            End If
            If Not 播放列表.是支持的媒体文件("sample" & 扩展名) Then
                失败 += 1
                Console.WriteLine($"FAIL  媒体白名单缺少 {扩展名}")
            End If
            If Not 播放列表.是图片文件("sample" & 扩展名) Then
                失败 += 1
                Console.WriteLine($"FAIL  图片白名单缺少 {扩展名}")
            End If
            If Not 播放列表.是相机RAW文件("sample" & 扩展名) Then
                失败 += 1
                Console.WriteLine($"FAIL  RAW 识别缺少 {扩展名}")
            End If
        Next

        ' 这几项不是 RAW，必须继续被排除，防止集合被扩成“所有扩展名都算 RAW”。
        For Each 扩展名 In {".mp4", ".png", ".txt"}
            If 播放列表.是相机RAW文件("sample" & 扩展名) Then
                失败 += 1
                Console.WriteLine($"FAIL  非 RAW 扩展名被误判：{扩展名}")
            End If
        Next

        运行注册表往返(失败)
        运行设置映射往返(失败)
        运行宿主内核一致(失败)

        Console.WriteLine()
        Console.WriteLine($"RAW 文件关联回归：{Raw格式.扩展名列表.Length} 个扩展名，失败 {失败} 条")
        Return If(失败 = 0, 0, 1)
    End Function

    Private Sub 运行设置页(ByRef 失败 As Integer)
        Dim 原设置 = 设置.实例对象
        Try
            设置.实例对象 = New 设置 With {.关联扩展名 = {".mp4", ".DNG"}}
            Using 窗体 As New Form设置_文件关联()
                窗体.初始化页面()
                Dim 列表 = 窗体.UltraDetailListView1
                Dim 类别集合 = [Enum].GetValues(Of 文件关联类别)()
                断言条件(列表.Groups.Count = 类别集合.Length, "列表分组数量与关联类别一致", 失败)
                For Each 类别 In 类别集合
                    Dim 组名 = 类别.ToString()
                    Dim 实际 = 列表.Items.Where(Function(项) 项.GroupName = 组名).Select(Function(项) CStr(项.Tag))
                    断言条件(实际.SequenceEqual(文件关联管理器.取得扩展名(类别)), $"{组名}显示对应的扩展名", 失败)
                Next
                断言条件(列表.MultiSelect AndAlso Not 列表.AllowDragReorder AndAlso Not 列表.AllowColumnResize,
                        "列表开启多选并禁用拖动排序和手动列宽", 失败)
                断言条件(列表.CheckedItems.Select(Function(项) CStr(项.Tag)).SequenceEqual({".mp4", ".dng"}),
                        "列表只勾选配置中的后缀", 失败)
                Dim 原数量 = 列表.Items.Count
                设置.实例对象.关联扩展名 = {".mkv"}
                窗体.初始化页面()
                断言条件(列表.Items.Count = 原数量 AndAlso
                        列表.CheckedItems.Select(Function(项) CStr(项.Tag)).SequenceEqual({".mkv"}),
                        "重新初始化回显新勾选且不重复增加列表项", 失败)

                窗体.PerformLayout()
                Dim 原列宽 = 列表.Columns(0).Width
                Dim 原列表宽 = 列表.ClientSize.Width
                窗体.ClientSize = New Size(窗体.ClientSize.Width + 80, 窗体.ClientSize.Height)
                断言条件(列表.Columns(0).Width > 原列宽 AndAlso
                        列表.Columns(0).Width - 原列宽 = 列表.ClientSize.Width - 原列表宽,
                        "列宽跟随列表调整", 失败)
                Dim 滚动条宽 = CInt(列表.ScrollBarWidth * D3D_D2DInterop.GetCurrentDpiScale(列表))
                断言条件(列表.ClientSize.Width - 列表.Columns(0).Width >= 滚动条宽 + D3D_ScrollBarRenderer.Margin * 2,
                        "列宽预留纵向滚动条及其命中区", 失败)
            End Using
        Finally
            设置.实例对象 = 原设置
        End Try
    End Sub

    Private Sub 运行列表交互(ByRef 失败 As Integer)
        Using 列表 As New 可测试关联列表 With {
            .Size = New Size(220, 200), .CheckBoxVisible = True, .CheckBoxSize = 20,
            .CheckBoxBorderSize = 0, .BorderSize = 0, .ItemPadding = New Padding(8, 4, 4, 4)}
            列表.Columns.Add(New UltraDetailListView.ListColumn("扩展名", 180))
            Dim 首项 As New UltraDetailListView.ListItem(New UltraDetailListView.ListSubItem(".mp4"))
            Dim 次项 As New UltraDetailListView.ListItem(New UltraDetailListView.ListSubItem(".mkv")) With {.Checked = True}
            Dim 其他项 As New UltraDetailListView.ListItem(New UltraDetailListView.ListSubItem(".mov")) With {.Checked = True}
            列表.Items.AddRange({首项, 次项, 其他项})
            首项.Selected = True
            次项.Selected = True
            Dim 已处理 = 列表.模拟按键(Keys.Enter)
            断言条件(已处理 AndAlso 首项.Checked AndAlso 次项.Checked AndAlso 其他项.Checked,
                    "Enter 勾选所选项并保留已勾选状态", 失败)
            已处理 = 列表.模拟按键(Keys.Delete)
            断言条件(已处理 AndAlso Not 首项.Checked AndAlso Not 次项.Checked AndAlso 其他项.Checked,
                    "Delete 取消所选项，保留未选中项的勾选", 失败)
            断言条件(列表.SelectedItems.Count = 2, "批量勾选不改变列表选区", 失败)

            Dim 首行坐标 = Enumerable.Range(0, 列表.ClientSize.Height).Where(Function(y) 列表.GetItemAt(100, y) Is 首项).ToArray()
            断言条件(首行坐标.Length > 0, "列表可命中第一行", 失败)
            If 首行坐标.Length = 0 Then Return
            Dim 行中点 = 首行坐标(首行坐标.Length \ 2)
            列表.模拟双击(MouseButtons.Left, New Point(100, 行中点))
            断言条件(首项.Checked AndAlso Not 次项.Checked AndAlso 其他项.Checked, "双击行只切换该行的勾选", 失败)
            列表.模拟双击(MouseButtons.Right, New Point(100, 行中点))
            断言条件(首项.Checked, "右键双击不改变勾选", 失败)
            Dim 复选框中点 = CInt((列表.ItemPadding.Left + 列表.CheckBoxSize / 2.0F) * 列表.DeviceDpi / 96.0F)
            列表.模拟双击(MouseButtons.Left, New Point(复选框中点, 行中点))
            断言条件(Not 首项.Checked, "双击复选框的结果仍是切换一次", 失败)
        End Using
    End Sub

    Private Class 可测试关联列表
        Inherits 文件关联列表

        Friend Function 模拟按键(按键 As Keys) As Boolean
            Dim 消息 As New Message()
            Return ProcessCmdKey(消息, 按键)
        End Function

        Friend Sub 模拟双击(按钮 As MouseButtons, 位置 As Point)
            Dim 单击 As New MouseEventArgs(按钮, 1, 位置.X, 位置.Y, 0)
            Dim 双击 As New MouseEventArgs(按钮, 2, 位置.X, 位置.Y, 0)
            OnMouseDown(单击)
            OnMouseUp(单击)
            OnMouseDown(双击)
            OnMouseDoubleClick(双击)
            OnMouseUp(单击)
        End Sub
    End Class

    ''' <summary>在独立的 HKCU 测试根上跑一次注册→回读→注销→恢复，不碰真实 .dng 关联。</summary>
    Private Sub 运行注册表往返(ByRef 失败 As Integer)
        Dim 根 = "Software\Classes\3FCompareRawAssociationTest-" & Guid.NewGuid().ToString("N")
        Dim 位置 As New 文件关联注册表位置(
            根 & "\Classes", 根 & "\Application", 根 & "\RegisteredApplications")
        Dim 程序路径 = Path.Combine(AppContext.BaseDirectory, "FFF.Player.exe")
        Try
            If Not File.Exists(程序路径) Then
                失败 += 1
                Console.WriteLine($"FAIL  注册表回归找不到宿主程序：{程序路径}")
                Return
            End If

            ' 造一个“用户原本已有 .dng 关联”的现场，注销后必须原样恢复。
            Using 扩展键 = Registry.CurrentUser.CreateSubKey(位置.ClassesRoot & "\.dng", True)
                扩展键.SetValue(String.Empty, "Existing.DNG", RegistryValueKind.String)
                扩展键.SetValue("Content Type", "image/x-existing", RegistryValueKind.String)
                扩展键.SetValue("PerceivedType", "document", RegistryValueKind.String)
                Using 打开方式 = 扩展键.CreateSubKey("OpenWithProgids", True)
                    打开方式.SetValue("Existing.DNG", Array.Empty(Of Byte)(), RegistryValueKind.None)
                End Using
            End Using

            Dim 配置 As New 设置 With {.关联扩展名 = {".dng"}}
            调用同步核心(文件关联选项.从设置(配置), 程序路径, 位置)

            断言字符串(位置.ClassesRoot & "\.dng", String.Empty, "FFF.Player.dng", 失败)
            断言字符串(位置.ClassesRoot & "\.dng", "Content Type", "image/x-adobe-dng", 失败)
            断言字符串(位置.ClassesRoot & "\.dng", "PerceivedType", "image", 失败)
            断言值存在(位置.ClassesRoot & "\.dng\OpenWithProgids", "FFF.Player.dng", True, 失败)
            断言值存在(位置.ClassesRoot & "\Applications\FFF.Player.exe\SupportedTypes",
                ".dng", True, 失败)
            断言字符串(位置.CapabilitiesRoot & "\FileAssociations", ".dng",
                "FFF.Player.dng", 失败)
            断言字符串(位置.RegisteredApplications, "FFF.Player", 位置.CapabilitiesRoot, 失败)
            断言键存在(位置.ApplicationRoot & "\FileAssociationBackups\dng", True, 失败)

            断言键存在(位置.ClassesRoot & "\.nef", False, 失败)
            断言值存在(位置.CapabilitiesRoot & "\FileAssociations", ".nef", False, 失败)
            断言值存在(位置.ClassesRoot & "\Applications\FFF.Player.exe\SupportedTypes", ".nef", False, 失败)

            ' 关联两个同组后缀后只取消 .dng，.nef 仍应关联到播放器。
            配置.关联扩展名 = {".dng", ".nef"}
            调用同步核心(文件关联选项.从设置(配置), 程序路径, 位置)
            配置.关联扩展名 = {".nef"}
            调用同步核心(文件关联选项.从设置(配置), 程序路径, 位置)

            断言字符串(位置.ClassesRoot & "\.dng", String.Empty, "Existing.DNG", 失败)
            断言字符串(位置.ClassesRoot & "\.dng", "Content Type", "image/x-existing", 失败)
            断言字符串(位置.ClassesRoot & "\.dng", "PerceivedType", "document", 失败)
            断言值存在(位置.ClassesRoot & "\.dng\OpenWithProgids", "FFF.Player.dng", False, 失败)
            断言值存在(位置.ClassesRoot & "\.dng\OpenWithProgids", "Existing.DNG", True, 失败)
            断言键存在(位置.ClassesRoot & "\FFF.Player.dng", False, 失败)
            断言键存在(位置.ApplicationRoot & "\FileAssociationBackups\dng", False, 失败)
            断言字符串(位置.ClassesRoot & "\.nef", String.Empty, "FFF.Player.nef", 失败)
            断言值存在(位置.ClassesRoot & "\Applications\FFF.Player.exe\SupportedTypes", ".dng", False, 失败)
            断言值存在(位置.ClassesRoot & "\Applications\FFF.Player.exe\SupportedTypes", ".nef", True, 失败)
            断言值存在(位置.CapabilitiesRoot & "\FileAssociations", ".dng", False, 失败)
            断言字符串(位置.CapabilitiesRoot & "\FileAssociations", ".nef", "FFF.Player.nef", 失败)

            配置.关联扩展名 = Array.Empty(Of String)()
            调用同步核心(文件关联选项.从设置(配置), 程序路径, 位置)
            断言键存在(位置.ClassesRoot & "\.nef", False, 失败)
            断言键存在(位置.ClassesRoot & "\Applications\FFF.Player.exe", False, 失败)
            断言键存在(位置.CapabilitiesRoot, False, 失败)
            断言值存在(位置.RegisteredApplications, "FFF.Player", False, 失败)
        Catch ex As Exception
            失败 += 1
            Console.WriteLine($"FAIL  注册表往返异常：{ex}")
        Finally
            Try
                Registry.CurrentUser.DeleteSubKeyTree(根, False)
            Catch ex As Exception
                Console.WriteLine($"WARN  清理临时注册表根失败：{ex.Message}")
            End Try
        End Try
    End Sub

    Private Sub 运行设置映射往返(ByRef 失败 As Integer)
        Dim 配置 As New 设置 With {.关联扩展名 = {".DNG", ".mp4"}}
        Dim 回读 = JsonSerializer.Deserialize(Of 设置)(JsonSerializer.Serialize(配置))
        Dim 选项 = 文件关联选项.从设置(回读)
        断言条件(选项.已启用(".dng") AndAlso 选项.已启用(".mp4") AndAlso Not 选项.已启用(".nef"),
                "JSON 回读后仍按扩展名独立启用，忽略大小写", 失败)
        Dim 空选项 = 文件关联选项.从设置(JsonSerializer.Deserialize(Of 设置)("{}"))
        断言条件(Not 空选项.已启用(".dng"), "缺少扩展名设置时默认未勾选", 失败)
    End Sub

    Private Sub 断言条件(条件 As Boolean, 说明 As String, ByRef 失败 As Integer)
        If 条件 Then Return
        失败 += 1
        Console.WriteLine("FAIL  " & 说明)
    End Sub

    Private Sub 运行宿主内核一致(ByRef 失败 As Integer)
        Dim 源路径 = 查找内核源文件()
        If 源路径 = String.Empty Then
            Console.WriteLine("INFO  未找到 RawStillSource.cpp，跳过宿主/内核扩展名一致性检查")
            Return
        End If
        Dim 文本 = File.ReadAllText(源路径)
        Dim 数组起点 = 文本.IndexOf("kExtensions[]", StringComparison.Ordinal)
        If 数组起点 < 0 Then
            失败 += 1
            Console.WriteLine("FAIL  RawStillSource.cpp 未找到 kExtensions 数组")
            Return
        End If
        Dim 数组结束 = 文本.IndexOf("};", 数组起点, StringComparison.Ordinal)
        If 数组结束 < 0 Then
            失败 += 1
            Console.WriteLine("FAIL  RawStillSource.cpp 的 kExtensions 数组未闭合")
            Return
        End If
        Dim 片段 = 文本.Substring(数组起点, 数组结束 - 数组起点)
        Dim 内核 = New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        For Each 匹配 As Match In Regex.Matches(片段, """\.?([a-z0-9]+)""")
            内核.Add("." & 匹配.Groups(1).Value.ToLowerInvariant())
        Next
        If 内核.Count = 0 Then
            失败 += 1
            Console.WriteLine("FAIL  RawStillSource.cpp 的 kExtensions 未解析出任何扩展名")
            Return
        End If
        For Each 扩展名 In Raw格式.扩展名列表
            If Not 内核.Contains(扩展名) Then
                失败 += 1
                Console.WriteLine($"FAIL  内核 LooksLikeRawPath 缺少宿主扩展名 {扩展名}")
            End If
        Next
        Dim 内核额外 = String.Join(", ", 内核.Where(Function(x) Not Raw格式.扩展名列表.Contains(x)))
        Console.WriteLine($"INFO  宿主 {Raw格式.扩展名列表.Length} 项，内核 {内核.Count} 项" &
                          If(内核额外 = String.Empty, "", "；内核额外：" & 内核额外))
    End Sub

    Private Function 查找内核源文件() As String
        Dim 目录 = New DirectoryInfo(AppContext.BaseDirectory)
        For 层 = 0 To 6
            Dim 候选 = Path.Combine(目录.FullName, "FFF.Native\3FP\Core\RawStillSource.cpp")
            If File.Exists(候选) Then Return 候选
            If 目录.Parent Is Nothing Then Exit For
            目录 = 目录.Parent
        Next
        Return String.Empty
    End Function

    Private Sub 调用同步核心(选项 As 文件关联选项, 程序路径 As String,
                           位置 As 文件关联注册表位置)
        Dim 方法 = GetType(文件关联管理器).GetMethod(
            "同步核心", BindingFlags.NonPublic Or BindingFlags.Static)
        If 方法 Is Nothing Then Throw New MissingMethodException("文件关联管理器", "同步核心")
        方法.Invoke(Nothing, New Object() {选项, 程序路径, 位置, False, False})
    End Sub

    Private Sub 断言字符串(路径 As String, 值名 As String, 期望 As String, ByRef 失败 As Integer)
        Using 键 = Registry.CurrentUser.OpenSubKey(路径)
            If 键 Is Nothing Then
                失败 += 1
                Console.WriteLine($"FAIL  注册表键不存在：{路径}")
                Return
            End If
            Dim 实际 = TryCast(键.GetValue(值名, Nothing,
                RegistryValueOptions.DoNotExpandEnvironmentNames), String)
            If Not String.Equals(实际, 期望, StringComparison.OrdinalIgnoreCase) Then
                失败 += 1
                Console.WriteLine($"FAIL  {路径} 的值 {值名}：期望 {期望}，实际 {实际}")
            End If
        End Using
    End Sub

    Private Sub 断言值存在(路径 As String, 值名 As String, 期望存在 As Boolean,
                           ByRef 失败 As Integer)
        Using 键 = Registry.CurrentUser.OpenSubKey(路径)
            If 键 Is Nothing Then
                If Not 期望存在 Then Return
                失败 += 1
                Console.WriteLine($"FAIL  注册表键不存在：{路径}（期望值 {值名}）")
                Return
            End If
            Dim 存在 = False
            For Each 名称 In 键.GetValueNames()
                If String.Equals(名称, 值名, StringComparison.OrdinalIgnoreCase) Then
                    存在 = True
                    Exit For
                End If
            Next
            If 存在 <> 期望存在 Then
                失败 += 1
                Console.WriteLine($"FAIL  {路径} 的值 {值名}：期望存在={期望存在}，实际存在={存在}")
            End If
        End Using
    End Sub

    Private Sub 断言键存在(路径 As String, 期望存在 As Boolean, ByRef 失败 As Integer)
        Using 键 = Registry.CurrentUser.OpenSubKey(路径)
            Dim 存在 = 键 IsNot Nothing
            If 存在 <> 期望存在 Then
                失败 += 1
                Console.WriteLine($"FAIL  注册表键 {路径}：期望存在={期望存在}，实际存在={存在}")
            End If
        End Using
    End Sub
End Module
