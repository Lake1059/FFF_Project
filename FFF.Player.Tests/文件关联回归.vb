Imports System
Imports System.Collections.Generic
Imports System.IO
Imports System.Linq
Imports System.Reflection
Imports System.Text.RegularExpressions
Imports Microsoft.Win32
Imports FFF.Player

''' <summary>相机 RAW 关联/打开白名单回归。
''' 跑法：dotnet FFF.Player.Tests.dll --raw-file-association-regression
''' （注册表往返必须用 dotnet 宿主；未签名的 apphost 会被系统拒绝写 HKCU）
''' 判据：关联档位、播放列表媒体/图片白名单、RAW 识别必须共用 Raw格式.扩展名列表；
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
            Console.WriteLine("FAIL  LibRaw 已启用 X3F，但 RAW 关联档位缺少 .x3f")
        End If

        If Not 播放列表.相机RAW格式筛选器.Contains("*.dng") Then
            失败 += 1
            Console.WriteLine("FAIL  RAW 打开对话框筛选器未包含 *.dng")
        End If

        ' 设置页布局用反射实例化，避免测试工程为 LakeUI 控件类型再加一条编译引用。
        Dim 窗体类型 = GetType(Form1).Assembly.GetType("FFF.Player.Form设置_文件关联", throwOnError:=True)
        Dim 窗体 = DirectCast(Activator.CreateInstance(窗体类型), System.Windows.Forms.Form)
        Try
            窗体.PerformLayout()
            Dim 面板 As System.Windows.Forms.Control = Nothing
            For Each 控件 As System.Windows.Forms.Control In 窗体.Controls
                If 控件.Name = "ModernPanel1" Then 面板 = 控件
            Next
            Dim 相机复选框 As System.Windows.Forms.Control = Nothing
            If 面板 IsNot Nothing Then
                For Each 控件 As System.Windows.Forms.Control In 面板.Controls
                    If 控件.Name = "MCK_关联相机RAW" Then 相机复选框 = 控件
                Next
            End If
            If 相机复选框 Is Nothing Then
                失败 += 1
                Console.WriteLine("FAIL  设置页未找到 MCK_关联相机RAW")
            ElseIf 相机复选框.Bottom > 窗体.ClientSize.Height Then
                失败 += 1
                Console.WriteLine($"FAIL  设置页相机 RAW 复选框超出客户区：bottom={相机复选框.Bottom}, client={窗体.ClientSize.Height}")
            End If
            If 相机复选框 IsNot Nothing Then
                Dim 初始化方法 = 窗体类型.GetMethod("初始化页面",
                    BindingFlags.NonPublic Or BindingFlags.Instance)
                If 初始化方法 Is Nothing Then
                    失败 += 1
                    Console.WriteLine("FAIL  设置页缺少 初始化页面")
                Else
                    Dim 原值 = 设置.实例对象.关联相机RAW
                    Try
                        设置.实例对象.关联相机RAW = True
                        初始化方法.Invoke(窗体, Nothing)
                        Dim 选中 = CBool(相机复选框.GetType().GetProperty("Checked").GetValue(相机复选框))
                        If Not 选中 Then
                            失败 += 1
                            Console.WriteLine("FAIL  设置.关联相机RAW=True 未加载到设置页复选框")
                        End If
                        设置.实例对象.关联相机RAW = False
                        初始化方法.Invoke(窗体, Nothing)
                        Dim 未选中 = CBool(相机复选框.GetType().GetProperty("Checked").GetValue(相机复选框))
                        If 未选中 Then
                            失败 += 1
                            Console.WriteLine("FAIL  设置.关联相机RAW=False 仍显示为勾选")
                        End If
                    Finally
                        设置.实例对象.关联相机RAW = 原值
                    End Try
                End If
            End If
        Finally
            窗体.Dispose()
        End Try

        If 关联扩展名.Count <> Raw格式.扩展名列表.Length Then
            失败 += 1
            Console.WriteLine($"FAIL  RAW 关联档位数量：期望 {Raw格式.扩展名列表.Length}，实际 {关联扩展名.Count}")
        End If

        For Each 扩展名 In Raw格式.扩展名列表
            If Not 关联扩展名.Contains(扩展名) Then
                失败 += 1
                Console.WriteLine($"FAIL  关联档位缺少 {扩展名}")
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

    ''' <summary>在独立的 HKCU 测试根上跑一次注册→回读→注销→恢复，不碰真实 .dng 关联。</summary>
    Private Sub 运行注册表往返(ByRef 失败 As Integer)
        Dim 测试根 = "Software\Classes\3FCompareTests"
        Dim 根 = 测试根 & "\RawAssociation\" & Guid.NewGuid().ToString("N")
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

            Dim 选项 As New 文件关联选项 With {.关联相机RAW = True}
            调用同步核心(选项, 程序路径, 位置)

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

            ' 全部取消后：恢复原有 .dng 值，删掉本程序自己的 ProgID/应用注册，不碰其他值。
            Dim 空选项 As New 文件关联选项()
            调用同步核心(空选项, 程序路径, 位置)

            断言字符串(位置.ClassesRoot & "\.dng", String.Empty, "Existing.DNG", 失败)
            断言字符串(位置.ClassesRoot & "\.dng", "Content Type", "image/x-existing", 失败)
            断言字符串(位置.ClassesRoot & "\.dng", "PerceivedType", "document", 失败)
            断言值存在(位置.ClassesRoot & "\.dng\OpenWithProgids", "FFF.Player.dng", False, 失败)
            断言值存在(位置.ClassesRoot & "\.dng\OpenWithProgids", "Existing.DNG", True, 失败)
            断言键存在(位置.ApplicationRoot & "\Applications\FFF.Player.exe", False, 失败)
            断言键存在(位置.CapabilitiesRoot, False, 失败)
            断言值存在(位置.RegisteredApplications, "FFF.Player", False, 失败)
            断言键存在(位置.ApplicationRoot & "\FileAssociationBackups\dng", False, 失败)
        Catch ex As Exception
            失败 += 1
            Console.WriteLine($"FAIL  注册表往返异常：{ex}")
        Finally
            Try
                Registry.CurrentUser.DeleteSubKeyTree(根, False)
                Registry.CurrentUser.DeleteSubKeyTree(测试根, False)
            Catch ex As Exception
                Console.WriteLine($"WARN  清理临时注册表根失败：{ex.Message}")
            End Try
        End Try
    End Sub

    Private Sub 运行设置映射往返(ByRef 失败 As Integer)
        Dim 设置对象 As New 设置()
        设置对象.关联相机RAW = True
        Dim 选项 = 文件关联选项.从设置(设置对象)
        If Not 选项.已启用(文件关联类别.相机RAW) Then
            失败 += 1
            Console.WriteLine("FAIL  设置.关联相机RAW=True 未映射到关联选项")
        End If
        设置对象.关联相机RAW = False
        Dim 关闭选项 = 文件关联选项.从设置(设置对象)
        If 关闭选项.已启用(文件关联类别.相机RAW) Then
            失败 += 1
            Console.WriteLine("FAIL  设置.关联相机RAW=False 仍映射为启用")
        End If
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
