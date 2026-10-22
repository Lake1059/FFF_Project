' 外部色彩变换 kind=3 实机像素回读 —— 真内核 + 真模块 DLL + 真渲染着色链
'
' 目的：回答"画面到底长什么样"，而不是"注入了几帧"。注入计数只证明 LUT 递上去了，
' 不证明像素对。本档开真输出窗口走完整渲染管线，再用内核的离屏帧回读
' （FFF3FP_CopyFrame，经 播放器会话.读取原始帧 —— 与呈现同一条着色链、含 kind=3
' LUT 采样，不依赖窗口可见）抓回真实渲染像素，量三个读数：
'   meanR/meanG/meanB —— 画面中心 40% 区域的均值（8 位按字节，16 位按 scRGB 半浮点）；
'   greenDominance   —— 绿主导像素占比（G > R+40 且 G > B+40，16 位等比换算）；
'   shots            —— first/last 落盘 BMP 张数（16 位先映射到 8 位再存，供人眼复核）。
'
' 用法：FFF.Player.Tests --ct-frame-readback <夹具> [采样秒数=6] [输出目录]
' 环境前置（与 ColorTransformInjectionRegression 同口径）：
'   FC_COLORTRANSFORM_DIR / FC_COLORTRANSFORM_MODULE 指向模块目录；
'   票据已解锁（测试 exe 旁或 LOCALAPPDATA），无人值守不会弹框。
' 本档只负责采集与打印 KEY=VALUE 读数：模块未采用/未注入 exit 2（未判），
' 采样失败 exit 1；像素判真伪仍由驱动/人看读数。
Imports System
Imports System.Collections.Generic
Imports System.Drawing
Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module ColorTransformFrameReadback

    Friend Function 运行(args As String()) As Integer
        If args.Length < 1 Then
            Console.WriteLine("用法：--ct-frame-readback <夹具> [采样秒数=6] [输出目录]")
            Return 2
        End If
        Dim 夹具 = args(0)
        If Not IO.File.Exists(夹具) Then
            Console.WriteLine($"FAIL: 夹具不存在 {夹具}")
            Return 2
        End If
        Dim 采样秒 As Double = 6.0
        If args.Length > 1 AndAlso
            (Not Double.TryParse(args(1), 采样秒) OrElse 采样秒 <= 0.0 OrElse 采样秒 > 120.0) Then
            Console.WriteLine($"FAIL: 采样秒数『{args(1)}』不是 (0,120] 内的数。")
            Return 2
        End If
        Dim 输出目录 As String = If(args.Length > 2, args(2),
            IO.Path.Combine(AppContext.BaseDirectory, "ct_readback_shots"))
        IO.Directory.CreateDirectory(输出目录)

        Dim 模块目录 = Environment.GetEnvironmentVariable("FC_COLORTRANSFORM_MODULE")
        Console.WriteLine($"fixture={夹具}")
        Console.WriteLine($"module={If(模块目录, "(未设置)")}")

        Dim 窗体 As New Form() With {
            .Text = "ct-frame-readback",
            .StartPosition = FormStartPosition.Manual,
            .Location = New Point(-8000, -8000),
            .ClientSize = New Size(1280, 720),
            .ShowInTaskbar = False
        }
        Dim 画面 As New 播放器画面控件() With {.Dock = DockStyle.Fill}
        窗体.Controls.Add(画面)
        窗体.Show()
        泵消息(0.25)

        Try
            Dim 配置 As New 播放器配置 With {
                .解码器 = 解码模式.CPU,
                .输出窗口句柄 = 画面.输出窗口句柄}
            Using 会话 = New 播放器会话(配置)
                会话.设置音量(0, True)
                会话.打开Async(夹具).GetAwaiter().GetResult()
                会话.播放()
                Dim 快照 = 等待帧(会话, 3.0, 30.0)
                If 快照 Is Nothing Then
                    Console.WriteLine("FAIL: 30s 内帧序号未过 3，会话没有出画。")
                    Return 1
                End If

                ' 等模块泵把 LUT 真正递进内核（诊断出现"模块已采用"），再给一拍余量。
                Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(15)
                Dim 诊断 As String = 会话.色彩变换诊断
                While DateTimeOffset.UtcNow < 截止 AndAlso Not 诊断.Contains("模块已采用")
                    Thread.Sleep(100)
                    诊断 = 会话.色彩变换诊断
                End While
                Dim 已采用 = 诊断.Contains("模块已采用") AndAlso Not 诊断.Contains("模块已采用但不可用")
                Console.WriteLine($"diag_adopted={已采用}")
                Console.WriteLine($"diag={诊断.Replace(vbCrLf, " | ")}")
                Console.WriteLine($"content={会话.色彩变换内容描述}")
                Dim 注入数 As Integer = 取注入数(诊断)
                Console.WriteLine($"injected_frames={注入数}")
                If Not 已采用 OrElse 注入数 <= 0 Then
                    Console.WriteLine("NOT JUDGED：色彩变换模块未采用或尚未注入；本档只判 LUT 已生效后的像素，不拿原始像素充数。")
                    Return 2
                End If

                Thread.Sleep(500)
                Dim 结果 = 采样(会话, 采样秒, 输出目录)
                For Each 行 In 结果
                    Console.WriteLine(行)
                Next
                If 结果.Count = 0 Then Return 1
            End Using
        Finally
            窗体.Close()
            窗体.Dispose()
            泵消息(0.5)
        End Try
        Return 0
    End Function

    ' ── 采样：循环回读真实渲染帧，聚合统计，first/last 落 BMP（16 位映射 8 位）──
    Private Function 采样(会话 As 播放器会话, 采样秒 As Double, 输出目录 As String) As List(Of String)
        Dim 读数 As New List(Of String)
        Dim 总R As Double = 0, 总G As Double = 0, 总B As Double = 0
        Dim 绿主导 As Long = 0, 总像素 As Long = 0
        Dim 张数 As Integer = 0
        Dim 末张 As Bitmap = Nothing, 首张 As Bitmap = Nothing
        Dim 首 As 帧统计 = Nothing, 末 As 帧统计 = Nothing
        Dim 位深 As Integer = 0, 宽 As Integer = 0, 高 As Integer = 0
        Dim 签名集 As New HashSet(Of ULong)()
        Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(采样秒)

        While DateTimeOffset.UtcNow < 截止
            Dim 帧 = 会话.读取原始帧()
            If 帧 IsNot Nothing Then
                Dim s As 帧统计 = 统计一帧(帧)
                If 首 Is Nothing Then 首 = s
                末 = s
                签名集.Add(s.签名)
                总R += s.均值R : 总G += s.均值G : 总B += s.均值B
                绿主导 += s.绿主导像素 : 总像素 += s.总像素
                位深 = If(帧.PixelFormat = Imaging.PixelFormat.Format64bppArgb, 16, 8)
                宽 = 帧.Width : 高 = 帧.Height
                张数 += 1
                If 首张 Is Nothing Then 首张 = CType(帧.Clone(), Bitmap)
                If 末张 IsNot Nothing Then 末张.Dispose()
                末张 = CType(帧.Clone(), Bitmap)
                帧.Dispose()
            End If
            Thread.Sleep(120)
        End While

        If 张数 = 0 OrElse 总像素 = 0 Then
            Console.WriteLine("FAIL: 采样窗内没有回读到任何帧（内核不支持或无画面）。")
            Return 读数
        End If

        Dim 保存 = Sub(b As Bitmap, 名 As String)
                       ' GDI+ 的 PNG 编码器不吃 64bpp 且实测对大图 Save 有 AV（GdipSaveImageToFile
                       ' 0xC0000005）。16 位 scRGB 不能直接 Clone 成 32bpp：GDI+ 会把半浮点
                       ' 位型当 16bit 整数缩放，落盘图会暗得没有参考价值。这里先逐像素映射。
                       Using 存 As Bitmap = 转人眼(b)
                           Dim p = IO.Path.Combine(输出目录, 名 & ".bmp")
                           存.Save(p, Imaging.ImageFormat.Bmp)
                           Console.WriteLine($"shot={p}")
                       End Using
                   End Sub
        保存(首张, "readback_first")
        保存(末张, "readback_last")
        首张?.Dispose()
        末张?.Dispose()

        读数.Add($"samples={张数}")
        读数.Add($"distinct_frames={签名集.Count}")
        读数.Add($"readback_size={宽}x{高} bitdepth={位深}")
        读数.Add($"firstFrame meanR={首.均值R:F2} meanG={首.均值G:F2} meanB={首.均值B:F2} green={100.0 * 首.绿主导像素 / 首.总像素:F4}%")
        读数.Add($"lastFrame  meanR={末.均值R:F2} meanG={末.均值G:F2} meanB={末.均值B:F2} green={100.0 * 末.绿主导像素 / 末.总像素:F4}%")
        读数.Add($"meanR={总R / 张数:F2}")
        读数.Add($"meanG={总G / 张数:F2}")
        读数.Add($"meanB={总B / 张数:F2}")
        读数.Add($"greenDominance={(100.0 * 绿主导 / 总像素):F4}%")
        If 张数 > 1 AndAlso 签名集.Count = 1 Then
            Console.WriteLine("note: 回读帧签名全部相同（静图夹具或管线未推进）；数值仍有效，但不能当'多帧已变换'的证据。")
        End If
        Return 读数
    End Function

    ''' <summary>把回读帧变成人眼可看的 32bpp 位图。16 位 scRGB 半浮点按 1.0→255 映射；
    ''' 8 位只做格式克隆。返回新位图，调用方负责释放。</summary>
    Private Function 转人眼(b As Bitmap) As Bitmap
        Dim rect As New Rectangle(0, 0, b.Width, b.Height)
        If b.PixelFormat <> Imaging.PixelFormat.Format64bppArgb Then
            Return b.Clone(rect, Imaging.PixelFormat.Format32bppArgb)
        End If
        Dim out As New Bitmap(b.Width, b.Height, Imaging.PixelFormat.Format32bppArgb)
        Dim srcData = b.LockBits(rect, Imaging.ImageLockMode.ReadOnly, b.PixelFormat)
        Try
            Dim srcStride = Math.Abs(srcData.Stride)
            Dim src(srcStride * b.Height - 1) As Byte
            Marshal.Copy(srcData.Scan0, src, 0, src.Length)
            Dim dstData = out.LockBits(rect, Imaging.ImageLockMode.WriteOnly, out.PixelFormat)
            Try
                Dim dstStride = Math.Abs(dstData.Stride)
                Dim dst(dstStride * b.Height - 1) As Byte
                For y As Integer = 0 To b.Height - 1
                    Dim sy = If(srcData.Stride < 0, b.Height - 1 - y, y)
                    Dim srow = sy * srcStride
                    Dim drow = y * dstStride
                    For x As Integer = 0 To b.Width - 1
                        Dim si = srow + x * 8
                        Dim di = drow + x * 4
                        dst(di) = CByte(Math.Min(255.0, Math.Max(0.0, 半转单(src, si) * 255.0)))
                        dst(di + 1) = CByte(Math.Min(255.0, Math.Max(0.0, 半转单(src, si + 2) * 255.0)))
                        dst(di + 2) = CByte(Math.Min(255.0, Math.Max(0.0, 半转单(src, si + 4) * 255.0)))
                        dst(di + 3) = 255
                    Next
                Next
                Marshal.Copy(dst, 0, dstData.Scan0, dst.Length)
            Finally
                out.UnlockBits(dstData)
            End Try
        Finally
            b.UnlockBits(srcData)
        End Try
        Return out
    End Function

    Private Class 帧统计
        Public 均值R As Double, 均值G As Double, 均值B As Double
        Public 绿主导像素 As Long, 总像素 As Long
        Public 签名 As ULong
    End Class

    ''' <summary>中心 40% 区域统计。8 位直接量字节；16 位是 scRGB 半浮点（1.0=80nits），
    ''' 按 0..1×255 折到同一把尺上。绿主导阈值 R+40/G+40 与 8 位口径一致。</summary>
    Private Function 统计一帧(帧 As Bitmap) As 帧统计
        Dim s As New 帧统计()
        Dim x0 = CInt(帧.Width * 0.3), x1 = CInt(帧.Width * 0.7)
        Dim y0 = CInt(帧.Height * 0.3), y1 = CInt(帧.Height * 0.7)
        If x1 <= x0 OrElse y1 <= y0 Then x0 = 0 : y0 = 0 : x1 = 帧.Width : y1 = 帧.Height
        Dim r16 = 帧.PixelFormat = Imaging.PixelFormat.Format64bppArgb

        Dim 区域 As New Rectangle(x0, y0, x1 - x0, y1 - y0)
        Dim 数据 = 帧.LockBits(区域, Imaging.ImageLockMode.ReadOnly, 帧.PixelFormat)
        Try
            Dim 步长 = Math.Abs(数据.Stride)
            Dim 每像素 = If(r16, 8, 4)
            Dim 缓冲((y1 - y0) * 步长 - 1) As Byte
            Runtime.InteropServices.Marshal.Copy(数据.Scan0, 缓冲, 0, 缓冲.Length)
            ' 签名只用于区分"回读到的是不是同一张图"，不参与像素统计；每隔约 1 KB 取一个字节。
            Dim 哈希 As ULong = &HCBF29CE484222325UL
            For i As Integer = 0 To 缓冲.Length - 1 Step 997
                哈希 = 哈希 Xor 缓冲(i)
                哈希 = (哈希 << 7) Or (哈希 >> 57)
            Next
            s.签名 = 哈希
            For iy As Integer = 0 To y1 - y0 - 1
                Dim 行基 = iy * 步长
                For ix As Integer = 0 To x1 - x0 - 1
                    Dim 基 = 行基 + ix * 每像素
                    Dim r As Double, g As Double, b As Double
                    If r16 Then
                        ' BGRA 顺序，各 16 位半浮点；scRGB 1.0 → 255。
                        b = Math.Min(255.0, Math.Max(0.0, 半转单(缓冲, 基) * 255.0))
                        g = Math.Min(255.0, Math.Max(0.0, 半转单(缓冲, 基 + 2) * 255.0))
                        r = Math.Min(255.0, Math.Max(0.0, 半转单(缓冲, 基 + 4) * 255.0))
                    Else
                        b = 缓冲(基) : g = 缓冲(基 + 1) : r = 缓冲(基 + 2)
                    End If
                    s.均值R += r : s.均值G += g : s.均值B += b
                    s.总像素 += 1
                    If g > r + 40.0 AndAlso g > b + 40.0 Then s.绿主导像素 += 1
                Next
            Next
            If s.总像素 > 0 Then
                s.均值R /= s.总像素 : s.均值G /= s.总像素 : s.均值B /= s.总像素
            End If
        Finally
            帧.UnlockBits(数据)
        End Try
        Return s
    End Function

    Private Function 半转单(缓冲 As Byte(), 偏移 As Integer) As Single
        Dim h As Half = BitConverter.ToHalf(缓冲, 偏移)
        Return CSng(h)
    End Function

    Private Function 等待帧(会话 As 播放器会话, 帧 As Double, 超时秒 As Double) As 播放器快照
        Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(超时秒)
        While DateTimeOffset.UtcNow < 截止
            Dim s = 会话.当前快照
            If s.帧序号 > 帧 Then Return s
            Thread.Sleep(100)
            泵消息(0.01)
        End While
        Return Nothing
    End Function

    Private Function 取注入数(诊断 As String) As Integer
        Dim 位置 = 诊断.IndexOf("注入 ")
        If 位置 < 0 Then Return 0
        Dim 尾 = 诊断.Substring(位置 + 3)
        Dim 空格 = 尾.IndexOf(" "c)
        Dim n As Integer = 0
        If 空格 > 0 Then Integer.TryParse(尾.Substring(0, 空格), n)
        Return n
    End Function

    Private Sub 泵消息(秒 As Double)
        Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(秒)
        While DateTimeOffset.UtcNow < 截止
            Application.DoEvents()
            Thread.Sleep(10)
        End While
    End Sub
End Module
