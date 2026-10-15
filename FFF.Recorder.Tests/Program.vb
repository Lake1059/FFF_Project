Imports System.Diagnostics
Imports System.IO
Imports System.Linq
Imports System.Runtime.InteropServices
Imports System.Text.Json
Imports FFF.Recorder
Imports Vortice.Direct3D11
Imports Vortice.DXGI

Friend Module Program
    Private Const 测试宽度 As UInteger = 320UI
    Private Const 测试高度 As UInteger = 180UI
    Private Const 测试帧数 As Integer = 30

    <STAThread>
    Public Function Main(参数 As String()) As Integer
        If 参数.Length < 1 OrElse 参数.Length > 2 OrElse
            Not String.Equals(参数(0), "--recording-functional", StringComparison.OrdinalIgnoreCase) Then
            Console.Error.WriteLine("用法：FFF.Recorder.Tests --recording-functional [输出目录]")
            Return 2
        End If

        Dim 输出目录 = If(参数.Length = 2, Path.GetFullPath(参数(1)),
            Path.Combine(Path.GetTempPath(), "FFF.Recorder.Tests", DateTime.Now.ToString("yyyyMMdd-HHmmss")))
        Directory.CreateDirectory(输出目录)

        Try
            Dim 基础自检 = 录制引擎.运行基础自检()
            Using 文档 = JsonDocument.Parse(基础自检)
                断言(文档.RootElement.GetProperty("passed").GetBoolean(), "原生录制基础自检失败。")
            End Using
            测试总控台帧数统计()
            测试HDR目标峰值不低于参考白()

            Using 图形 = 图形设备.创建默认设备()
                测试固定帧率落后时重复补帧(图形)
                测试固定帧率接纳稍晚到达帧(图形)
                测试可变帧率提交时间戳覆盖(图形)
                测试SDR处理(图形)
                测试HDR到PQ峰值钳制(图形)
                测试BT2390高SDR白电平(图形)
                测试HDR到PQ录制链路(图形, 输出目录)
                测试BT2390录制链路(图形, 输出目录)
            End Using

            Console.WriteLine($"录制功能测试通过：GPU 三条处理路径、BT.2390、编码会话与 MKV 文件尾均正常。")
            Console.WriteLine($"测试输出：{输出目录}")
            Return 0
        Catch 错误 As Exception
            Console.Error.WriteLine(错误.ToString())
            Console.Error.WriteLine($"失败输出：{输出目录}")
            Return 1
        End Try
    End Function

    Private Sub 测试总控台帧数统计()
        Dim 统计 As New 录制统计 With {
            .已提交帧数 = 120UL, .已丢弃帧数 = 7UL, .已重复帧数 = 20UL
        }
        Dim 文本 = Form总控台.生成录制统计文本(统计)
        断言(文本.StartsWith("总帧数：120<br>", StringComparison.Ordinal),
            $"总控台把未写入文件的丢弃帧计入了总帧数：{文本}")
        断言(文本.Contains("已丢帧：7<br>重复帧：20", StringComparison.Ordinal),
            $"总控台的丢帧或重复帧统计错误：{文本}")
    End Sub

    Private Sub 测试HDR目标峰值不低于参考白()
        Dim 配置 As New 视频处理配置()
        配置.设置色彩模式(True, 500.0F, 400.0F)
        配置.验证()
        断言(配置.参考白尼特 = 500.0F, $"HDR 参考白异常：{配置.参考白尼特}。")
        断言(配置.目标峰值尼特 = 500.0F, $"HDR 目标峰值没有覆盖参考白：{配置.目标峰值尼特}。")
        断言(配置.源峰值尼特 = 500.0F, $"HDR 源峰值没有与实际峰值一致：{配置.源峰值尼特}。")
    End Sub

    Private Sub 测试SDR处理(图形 As 图形设备)
        Dim 配置 As New 视频处理配置 With {
            .输出宽度 = 测试宽度, .输出高度 = 测试高度, .高质量缩放 = True
        }
        配置.设置色彩模式(False, 100.0F, 1000.0F)
        Using 处理器 As New 视频处理器(图形, 配置)
            Using 源纹理 = 创建测试纹理(图形, Format.B8G8R8A8_UNorm, 0.2F, 0.4F, 0.7F)
                Using 捕获帧 As New 显示器捕获帧(源纹理, Stopwatch.GetTimestamp(), False,
                    视频旋转方式.不旋转, AddressOf 忽略纹理回收)
                    Using 输出帧 = 处理器.处理帧(捕获帧)
                        验证处理输出(图形, 输出帧, Format.B8G8R8A8_UNorm, "SDR")
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Sub 测试固定帧率落后时重复补帧(图形 As 图形设备)
        Dim 提交记录 As New List(Of (时间戳 As Long, 重复 As Boolean))
        Dim 已报告丢帧 As UInteger
        Dim 提交 As Action(Of IntPtr, Long, UInteger, Boolean) =
            Sub(纹理指针, 时间戳, 数组索引, 重复帧)
                SyncLock 提交记录
                    提交记录.Add((时间戳, 重复帧))
                End SyncLock
            End Sub
        Dim 报告丢帧 As Action(Of UInteger) =
            Sub(帧数)
                已报告丢帧 += 帧数
            End Sub

        Using 源纹理 = 创建测试纹理(图形, Format.B8G8R8A8_UNorm, 0.2F, 0.3F, 0.4F)
            Dim 起始时间 = Stopwatch.GetTimestamp() - Stopwatch.Frequency \ 2
            Using 调度器 As New 固定帧率调度器(提交, 报告丢帧, 30UI, 1UI)
                调度器.开始(起始时间)
                调度器.提交帧(New 处理后视频帧(源纹理, Nothing, 起始时间, False, AddressOf 忽略纹理回收))
                Dim 截止 = Stopwatch.GetTimestamp() + Stopwatch.Frequency * 2L
                Do
                    SyncLock 提交记录
                        If 提交记录.Count >= 10 Then Exit Do
                    End SyncLock
                    Threading.Thread.Sleep(5)
                Loop While Stopwatch.GetTimestamp() < 截止
                调度器.停止()
            End Using
        End Using

        Dim 快照 As (时间戳 As Long, 重复 As Boolean)()
        SyncLock 提交记录
            快照 = 提交记录.ToArray()
        End SyncLock
        断言(快照.Length >= 10, $"CFR 落后追赶时没有补足固定输出 tick，只提交了 {快照.Length} 帧。")
        断言(Not 快照(0).重复, "CFR 首帧不应标记为重复帧。")
        断言(快照.Skip(1).All(Function(记录) 记录.重复), "CFR 落后补帧应使用上一帧标记为重复帧。")
        For 索引 = 1 To 快照.Length - 1
            断言(快照(索引).时间戳 > 快照(索引 - 1).时间戳, "CFR 补帧时间戳必须严格递增。")
        Next
        断言(已报告丢帧 = 0UI, $"CFR 落后补帧不应报告输出丢帧：{已报告丢帧}。")
    End Sub

    Private Sub 测试固定帧率接纳稍晚到达帧(图形 As 图形设备)
        Dim 提交记录 As New List(Of (时间戳 As Long, 重复 As Boolean))
        Dim 提交 As Action(Of IntPtr, Long, UInteger, Boolean) =
            Sub(纹理指针, 时间戳, 数组索引, 重复帧)
                SyncLock 提交记录
                    提交记录.Add((时间戳, 重复帧))
                End SyncLock
            End Sub
        Dim 报告丢帧 As Action(Of UInteger) = Sub(帧数)
                                             End Sub

        Using 源纹理 = 创建测试纹理(图形, Format.B8G8R8A8_UNorm, 0.4F, 0.3F, 0.2F)
            Dim 起始时间 = Stopwatch.GetTimestamp()
            Using 调度器 As New 固定帧率调度器(提交, 报告丢帧, 60UI, 1UI)
                调度器.开始(起始时间)
                Dim 晚到时间戳 = 起始时间 + Stopwatch.Frequency \ 20L
                调度器.提交帧(New 处理后视频帧(源纹理, Nothing, 晚到时间戳, False, AddressOf 忽略纹理回收))
                Dim 截止 = Stopwatch.GetTimestamp() + Stopwatch.Frequency
                Do
                    SyncLock 提交记录
                        If 提交记录.Count >= 1 Then Exit Do
                    End SyncLock
                    Threading.Thread.Sleep(2)
                Loop While Stopwatch.GetTimestamp() < 截止
                调度器.停止()
            End Using
        End Using

        Dim 快照 As (时间戳 As Long, 重复 As Boolean)()
        SyncLock 提交记录
            快照 = 提交记录.ToArray()
        End SyncLock
        断言(快照.Length >= 1, "CFR 没有接纳已到达但时间戳略晚的帧。")
        断言(Not 快照(0).重复, "CFR 接纳首个稍晚到达帧时不应标记为重复帧。")
    End Sub

    Private Sub 测试可变帧率提交时间戳覆盖(图形 As 图形设备)
        Dim 提交记录 As New List(Of (时间戳 As Long, 重复 As Boolean))
        Dim 提交 As Action(Of IntPtr, Long, UInteger, Boolean) =
            Sub(纹理指针, 时间戳, 数组索引, 重复帧)
                SyncLock 提交记录
                    提交记录.Add((时间戳, 重复帧))
                End SyncLock
            End Sub
        Dim 报告丢帧 As Action(Of UInteger) = Sub(帧数)
                                         End Sub

        Dim 预期时间戳 As Long
        Using 源纹理 = 创建测试纹理(图形, Format.B8G8R8A8_UNorm, 0.2F, 0.3F, 0.4F)
            Dim 源时间戳 = Stopwatch.GetTimestamp() - Stopwatch.Frequency
            预期时间戳 = Stopwatch.GetTimestamp()
            Using 编码器 As New 可变帧率编码器(提交, 报告丢帧)
                编码器.开始()
                编码器.提交帧(New 处理后视频帧(源纹理, Nothing, 源时间戳, False, AddressOf 忽略纹理回收),
                    预期时间戳)
                Dim 截止 = Stopwatch.GetTimestamp() + Stopwatch.Frequency * 2L
                Do
                    SyncLock 提交记录
                        If 提交记录.Count >= 1 Then Exit Do
                    End SyncLock
                    Threading.Thread.Sleep(5)
                Loop While Stopwatch.GetTimestamp() < 截止
                编码器.停止()
            End Using
        End Using

        Dim 快照 As (时间戳 As Long, 重复 As Boolean)()
        SyncLock 提交记录
            快照 = 提交记录.ToArray()
        End SyncLock
        断言(快照.Length = 1, $"VFR 时间戳覆盖测试提交帧数异常：{快照.Length}。")
        断言(快照(0).时间戳 = 预期时间戳,
            $"VFR 首帧没有使用会话启动后的提交时间戳：实际 {快照(0).时间戳}，预期 {预期时间戳}。")
        断言(Not 快照(0).重复, "VFR 不应把正常首帧标记为重复帧。")
    End Sub

    Private Sub 测试HDR到PQ录制链路(图形 As 图形设备, 输出目录 As String)
        Dim 配置 As New 视频处理配置 With {
            .输出宽度 = 测试宽度, .输出高度 = 测试高度, .高质量缩放 = True
        }
        配置.设置色彩模式(True, 203.0F, 1000.0F)
        Using 处理器 As New 视频处理器(图形, 配置)
            Using 源纹理 = 创建测试纹理(图形, Format.R16G16B16A16_Float, 4.0F, 2.0F, 1.0F)
                Using 捕获帧 As New 显示器捕获帧(源纹理, Stopwatch.GetTimestamp(), True,
                    视频旋转方式.不旋转, AddressOf 忽略纹理回收)
                    Using 输出帧 = 处理器.处理帧(捕获帧)
                        断言(输出帧.是HDR输出, "HDR 到 PQ 路径没有标记 HDR 输出。")
                        验证处理输出(图形, 输出帧, Format.R10G10B10A2_UNorm, "HDR 到 PQ")
                        测试真实录制会话(图形, 处理器, 输出帧, 输出目录,
                            "hdr10-functional", "R16G16B16A16_FLOAT -> Rec.2100 PQ R10G10B10A2")
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Sub 测试HDR到PQ峰值钳制(图形 As 图形设备)
        Dim 配置 As New 视频处理配置 With {
            .输出宽度 = 测试宽度, .输出高度 = 测试高度, .高质量缩放 = True
        }
        配置.设置色彩模式(True, 203.0F, 1000.0F)
        Using 处理器 As New 视频处理器(图形, 配置)
            Using 源纹理 = 创建测试纹理(图形, Format.R16G16B16A16_Float, 100.0F, 100.0F, 100.0F)
                Using 捕获帧 As New 显示器捕获帧(源纹理, Stopwatch.GetTimestamp(), True,
                    视频旋转方式.不旋转, AddressOf 忽略纹理回收)
                    Using 输出帧 = 处理器.处理帧(捕获帧)
                        Dim 像素 = 验证处理输出(图形, 输出帧, Format.R10G10B10A2_UNorm, "HDR 到 PQ 峰值钳制")
                        Dim 红 = CInt(像素 And &H3FFUI)
                        Dim 绿 = CInt((像素 >> 10) And &H3FFUI)
                        Dim 蓝 = CInt((像素 >> 20) And &H3FFUI)
                        断言(New Integer() {红, 绿, 蓝}.All(Function(分量) 分量 > 0 AndAlso 分量 < 1000),
                            $"HDR 到 PQ 未按目标峰值钳制：R={红}, G={绿}, B={蓝}。")
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Sub 测试BT2390高SDR白电平(图形 As 图形设备)
        Dim 视频配置 As New 视频处理配置 With {
            .输出宽度 = 测试宽度, .输出高度 = 测试高度, .高质量缩放 = True
        }
        视频配置.设置色彩模式(False, 300.0F, 1000.0F)

        Using 处理器 As New 视频处理器(图形, 视频配置)
            Using 源纹理 = 创建测试纹理(图形, Format.R16G16B16A16_Float, 2.0F, 2.0F, 2.0F)
                Using 捕获帧 As New 显示器捕获帧(源纹理, Stopwatch.GetTimestamp(), True,
                    视频旋转方式.不旋转, AddressOf 忽略纹理回收)
                    Using 输出帧 = 处理器.处理帧(捕获帧)
                        Dim 像素 = 验证处理输出(图形, 输出帧, Format.B8G8R8A8_UNorm, "BT.2390 高 SDR 白电平")
                        Dim 蓝 = CInt(像素 And &HFFUI)
                        Dim 绿 = CInt((像素 >> 8) And &HFFUI)
                        Dim 红 = CInt((像素 >> 16) And &HFFUI)
                        Dim 最大 = Math.Max(红, Math.Max(绿, 蓝))
                        Dim 最小 = Math.Min(红, Math.Min(绿, 蓝))
                        断言(最小 > 0 AndAlso 最大 < 250,
                            $"BT.2390 高 SDR 白电平输出被剪切：R={红}, G={绿}, B={蓝}。")
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Sub 测试BT2390录制链路(图形 As 图形设备, 输出目录 As String)
        Dim 视频配置 As New 视频处理配置 With {
            .输出宽度 = 测试宽度, .输出高度 = 测试高度, .高质量缩放 = True
        }
        视频配置.设置色彩模式(False, 100.0F, 1000.0F)

        Using 处理器 As New 视频处理器(图形, 视频配置)
            Using 源纹理 = 创建测试纹理(图形, Format.R16G16B16A16_Float, 4.0F, 2.0F, 1.0F)
                Using 捕获帧 As New 显示器捕获帧(源纹理, Stopwatch.GetTimestamp(), True,
                    视频旋转方式.不旋转, AddressOf 忽略纹理回收)
                    Using 输出帧 = 处理器.处理帧(捕获帧)
                        Dim 像素 = 验证处理输出(图形, 输出帧, Format.B8G8R8A8_UNorm, "BT.2390 HDR 到 SDR")
                        验证BT2390像素(像素)
                        测试真实录制会话(图形, 处理器, 输出帧, 输出目录,
                            "bt2390-functional", "R16G16B16A16_FLOAT -> BT.2390 -> BGRA8")
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Sub 测试真实录制会话(图形 As 图形设备, 处理器 As 视频处理器,
        输出帧 As 处理后视频帧, 输出目录 As String, 基础名称 As String, 源格式说明 As String)
        Dim 输出文件 = Path.Combine(输出目录, 基础名称 & ".mkv")
        Dim 诊断文件 = Path.Combine(输出目录, 基础名称 & ".json")
        Dim 配置 As New 录制配置 With {
            .输出文件 = 输出文件, .诊断日志文件 = 诊断文件,
            .编码器名称 = "libx265", .宽度 = 测试宽度, .高度 = 测试高度,
            .帧率分子 = 30UI, .帧率分母 = 1UI, .视频码率 = 2_000_000,
            .最大码率 = 3_000_000, .关键帧间隔 = 30UI,
            .编码预设 = "ultrafast", .视频采样 = 视频采样格式.YUV四二零,
            .速率控制 = 编码速率控制.可变码率, .质量值 = 23,
            .捕获后端 = "FFF.Recorder.Tests", .捕获源说明 = "Synthetic FP16 scRGB",
            .捕获源格式 = 源格式说明
        }
        处理器.应用到配置(配置)
        If 配置.使用十位色 Then 配置.编码配置档 = "main10"

        Dim 探测 = 录制引擎.探测D3D11编码器(图形, 配置, False)
        断言(探测.支持, $"libx265 D3D11 录制组合不可用：{探测.原因}")

        Using 会话 = 录制引擎.创建会话(配置, 图形.原生设备指针)
            会话.开始()
            Dim 起始时间 = Stopwatch.GetTimestamp()
            Dim 帧间隔 = Math.Max(1L, CLng(Math.Round(CDbl(Stopwatch.Frequency) / 30.0)))
            For 索引 = 0 To 测试帧数 - 1
                会话.提交视频纹理(输出帧.原生纹理指针, 起始时间 + (索引 + 1L) * 帧间隔,
                    是重复帧:=索引 > 0)
            Next
            会话.停止()

            Dim 统计 = 会话.读取统计()
            断言(统计.状态 = 录制会话状态.已停止, $"录制停止后的状态错误：{统计.状态}")
            断言(统计.已提交帧数 = CULng(测试帧数), $"实际提交帧数为 {统计.已提交帧数}。")
            断言(统计.已重复帧数 = CULng(测试帧数 - 1), $"实际重复帧数为 {统计.已重复帧数}。")
            断言(统计.视频字节数 > 0UL, "录制器没有写入视频数据。")
            断言(统计.已写正常文件尾, "录制器没有写入正常 MKV 文件尾。")
            断言(统计.最后错误码 = 0, $"录制器最后错误码为 {统计.最后错误码}。")
        End Using

        验证MKV(输出文件)
        验证诊断日志(诊断文件)
    End Sub

    Private Function 创建测试纹理(图形 As 图形设备, 格式 As Format,
        红 As Single, 绿 As Single, 蓝 As Single) As ID3D11Texture2D
        Dim 像素 = If(格式 = Format.R16G16B16A16_Float,
            创建半精度像素(红, 绿, 蓝, 1.0F),
            {转八位(蓝), 转八位(绿), 转八位(红), CByte(255)})
        Dim 行跨度 = CInt(测试宽度 * 2UI) * 像素.Length
        Dim 数据(行跨度 * CInt(测试高度 * 2UI) - 1) As Byte
        For 偏移 = 0 To 数据.Length - 1 Step 像素.Length
            Array.Copy(像素, 0, 数据, 偏移, 像素.Length)
        Next
        Dim 描述 As New Texture2DDescription(格式, 测试宽度 * 2UI, 测试高度 * 2UI,
            1, 1, BindFlags.ShaderResource, ResourceUsage.Default,
            CpuAccessFlags.None, 1, 0, ResourceOptionFlags.None)
        Dim 纹理 = 图形.设备.CreateTexture2D(描述)
        Dim 数据指针 = Marshal.AllocHGlobal(数据.Length)
        Try
            Marshal.Copy(数据, 0, 数据指针, 数据.Length)
            图形.执行图形命令(Sub() 图形.上下文.UpdateSubresource(
                纹理, 0UI, Nothing, 数据指针, CUInt(行跨度), CUInt(数据.Length)))
            Return 纹理
        Catch
            纹理.Dispose()
            Throw
        Finally
            Marshal.FreeHGlobal(数据指针)
        End Try
    End Function

    Private Function 创建半精度像素(红 As Single, 绿 As Single, 蓝 As Single, 透明度 As Single) As Byte()
        Dim 结果(7) As Byte
        Dim 分量 = {红, 绿, 蓝, 透明度}
        For 索引 = 0 To 分量.Length - 1
            Dim 位 = BitConverter.HalfToUInt16Bits(CType(分量(索引), Half))
            结果(索引 * 2) = CByte(位 And &HFFUS)
            结果(索引 * 2 + 1) = CByte(位 >> 8)
        Next
        Return 结果
    End Function

    Private Function 转八位(值 As Single) As Byte
        Return CByte(Math.Round(Math.Clamp(值, 0.0F, 1.0F) * 255.0F))
    End Function

    Private Function 验证处理输出(图形 As 图形设备, 帧 As 处理后视频帧,
        预期格式 As Format, 路径名称 As String) As UInteger
        断言(帧 IsNot Nothing, $"{路径名称} 没有返回处理帧。")
        Dim 描述 = 帧.纹理.Description
        断言(描述.Width = 测试宽度 AndAlso 描述.Height = 测试高度,
            $"{路径名称} 输出尺寸错误：{描述.Width}x{描述.Height}。")
        断言(描述.Format = 预期格式, $"{路径名称} 输出格式错误：{描述.Format}。")
        Dim 像素 = 读取首像素(图形, 帧.纹理)
        Dim 色彩掩码 = If(预期格式 = Format.R10G10B10A2_UNorm, &H3FFFFFFFUI, &HFFFFFFUI)
        断言((像素 And 色彩掩码) <> 0UI, $"{路径名称} 输出是空白像素。")
        Return 像素
    End Function

    Private Function 读取首像素(图形 As 图形设备, 来源 As ID3D11Texture2D) As UInteger
        Dim 来源描述 = 来源.Description
        Dim 暂存描述 As New Texture2DDescription(来源描述.Format, 来源描述.Width, 来源描述.Height,
            1, 1, BindFlags.None, ResourceUsage.Staging, CpuAccessFlags.Read,
            1, 0, ResourceOptionFlags.None)
        Using 暂存 = 图形.设备.CreateTexture2D(暂存描述)
            Dim 像素 As UInteger
            图形.执行图形命令(
                Sub()
                    图形.上下文.CopyResource(暂存, 来源)
                    Dim 映射 As MappedSubresource
                    Dim 结果 = 图形.上下文.Map(暂存, 0UI, MapMode.Read,
                        Vortice.Direct3D11.MapFlags.None, 映射)
                    If 结果.Failure Then Throw New InvalidOperationException($"读取 GPU 输出失败：0x{结果.Code:X8}")
                    Try
                        Dim 原始像素(3) As Byte
                        Marshal.Copy(映射.DataPointer, 原始像素, 0, 原始像素.Length)
                        像素 = BitConverter.ToUInt32(原始像素, 0)
                    Finally
                        图形.上下文.Unmap(暂存, 0UI)
                    End Try
                End Sub)
            Return 像素
        End Using
    End Function

    Private Sub 验证BT2390像素(像素 As UInteger)
        Dim 蓝 = CInt(像素 And &HFFUI)
        Dim 绿 = CInt((像素 >> 8) And &HFFUI)
        Dim 红 = CInt((像素 >> 16) And &HFFUI)
        Dim 透明度 = CInt((像素 >> 24) And &HFFUI)
        断言(透明度 = 255, $"BT.2390 输出透明度错误：{透明度}。")
        断言(红 > 绿 AndAlso 绿 > 蓝, $"BT.2390 色相顺序异常：R={红}, G={绿}, B={蓝}。")
        断言(红 < 255, $"BT.2390 高光被直接裁剪：R={红}。")
    End Sub

    Private Sub 忽略纹理回收(忽略 As ID3D11Texture2D)
    End Sub

    Private Sub 验证MKV(路径 As String)
        Dim 信息 As New FileInfo(路径)
        断言(信息.Exists AndAlso 信息.Length > 1024, "没有生成有效的 MKV 文件。")
        Dim 文件头(3) As Byte
        Using 流 = File.OpenRead(路径)
            断言(流.Read(文件头, 0, 文件头.Length) = 文件头.Length, "MKV 文件头不完整。")
        End Using
        断言(文件头.SequenceEqual({&H1A, &H45, &HDF, &HA3}), "MKV EBML 文件头无效。")
    End Sub

    Private Sub 验证诊断日志(路径 As String)
        断言(File.Exists(路径), "录制诊断日志不存在。")
        Using 文档 = JsonDocument.Parse(File.ReadAllText(路径))
            Dim 有开始 As Boolean
            Dim 有停止 As Boolean
            For Each 项目 In 文档.RootElement.EnumerateArray()
                Dim 名称 = 项目.GetProperty("event").GetString()
                有开始 = 有开始 OrElse String.Equals(名称, "start", StringComparison.Ordinal)
                有停止 = 有停止 OrElse String.Equals(名称, "stop", StringComparison.Ordinal)
            Next
            断言(有开始 AndAlso 有停止, "诊断日志缺少 start 或 stop 事件。")
        End Using
    End Sub

    Private Sub 断言(条件 As Boolean, 消息 As String)
        If Not 条件 Then Throw New InvalidOperationException(消息)
    End Sub
End Module
