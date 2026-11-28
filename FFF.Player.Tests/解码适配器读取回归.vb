Imports System
Imports System.Drawing
Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

''' <summary>
''' 信息层"视频"行显卡名的整条链路回归：内核 FFF3FP_GetDecodeAdapterInfo（追加导出）
''' → 播放器会话.实际解码适配器索引 → GPU解码设备.按索引名称（与设置页同一份枚举）。
''' <para>用真实样片跑一次解码。本机确实不能硬解时（远程会话 / 无可用适配器）只断言
''' "软解不得报显卡"，不判失败 —— 那正是宿主要退化成的样子。</para>
''' 跑法：FFF.Player.Tests.exe --decode-adapter-readback-regression
''' </summary>
Friend Module 解码适配器读取回归

    ' 样片相对仓库的固定位置。找不到要判失败而不是静默跳过，否则路径一漂移这项回归
    ' 就悄悄失效了。
    Private ReadOnly 样片路径 As String = Path.GetFullPath(Path.Combine(
        AppContext.BaseDirectory, "..\..\..\..\..\samples\media\vfr\12a_vfr_25then60.mp4"))

    Friend Function 运行() As Integer
        Dim 用例数 As Integer
        Dim 失败数 As Integer
        Dim 窗体 As New Form()
        Dim 会话 As 播放器会话 = Nothing
        Try
            ' 1) ABI：结构必须与内核 FFF3FPDecodeAdapterInfo 同宽（2×UInteger + Integer +
            '    UInteger = 16 字节）。尺寸不符时内核会拒收，宿主只能显示 "GPU"。
            检查("解码适配器结构 16 字节",
                Marshal.SizeOf(Of 原生解码适配器信息)() = 16, 用例数, 失败数)
            检查("当前内核导出解码适配器查询", 播放器原生接口.支持解码适配器查询, 用例数, 失败数)
            ' 2) 负索引永远不编造名字：信息层据此退回只显示 "GPU"。
            检查("负索引不返回名称", GPU解码设备.按索引名称(-1) = String.Empty, 用例数, 失败数)

            检查("样片存在", File.Exists(样片路径), 用例数, 失败数)
            If Not File.Exists(样片路径) Then Return 汇总(用例数, 失败数)

            ' 排查用：设置页与信息层看到的就是这份枚举，先把它原样打出来。
            For Each 设备 In GPU解码设备.枚举()
                Console.WriteLine($"      枚举 #{设备.索引} {设备.名称} [{设备.标识}]")
            Next

            窗体.Text = "decode adapter readback"
            窗体.ShowInTaskbar = False
            窗体.StartPosition = FormStartPosition.Manual
            ' 放到屏幕外：回归要真窗口句柄才能建交换链，但不必让调试者眼前闪一个播放器。
            窗体.Location = New Point(-2400, 0)
            窗体.Width = 320
            窗体.Height = 180
            窗体.Show()
            Application.DoEvents()

            Dim 配置 As New 播放器配置 With {
                .输出窗口句柄 = 窗体.Handle,
                .解码器 = 解码模式.GPU,
                .色彩模式 = 色彩输出模式.映射到SDR
            }
            会话 = New 播放器会话(配置)
            会话.打开(样片路径)
            ' Open 只把"用户请求 GPU"写进快照；解码器是在 worker 上随后才打开的。
            ' 这里必须等到**内核真报出落点**（索引 >= 0），或落定为软解，才能断言。
            Dim 截止 = Environment.TickCount64 + 10000
            Dim 已落定 = False
            Do
                Application.DoEvents()
                Thread.Sleep(50)
                If 会话.当前快照.解码器 = 解码模式.CPU Then
                    已落定 = True
                    Exit Do
                End If
                If 会话.实际解码适配器索引() >= 0 Then
                    已落定 = True
                    Exit Do
                End If
            Loop While Environment.TickCount64 < 截止
            ' 让首帧真正解出来、present 跑起来后再读一次。
            Thread.Sleep(300)
            Application.DoEvents()
            If Not 已落定 Then Console.WriteLine("      WARN 等待解码适配器落定超时（10s）")

            Dim 快照 = 会话.当前快照
            Dim 索引 = 会话.实际解码适配器索引()
            Dim 句柄 = TryCast(GetType(播放器会话).GetField("句柄",
                System.Reflection.BindingFlags.NonPublic Or System.Reflection.BindingFlags.Instance).
                GetValue(会话), 播放器原生句柄)
            Dim 原生 = 播放器原生接口.读取解码适配器(句柄)
            If 原生.HasValue Then
                Console.WriteLine($"      RAW hardware={原生.Value.硬件解码} adapterIndex={原生.Value.适配器索引} snapshot={快照.解码器}")
            Else
                Console.WriteLine($"      RAW 读取失败 snapshot={快照.解码器}")
            End If
            If 快照.解码器 = 解码模式.GPU Then
                检查("硬解时内核回报有效适配器索引", 索引 >= 0, 用例数, 失败数)
                Dim 名称 = GPU解码设备.按索引名称(索引)
                检查($"索引 {索引} 解析出非空显卡名", Not String.IsNullOrWhiteSpace(名称), 用例数, 失败数)
                Console.WriteLine($"      解码 GPU = #{索引} · {名称}")
            Else
                ' 软解（无可用硬解适配器 / 远程会话）：内核必须回 hardware = 0，宿主归一成 -1，
                ' 信息层此时只显示 "CPU"。这里绝不能"猜一张卡"。
                检查("软解时索引归一为 -1", 索引 = -1, 用例数, 失败数)
                Console.WriteLine("      NOTE 本机当前无法硬解此样片，已跳过显卡名断言")
            End If
        Catch ex As Exception
            失败数 += 1
            Console.WriteLine("FAIL  解码适配器读取回归抛出异常：" & ex.GetType().Name & " - " & ex.Message)
        Finally
            Try
                ' 播放器会话 显式实现 IDisposable（Dispose 不是 Public 成员），只能经接口释放。
                If 会话 IsNot Nothing Then CType(会话, IDisposable).Dispose()
            Catch
            End Try
            Try
                窗体.Close()
                Application.DoEvents()
                窗体.Dispose()
            Catch
            End Try
        End Try
        Return 汇总(用例数, 失败数)
    End Function

    Private Function 汇总(用例数 As Integer, 失败数 As Integer) As Integer
        Console.WriteLine()
        Console.WriteLine($"解码适配器读取回归：{用例数} 条，失败 {失败数} 条")
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
