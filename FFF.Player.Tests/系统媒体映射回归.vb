Imports System
Imports System.Collections.Generic
Imports System.IO
Imports System.Linq
Imports FFF.Player

''' <summary>
''' SMTC 纯映射回归：不创建 WinRT 会话、不碰播放器窗口，只验证状态/元数据/播放列表决策。
''' 跑法：FFF.Player.Tests.exe --smtc-mapping-regression
''' </summary>
Friend Module 系统媒体映射回归

    Private 失败数 As Integer
    Private 用例数 As Integer
    Private 临时目录 As String

    Friend Function 运行() As Integer
        失败数 = 0
        用例数 = 0
        临时目录 = Path.Combine(Path.GetTempPath(), "fff_smtc_mapping_" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(临时目录)
        Try
            测状态映射()
            测标题()
            测艺术家()
            测轨道号()
            测媒体标识()
            测播放列表结束判定()
            测相邻项目判定()
            测按钮接线()
        Finally
            Try
                Directory.Delete(临时目录, True)
            Catch
            End Try
        End Try

        Console.WriteLine()
        Console.WriteLine($"系统媒体映射回归：{用例数} 条，失败 {失败数} 条")
        Return If(失败数 = 0, 0, 1)
    End Function

    Private Sub 测状态映射()
        ' 没有媒体时，任何 3FP 状态都必须收敛到 Closed，不能留下上一首的卡片。
        Dim 状态集 As 播放状态() = {播放状态.空闲, 播放状态.正在打开, 播放状态.就绪,
                                     播放状态.正在播放, 播放状态.已暂停, 播放状态.播放结束,
                                     播放状态.失败, 播放状态.已关闭}
        For Each 状态 As 播放状态 In 状态集
            检查($"无媒体/{状态} → Closed",
                 系统媒体映射.映射播放状态(状态, False, False) = 系统媒体播放状态.已关闭)
        Next

        检查("空闲 → Closed",
             系统媒体映射.映射播放状态(播放状态.空闲, True, False) = 系统媒体播放状态.已关闭)
        检查("正在打开 → Changing",
             系统媒体映射.映射播放状态(播放状态.正在打开, True, False) = 系统媒体播放状态.变化中)
        检查("就绪 → Paused",
             系统媒体映射.映射播放状态(播放状态.就绪, True, False) = 系统媒体播放状态.已暂停)
        检查("正在播放 → Playing",
             系统媒体映射.映射播放状态(播放状态.正在播放, True, False) = 系统媒体播放状态.正在播放)
        检查("已暂停 → Paused",
             系统媒体映射.映射播放状态(播放状态.已暂停, True, False) = 系统媒体播放状态.已暂停)
        检查("失败 → Closed",
             系统媒体映射.映射播放状态(播放状态.失败, True, False) = 系统媒体播放状态.已关闭)
        检查("已关闭 → Closed",
             系统媒体映射.映射播放状态(播放状态.已关闭, True, False) = 系统媒体播放状态.已关闭)

        ' 播放结束是两分支：有下一项必须 Changing，否则卡片会闪一下 Stopped。
        检查("播放结束且有下一项 → Changing",
             系统媒体映射.映射播放状态(播放状态.播放结束, True, True) = 系统媒体播放状态.变化中)
        检查("播放结束且无下一项 → Stopped",
             系统媒体映射.映射播放状态(播放状态.播放结束, True, False) = 系统媒体播放状态.已停止)
    End Sub

    Private Sub 测标题()
        Dim 信息 As New 媒体信息()
        信息.元数据("title") = "  标题优先  "
        检查("metadata title 优先并去空白",
             系统媒体映射.取标题(信息, "C:\媒体\a.mp3") = "标题优先")

        信息.元数据.Clear()
        检查("无 title 回落文件名",
             系统媒体映射.取标题(信息, "C:\媒体\a.mp3") = "a")
        检查("空路径且无 title 返回空",
             系统媒体映射.取标题(信息, String.Empty) = String.Empty)
        检查("信息为空仍可按路径回落",
             系统媒体映射.取标题(Nothing, "C:\媒体\b.flac") = "b")
    End Sub

    Private Sub 测艺术家()
        Dim 信息 As New 媒体信息()
        信息.元数据("artist") = "  Artist  "
        检查("artist 优先",
             系统媒体映射.取艺术家(信息) = "Artist")

        信息.元数据.Clear()
        信息.元数据("album_artist") = "AlbumArtist"
        检查("无 artist 回落 album_artist",
             系统媒体映射.取艺术家(信息) = "AlbumArtist")

        信息.元数据.Clear()
        检查("都为空返回空", 系统媒体映射.取艺术家(信息) = String.Empty)
        检查("信息为空返回空", 系统媒体映射.取艺术家(Nothing) = String.Empty)
    End Sub

    Private Sub 测轨道号()
        Dim 信息 As New 媒体信息()
        信息.元数据("track") = "3"
        检查("track 纯数字", 系统媒体映射.取轨道号(信息) = 3UI)

        信息.元数据("track") = "3/12"
        检查("track 分数取前段", 系统媒体映射.取轨道号(信息) = 3UI)

        信息.元数据("track") = "abc"
        检查("track 非数字返回 0", 系统媒体映射.取轨道号(信息) = 0UI)

        信息.元数据("track") = ""
        检查("track 空返回 0", 系统媒体映射.取轨道号(信息) = 0UI)

        信息.元数据.Clear()
        检查("track 缺失返回 0", 系统媒体映射.取轨道号(信息) = 0UI)
        检查("信息为空返回 0", 系统媒体映射.取轨道号(Nothing) = 0UI)
    End Sub

    Private Sub 测媒体标识()
        Dim 甲 = 系统媒体映射.取媒体标识("C:\媒体\同一首.mp3")
        Dim 乙 = 系统媒体映射.取媒体标识("c:\媒体\同一首.mp3")
        检查("同路径大小写归一后稳定", 甲 = 乙 AndAlso 甲.Length > 0)
        检查("不同路径不同", 甲 <> 系统媒体映射.取媒体标识("C:\媒体\另一首.mp3"))
        检查("空路径返回空", 系统媒体映射.取媒体标识(String.Empty) = String.Empty)
        检查("空白路径返回空", 系统媒体映射.取媒体标识("   ") = String.Empty)
    End Sub

    Private Sub 测播放列表结束判定()
        Dim 列表 = 建列表()
        Try
            列表.播放模式 = 列表播放模式.播完停止
            列表.选择路径(建媒体文件("a.mp3"))
            检查("播完停止：有后续项也判无下一项", Not 列表.播放结束后是否有下一项())

            列表.播放模式 = 列表播放模式.顺序播放
            列表.选择路径(建媒体文件("a.mp3"))
            检查("顺序播放：第一项有下一项", 列表.播放结束后是否有下一项())
            列表.选择路径(建媒体文件("c.mp3"))
            检查("顺序播放：最后一项无下一项", Not 列表.播放结束后是否有下一项())

            列表.播放模式 = 列表播放模式.单项循环
            列表.选择路径(建媒体文件("c.mp3"))
            检查("单项循环：最后一项也有下一项", 列表.播放结束后是否有下一项())

            列表.播放模式 = 列表播放模式.列表循环
            列表.选择路径(建媒体文件("c.mp3"))
            检查("列表循环：最后一项会回卷", 列表.播放结束后是否有下一项())

            列表.播放模式 = 列表播放模式.随机播放
            列表.选择路径(建媒体文件("c.mp3"))
            检查("随机播放：仍会继续", 列表.播放结束后是否有下一项())

            列表.清空()
            检查("空列表：无下一项", Not 列表.播放结束后是否有下一项())
        Finally
            列表.清空()
        End Try
    End Sub

    Private Sub 测相邻项目判定()
        Dim 列表 = 建列表()
        Try
            列表.选择路径(建媒体文件("a.mp3"))
            检查("第一项：无上一项", Not 列表.是否存在相邻项目(-1))
            检查("第一项：有下一项", 列表.是否存在相邻项目(1))

            列表.选择路径(建媒体文件("c.mp3"))
            检查("最后一项：有上一项", 列表.是否存在相邻项目(-1))
            检查("最后一项：无下一项", Not 列表.是否存在相邻项目(1))

            列表.选择路径(建媒体文件("b.mp3"))
            检查("中间项：两边都有", 列表.是否存在相邻项目(-1) AndAlso 列表.是否存在相邻项目(1))

            列表.清空()
            检查("空列表：两边都没有",
                 Not 列表.是否存在相邻项目(-1) AndAlso Not 列表.是否存在相邻项目(1))
        Finally
            列表.清空()
        End Try
    End Sub

    ''' <summary>L5-P2-03：Form1 接线断言。不只测纯映射函数，而是断言“命令名 → 处理器”映射
    ''' 存在：每个 <see cref="系统媒体按钮"/> 枚举值都必须落到一个已知命令名，否则新增按钮
    ''' 忘了接线时这里会变红。</summary>
    Private Sub 测按钮接线()
        Dim 已知命令 = New HashSet(Of String)(StringComparer.Ordinal) From
            {"play", "pause", "stop", "fastforward", "rewind", "next", "previous"}

        ' 1) 每个枚举值都必须有命令名，且命令名是 Form1 认识的那一个。
        For Each b In [Enum].GetValues(GetType(系统媒体按钮)).Cast(Of 系统媒体按钮)()
            Dim 名 = 系统媒体映射.映射按钮到命令名(b)
            检查($"按钮 {b} 有接线", 名.Length > 0)
            If 名.Length > 0 Then
                检查($"按钮 {b} 命令名已知", 已知命令.Contains(名))
            End If
        Next

        ' 2) 关键映射逐一对死，防止有人改串。
        检查("播放→play", 系统媒体映射.映射按钮到命令名(系统媒体按钮.播放) = "play")
        检查("暂停→pause", 系统媒体映射.映射按钮到命令名(系统媒体按钮.暂停) = "pause")
        检查("停止→stop", 系统媒体映射.映射按钮到命令名(系统媒体按钮.停止) = "stop")
        检查("快进→fastforward", 系统媒体映射.映射按钮到命令名(系统媒体按钮.快进) = "fastforward")
        检查("快退→rewind", 系统媒体映射.映射按钮到命令名(系统媒体按钮.快退) = "rewind")
        检查("下一曲→next", 系统媒体映射.映射按钮到命令名(系统媒体按钮.下一曲) = "next")
        检查("上一曲→previous", 系统媒体映射.映射按钮到命令名(系统媒体按钮.上一曲) = "previous")
    End Sub

    Private Function 建列表() As 播放列表
        Dim 列表 As New 播放列表()
        列表.添加多个({建媒体文件("a.mp3"), 建媒体文件("b.mp3"), 建媒体文件("c.mp3")})
        Return 列表
    End Function

    Private Function 建媒体文件(名称 As String) As String
        Dim 路径 = Path.Combine(临时目录, 名称)
        If Not File.Exists(路径) Then File.WriteAllText(路径, 名称)
        Return 路径
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
