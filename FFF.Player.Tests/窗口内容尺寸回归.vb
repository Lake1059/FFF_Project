Imports System
Imports System.Drawing
Imports FFF.Player

''' <summary>内容原始尺寸模式的纯几何回归：旋转、窗口 chrome 相加、最大化判定。
''' 跑法：FFF.Player.Tests.exe --content-size-regression</summary>
Friend Module 窗口内容尺寸回归

    Private 失败数 As Integer
    Private 用例数 As Integer

    Friend Function 运行() As Integer
        失败数 = 0
        用例数 = 0
        测旋转()
        测窗口大小()
        测最小窗口尺寸()
        测最大化判定()
        测空尺寸()
        Console.WriteLine()
        Console.WriteLine($"内容原始尺寸回归：{用例数} 条，失败 {失败数} 条")
        Return If(失败数 = 0, 0, 1)
    End Function

    Private Sub 测旋转()
        检查尺寸("旋转 0 不交换", 播放器窗口布局控制器.计算旋转后内容尺寸(New Size(1920, 1080), 0UI),
                 New Size(1920, 1080))
        检查尺寸("旋转 1 交换", 播放器窗口布局控制器.计算旋转后内容尺寸(New Size(1920, 1080), 1UI),
                 New Size(1080, 1920))
        检查尺寸("旋转 2 不交换", 播放器窗口布局控制器.计算旋转后内容尺寸(New Size(1920, 1080), 2UI),
                 New Size(1920, 1080))
        检查尺寸("旋转 3 交换", 播放器窗口布局控制器.计算旋转后内容尺寸(New Size(1920, 1080), 3UI),
                 New Size(1080, 1920))
    End Sub

    Private Sub 测窗口大小()
        检查尺寸("1280×720 + chrome",
                 播放器窗口布局控制器.计算内容窗口大小(New Size(1280, 720),
                     New Size(0, 120), New Size(0, 8)),
                 New Size(1280, 848))
        检查尺寸("1920×1080 + chrome",
                 播放器窗口布局控制器.计算内容窗口大小(New Size(1920, 1080),
                     New Size(0, 120), New Size(0, 8)),
                 New Size(1920, 1208))
    End Sub

    Private Sub 测最小窗口尺寸()
        检查尺寸("640×360 目标小于默认最小宽",
                 播放器窗口布局控制器.计算内容模式最小尺寸(New Size(875, 500), New Size(644, 527)),
                 New Size(644, 500))
        检查尺寸("1280×720 目标不小于默认最小",
                 播放器窗口布局控制器.计算内容模式最小尺寸(New Size(875, 500), New Size(1284, 887)),
                 New Size(875, 500))
    End Sub

    Private Sub 测最大化判定()
        检查放置("内容高等于工作区 -> 最大化",
                 播放器窗口布局控制器.决定内容窗口放置(New Size(1920, 1080), New Size(1920, 1208),
                     New Rectangle(0, 0, 1920, 1040)), 内容窗口放置.最大化)
        检查放置("内容宽等于工作区 -> 最大化",
                 播放器窗口布局控制器.决定内容窗口放置(New Size(1920, 900), New Size(1920, 1028),
                     New Rectangle(0, 0, 1920, 1040)), 内容窗口放置.最大化)
        检查放置("内容小于工作区但窗口放不下 -> 最大化",
                 播放器窗口布局控制器.决定内容窗口放置(New Size(1919, 1039), New Size(1939, 1167),
                     New Rectangle(0, 0, 1920, 1040)), 内容窗口放置.最大化)
        检查放置("内容与窗口都放得下 -> 普通",
                 播放器窗口布局控制器.决定内容窗口放置(New Size(1280, 720), New Size(1280, 848),
                     New Rectangle(0, 0, 1920, 1040)), 内容窗口放置.普通)
        检查放置("1920×1080 在 2560×1440 -> 普通",
                 播放器窗口布局控制器.决定内容窗口放置(New Size(1920, 1080), New Size(1920, 1208),
                     New Rectangle(0, 40, 2560, 1400)), 内容窗口放置.普通)
    End Sub

    Private Sub 测空尺寸()
        检查尺寸("旋转空尺寸仍为空", 播放器窗口布局控制器.计算旋转后内容尺寸(Size.Empty, 1UI), Size.Empty)
        检查尺寸("空内容窗口为空", 播放器窗口布局控制器.计算内容窗口大小(Size.Empty,
                     New Size(0, 120), New Size(0, 8)), Size.Empty)
        检查放置("空内容不最大化", 播放器窗口布局控制器.决定内容窗口放置(Size.Empty,
                     New Size(100, 100), New Rectangle(0, 0, 1920, 1040)), 内容窗口放置.普通)
    End Sub

    Private Sub 检查尺寸(名称 As String, 实际 As Size, 期望 As Size)
        用例数 += 1
        If 实际 = 期望 Then
            Console.WriteLine($"ok    {名称}")
        Else
            失败数 += 1
            Console.WriteLine($"FAIL  {名称}    期望={期望}  实际={实际}")
        End If
    End Sub

    Private Sub 检查放置(名称 As String, 实际 As 内容窗口放置, 期望 As 内容窗口放置)
        用例数 += 1
        If 实际 = 期望 Then
            Console.WriteLine($"ok    {名称}")
        Else
            失败数 += 1
            Console.WriteLine($"FAIL  {名称}    期望={期望}  实际={实际}")
        End If
    End Sub
End Module
