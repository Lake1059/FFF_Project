Imports System
Imports System.Drawing
Imports System.IO
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

''' <summary>
''' SMTC 运行时冒烟：在 STA 测试进程里真的创建一个窗口句柄，走一遍
''' GetForWindow → 发布卡片 → 发布状态/时间轴 → 清空。只验证“能不能建、
''' 会不会抛异常”，不依赖系统媒体面板是否在前台可见。
'''
''' 跑法：FFF.Player.Tests.exe --smtc-runtime-smoke
''' </summary>
Friend Module 系统媒体会话冒烟

    ' L5-P2-02：记录分派钩子实际收到过哪些按钮。
    Private ReadOnly 已分派按钮 As New HashSet(Of 系统媒体按钮)()

    Private Function 记录按钮(按钮 As 系统媒体按钮) As Boolean
        已分派按钮.Add(按钮)
        Return True
    End Function

    Friend Function 运行() As Integer
        Dim 用例数 As Integer
        Dim 失败数 As Integer
        Dim 窗体 As New Form()
        Dim 会话 As 系统媒体会话 = Nothing
        Try
            窗体.Text = "SMTC smoke"
            窗体.ShowInTaskbar = False
            窗体.Width = 320
            窗体.Height = 200
            窗体.Show()
            Application.DoEvents()

            ' 缩略图编码是“没有内嵌封面 → 首帧”的最后一公里：先验证 4K 级源帧
            ' 会被限制到 SMTC 卡片能接受的小尺寸，且输出是真 PNG。
            Using 源 As New Bitmap(640, 360)
                Using 图形 = Graphics.FromImage(源)
                    图形.Clear(Color.CadetBlue)
                End Using
                Dim 编码 = 系统媒体缩略图.编码PNG(源)
                检查("缩略图 PNG 编码非空", 编码.Length > 0, 用例数, 失败数)
                Using 流 As New MemoryStream(编码), 解码 As New Bitmap(流)
                    检查("缩略图最长边不超过 320",
                        Math.Max(解码.Width, 解码.Height) <= 系统媒体缩略图.默认最大边长,
                        用例数, 失败数)
                End Using
            End Using
            检查("无帧缩略图返回空数组", 系统媒体缩略图.编码PNG(Nothing).Length = 0, 用例数, 失败数)

            会话 = New 系统媒体会话(
                Nothing,
                AddressOf 记录按钮,
                Function(位置 As TimeSpan) False,
                Function(倍速 As Double) False)
            检查("GetForWindow 初始化", 会话.初始化(窗体.Handle), 用例数, 失败数)
            If 会话.已启用面板 Then
                会话.发布卡片("smoke-id", "冒烟标题", "冒烟艺术家", "", "冒烟专辑艺术家", 1UI,
                                系统媒体显示类型.音乐, 1.0R)
                ' 1x1 透明 PNG：验证 PNG 字节 → InMemoryRandomAccessStream →
                ' RandomAccessStreamReference → DisplayUpdater.Thumbnail 的 ABI 通路不抛异常。
                会话.设置缩略图(Convert.FromBase64String(
                    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="))
                Dim 缩略图返回码 = 会话.测试用设置缩略图(Convert.FromBase64String(
                    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="))
                检查("PNG 缩略图 ABI 返回 0", 缩略图返回码 = 0, 用例数, 失败数)
                会话.发布状态(系统媒体播放状态.已暂停)
                会话.发布时间轴(New 系统媒体时间轴 With {
                    .StartTime = TimeSpan.Zero,
                    .EndTime = TimeSpan.FromMinutes(2),
                    .Position = TimeSpan.FromSeconds(30),
                    .MinSeekTime = TimeSpan.Zero,
                    .MaxSeekTime = TimeSpan.FromMinutes(2)
                })
                会话.设置按钮可用性(True, False, True, True, False, False)
                Application.DoEvents()
                检查("发布卡片不抛异常", True, 用例数, 失败数)

                ' L5-P2-02：断言“按钮回调被注册且分派到对应播放器方法”。这里没有真实 WinRT
                ' 按钮可按，改走测试钩子 测试用分派按钮——它与生产 控件_按钮按下 调用同一个
                ' 命令接收器；若接收器没被挂上，下面 7 条全部收不到按钮而失败。
                已分派按钮.Clear()
                For Each b In {系统媒体按钮.播放, 系统媒体按钮.暂停, 系统媒体按钮.停止,
                               系统媒体按钮.快进, 系统媒体按钮.快退, 系统媒体按钮.下一曲,
                               系统媒体按钮.上一曲}
                    会话.测试用分派按钮(b)
                Next
                For Each b In {系统媒体按钮.播放, 系统媒体按钮.暂停, 系统媒体按钮.停止,
                               系统媒体按钮.快进, 系统媒体按钮.快退, 系统媒体按钮.下一曲,
                               系统媒体按钮.上一曲}
                    检查($"按钮分派到达接收器/{b}", 已分派按钮.Contains(b), 用例数, 失败数)
                Next

                会话.关闭()
                检查("关闭后不再启用", Not 会话.已启用面板, 用例数, 失败数)
            End If
        Catch ex As Exception
            失败数 += 1
            Console.WriteLine("FAIL  SMTC 运行时冒烟抛出异常：" & ex.GetType().Name & " - " & ex.Message)
        Finally
            Try
                If 会话 IsNot Nothing Then 会话.Dispose()
            Catch
            End Try
            Try
                窗体.Close()
                Application.DoEvents()
                窗体.Dispose()
            Catch
            End Try
        End Try

        Console.WriteLine()
        Console.WriteLine($"系统媒体会话冒烟：{用例数} 条，失败 {失败数} 条")
        Return If(失败数 = 0, 0, 1)
    End Function

    Private Sub 检查(名称 As String, 条件 As Boolean, ByRef 用例数 As Integer, ByRef 失败数 As Integer)
        用例数 += 1
        If 条件 Then
            Console.WriteLine("ok    " & 名称)
        Else
            失败数 += 1
            Console.WriteLine("FAIL  " & 名称)
        End If
    End Sub
End Module
