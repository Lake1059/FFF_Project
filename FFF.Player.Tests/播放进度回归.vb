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
            测开关关闭不读写()
            测太靠前不记()
            测片尾视为看完()
            测文件变化失效()
            测文件消失失效()
            测退出清理()
            测清空()
            测JSON结构()
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
        File.Delete(媒体)
        检查("源文件消失后不续播", 播放进度存储.查询(媒体, TimeSpan.FromSeconds(120).Ticks) = TimeSpan.Zero)
    End Sub

    Private Sub 测退出清理()
        Dim 保留媒体 = 建媒体("保留.mkv", "v1")
        Dim 消失媒体 = 建媒体("清理时消失.mkv", "v1")
        Dim 进度 = 建进度路径("清理")
        播放进度存储.测试用重定向(进度)

        播放进度存储.记录(保留媒体, TimeSpan.FromSeconds(50), TimeSpan.FromSeconds(120), 立即落盘:=True)
        播放进度存储.记录(消失媒体, TimeSpan.FromSeconds(60), TimeSpan.FromSeconds(120), 立即落盘:=True)
        File.Delete(消失媒体)

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
        检查("清空后文件删除", Not File.Exists(进度))
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
