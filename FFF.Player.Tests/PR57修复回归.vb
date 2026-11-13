Imports System
Imports System.IO
Imports System.Reflection
Imports FFF.Player

''' <summary>PR #57 修复的纯逻辑回归：包归属哨兵、模块注入显示名、模块文本原样解码。
''' 跑法：FFF.Player.Tests.exe --pr57-fixes-regression</summary>
Friend Module PR57修复回归

    Private 失败数 As Integer
    Private 用例数 As Integer

    Friend Function 运行() As Integer
        失败数 = 0
        用例数 = 0
        测包归属哨兵()
        测下载目录护栏()
        测直通图层文本()
        测模块文本注入()
        测HDR额外注入()
        测显示面契约()
        Console.WriteLine()
        Console.WriteLine($"PR57 修复回归：{用例数} 条，失败 {失败数} 条")
        Return If(失败数 = 0, 0, 1)
    End Function

    Private Sub 检查(名称 As String, 实际 As String, 期望 As String)
        用例数 += 1
        If String.Equals(实际, 期望, StringComparison.Ordinal) Then
            Console.WriteLine($"ok    {名称}")
        Else
            失败数 += 1
            Console.WriteLine($"FAIL  {名称}    期望=""{期望}""  实际=""{实际}""")
        End If
    End Sub

    Private Sub 检查(名称 As String, 实际 As Boolean, 期望 As Boolean)
        用例数 += 1
        If 实际 = 期望 Then
            Console.WriteLine($"ok    {名称}")
        Else
            失败数 += 1
            Console.WriteLine($"FAIL  {名称}    期望={期望}  实际={实际}")
        End If
    End Sub

    Private Sub 检查(名称 As String, 实际 As Integer, 期望 As Integer)
        用例数 += 1
        If 实际 = 期望 Then
            Console.WriteLine($"ok    {名称}")
        Else
            失败数 += 1
            Console.WriteLine($"FAIL  {名称}    期望={期望}  实际={实际}")
        End If
    End Sub

    Private Sub 检查(名称 As String, 实际 As Type, 期望 As Type)
        用例数 += 1
        If 实际 Is 期望 Then
            Console.WriteLine($"ok    {名称}")
        Else
            失败数 += 1
            Console.WriteLine($"FAIL  {名称}    期望={期望}  实际={实际}")
        End If
    End Sub

    ''' <summary>P1-2：只有本程序写的哨兵才算归属；外来/损坏/缺失一律不认。</summary>
    Private Sub 测包归属哨兵()
        Dim 目录 = Path.Combine(AppContext.BaseDirectory, "pr57-test-" & Guid.NewGuid().ToString("N"))
        Try
            检查("写哨兵成功", Ai字幕包.写归属哨兵(目录, "测试包"), True)
            检查("哨兵文件存在", File.Exists(Path.Combine(目录, Ai字幕包.哨兵文件名)), True)
            检查("读回归属", Ai字幕包.读取哨兵归属(目录), Ai字幕包.哨兵归属)
            检查("有归属哨兵", Ai字幕包.有归属哨兵(目录), True)

            File.WriteAllText(Path.Combine(目录, Ai字幕包.哨兵文件名),
                "{""owner"": ""OtherApp""}", Text.Encoding.UTF8)
            检查("外来 owner 不算归属", Ai字幕包.有归属哨兵(目录), False)
            检查("外来 owner 原样读出", Ai字幕包.读取哨兵归属(目录), "OtherApp")

            File.WriteAllText(Path.Combine(目录, Ai字幕包.哨兵文件名), "{ broken", Text.Encoding.UTF8)
            检查("损坏哨兵不认", Ai字幕包.有归属哨兵(目录), False)
            检查("损坏哨兵答 Nothing", Ai字幕包.读取哨兵归属(目录), CStr(Nothing))

            File.Delete(Path.Combine(目录, Ai字幕包.哨兵文件名))
            检查("缺失哨兵不认", Ai字幕包.有归属哨兵(目录), False)
        Finally
            Try
                If Directory.Exists(目录) Then Directory.Delete(目录, True)
            Catch
            End Try
        End Try
    End Sub

    ''' <summary>P1-2：盘符根/用户主目录/有杂物的目录必须拒绝；空目录与归属包目录放行。</summary>
    Private Sub 测下载目录护栏()
        Dim 方法 = GetType(Ai字幕包下载器).GetMethod("校验下载目标",
            BindingFlags.NonPublic Or BindingFlags.Static)
        检查("找到校验方法", 方法 IsNot Nothing, True)
        If 方法 Is Nothing Then Return

        Dim 根 = Path.Combine(AppContext.BaseDirectory, "guard-test-" & Guid.NewGuid().ToString("N"))
        Dim 空目录 = Path.Combine(根, "empty")
        Dim 杂物目录 = Path.Combine(根, "mixed")
        Dim 哨兵目录 = Path.Combine(根, "owned")
        Try
            Directory.CreateDirectory(空目录)
            Directory.CreateDirectory(杂物目录)
            File.WriteAllText(Path.Combine(杂物目录, "note.txt"), "user data", Text.Encoding.UTF8)
            Directory.CreateDirectory(哨兵目录)
            Ai字幕包.写归属哨兵(哨兵目录, "测试包")

            检查("空路径被拒", 调用校验(方法, ""), "下载目录为空。")
            Dim 盘根 = Path.GetPathRoot(Path.GetFullPath(AppContext.BaseDirectory))
            检查("盘符根被拒", 调用校验(方法, 盘根).Contains("盘符根", StringComparison.Ordinal), True)
            Dim 主目录 = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
            If Not String.IsNullOrWhiteSpace(主目录) Then
                检查("用户主目录被拒", 调用校验(方法, 主目录).Contains("主目录", StringComparison.Ordinal), True)
            End If
            检查("不存在目录放行", 调用校验(方法, Path.Combine(根, "not-yet")), "")
            检查("空目录放行", 调用校验(方法, 空目录), "")
            检查("杂物目录被拒",
                调用校验(方法, 杂物目录).Contains("其它文件", StringComparison.Ordinal), True)
            检查("归属包目录放行", 调用校验(方法, 哨兵目录), "")
        Finally
            Try
                If Directory.Exists(根) Then Directory.Delete(根, True)
            Catch
            End Try
        End Try
    End Sub

    Private Function 调用校验(方法 As MethodInfo, 路径 As String) As String
        Return CStr(方法.Invoke(Nothing, New Object() {路径}))
    End Function
    ''' <summary>直通行只显示模块注入的显示名；模块没给名称也报中性“已接管”，不造格式名。</summary>
    Private Sub 测直通图层文本()
        检查("未接管答空串", 播放器控制器.直通图层文本(0, "模块显示名"), "")
        检查("模块显示名原样上屏", 播放器控制器.直通图层文本(1, "模块显示名"), "模块显示名")
        检查("模块名两端空白裁掉", 播放器控制器.直通图层文本(1, "  模块显示名  "), "模块显示名")
        检查("模块没说名称报已接管", 播放器控制器.直通图层文本(1, ""), "已接管")
        检查("模块名全空白报已接管", 播放器控制器.直通图层文本(1, "   "), "已接管")
    End Sub

    ''' <summary>模块文本按 UTF-8 原样解码、NUL 截断；宿主只搬运不改写。</summary>
    Private Sub 测模块文本注入()
        检查("Nothing 答空", 色彩变换模块接口.注入文本(Nothing), "")
        检查("全零答空", 色彩变换模块接口.注入文本(New Byte() {0, 0, 0}), "")
        检查("NUL 截断", 色彩变换模块接口.注入文本(New Byte() {65, 0, 66}), "A")
        检查("UTF-8 原样", 色彩变换模块接口.注入文本(System.Text.Encoding.UTF8.GetBytes("模块自述文本")),
            "模块自述文本")
    End Sub

    ''' <summary>模块激活是加法：基础 HDR 规格与宿主动态段保留，模块内容作为额外段。</summary>
    Private Sub 测HDR额外注入()
        Dim 注入行 = 播放器信息图层呈现器.HDR行文本(
            "HDR10 PQ", "模块等级文本", "动态元数据已用于处理", "模块合成文本", "", "")
        检查("基础规格保留", 注入行.Contains("HDR10 PQ", StringComparison.Ordinal), True)
        检查("模块内容作为额外段", 注入行.Contains("内容：模块等级文本", StringComparison.Ordinal), True)
        检查("宿主动态段保留", 注入行.Contains("动态元数据已用于处理", StringComparison.Ordinal), True)
        检查("模块合成文本保留", 注入行.Contains("模块合成文本", StringComparison.Ordinal), True)
        检查("模块缺席只留基础", 播放器信息图层呈现器.HDR行文本("HDR10 PQ", "", "", "", "", ""),
            "HDR10 PQ")
    End Sub

    ''' <summary>模块注入面必须存在：内容描述、合成文本、直通名称都从模块来，宿主只显示。</summary>
    Private Sub 测显示面契约()
        Dim 控制器 = GetType(播放器控制器)
        Dim 会话 = GetType(播放器会话)
        Dim 图层 = GetType(播放器信息图层呈现器)
        Dim 全查 = BindingFlags.Public Or BindingFlags.NonPublic Or BindingFlags.Instance Or BindingFlags.Static

        检查("控制器有模块内容描述入口",
            控制器.GetMethod("读取色彩变换内容描述", 全查) IsNot Nothing, True)
        检查("控制器有模块合成文本入口",
            控制器.GetMethod("读取色彩变换合成文本", 全查) IsNot Nothing, True)
        检查("会话有模块内容描述属性",
            会话.GetProperty("色彩变换内容描述", 全查) IsNot Nothing, True)
        检查("会话有模块合成文本属性",
            会话.GetProperty("色彩变换合成文本", 全查) IsNot Nothing, True)

        Dim 有合成参数 = False
        For Each 构造 In 图层.GetConstructors(BindingFlags.Public Or BindingFlags.NonPublic Or BindingFlags.Instance)
            For Each 参数 In 构造.GetParameters()
                If 参数.Name = "合成文本提供器" Then 有合成参数 = True
            Next
        Next
        检查("信息图层有模块合成文本提供器", 有合成参数, True)

        Dim 直通方法 = 会话.GetMethod("读取音频直通状态", 全查)
        检查("直通读数存在", 直通方法 IsNot Nothing, True)
        If 直通方法 IsNot Nothing Then
            Dim 返回参数 = 直通方法.ReturnType.GetGenericArguments()
            检查("直通读数返回五元组", 返回参数.Length, 5)
            检查("直通读数第四位是模块名", 返回参数(3), GetType(String))
        End If

        Dim 纯映射 = 控制器.GetMethod("直通图层文本", 全查)
        检查("直通映射存在", 纯映射 IsNot Nothing, True)
        If 纯映射 IsNot Nothing Then
            Dim 参数 = 纯映射.GetParameters()
            检查("直通映射只收生效+模块名", 参数.Length, 2)
            检查("直通映射第二参数是模块名", 参数(1).ParameterType, GetType(String))
        End If
    End Sub

End Module
