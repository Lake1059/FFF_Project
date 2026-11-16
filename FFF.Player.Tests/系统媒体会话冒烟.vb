Imports System
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

            会话 = New 系统媒体会话(
                Nothing,
                Function(按钮 As 系统媒体按钮) False,
                Function(位置 As TimeSpan) False,
                Function(倍速 As Double) False)
            检查("GetForWindow 初始化", 会话.初始化(窗体.Handle), 用例数, 失败数)
            If 会话.已启用面板 Then
                会话.发布卡片("smoke-id", "冒烟标题", "冒烟艺术家", "", "", 1UI,
                                系统媒体显示类型.音乐, 1.0R)
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
