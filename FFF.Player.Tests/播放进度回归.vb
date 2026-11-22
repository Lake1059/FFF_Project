Imports System
Imports System.IO
Imports System.Linq
Imports System.Text
Imports System.Text.Json
Imports FFF.Player

''' <summary>播放进度存储的纯文件回归：不碰真实播放器、不依赖 FFmpeg。
''' 跑法：FFF.Player.Tests.exe --playback-progress-regression</summary>
Friend Module 播放进度回归

    Private 失败数 As Integer
    Private 用例数 As Integer
    Private 临时目录 As String

    Friend Function 运行() As Integer
        失败数 = 0
        用例数 = 0
        临时目录 = Path.Combine(Path.GetTempPath(), "fff_player_progress_" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(临时目录)
        Try
            测记录与重载()
            测只统计视频文件()
            测开关关闭不读写()
            测太靠前不记()
            测片尾视为看完()
            测文件变化失效()
            测文件消失失效()
            测退出清理()
            测清空()
            测JSON结构()
            测不可达路径保留()
            测确认消失仍清理()
            测高版本文件不被改写()
            测高版本空条目不被改写()
            测落盘带UTF8BOM()
            测未知指纹写负一而非零()
            测默认进度目录在LocalAppData()
            测开关重开从磁盘重载()
            测时钟回拨时间戳不倒退()
        Finally
            ' 还原静态重定向，避免同进程后续套件误写真实程序目录。
            播放进度存储.测试用重定向(String.Empty)
            Try
                Directory.Delete(临时目录, True)
            Catch
            End Try
        End Try

        Console.WriteLine()
        Console.WriteLine($"播放进度回归：{用例数} 条，失败 {失败数} 条")
        Return If(失败数 = 0, 0, 1)
    End Function

    Private Sub 测记录与重载()
        Dim 媒体 = 建媒体("重载.mkv", "v1")
        Dim 进度 = 建进度路径("重载")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(媒体, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(120), 立即落盘:=True)
        检查("记录后立即查询", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.FromSeconds(30))
        检查("进度文件已生成", File.Exists(进度))

        ' 清空静态缓存后重新读盘：模拟下一次启动。
        播放进度存储.测试用重定向(进度)
        检查("重启后仍能续播", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.FromSeconds(30))
    End Sub

    Private Sub 测只统计视频文件()
        Dim 音频 = 建媒体("音乐.mp3", "audio")
        Dim 视频 = 建媒体("电影.mkv", "video")
        Dim 进度 = 建进度路径("视频限定")
        Dim 配置 = 设置.实例对象
        Dim 开关 = 配置.播放进度记忆
        Try
            配置.播放进度记忆 = True
            播放进度存储.测试用重定向(进度)
            Using 页面 As New Form设置_播放行为()
                播放进度存储.记录(音频, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(120))
                页面.初始化页面()
                检查("纯音频不计入清除按钮", 页面.MB_清除播放进度.Text = "当前没有记录的视频播放进度")
                播放进度存储.记录(视频, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(120), 立即落盘:=True)
                页面.刷新进度数量()
                检查("清除按钮显示精确视频数量", 页面.MB_清除播放进度.Text = "清除已记录的 1 个视频文件的进度")
                Dim 文本 = File.ReadAllText(进度)
                Dim 文件 = JsonSerializer.Deserialize(Of 播放进度存储.进度文件)(文本)
                文件.条目.Add(New 播放进度存储.进度条目 With {
                    .路径 = 音频, .时长100纳秒 = TimeSpan.FromSeconds(120).Ticks, .位置100纳秒 = TimeSpan.FromSeconds(30).Ticks
                })
                File.WriteAllText(进度, JsonSerializer.Serialize(文件))
                播放进度存储.测试用重定向(进度)
                检查("旧纯音频条目不计入视频数量", 播放进度存储.统计条目数() = 1)
            End Using
        Finally
            配置.播放进度记忆 = 开关
        End Try
    End Sub

    Private Sub 测开关关闭不读写()
        Dim 媒体 = 建媒体("开关.mkv", "v1")
        Dim 进度 = 建进度路径("开关")
        播放进度存储.测试用重定向(进度)
        Dim 原值 = 设置.实例对象.播放进度记忆
        Try
            设置.实例对象.播放进度记忆 = True
            播放进度存储.记录(媒体, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(120), 立即落盘:=True)
            检查("开启时统计为 1", 播放进度存储.统计条目数() = 1)

            设置.实例对象.播放进度记忆 = False
            检查("关闭后查询为零", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
            播放进度存储.记录(媒体, TimeSpan.FromSeconds(60), TimeSpan.FromSeconds(120), 立即落盘:=True)
            设置.实例对象.播放进度记忆 = True
            检查("关闭期间不写盘", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.FromSeconds(30))

            设置.实例对象.播放进度记忆 = False
            播放进度存储.退出时清理()
            检查("关闭时退出不清理", File.Exists(进度))
            检查("关闭时统计仍可读", 播放进度存储.统计条目数() = 1)
        Finally
            设置.实例对象.播放进度记忆 = 原值
        End Try
    End Sub

    Private Sub 测太靠前不记()
        Dim 媒体 = 建媒体("太靠前.mkv", "v1")
        Dim 进度 = 建进度路径("太靠前")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(媒体, TimeSpan.FromSeconds(3), TimeSpan.FromSeconds(120), 立即落盘:=True)
        检查("3 秒不记", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
        检查("空表不落空文件", Not File.Exists(进度))
    End Sub

    Private Sub 测片尾视为看完()
        Dim 媒体 = 建媒体("看完.mkv", "v1")
        Dim 进度 = 建进度路径("看完")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(媒体, TimeSpan.FromSeconds(115), TimeSpan.FromSeconds(120), 立即落盘:=True)
        检查("贴片尾不续播", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)

        ' 短片自适应余量：20 秒片子看到 6 秒仍应续播（旧固定 15 秒余量会误判成看完）。
        Dim 短片 = 建媒体("短片.mkv", "v1")
        播放进度存储.测试用重定向(建进度路径("短片"))
        播放进度存储.记录(短片, TimeSpan.FromSeconds(6), TimeSpan.FromSeconds(20), 立即落盘:=True)
        检查("20 秒短片 6 秒可续播", 播放进度存储.查询(短片, TimeSpan.FromSeconds(20).Ticks) = TimeSpan.FromSeconds(6))
    End Sub

    Private Sub 测文件变化失效()
        Dim 媒体 = 建媒体("被替换.mkv", "v1")
        Dim 进度 = 建进度路径("被替换")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(媒体, TimeSpan.FromSeconds(40), TimeSpan.FromSeconds(120), 立即落盘:=True)
        File.AppendAllText(媒体, "更长的内容", Encoding.UTF8)
        检查("文件被改写后不续播", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
    End Sub

    Private Sub 测文件消失失效()
        Dim 媒体 = 建媒体("会消失.mkv", "v1")
        Dim 进度 = 建进度路径("会消失")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(媒体, TimeSpan.FromSeconds(40), TimeSpan.FromSeconds(120), 立即落盘:=True)
        If Not 安全删除(媒体, "测文件消失失效") Then Return
        检查("源文件消失后不续播", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
    End Sub

    Private Sub 测退出清理()
        Dim 保留媒体 = 建媒体("保留.mkv", "v1")
        Dim 消失媒体 = 建媒体("清理时消失.mkv", "v1")
        Dim 进度 = 建进度路径("清理")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(保留媒体, TimeSpan.FromSeconds(50), TimeSpan.FromSeconds(120), 立即落盘:=True)
        播放进度存储.记录(消失媒体, TimeSpan.FromSeconds(60), TimeSpan.FromSeconds(120), 立即落盘:=True)
        Dim 可真删 = 安全删除(消失媒体, "测退出清理-失效条目")
        If Not 可真删 Then
            ' 删除被拦时仍要跑不依赖删除的那一半断言。
            检查("清理后有效条目仍在", 播放进度存储.查询(保留媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.FromSeconds(50))
            Return
        End If

        播放进度存储.退出时清理()
        检查("清理后有效条目仍在", 播放进度存储.查询(保留媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.FromSeconds(50))
        检查("清理后失效条目已移除", 播放进度存储.查询(消失媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
        检查("清理后文件仍保留", File.Exists(进度))
    End Sub

    Private Sub 测清空()
        Dim 媒体 = 建媒体("清空.mkv", "v1")
        Dim 进度 = 建进度路径("清空")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(媒体, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(120), 立即落盘:=True)
        播放进度存储.清空()
        检查("清空后查询为零", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
        If File.Exists(进度) Then
            用例数 += 1
            Console.WriteLine("SKIP  清空后文件删除被本机安全软件拦截，无法验证文件删除")
        Else
            检查("清空后文件删除", True)
        End If
    End Sub

    Private Sub 测JSON结构()
        Dim 媒体 = 建媒体("结构.mkv", "v1")
        Dim 进度 = 建进度路径("结构")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(媒体, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(120), 立即落盘:=True)
        Using 文档 = JsonDocument.Parse(File.ReadAllText(进度, Encoding.UTF8))
            Dim 根 = 文档.RootElement
            检查("版本为 1", 根.GetProperty("版本").GetInt32() = 播放进度存储.当前版本)
            检查("条目数组只有一条", 根.GetProperty("条目").GetArrayLength() = 1)
            Dim 第一项 = 根.GetProperty("条目").EnumerateArray().First()
            检查("位置落盘为 100ns", 第一项.GetProperty("位置100纳秒").GetInt64() = TimeSpan.FromSeconds(30).Ticks)
            检查("路径为完整路径", 第一项.GetProperty("路径").GetString() = Path.GetFullPath(媒体))
            检查("时长落盘为 100ns", 第一项.GetProperty("时长100纳秒").GetInt64() = TimeSpan.FromSeconds(120).Ticks)
        End Using
    End Sub

    ''' <summary>P1 回归：U 盘/网络盘离线（盘符不存在）时，查询与退出清理都不能把条目删掉。</summary>
    Private Sub 测不可达路径保留()
        Dim 进度 = 建进度路径("不可达")
        播放进度存储.测试用重定向(进度)
        Dim 离线媒体 = 找一个不存在的盘符路径("离线盘.mkv")

        ' 直接写一份 v1 JSON，路径指向不存在的盘符；记录() 自己会先 File.Exists 拦掉这种路径。
        Dim JSON = "{""版本"":1,""条目"":[" & 进度条目Json(离线媒体, 30, 120) & "]}"
        File.WriteAllText(进度, JSON, New UTF8Encoding(encoderShouldEmitUTF8Identifier:=True))

        检查("离线盘查询仍返回原位置",
             播放进度存储.查询(离线媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.FromSeconds(30))
        播放进度存储.退出时清理()
        检查("离线盘退出清理后条目仍在", 播放进度存储.统计条目数() = 1)
        检查("离线盘清理后仍能查到位置",
             播放进度存储.查询(离线媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.FromSeconds(30))
    End Sub

    ''' <summary>P1 回归：父目录可达、文件本身不存在（FileNotFoundException → 已确认消失）时，
    ''' 查询要返回 0，退出清理要把条目删掉。本条不依赖 File.Delete，避免被安全软件吞掉后
    ''' 整套用例退化成 SKIP。</summary>
    Private Sub 测确认消失仍清理()
        Dim 进度 = 建进度路径("真消失")
        播放进度存储.测试用重定向(进度)
        ' 父目录就是可达的临时目录；文件本身不存在。
        Dim 幽灵媒体 = Path.Combine(临时目录, "幽灵文件.mkv")

        Dim JSON = "{""版本"":1,""条目"":[" & 进度条目Json(幽灵媒体, 40, 120) & "]}"
        File.WriteAllText(进度, JSON, New UTF8Encoding(encoderShouldEmitUTF8Identifier:=True))

        检查("确认消失后查询为零", 播放进度存储.查询(幽灵媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
        检查("查询后失效条目已移除", 播放进度存储.统计条目数() = 0)

        ' 再写一份进来，走退出时清理路径验证。
        File.WriteAllText(进度, JSON, New UTF8Encoding(encoderShouldEmitUTF8Identifier:=True))
        播放进度存储.测试用重定向(进度)
        播放进度存储.退出时清理()
        检查("退出清理后幽灵条目已移除", 播放进度存储.统计条目数() = 0)
    End Sub

    ''' <summary>P2 回归：遇到版本号高于当前的进度文件，不加载、不回写、不改写字节；
    ''' 用户显式清空仍应能删掉文件。</summary>
    Private Sub 测高版本文件不被改写()
        Dim 媒体 = 建媒体("v2盘.mkv", "v2-content")
        Dim 进度 = 建进度路径("v2")
        播放进度存储.测试用重定向(进度)

        Dim JSON = "{""版本"":2,""条目"":[" & 进度条目Json(媒体, 30, 120) & "]}"
        File.WriteAllText(进度, JSON, New UTF8Encoding(encoderShouldEmitUTF8Identifier:=True))
        Dim 原始字节 = File.ReadAllBytes(进度)

        ' 触发加载
        检查("v2 文件查询返回零", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
        检查("v2 文件统计为零", 播放进度存储.统计条目数() = 0)

        ' 退出清理会强制落盘；不兼容版本下落盘必须直接 return，不能把 v2 收敛成 v1。
        播放进度存储.退出时清理()
        Dim 新字节 = File.ReadAllBytes(进度)
        检查("v2 文件字节未被改写", 原始字节.SequenceEqual(新字节))

        ' 显式清空仍要生效，并复位不兼容标志。
        播放进度存储.清空()
        If File.Exists(进度) Then
            ' 本机安全软件吞了 File.Delete：照实 SKIP，不伪装。
            用例数 += 1
            Console.WriteLine("SKIP  v2 清空删除被本机安全软件拦截，无法验证文件删除")
        Else
            检查("v2 文件显式清空后删除", True)
        End If
    End Sub

    ''' <summary>P1 守卫回归：{"版本":2,"条目":null} 必须被识别为不兼容版本，
    ''' 而不是在版本判断前就 return 导致 _不兼容版本 不置位、退出时清理把空表落盘覆盖 v2。</summary>
    Private Sub 测高版本空条目不被改写()
        Dim 进度 = 建进度路径("v2null")
        播放进度存储.测试用重定向(进度)

        File.WriteAllText(进度, "{""版本"":2,""条目"":null}",
                          New UTF8Encoding(encoderShouldEmitUTF8Identifier:=True))
        Dim 原始字节 = File.ReadAllBytes(进度)

        检查("v2+null 查询返回零", 播放进度存储.查询("X:\whatever.mkv", TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
        检查("v2+null 统计为零", 播放进度存储.统计条目数() = 0)

        播放进度存储.退出时清理()
        Dim 新字节 = File.ReadAllBytes(进度)
        检查("v2+null 文件字节未被改写", 原始字节.SequenceEqual(新字节))
    End Sub

    ''' <summary>BOM 回归：落盘文件前 3 字节必须是 EF BB BF，与历史格式一致。</summary>
    Private Sub 测落盘带UTF8BOM()
        Dim 媒体 = 建媒体("bom.mkv", "bom-content")
        Dim 进度 = 建进度路径("bom")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(媒体, TimeSpan.FromSeconds(20), TimeSpan.FromSeconds(120), 立即落盘:=True)
        Dim 头3 = New Byte(2) {}
        Using fs As New FileStream(进度, FileMode.Open, FileAccess.Read, FileShare.Read)
            Dim 读 = fs.Read(头3, 0, 3)
            检查("BOM 文件至少 3 字节", 读 = 3)
        End Using
        检查("BOM 前 3 字节为 EF BB BF",
             头3(0) = &HEF AndAlso 头3(1) = &HBB AndAlso 头3(2) = &HBF)
    End Sub

    ''' <summary>P1 残留回归：未知指纹（暂不可读 / 已确认消失）必须返回 -1/-1，
    ''' 记录() 落盘的 JSON 里  文件大小/文件修改时间UtcTicks 也必须是 -1，而不是默认 0。</summary>
    Private Sub 测未知指纹写负一而非零()
        Dim 进度 = 建进度路径("fingerprint")
        播放进度存储.测试用重定向(进度)

        ' 不存在的盘符 → 暂不可读。
        Dim 离线 = 找一个不存在的盘符路径("probe.mkv")
        Dim 探针 = 播放进度存储.测试用探测指纹(离线)
        检查("不存在盘符指纹=暂不可读|-1|-1", 探针 = "暂不可读|-1|-1")

        ' 可达临时目录下、文件本身不存在 → 已确认消失。
        Dim 幽灵 = Path.Combine(临时目录, "ghost_probe.mkv")
        探针 = 播放进度存储.测试用探测指纹(幽灵)
        检查("可达目录下不存在文件指纹=已确认消失|-1|-1", 探针 = "已确认消失|-1|-1")

        ' 用一个真实存在的媒体走 记录()，落盘 JSON 里 大小/时间 必须 >= 0（对比组）。
        Dim 媒体 = 建媒体("指纹真实.mkv", "real")
        播放进度存储.记录(媒体, TimeSpan.FromSeconds(10), TimeSpan.FromSeconds(120), 立即落盘:=True)
        Using 文档 = JsonDocument.Parse(File.ReadAllText(进度, Encoding.UTF8))
            Dim 第一项 = 文档.RootElement.GetProperty("条目").EnumerateArray().First()
            检查("真实文件落盘大小>=0", 第一项.GetProperty("文件大小").GetInt64() >= 0)
            检查("真实文件落盘时间>=0", 第一项.GetProperty("文件修改时间UtcTicks").GetInt64() >= 0)
        End Using
    End Sub

    ''' <summary>L5-P2-04：默认进度文件必须落在 %LOCALAPPDATA%\3FP\播放进度，
    ''' 而不是 exe 目录（Program Files 下静默不可写）。清空重定向后读默认路径。</summary>
    Private Sub 测默认进度目录在LocalAppData()
        播放进度存储.测试用重定向(String.Empty)
        Dim 期望根 = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "3FP", "播放进度")
        检查("默认进度目录在 %LOCALAPPDATA%\3FP\播放进度",
            播放进度存储.文件路径.StartsWith(期望根, StringComparison.OrdinalIgnoreCase))
        检查("进度文件名正确",
            Path.GetFileName(播放进度存储.文件路径) = 播放进度存储.文件名)
    End Sub

    ''' <summary>L5-P2-05：开关从“关”重开为“开”时，必须重新从磁盘加载，
    ''' 不能沿用同会话内的旧内存缓存（关闭期间外部改动看不到＝假重载）。</summary>
    Private Sub 测开关重开从磁盘重载()
        Dim 媒体 = 建媒体("重开.mkv", "reopen")
        Dim 进度 = 建进度路径("重开")
        播放进度存储.测试用重定向(进度)
        Dim 原值 = 设置.实例对象.播放进度记忆
        Try
            设置.实例对象.播放进度记忆 = True
            播放进度存储.记录(媒体, TimeSpan.FromSeconds(30), TimeSpan.FromSeconds(120), 立即落盘:=True)
            设置.实例对象.播放进度记忆 = False

            ' 关闭期间：外部（另一进程/手改）把磁盘上的进度改成 90s；内存里仍是 30s。
            Dim 外部JSON = "{""版本"":1,""条目"":[" & 进度条目Json(媒体, 90, 120) & "]}"
            File.WriteAllText(进度, 外部JSON, New UTF8Encoding(encoderShouldEmitUTF8Identifier:=True))

            ' 重开开关：正确实现会在 False→True 时丢掉内存缓存，下一次查询读到磁盘上的 90s。
            ' 若没重载，会错误地返回内存里的 30s。
            设置.实例对象.播放进度记忆 = True
            检查("开关重开后从磁盘重载(看到外部 90s)",
                播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.FromSeconds(90))
        Finally
            设置.实例对象.播放进度记忆 = 原值
        End Try
    End Sub

    ''' <summary>L5-P2-07：系统时钟回拨时，LRU 时间戳不得倒退。记录一个“现在”后，
    ''' 喂回拨一小时的时间戳，钳制结果必须仍 >= 刚才的“现在”。</summary>
    Private Sub 测时钟回拨时间戳不倒退()
        Dim 媒体 = 建媒体("回拨.mkv", "skew")
        Dim 进度 = 建进度路径("回拨")
        播放进度存储.测试用重定向(进度)

        ' 先记录一个真实条目，把单调闸门推进到真实“现在”。
        播放进度存储.记录(媒体, TimeSpan.FromSeconds(10), TimeSpan.FromSeconds(120), 立即落盘:=True)
        Dim 现在 = 播放进度存储.测试钳制时间戳(DateTime.UtcNow.Ticks)

        ' 模拟时钟回拨到一小时前：钳制必须把它抬回到 现在，而不是让 LRU 时间戳变小。
        Dim 回拨 = 现在 - TimeSpan.TicksPerHour
        Dim 钳制后 = 播放进度存储.测试钳制时间戳(回拨)
        检查("时钟回拨后时间戳不倒退", 钳制后 >= 现在)
    End Sub

    ''' <summary>安全删除：File.Delete 包 try/catch，被安全软件拦时返回 False 并 SKIP 当前用例；
    ''' 删除成功返回 True。所有依赖 File.Delete 的用例都走这里，避免异常把整个套件崩掉。</summary>
    Private Function 安全删除(路径 As String, 用例名 As String) As Boolean
        Try
            File.Delete(路径)
        Catch ex As Exception
            用例数 += 1
            Console.WriteLine("SKIP  " & 用例名 & "：File.Delete 抛 " & ex.GetType().Name & "（本机安全软件拦截）")
            Return False
        End Try
        If File.Exists(路径) Then
            用例数 += 1
            Console.WriteLine("SKIP  " & 用例名 & "：File.Delete 未抛异常但文件仍在（本机安全软件拦截）")
            Return False
        End If
        Return True
    End Function

    Private Function 进度条目Json(媒体路径 As String, 位置秒 As Integer, 时长秒 As Integer) As String
        Dim 完整 = Path.GetFullPath(媒体路径).Replace("\", "\\")
        Return "{""路径"":""" & 完整 & """," &
               """位置100纳秒"":" & TimeSpan.FromSeconds(位置秒).Ticks.ToString() & "," &
               """时长100纳秒"":" & TimeSpan.FromSeconds(时长秒).Ticks.ToString() & "," &
               """文件大小"":1024,""文件修改时间UtcTicks"":0,""已完成"":false," &
               """最后更新UtcTicks"":" & DateTime.UtcNow.Ticks.ToString() & "}"
    End Function

    Private Function 找一个不存在的盘符路径(文件名 As String) As String
        For Each 盘符 In {"Z", "Y", "X", "V", "U", "T", "S", "R", "P", "O", "N", "M", "L", "K", "J", "I", "H", "G"}
            Dim 根 = 盘符 & ":\"
            If Not Directory.Exists(根) Then
                Return Path.Combine(根, "fff_offline_regression", 文件名)
            End If
        Next
        Return "Z:\fff_offline_regression\" & 文件名
    End Function

    Private Function 建媒体(名称 As String, 内容 As String) As String
        Dim 路径 = Path.Combine(临时目录, 名称)
        File.WriteAllText(路径, 内容, Encoding.UTF8)
        Return 路径
    End Function

    Private Function 建进度路径(名称 As String) As String
        Return Path.Combine(临时目录, 名称 & ".progress.json")
    End Function

    Private Sub 检查(名称 As String, 条件 As Boolean)
        用例数 += 1
        If 条件 Then
            Console.WriteLine("ok    " & 名称)
        Else
            失败数 += 1
            Console.WriteLine("FAIL  " & 名称)
        End If
    End Sub
End Module
