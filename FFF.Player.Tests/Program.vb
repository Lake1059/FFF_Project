Imports System.Diagnostics
Imports System.Drawing
Imports System.IO
Imports System.Reflection
Imports System.Runtime.InteropServices
Imports System.Text
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player
Imports Microsoft.Win32

Friend Module Program
    Private Const 测量秒数 As Double = 12.0
    Private Const 目标帧率 As Double = 24000.0 / 1001.0
    Private Const 音频缓冲平均上限毫秒 As Double = 200.0
    Private Const 音频缓冲峰值上限毫秒 As Double = 300.0

    <StructLayout(LayoutKind.Sequential)>
    Private Structure 原生色彩变换
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 色彩模式 As UInteger
        Public 传递函数 As UInteger
        Public 源是BT2020 As UInteger
        Public 保留 As UInteger
        Public 输入红 As Single
        Public 输入绿 As Single
        Public 输入蓝 As Single
        Public SDR峰值尼特 As Single
        Public 源峰值尼特 As Single
        Public 纸白尼特 As Single
        Public 输出红 As Single
        Public 输出绿 As Single
        Public 输出蓝 As Single
    End Structure

    <StructLayout(LayoutKind.Sequential)>
    Private Structure 原生HDR处理探针
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 传递函数 As UInteger
        Public 杜比配置档次 As UInteger
        Public 杜比级别 As UInteger
        Public 杜比兼容标识 As UInteger
        Public 有RPU As UInteger
        Public 有增强层 As UInteger
        Public 杜比残差 As UInteger
        Public 有HDR10Plus As UInteger
        Public 有HDRVivid As UInteger
        Public 显示峰值 As Single
        Public 显示全屏峰值 As Single
        Public 目标峰值覆盖 As Single
        Public 输出格式 As UInteger
        Public 输出兼容格式 As UInteger
        Public 输出处理路径 As UInteger
        Public 输出增强层 As UInteger
        Public 输出动态元数据 As UInteger
        Public 输出回退 As UInteger
        Public 输出目标峰值 As UInteger
    End Structure

    <StructLayout(LayoutKind.Sequential)>
    Private Structure 原生定时文字栅格诊断
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 描边宽度 As Single
        Public 阴影X偏移 As Single
        Public 阴影Y偏移 As Single
        Public 几何笔宽 As Single
        Public 左外扩 As Single
        Public 上外扩 As Single
        Public 右外扩 As Single
        Public 下外扩 As Single
        Public 阴影角度 As Single
        Public 自然对称渲染 As UInteger
        Public 灰度抗锯齿 As UInteger
        Public 禁用像素吸附 As UInteger
        Public 仅向外描边 As UInteger
    End Structure

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Private Function FFF3FP_EvaluateColorTransform(ByRef 变换 As 原生色彩变换) As Integer
    End Function

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Private Function FFF3FP_EvaluateHdrProcessing(ByRef 探针 As 原生HDR处理探针) As Integer
    End Function

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Private Function FFF3FP_EvaluateTimedTextRasterization(ByRef 诊断 As 原生定时文字栅格诊断) As Integer
    End Function

    <STAThread>
    Public Function Main(参数 As String()) As Integer
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2)
        Try
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--hdr-processing-regression", StringComparison.OrdinalIgnoreCase) Then
                测试HDR规格处理策略()
                Console.WriteLine("HDR10/HDR10+/HLG/Vivid、Dolby Profile/FEL 回退与显示峰值策略通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--bt2390-regression", StringComparison.OrdinalIgnoreCase) Then
                测试BT2390数值映射()
                Console.WriteLine("BT.2390 EETF 暗部保留、连续性、单调性与峰值映射通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--audio-latency-regression", StringComparison.OrdinalIgnoreCase) Then
                测试音频延迟回归()
                Console.WriteLine("无画面 PCM 音频延迟与欠载回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--audio-cover-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 音频路径 = Path.GetFullPath(参数(1))
                检查文件(音频路径)
                测试纯音频封面回归(音频路径)
                Console.WriteLine("纯音频封面无窗口打开与延迟绑定回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--cover-backdrop-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 音频路径 = Path.GetFullPath(参数(1))
                检查文件(音频路径)
                测试封面模糊背景(音频路径)
                Console.WriteLine("音乐封面全画布模糊背景与黑色遮罩回归通过。")
                Return 0
            End If
            If 参数.Length = 4 AndAlso String.Equals(参数(0), "--lyrics-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 歌词路径 = Path.GetFullPath(参数(1))
                Dim 封面音频路径 = Path.GetFullPath(参数(2))
                Dim 无封面音频路径 = Path.GetFullPath(参数(3))
                检查文件(歌词路径)
                检查文件(封面音频路径)
                检查文件(无封面音频路径)
                测试LRC歌词回归(歌词路径, 封面音频路径, 无封面音频路径)
                Console.WriteLine("LRC 解析、多语言分组、平滑滚动及有无封面布局回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--timed-text-regression", StringComparison.OrdinalIgnoreCase) Then
                测试定时文字精确渲染合同()
                Console.WriteLine("弹幕边界、连续小数位移、Seek、外描边与软阴影精确诊断通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--settings-size-visibility-regression", StringComparison.OrdinalIgnoreCase) Then
                测试初始画面尺寸DPI缩放()
                测试自定义初始尺寸可见性()
                测试自定义HDR峰值可见性()
                测试字幕弹幕设置与字号单位()
                Console.WriteLine("DPI 尺寸、设置控件布局、HDR、字幕/弹幕设置接线及字号 point 单位回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--file-association-regression", StringComparison.OrdinalIgnoreCase) Then
                测试文件关联注册表()
                Console.WriteLine("文件关联分组、程序路径更新及原关联恢复回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--gpu-decode-matrix", StringComparison.OrdinalIgnoreCase) Then
                测试GPU解码矩阵(Path.GetFullPath(参数(1)))
                Console.WriteLine("GPU 解码规格接受与 CPU 回退矩阵通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--video-scaling-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 缩放测试路径 = Path.GetFullPath(参数(1))
                检查文件(缩放测试路径)
                测试视频缩放路径(缩放测试路径)
                Console.WriteLine("GPU 与 CPU 解码统一自有高质量缩放路径通过。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--sdr-pixel-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 媒体路径 = Path.GetFullPath(参数(1))
                Dim 参考图路径 = Path.GetFullPath(参数(2))
                检查文件(媒体路径)
                检查文件(参考图路径)
                测试SDR灰阶像素(媒体路径, 参考图路径)
                Console.WriteLine("SDR GPU 与 CPU 的自有缩放像素回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--sdr-desktop-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 图像路径 = Path.GetFullPath(参数(1))
                检查文件(图像路径)
                测试SDR桌面像素(图像路径)
                Console.WriteLine("SDR 图像后缓冲与桌面截取像素回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--static-image-open-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 图像路径 = Path.GetFullPath(参数(1))
                检查文件(图像路径)
                测试图片打开首帧(图像路径)
                Console.WriteLine("GPU 配置下图片打开即显示首帧回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--stream-selector-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 流媒体路径 = Path.GetFullPath(参数(1))
                检查文件(流媒体路径)
                测试流选择器后端(流媒体路径)
                Console.WriteLine("流元数据、音轨切换、内嵌字幕加载和外部字幕扫描回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--external-subtitle-scan-regression", StringComparison.OrdinalIgnoreCase) Then
                测试外部字幕扫描顺序()
                Console.WriteLine("外部字幕完整扫描、同名过滤和后缀优先级回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--track-switch-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 轨道媒体路径 = Path.GetFullPath(参数(1))
                检查文件(轨道媒体路径)
                测试全部音轨切换(轨道媒体路径)
                测试内嵌字幕切换延迟(轨道媒体路径)
                Console.WriteLine("全部音轨播放中切换与内嵌字幕延迟回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--subtitle-switch-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 字幕媒体路径 = Path.GetFullPath(参数(1))
                检查文件(字幕媒体路径)
                测试内嵌字幕切换延迟(字幕媒体路径)
                Console.WriteLine("内嵌字幕切换延迟回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--sup-timeline-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim SUP路径 = Path.GetFullPath(参数(1))
                检查文件(SUP路径)
                测试SUP时间轴(SUP路径)
                Console.WriteLine("SUP 原始 PTS 与跳转后时间轴回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--clip-focus-regression", StringComparison.OrdinalIgnoreCase) Then
                测试剪辑模式焦点()
                Console.WriteLine("剪辑模式键盘焦点与出入点保留回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--volume-interaction-regression", StringComparison.OrdinalIgnoreCase) Then
                测试音量交互()
                Console.WriteLine("音量滑块、画面滚轮与操作提示合并回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--unknown-duration-progress-regression", StringComparison.OrdinalIgnoreCase) Then
                测试未知时长进度条()
                Console.WriteLine("未知时长进度推进、已知范围回拖与总时长恢复回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--unknown-duration-media-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 无时间轴媒体路径 = Path.GetFullPath(参数(1))
                检查文件(无时间轴媒体路径)
                测试无时间轴媒体播放(无时间轴媒体路径)
                Console.WriteLine("无时间轴媒体的合成时钟与播放进度回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--information-overlay-regression", StringComparison.OrdinalIgnoreCase) Then
                测试信息层交互与文本()
                Console.WriteLine("媒体信息按钮左右键与信息层逐字段精确回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--empty-layer-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 调度视频路径 = Path.GetFullPath(参数(1))
                检查文件(调度视频路径)
                测试空图层与真实呈现调度(调度视频路径)
                Console.WriteLine("空图层、真实 Present 计数与音频背压回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--hdr-switch-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim HDR视频路径 = Path.GetFullPath(参数(1))
                检查文件(HDR视频路径)
                测试播放中HDR交换链切换(HDR视频路径)
                Console.WriteLine("播放中 HDR/SDR 交换链切换与定时文字图层持续呈现回归通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--decoder-switch-audio-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 门控视频路径 = Path.GetFullPath(参数(1))
                检查文件(门控视频路径)
                测试解码切换可见首帧音频门控(门控视频路径)
                Console.WriteLine("CPU/GPU 候选会话在可见首帧前保持音频门控回归通过。")
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--ass-render-benchmark", StringComparison.OrdinalIgnoreCase) Then
                测试ASS渲染性能()
                Return 0
            End If
            If 参数.Length = 1 AndAlso String.Equals(参数(0), "--ass-encoding-regression", StringComparison.OrdinalIgnoreCase) Then
                测试ASS编码兼容()
                Console.WriteLine("ASS 的 Unicode 与常见旧式代码页统一转 UTF-8 回归通过。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--ass-file-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim ASS字幕路径 = Path.GetFullPath(参数(1))
                Dim 时间毫秒 = Long.Parse(参数(2), Globalization.CultureInfo.InvariantCulture)
                检查文件(ASS字幕路径)
                测试ASS文件(ASS字幕路径, TimeSpan.FromMilliseconds(时间毫秒))
                Console.WriteLine("指定 ASS 文件已成功打开并在目标时间生成字幕帧。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--vcb-ass-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim ASS视频路径 = Path.GetFullPath(参数(1))
                Dim ASS字幕路径 = Path.GetFullPath(参数(2))
                检查文件(ASS视频路径)
                检查文件(ASS字幕路径)
                测试ASS特效字幕(ASS视频路径, ASS字幕路径)
                Console.WriteLine("ASS/SSA libass 特效、媒体字体与资源释放回归全部通过。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--targeted-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 专项视频路径 = Path.GetFullPath(参数(1))
                Dim 专项SUP路径 = Path.GetFullPath(参数(2))
                检查文件(专项视频路径)
                检查文件(专项SUP路径)
                测试音频规格回归(专项视频路径)
                测试播放中字幕替换(专项视频路径, 专项SUP路径)
                测试播放中弹幕替换(专项视频路径)
                Console.WriteLine("连续 PCM 音频与播放中字幕/弹幕替换专项回归全部通过。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--performance-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim SDR路径 = Path.GetFullPath(参数(1))
                Dim HDR路径 = Path.GetFullPath(参数(2))
                检查文件(SDR路径)
                检查文件(HDR路径)
                测试性能回归(SDR路径, HDR路径)
                Console.WriteLine("解码、渲染、字幕/弹幕和音频性能回归全部通过。")
                Return 0
            End If
            If 参数.Length = 3 AndAlso String.Equals(参数(0), "--color-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim SDR路径 = Path.GetFullPath(参数(1))
                Dim HDR路径 = Path.GetFullPath(参数(2))
                检查文件(SDR路径)
                检查文件(HDR路径)
                测试色彩回归(SDR路径, HDR路径)
                Console.WriteLine("SDR/HDR 色彩回归测试全部通过。")
                Return 0
            End If
            If 参数.Length = 2 AndAlso String.Equals(参数(0), "--clip-step-regression", StringComparison.OrdinalIgnoreCase) Then
                Dim 逐帧视频路径 = Path.GetFullPath(参数(1))
                检查文件(逐帧视频路径)
                测试剪辑区间逐帧(逐帧视频路径)
                Console.WriteLine("剪辑区间前后逐帧回归通过。")
                Return 0
            End If
            If 参数.Length < 2 Then
                Console.Error.WriteLine("用法: FFF.Player.Tests <视频.mp4> <弹幕.xml> [字幕.ass] [字幕.srt]")
                Console.Error.WriteLine("   或: FFF.Player.Tests --audio-latency-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --audio-cover-regression <带内嵌封面的纯音频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --cover-backdrop-regression <带 0xC80000 纯色封面的音频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --lyrics-regression <歌词.lrc> <带封面音频> <无封面音频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --color-regression <SDR视频> <HDR视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --performance-regression <SDR视频> <HDR视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --targeted-regression <视频> <字幕.sup>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --vcb-ass-regression <视频> <字幕.ass>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --clip-step-regression <视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --clip-focus-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --volume-interaction-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --unknown-duration-progress-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --unknown-duration-media-regression <无时间轴视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --information-overlay-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --empty-layer-regression <视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --hdr-switch-regression <HDR视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --decoder-switch-audio-regression <视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --hdr-processing-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --bt2390-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --gpu-decode-matrix <视频目录>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --video-scaling-regression <视频>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --sdr-pixel-regression <视频> <参考图.png>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --sdr-desktop-regression <参考图.png>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --static-image-open-regression <图片>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --stream-selector-regression <多流媒体>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --external-subtitle-scan-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --track-switch-regression <多音轨媒体>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --subtitle-switch-regression <多字幕媒体>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --sup-timeline-regression <字幕.sup>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --ass-render-benchmark")
                Console.Error.WriteLine("   或: FFF.Player.Tests --ass-encoding-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --ass-file-regression <字幕.ass> <时间毫秒>")
                Console.Error.WriteLine("   或: FFF.Player.Tests --timed-text-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --settings-size-visibility-regression")
                Console.Error.WriteLine("   或: FFF.Player.Tests --file-association-regression")
                Return 2
            End If
            Dim 视频路径 = Path.GetFullPath(参数(0))
            Dim 弹幕路径 = Path.GetFullPath(参数(1))
            Dim ASS路径 = 参数.Skip(2).FirstOrDefault(Function(x) Path.GetExtension(x).Equals(".ass", StringComparison.OrdinalIgnoreCase) OrElse
                                                        Path.GetExtension(x).Equals(".ssa", StringComparison.OrdinalIgnoreCase))
            Dim SRT路径 = 参数.Skip(2).FirstOrDefault(Function(x) Path.GetExtension(x).Equals(".srt", StringComparison.OrdinalIgnoreCase))
            If Not String.IsNullOrWhiteSpace(ASS路径) Then ASS路径 = Path.GetFullPath(ASS路径)
            If Not String.IsNullOrWhiteSpace(SRT路径) Then SRT路径 = Path.GetFullPath(SRT路径)
            检查文件(视频路径)
            检查文件(弹幕路径)
            If Not String.IsNullOrWhiteSpace(ASS路径) Then 检查文件(ASS路径)
            If Not String.IsNullOrWhiteSpace(SRT路径) Then 检查文件(SRT路径)

            If Not String.IsNullOrWhiteSpace(ASS路径) AndAlso Not String.IsNullOrWhiteSpace(SRT路径) Then
                测试字幕(视频路径, ASS路径, SRT路径)
            End If
            Dim 弹幕资料库 = B站弹幕解析器.解析文件(弹幕路径)
            Dim 弹幕测试位置 = 测试弹幕(视频路径, 弹幕资料库)
            Dim CPU结果 = 测试播放(视频路径, SRT路径, Nothing, TimeSpan.Zero, 解码模式.CPU)
            输出播放报告(解码模式.CPU, CPU结果)
            验证播放结果(CPU结果, "CPU 顺播")
            Dim GPU结果 = 测试播放(视频路径, ASS路径, 弹幕资料库, 弹幕测试位置, 解码模式.GPU)
            输出播放报告(解码模式.GPU, GPU结果)
            验证播放结果(GPU结果, "GPU 顺播")
            Console.WriteLine("全部诊断测试通过。")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine($"测试失败：{ex.Message}")
            Return 1
        End Try
    End Function

    Private Sub 测试文件关联注册表()
        Dim 默认设置 As New 设置()
        断言(Not 默认设置.关联常见视频 AndAlso Not 默认设置.关联不常见视频 AndAlso
               Not 默认设置.关联老旧视频 AndAlso Not 默认设置.关联常见音频 AndAlso
               Not 默认设置.关联不常见音频 AndAlso Not 默认设置.关联老旧音频,
               "文件关联默认值不是全部关闭。")
        断言(文件关联管理器.取得扩展名(文件关联类别.常见视频).Contains(".mp4") AndAlso
               文件关联管理器.取得扩展名(文件关联类别.常见音频).Contains(".mp3") AndAlso
               文件关联管理器.取得扩展名(文件关联类别.老旧视频).Contains(".rmvb") AndAlso
               文件关联管理器.取得扩展名(文件关联类别.老旧音频).Contains(".wma"),
               "文件关联扩展名没有按媒体类型和常用程度分组。")
        Using page As New Form设置_文件关联()
            page.PerformLayout()
            Dim checkBoxes = {page.MCK_关联常见视频, page.MCK_关联不常见视频, page.MCK_关联老旧视频,
                              page.MCK_关联常见音频, page.MCK_关联不常见音频, page.MCK_关联老旧音频}
            断言(checkBoxes.All(Function(x) Not String.IsNullOrWhiteSpace(x.SubText)),
                   "文件关联页没有在运行时填写扩展名副文本。")
            断言(checkBoxes.All(Function(x) Not x.Checked), "文件关联页的复选框不是默认全部关闭。")
            断言(checkBoxes.All(Function(x) x.Height > 34), "扩展名副文本没有撑开文件关联选项高度。")
        End Using

        Dim testRoot = "Software\1059 Studio\FFF.Player.Tests-" & Guid.NewGuid().ToString("N")
        Dim classesRoot = testRoot & "\Classes"
        Dim applicationRoot = testRoot & "\Application"
        Dim registeredApplications = testRoot & "\RegisteredApplications"
        Dim locations As New 文件关联注册表位置(classesRoot, applicationRoot, registeredApplications)
        Dim executablePath = Environment.ProcessPath
        If String.IsNullOrWhiteSpace(executablePath) Then executablePath = Assembly.GetExecutingAssembly().Location
        断言(String.Equals(Form1.取得命令行文件({"不存在的文件", executablePath}),
                           Path.GetFullPath(executablePath), StringComparison.OrdinalIgnoreCase),
               "关联启动参数没有筛选出第一个存在的本地文件。")
        Dim testApplication As New FFF.Player.My.MyApplication()
        testApplication.记录初次启动命令行({"不存在的文件", executablePath})
        断言(String.Equals(testApplication.取出待处理启动文件(), Path.GetFullPath(executablePath),
                           StringComparison.OrdinalIgnoreCase) AndAlso
               String.IsNullOrEmpty(testApplication.取出待处理启动文件()),
               "ApplicationEvents 没有接入或正确消费初次启动文件。")

        Try
            Using extensionKey = Registry.CurrentUser.CreateSubKey(classesRoot & "\.mp4", True)
                extensionKey.SetValue(String.Empty, "Previous.Player", RegistryValueKind.String)
                extensionKey.SetValue("Content Type", "application/x-previous", RegistryValueKind.String)
                extensionKey.SetValue("PerceivedType", "document", RegistryValueKind.String)
            End Using
            Using iconKey = Registry.CurrentUser.CreateSubKey(classesRoot & "\FFF.Player.mp4\DefaultIcon", True)
                iconKey.SetValue(String.Empty, "stale.ico,0", RegistryValueKind.String)
            End Using
            Using iconKey = Registry.CurrentUser.CreateSubKey(
                    classesRoot & "\Applications\FFF.Player.exe\DefaultIcon", True)
                iconKey.SetValue(String.Empty, "stale.ico,0", RegistryValueKind.String)
            End Using
            Using capabilities = Registry.CurrentUser.CreateSubKey(applicationRoot & "\Capabilities", True)
                capabilities.SetValue("ApplicationIcon", "stale.ico,0", RegistryValueKind.String)
            End Using
            Using thumbnailKey = Registry.CurrentUser.CreateSubKey(
                    classesRoot & "\.mp4\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}", True)
                thumbnailKey.SetValue(String.Empty, "{9DBD2C50-62AD-11D0-B806-00C04FD706EC}", RegistryValueKind.String)
            End Using
            Using backup = Registry.CurrentUser.CreateSubKey(
                    applicationRoot & "\FileAssociationBackups\mp4", True)
                backup.SetValue("ThumbnailProvider.Captured", 1, RegistryValueKind.DWord)
                backup.SetValue("ThumbnailProvider.Exists", 0, RegistryValueKind.DWord)
            End Using
            Dim enabled As New 文件关联选项 With {.关联常见视频 = True}
            文件关联管理器.同步用于测试(enabled, executablePath, locations)
            Using extensionKey = Registry.CurrentUser.OpenSubKey(classesRoot & "\.mp4")
                断言(String.Equals(CStr(extensionKey.GetValue(String.Empty)), "FFF.Player.mp4", StringComparison.Ordinal),
                       "启用后没有写入 MP4 ProgID。")
                断言(String.Equals(CStr(extensionKey.GetValue("Content Type")), "video/mp4", StringComparison.Ordinal),
                       "启用后没有写入 MP4 媒体类型。")
                断言(String.Equals(CStr(extensionKey.GetValue("PerceivedType")), "video", StringComparison.Ordinal),
                       "启用后没有写入视频感知类型。")
            End Using
            Using commandKey = Registry.CurrentUser.OpenSubKey(classesRoot & "\FFF.Player.mp4\shell\open\command")
                断言(String.Equals(CStr(commandKey.GetValue(String.Empty)), $"""{Path.GetFullPath(executablePath)}"" ""%1""",
                                     StringComparison.OrdinalIgnoreCase),
                       "打开命令没有指向当前程序路径。")
            End Using
            Using applicationKey = Registry.CurrentUser.OpenSubKey(
                    classesRoot & "\Applications\FFF.Player.exe\shell\open\command")
                断言(applicationKey IsNot Nothing AndAlso
                       String.Equals(CStr(applicationKey.GetValue(String.Empty)),
                                     $"""{Path.GetFullPath(executablePath)}"" ""%1""",
                                     StringComparison.OrdinalIgnoreCase),
                       "播放器没有注册标准的打开方式命令。")
            End Using
            Using supportedTypes = Registry.CurrentUser.OpenSubKey(
                    classesRoot & "\Applications\FFF.Player.exe\SupportedTypes")
                断言(supportedTypes IsNot Nothing AndAlso supportedTypes.GetValueNames().Contains(".mp4"),
                       "播放器的打开方式注册没有声明已启用的扩展名。")
            End Using
            断言(Registry.CurrentUser.OpenSubKey(classesRoot & "\FFF.Player.mp4\DefaultIcon") Is Nothing,
                   "启用关联后仍保留 ProgID 图标设置。")
            断言(Registry.CurrentUser.OpenSubKey(
                       classesRoot & "\Applications\FFF.Player.exe\DefaultIcon") Is Nothing,
                   "启用关联后仍保留应用图标设置。")
            Using capabilities = Registry.CurrentUser.OpenSubKey(applicationRoot & "\Capabilities")
                断言(Not capabilities.GetValueNames().Contains("ApplicationIcon"),
                       "默认应用能力中仍保留图标设置。")
            End Using
            Using thumbnailKey = Registry.CurrentUser.OpenSubKey(
                    classesRoot & "\.mp4\ShellEx\{e357fccd-a995-4576-b01f-234630154e96}")
                断言(thumbnailKey IsNot Nothing AndAlso thumbnailKey.GetValue(String.Empty) Is Nothing,
                       "启用关联后仍保留缩略图处理器设置。")
            End Using
            Using registered = Registry.CurrentUser.OpenSubKey(registeredApplications)
                断言(String.Equals(CStr(registered.GetValue("FFF.Player")), locations.CapabilitiesRoot,
                                     StringComparison.OrdinalIgnoreCase),
                       "播放器没有注册到 Windows 默认应用列表。")
            End Using

            Dim secondExecutable = Path.Combine(Environment.SystemDirectory, "cmd.exe")
            文件关联管理器.同步用于测试(enabled, secondExecutable, locations)
            Using commandKey = Registry.CurrentUser.OpenSubKey(classesRoot & "\FFF.Player.mp4\shell\open\command")
                断言(String.Equals(CStr(commandKey.GetValue(String.Empty)), $"""{secondExecutable}"" ""%1""",
                                     StringComparison.OrdinalIgnoreCase),
                       "程序路径变化后没有修复打开命令。")
            End Using

            文件关联管理器.同步用于测试(New 文件关联选项(), secondExecutable, locations)
            Using extensionKey = Registry.CurrentUser.OpenSubKey(classesRoot & "\.mp4")
                断言(String.Equals(CStr(extensionKey.GetValue(String.Empty)), "Previous.Player", StringComparison.Ordinal),
                       "取消关联后没有恢复原 ProgID。")
                断言(String.Equals(CStr(extensionKey.GetValue("Content Type")), "application/x-previous", StringComparison.Ordinal),
                       "取消关联后没有恢复原媒体类型。")
                断言(String.Equals(CStr(extensionKey.GetValue("PerceivedType")), "document", StringComparison.Ordinal),
                       "取消关联后没有恢复原感知类型。")
            End Using
            Using progIdKey = Registry.CurrentUser.OpenSubKey(classesRoot & "\FFF.Player.mp4")
                断言(progIdKey Is Nothing, "取消关联后仍残留本程序的 ProgID。")
            End Using
            Using capabilities = Registry.CurrentUser.OpenSubKey(locations.CapabilitiesRoot)
                断言(capabilities Is Nothing, "全部取消关联后仍残留默认应用能力注册。")
            End Using
            Using applicationKey = Registry.CurrentUser.OpenSubKey(
                    classesRoot & "\Applications\FFF.Player.exe")
                断言(applicationKey Is Nothing, "全部取消关联后仍残留打开方式注册。")
            End Using
        Finally
            Registry.CurrentUser.DeleteSubKeyTree(testRoot, False)
        End Try
    End Sub

    Private Sub 测试初始画面尺寸DPI缩放()
        Dim 默认设置 = New 设置()
        断言(默认设置.初始画面尺寸选项 = 2 AndAlso
               默认设置.取得初始画面尺寸() = New Size(854, 480),
           "初始画面尺寸默认值不是 854x480。")

        Dim 逻辑尺寸 = New Size(1366, 768)
        断言(播放器窗口布局控制器.按DPI缩放画面尺寸(逻辑尺寸, 96) = 逻辑尺寸,
           "100% DPI 下初始画面尺寸不应变化。")
        断言(播放器窗口布局控制器.按DPI缩放画面尺寸(逻辑尺寸, 120) = New Size(1708, 960),
           "125% DPI 下初始画面尺寸换算或取整错误。")
        断言(播放器窗口布局控制器.按DPI缩放画面尺寸(逻辑尺寸, 144) = New Size(2049, 1152),
           "150% DPI 下初始画面尺寸换算错误。")
        断言(播放器窗口布局控制器.按DPI缩放画面尺寸(逻辑尺寸, 192) = New Size(2732, 1536),
           "200% DPI 下初始画面尺寸换算错误。")
        Dim 副屏工作区 = New Rectangle(-1920, 40, 1920, 1040)
        断言(播放器窗口布局控制器.计算工作区居中边界(New Size(854, 480), 副屏工作区) =
               New Rectangle(-1387, 320, 854, 480),
           "最终窗口没有在目标屏幕工作区内居中。")

        Dim 原尺寸选项 = 设置.实例对象.初始画面尺寸选项
        Try
            Dim 预设期望 = {
                (索引:=1, 尺寸:=New Size(640, 360)),
                (索引:=2, 尺寸:=New Size(854, 480)),
                (索引:=3, 尺寸:=New Size(960, 540)),
                (索引:=4, 尺寸:=New Size(1024, 576))
            }
            For Each 预设 In 预设期望
                设置.实例对象.初始画面尺寸选项 = 预设.索引
                断言(设置.实例对象.取得初始画面尺寸() = 预设.尺寸,
                   $"初始画面尺寸预设 {预设.索引} 映射错误。")
            Next
        Finally
            设置.实例对象.初始画面尺寸选项 = 原尺寸选项
        End Try
    End Sub

    Private Sub 测试自定义初始尺寸可见性()
        Dim 原尺寸选项 = 设置.实例对象.初始画面尺寸选项
        Try
            Using 页面 As New Form设置_界面与尺寸 With {.ShowInTaskbar = False, .Opacity = 0}
                页面.Show()
                Application.DoEvents()
                Dim 尺寸选项 = DirectCast(页面.Controls.Find("MCB_初始画面尺寸选项", True).FirstOrDefault(),
                                      LakeUI.ModernComboBox)
                Dim 宽度输入框 = 页面.Controls.Find("MTB_自定义初始画面尺寸宽度", True).FirstOrDefault()
                Dim 高度输入框 = 页面.Controls.Find("MTB_自定义初始画面尺寸高度", True).FirstOrDefault()
                Dim 第一间隔 = 页面.Controls.Find("JustEmptyControl1", True).FirstOrDefault()
                Dim 第二间隔 = 页面.Controls.Find("JustEmptyControl2", True).FirstOrDefault()
                断言(尺寸选项 IsNot Nothing AndAlso 宽度输入框 IsNot Nothing AndAlso 高度输入框 IsNot Nothing AndAlso
                       第一间隔 IsNot Nothing AndAlso 第二间隔 IsNot Nothing,
                   "无法取得初始画面尺寸设置控件。")
                断言(尺寸选项.Items.Count = 10 AndAlso
                       尺寸选项.Items(1).ToString() = "640x360" AndAlso
                       尺寸选项.Items(2).ToString() = "854x480" AndAlso
                       尺寸选项.Items(3).ToString() = "960x540" AndAlso
                       尺寸选项.Items(4).ToString() = "1024x576",
                   "初始画面尺寸下拉框的小尺寸预设或排列顺序错误。")

                尺寸选项.SelectedIndex = 1
                Application.DoEvents()
                断言(设置.实例对象.初始画面尺寸选项 = 1,
                   "640x360 下拉项没有写入对应的预设编号。")
                断言(Not 宽度输入框.Visible AndAlso Not 高度输入框.Visible AndAlso
                       Not 第一间隔.Visible AndAlso Not 第二间隔.Visible,
                   "选择预设尺寸后自定义宽高输入框或间隔仍然可见。")

                尺寸选项.SelectedIndex = 0
                Application.DoEvents()
                断言(宽度输入框.Visible AndAlso 高度输入框.Visible AndAlso
                       第一间隔.Visible AndAlso 第二间隔.Visible,
                   "选择自定义尺寸后宽高输入框或间隔没有显示。")
                断言(尺寸选项.Right = 第一间隔.Left AndAlso
                       第一间隔.Right = 宽度输入框.Left AndAlso
                       宽度输入框.Right = 第二间隔.Left AndAlso
                       第二间隔.Right = 高度输入框.Left,
                   $"自定义尺寸控件重显后的 Dock 顺序错误：选项={尺寸选项.Bounds}，" &
                   $"间隔1={第一间隔.Bounds}，宽度={宽度输入框.Bounds}，" &
                   $"间隔2={第二间隔.Bounds}，高度={高度输入框.Bounds}。")
            End Using
        Finally
            设置.实例对象.初始画面尺寸选项 = 原尺寸选项
        End Try
    End Sub

    Private Sub 测试自定义HDR峰值可见性()
        Dim 原峰值选项 = 设置.实例对象.HDR峰值亮度选项
        Try
            Using 页面 As New Form设置_HDR With {.ShowInTaskbar = False, .Opacity = 0}
                页面.Show()
                Application.DoEvents()
                Dim 峰值选项 = DirectCast(页面.Controls.Find("MCB_真实HDR峰值亮度选项", True).FirstOrDefault(),
                                       LakeUI.ModernComboBox)
                Dim 峰值输入框 = 页面.Controls.Find("MTB_自定义真实HDR峰值亮度", True).FirstOrDefault()
                Dim 峰值间隔 = 页面.Controls.Find("JustEmptyControl1", True).FirstOrDefault()
                断言(峰值选项 IsNot Nothing AndAlso 峰值输入框 IsNot Nothing AndAlso 峰值间隔 IsNot Nothing,
                   "无法取得 HDR 峰值亮度设置控件。")

                峰值选项.SelectedIndex = 0
                Application.DoEvents()
                断言(Not 峰值输入框.Visible AndAlso Not 峰值间隔.Visible,
                   "选择 HDR 预设后自定义峰值输入框或间隔仍然可见。")

                峰值选项.SelectedIndex = 1
                Application.DoEvents()
                断言(峰值输入框.Visible AndAlso 峰值间隔.Visible,
                   "选择自定义 HDR 峰值后输入框或间隔没有显示。")
            End Using
        Finally
            设置.实例对象.HDR峰值亮度选项 = 原峰值选项
        End Try
    End Sub

    Private Sub 测试字幕弹幕设置与字号单位()
        Dim 原设置 = 设置.实例对象
        设置.实例对象 = New 设置()
        Try
            Using Pixel字体 As New Font("Microsoft YaHei UI", 64.0F, FontStyle.Bold, GraphicsUnit.Pixel)
                Dim 字体名称 = String.Empty
                Dim 字号 = 0.0F
                Dim 样式 = 0
                字体控制.写入选择结果(Pixel字体, 字体名称, 字号, 样式)
                断言(Math.Abs(字号 - Pixel字体.SizeInPoints) < 0.001F AndAlso
                       Math.Abs(字号 - Pixel字体.Size) > 1.0F,
                   $"Pixel 字体仍按 Size 持久化：{Pixel字体.Size:F3}px -> {字号:F3}pt。")
                断言(样式 = CInt(FontStyle.Bold), "字体样式没有从字体对话框结果写回。")
            End Using

            Using 字幕页 As New Form设置_字幕()
                字幕页.初始化页面()
                Dim 不透明度 = DirectCast(字幕页.Controls.Find("ETB_字幕不透明度", True).Single(), LakeUI.ExcellentTrackBar)
                Dim 描边 = DirectCast(字幕页.Controls.Find("MCB_字幕描边样式", True).Single(), LakeUI.ModernComboBox)
                Dim 阴影 = DirectCast(字幕页.Controls.Find("MCB_字幕阴影样式", True).Single(), LakeUI.ModernComboBox)
                不透明度.Value = 137
                描边.SelectedIndex = 2
                阴影.SelectedIndex = 0
                断言(设置.实例对象.字幕不透明度 = 137 AndAlso
                       设置.实例对象.字幕描边样式 = 2 AndAlso 设置.实例对象.字幕阴影样式 = 0,
                   "字幕页新增的不透明度、描边或阴影设置没有写入配置。")
            End Using

            Using 弹幕页 As New Form设置_弹幕()
                弹幕页.初始化页面()
                Dim 不透明度 = DirectCast(弹幕页.Controls.Find("ETB_弹幕不透明度", True).Single(), LakeUI.ExcellentTrackBar)
                Dim 最大数量 = DirectCast(弹幕页.Controls.Find("ETB_弹幕最大渲染数量", True).Single(), LakeUI.ExcellentTrackBar)
                Dim 底部开关 = DirectCast(弹幕页.Controls.Find("MCK_是否渲染底部弹幕", True).Single(), LakeUI.ModernCheckBox)
                Dim 阴影 = DirectCast(弹幕页.Controls.Find("MCB_弹幕阴影样式", True).Single(), LakeUI.ModernComboBox)
                断言(阴影.Items.Count = 3 AndAlso
                       阴影.Items(0).ToString() = "不使用阴影" AndAlso
                       阴影.Items(1).ToString() = "使用基础阴影" AndAlso
                       阴影.Items(2).ToString() = "使用更深阴影",
                    "弹幕阴影下拉框文案没有保持原有表述。")
                不透明度.Value = 149
                最大数量.Value = 77
                底部开关.Checked = False
                断言(设置.实例对象.弹幕不透明度 = 149 AndAlso
                       设置.实例对象.弹幕最大渲染数量 = 77 AndAlso
                       Not 设置.实例对象.渲染底部弹幕,
                   "弹幕页新增设置没有写入配置。")
            End Using

            Dim 字幕配置 = 设置.实例对象.创建SRT字幕样式()
            Dim 弹幕配置 = 设置.实例对象.创建弹幕显示配置()
            断言((字幕配置.第一行颜色ARGB >> 24) = 137UI AndAlso 字幕配置.描边宽度 = 4.0F AndAlso
                   字幕配置.阴影颜色ARGB = 0UI,
               "字幕设置没有进入实际渲染样式。")
            断言(弹幕配置.不透明度 = 149 AndAlso 弹幕配置.同屏最大数量 = 77 AndAlso
                   (弹幕配置.启用类型 And 弹幕类型.底部) = 0,
               "弹幕设置没有进入实际调度/渲染配置。")
            断言((设置.应用不透明度(&HC0000000UI, 128) >> 24) = 96UI,
               "全局不透明度没有按源 alpha 成比例合成。")
        Finally
            设置.实例对象 = 原设置
        End Try
    End Sub

    Private Sub 测试HDR规格处理策略()
        Using 控制器 As New 播放器控制器(Function() IntPtr.Zero, Nothing)
            控制器.设置HDR峰值亮度(2000.0F)
            断言(控制器.取得HDR输出峰值参数(色彩输出模式.映射到SDR) = 0.0F AndAlso
                   控制器.取得HDR输出峰值参数(色彩输出模式.原始HDR按SDR呈现) = 0.0F,
               "真实 HDR 峰值设置泄漏到了 SDR 映射路径。")
            断言(控制器.取得HDR输出峰值参数(色彩输出模式.峰值映射HDR) = 2000.0F,
               "真实 HDR 输出没有使用用户设置的目标峰值。")
        End Using

        Dim P5 = 执行HDR探针(1UI, 5UI, 9UI, 0UI, True, False, 0UI, False, False, 720.0F, 0.0F)
        断言(P5.输出格式 = 4UI AndAlso P5.输出处理路径 = 4UI AndAlso
           P5.输出兼容格式 = 4UI AndAlso P5.输出回退 = 1UI AndAlso
           P5.输出动态元数据 = 0UI,
           "Dolby Vision P5 没有进入未授权 HDR10 受限回退。")

        Dim P7MEL = 执行HDR探针(1UI, 7UI, 6UI, 1UI, True, True, 1UI, False, False, 720.0F, 0.0F)
        断言(P7MEL.输出格式 = 4UI AndAlso P7MEL.输出处理路径 = 4UI AndAlso
           P7MEL.输出增强层 = 1UI AndAlso P7MEL.输出回退 = 1UI AndAlso
           P7MEL.输出动态元数据 = 0UI AndAlso (P7MEL.输出兼容格式 And 1UI) <> 0,
           "Dolby Vision P7 MEL 没有使用 HDR10 基础层回退。")

        Dim P7FEL = 执行HDR探针(1UI, 7UI, 6UI, 1UI, True, True, 2UI, False, False, 720.0F, 0.0F)
        断言(P7FEL.输出处理路径 = 5UI AndAlso P7FEL.输出增强层 = 2UI AndAlso
           P7FEL.输出回退 = 1UI AndAlso P7FEL.输出动态元数据 = 0UI,
           "Dolby Vision P7 FEL 没有明确报告 BL 基础层回退。")

        Dim P81 = 执行HDR探针(1UI, 8UI, 6UI, 1UI, True, False, 0UI, False, False, 720.0F, 0.0F)
        断言((P81.输出兼容格式 And 1UI) <> 0 AndAlso P81.输出处理路径 = 4UI AndAlso
           P81.输出回退 = 1UI AndAlso P81.输出动态元数据 = 0UI,
           "Dolby Vision P8.1 没有识别 HDR10 兼容基础层。")

        Dim P84 = 执行HDR探针(2UI, 8UI, 6UI, 4UI, True, False, 0UI, False, False, 720.0F, 0.0F)
        断言((P84.输出兼容格式 And 2UI) <> 0 AndAlso (P84.输出兼容格式 And 1UI) = 0 AndAlso
           P84.输出处理路径 = 4UI AndAlso P84.输出回退 = 1UI AndAlso
           P84.输出动态元数据 = 0UI,
           "Dolby Vision P8.4 没有识别 HLG 兼容基础层。")

        Dim HDR10Plus = 执行HDR探针(1UI, 0UI, 0UI, 0UI, False, False, 0UI, True, False, 720.0F, 0.0F)
        断言(HDR10Plus.输出格式 = 2UI AndAlso HDR10Plus.输出处理路径 = 2UI AndAlso
           HDR10Plus.输出动态元数据 = 1UI AndAlso HDR10Plus.输出目标峰值 = 720UI,
           "HDR10+ 动态元数据或自动显示峰值策略错误。")

        Dim Vivid覆盖 = 执行HDR探针(1UI, 0UI, 0UI, 0UI, False, False, 0UI, False, True, 720.0F, 1250.0F)
        断言(Vivid覆盖.输出格式 = 5UI AndAlso Vivid覆盖.输出处理路径 = 6UI AndAlso
           Vivid覆盖.输出动态元数据 = 1UI AndAlso Vivid覆盖.输出目标峰值 = 1250UI,
           "HDR Vivid 或用户目标峰值覆盖策略错误。")
    End Sub

    Private Function 执行HDR探针(传递 As UInteger, 配置档次 As UInteger, 级别 As UInteger,
                             兼容标识 As UInteger, RPU As Boolean, 增强层 As Boolean,
                             残差 As UInteger, HDR10Plus As Boolean, Vivid As Boolean,
                             显示峰值 As Single, 覆盖 As Single) As 原生HDR处理探针
        Dim 探针 As New 原生HDR处理探针 With {
            .大小 = CUInt(Marshal.SizeOf(Of 原生HDR处理探针)()), .版本 = 1UI,
            .传递函数 = 传递, .杜比配置档次 = 配置档次, .杜比级别 = 级别,
            .杜比兼容标识 = 兼容标识, .有RPU = If(RPU, 1UI, 0UI),
            .有增强层 = If(增强层, 1UI, 0UI), .杜比残差 = 残差,
            .有HDR10Plus = If(HDR10Plus, 1UI, 0UI), .有HDRVivid = If(Vivid, 1UI, 0UI),
            .显示峰值 = 显示峰值, .显示全屏峰值 = 显示峰值 * 0.6F,
            .目标峰值覆盖 = 覆盖}
        Dim 结果 = FFF3FP_EvaluateHdrProcessing(探针)
        断言(结果 = 0, $"原生 HDR 处理探针失败：{结果}。")
        Return 探针
    End Function

    Private Sub 测试SUP时间轴(SUP路径 As String)
        Dim header(5) As Byte
        Using stream = New FileStream(SUP路径, FileMode.Open, FileAccess.Read, FileShare.Read)
            断言(stream.Read(header, 0, header.Length) = header.Length AndAlso
               header(0) = AscW("P"c) AndAlso header(1) = AscW("G"c),
               "测试文件不是有效的 PGS SUP 字幕。")
        End Using
        Dim pts90k = (CULng(header(2)) << 24) Or (CULng(header(3)) << 16) Or
                     (CULng(header(4)) << 8) Or header(5)
        Dim expected = TimeSpan.FromTicks(CLng(pts90k * CULng(TimeSpan.TicksPerSecond) \ 90000UL))

        Using decoder As New SUP字幕解码器(SUP路径)
            Dim first = 读取首个SUP事件(decoder)
            断言(first IsNot Nothing AndAlso Math.Abs((first.开始时间 - expected).TotalMilliseconds) <= 2.0,
               $"SUP 首条事件被错误归零或偏移：文件 PTS {expected:c}，解码结果 {If(first Is Nothing, "无", first.开始时间.ToString("c"))}。")
            decoder.跳转(TimeSpan.Zero)
            Dim afterSeek = 读取首个SUP事件(decoder)
            断言(afterSeek IsNot Nothing AndAlso Math.Abs((afterSeek.开始时间 - expected).TotalMilliseconds) <= 2.0,
               "SUP 跳转到零点后没有恢复原始时间轴。")
        End Using
    End Sub

    Private Function 读取首个SUP事件(decoder As SUP字幕解码器) As SUP字幕事件
        For attempt = 0 To 255
            Dim item = decoder.读取下一事件()
            If item Is Nothing Then Return Nothing
            If Not item.仍需读取 AndAlso Not item.是清除事件 Then Return item
        Next
        Throw New InvalidOperationException("SUP 字幕在限定读取次数内没有产生显示事件。")
    End Function

    Private Sub 测试流选择器后端(媒体路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU,
            .输出窗口句柄 = IntPtr.Zero
        })
            会话.打开Async(媒体路径).GetAwaiter().GetResult()
            Dim 信息 = 会话.当前媒体信息
            Dim 快照 = 会话.当前快照
            断言(信息 IsNot Nothing, "没有返回媒体流信息。")

            Dim 视频流 = 信息.流.Where(Function(x) x.类型 = "video" AndAlso Not x.是封面图).ToArray()
            Dim 音频流 = 信息.流.Where(Function(x) x.类型 = "audio").ToArray()
            Dim 字幕流 = 信息.流.Where(Function(x) x.类型 = "subtitle").ToArray()
            断言(视频流.Length > 0 AndAlso 音频流.Length > 1 AndAlso 字幕流.Length > 0,
               $"测试媒体流数量不足：视频/音频/字幕 {视频流.Length}/{音频流.Length}/{字幕流.Length}。")
            断言(Not String.IsNullOrWhiteSpace(视频流(0).像素格式), "视频流缺少像素格式。")
            断言(Not String.IsNullOrWhiteSpace(字幕流(0).语言) AndAlso
               Not String.IsNullOrWhiteSpace(字幕流(0).标题), "字幕流缺少语言或标题元数据。")

            Dim 目标音频 = 音频流.First(Function(x) x.索引 <> 快照.当前音频流)
            会话.选择音频流(目标音频.索引)
            Dim 计时 = Stopwatch.StartNew()
            Do
                Application.DoEvents()
                If 会话.当前快照.当前音频流 = 目标音频.索引 Then Exit Do
                If 计时.Elapsed > TimeSpan.FromSeconds(5) Then Throw New TimeoutException("切换音频流超时。")
                Thread.Sleep(5)
            Loop

            Using 字幕 = 外部字幕自动加载器.加载内嵌字幕(
                媒体路径, 字幕流(0), CancellationToken.None)
                断言(字幕.是内嵌 AndAlso 字幕.流索引 = 字幕流(0).索引,
                   "内嵌字幕轨道没有保留来源流索引。")
                If 字幕.ASS特效生成器 IsNot Nothing Then
                    Dim 帧 = 字幕.ASS特效生成器.生成帧(TimeSpan.FromMilliseconds(500), 640, 360)
                    断言(帧 IsNot Nothing AndAlso 帧.像素BGRA.Length > 0,
                       "内嵌文字字幕没有生成可见 libass 位图。")
                End If
            End Using
        End Using
        测试外部字幕扫描顺序()
    End Sub

    Private Sub 测试外部字幕扫描顺序()
        Dim 临时目录 = Path.Combine(Path.GetTempPath(), "3FP-subtitle-scan-" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(临时目录)
        Try
            Dim 媒体路径 = Path.Combine(临时目录, "演示.mkv")
            File.WriteAllBytes(媒体路径, Array.Empty(Of Byte)())
            Dim 字幕文件 = {
                "演示.zh-Hans.srt", "演示.commentary.ass", "演示.sup",
                "演示.ssa", "演示.en.srt", "演示.srt",
                "演示2.srt", "其他.ass"}
            For Each 文件名 In 字幕文件
                File.WriteAllText(Path.Combine(临时目录, 文件名), String.Empty)
            Next

            Dim 实际 = 外部字幕自动加载器.扫描同名字幕(媒体路径).
                Select(Function(x) Path.GetFileName(x.路径)).ToArray()
            Dim 预期 = {
                "演示.srt", "演示.en.srt", "演示.zh-Hans.srt",
                "演示.commentary.ass", "演示.ssa", "演示.sup"}
            断言(实际.SequenceEqual(预期, StringComparer.OrdinalIgnoreCase),
               $"外部字幕扫描顺序异常：{String.Join("、", 实际)}。")
        Finally
            If Directory.Exists(临时目录) Then Directory.Delete(临时目录, True)
        End Try
    End Sub

    Private Sub 测试全部音轨切换(媒体路径 As String)
        Dim 音轨 As 媒体流信息()
        Using 探测会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU, .输出窗口句柄 = IntPtr.Zero})
            探测会话.打开Async(媒体路径).GetAwaiter().GetResult()
            音轨 = 探测会话.当前媒体信息.流.Where(Function(x) x.类型 = "audio").ToArray()
        End Using
        断言(音轨.Length > 0, "测试媒体不包含音轨。")

        Dim 失败 As New List(Of String)()
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(640, 360), .ShowInTaskbar = False,
            .FormBorderStyle = FormBorderStyle.None, .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(-32000, -32000)}
            Dim 输出句柄 = 输出窗口.Handle
            输出窗口.Show()
            Application.DoEvents()

            For Each 音频流 In 音轨
                Dim 错误详情 = String.Empty
                Using 会话 As New 播放器会话(New 播放器配置 With {
                    .解码器 = 解码模式.CPU, .输出窗口句柄 = 输出句柄})
                    AddHandler 会话.错误, Sub(sender, e) 错误详情 = e.详情JSON
                    会话.设置音量(0.0F, True)
                    会话.打开Async(媒体路径).GetAwaiter().GetResult()
                    会话.播放()
                    Dim 预热 = Stopwatch.StartNew()
                    Do
                        Application.DoEvents()
                        Dim 快照 = 会话.当前快照
                        If 快照.已解码音频帧数 > 0 OrElse 快照.状态 = 播放状态.失败 Then Exit Do
                        If 预热.Elapsed >= TimeSpan.FromSeconds(5) Then Exit Do
                        Thread.Sleep(5)
                    Loop

                    Dim 切换开始 = Stopwatch.StartNew()
                    If 会话.当前快照.当前音频流 <> 音频流.索引 Then 会话.选择音频流(音频流.索引)
                    Dim 已恢复音频 As Boolean
                    Do
                        Application.DoEvents()
                        Dim 快照 = 会话.当前快照
                        已恢复音频 = 快照.当前音频流 = 音频流.索引 AndAlso
                            快照.已解码音频帧数 > 0 AndAlso 快照.音频缓冲时长 > TimeSpan.Zero AndAlso
                            快照.状态 <> 播放状态.失败
                        If 已恢复音频 OrElse 快照.状态 = 播放状态.失败 Then Exit Do
                        If 切换开始.Elapsed >= TimeSpan.FromSeconds(8) Then Exit Do
                        Thread.Sleep(5)
                    Loop

                    Dim 末快照 = 会话.当前快照
                    Dim 说明 = $"#{音频流.索引} {音频流.编码} {音频流.采样率} Hz/{音频流.声道数} ch"
                    If 已恢复音频 Then
                        Console.WriteLine($"音轨 {说明}：{切换开始.Elapsed.TotalMilliseconds:F0} ms")
                    Else
                        Dim 失败说明 = $"{说明}，状态 {末快照.状态}，选中 {末快照.当前音频流}，错误 {错误详情}"
                        Console.WriteLine($"音轨失败：{失败说明}")
                        失败.Add(失败说明)
                    End If
                End Using
            Next

            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU, .输出窗口句柄 = 输出句柄})
                Dim 错误详情 = String.Empty
                AddHandler 会话.错误, Sub(sender, e) 错误详情 = e.详情JSON
                会话.设置音量(0.0F, True)
                会话.打开Async(媒体路径).GetAwaiter().GetResult()
                会话.播放()
                For Each 音频流 In 音轨
                    Dim 基线 = 会话.当前快照.已解码音频帧数
                    Dim 切换开始 = Stopwatch.StartNew()
                    If 会话.当前快照.当前音频流 <> 音频流.索引 Then 会话.选择音频流(音频流.索引)
                    Dim 已恢复音频 As Boolean
                    Do
                        Application.DoEvents()
                        Dim 快照 = 会话.当前快照
                        已恢复音频 = 快照.当前音频流 = 音频流.索引 AndAlso
                            快照.已解码音频帧数 > 基线 AndAlso 快照.音频缓冲时长 > TimeSpan.Zero AndAlso
                            快照.状态 <> 播放状态.失败
                        If 已恢复音频 OrElse 快照.状态 = 播放状态.失败 Then Exit Do
                        If 切换开始.Elapsed >= TimeSpan.FromSeconds(8) Then Exit Do
                        Thread.Sleep(5)
                    Loop
                    If 已恢复音频 Then
                        Console.WriteLine($"连续音轨 #{音频流.索引}：{切换开始.Elapsed.TotalMilliseconds:F0} ms")
                    Else
                        Dim 快照 = 会话.当前快照
                        失败.Add($"连续切换 #{音频流.索引} 失败，状态 {快照.状态}，" &
                               $"选中 {快照.当前音频流}，错误 {错误详情}")
                        Exit For
                    End If
                Next

                If 失败.Count = 0 AndAlso 音轨.Length > 1 Then
                    Dim 基线 = 会话.当前快照.已解码音频帧数
                    For Each 音频流 In 音轨
                        会话.选择音频流(音频流.索引)
                    Next
                    Dim 最后音轨 = 音轨.Last().索引
                    Dim 快速切换 = Stopwatch.StartNew()
                    Dim 已恢复音频 As Boolean
                    Do
                        Application.DoEvents()
                        Dim 快照 = 会话.当前快照
                        已恢复音频 = 快照.当前音频流 = 最后音轨 AndAlso
                            快照.已解码音频帧数 > 基线 AndAlso 快照.音频缓冲时长 > TimeSpan.Zero AndAlso
                            快照.状态 <> 播放状态.失败
                        If 已恢复音频 OrElse 快照.状态 = 播放状态.失败 Then Exit Do
                        If 快速切换.Elapsed >= TimeSpan.FromSeconds(8) Then Exit Do
                        Thread.Sleep(5)
                    Loop
                    If 已恢复音频 Then
                        Console.WriteLine($"连续音轨快速排队至 #{最后音轨}：{快速切换.Elapsed.TotalMilliseconds:F0} ms")
                    Else
                        Dim 快照 = 会话.当前快照
                        失败.Add($"快速排队切换失败，状态 {快照.状态}，选中 {快照.当前音频流}，错误 {错误详情}")
                    End If
                End If
            End Using
        End Using
        断言(失败.Count = 0, $"{失败.Count} 条音轨切换失败：{String.Join(Environment.NewLine, 失败)}")
    End Sub

    Private Sub 测试内嵌字幕切换延迟(媒体路径 As String)
        Dim 字幕流 As 媒体流信息()
        Dim 媒体时长 As TimeSpan
        Using 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU, .输出窗口句柄 = IntPtr.Zero})
            会话.打开Async(媒体路径).GetAwaiter().GetResult()
            Dim 信息 = 会话.当前媒体信息
            字幕流 = 信息.流.Where(Function(x) x.类型 = "subtitle").ToArray()
            媒体时长 = 信息.时长
        End Using
        断言(字幕流.Length > 0, "测试媒体不包含字幕轨道。")

        Dim 测试位置 = If(媒体时长 > TimeSpan.FromMinutes(2),
                        TimeSpan.FromTicks(CLng(媒体时长.Ticks / 2)), TimeSpan.FromMinutes(1))
        Dim 待测流 = {字幕流.First(), 字幕流.Last()}.
            GroupBy(Function(x) x.索引).Select(Function(x) x.First()).ToArray()
        Dim 区域 = 视频显示区域.计算(1280, 720, 96.0F, 3840, 2160)
        For Each 流 In 待测流
            Dim 打开计时 = Stopwatch.StartNew()
            Using 字幕 = 外部字幕自动加载器.加载内嵌字幕(媒体路径, 流, CancellationToken.None)
                打开计时.Stop()
                Dim 生成计时 = Stopwatch.StartNew()
                Dim 绘制项 As New List(Of SUP字幕绘制项)()
                字幕.SUP生成器.生成帧(测试位置, 区域, 绘制项)
                生成计时.Stop()
                断言(打开计时.Elapsed < TimeSpan.FromSeconds(2),
                   $"字幕 #{流.索引} 打开耗时 {打开计时.Elapsed.TotalMilliseconds:F0} ms。")
                断言(生成计时.Elapsed < TimeSpan.FromSeconds(2),
                   $"字幕 #{流.索引} 定位耗时 {生成计时.Elapsed.TotalMilliseconds:F0} ms。")
                Console.WriteLine($"字幕 #{流.索引} {流.编码}：打开 {打开计时.Elapsed.TotalMilliseconds:F0} ms，" &
                                  $"定位 {测试位置:c} 用时 {生成计时.Elapsed.TotalMilliseconds:F0} ms")
            End Using
        Next
    End Sub

    Private Sub 测试空图层与真实呈现调度(视频路径 As String)
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(640, 360), .ShowInTaskbar = False,
            .FormBorderStyle = FormBorderStyle.None, .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(-32000, -32000)}
            Dim 输出句柄 = 输出窗口.Handle
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU, .输出窗口句柄 = 输出句柄,
                .色彩模式 = 色彩输出模式.映射到SDR})
                会话.设置音量(0.0F, True)
                会话.打开Async(视频路径).GetAwaiter().GetResult()
                会话.播放()
                Dim 预热 = Stopwatch.StartNew()
                Do
                    Application.DoEvents()
                    Dim 快照 = 会话.当前快照
                    If 快照.已呈现视频帧数 >= 3UL Then Exit Do
                    If 快照.状态 = 播放状态.失败 Then Throw New InvalidOperationException("调度回归播放失败。")
                    If 预热.Elapsed >= TimeSpan.FromSeconds(15) Then
                        Throw New TimeoutException($"等待真实视频 Present 超时：状态 {快照.状态}，" &
                            $"呈现/解码/队列 {快照.已呈现视频帧数}/{快照.已解码视频帧数}/{快照.视频队列帧数}，" &
                            $"位置 {快照.播放位置.TotalSeconds:F2}s，音频缓冲 {快照.音频缓冲时长.TotalMilliseconds:F0}ms。")
                    End If
                    Thread.Sleep(5)
                Loop
                会话.暂停()
                等待状态(会话, 播放状态.已暂停, TimeSpan.FromSeconds(3))
                Thread.Sleep(100)
                Dim 软阴影命令 = 定时文字命令.创建文字(
                    "soft shadow GPU regression", "Microsoft YaHei UI", 36.0F,
                    New RectangleF(80.0F, 80.0F, 480.0F, 64.0F), &HFFFFFFFFUI,
                    &HFF000000UI, 1.0F, 定时文字对齐.靠前, 定时文字对齐.靠前,
                    样式:=定时文字样式.软阴影, 阴影色ARGB:=&H90000000UI,
                    阴影X偏移:=3.0F, 阴影Y偏移:=3.0F)
                会话.设置弹幕图层(New Size(640, 360), {软阴影命令}, 1UL, 60.0F)
                Dim 阴影收敛 = Stopwatch.StartNew()
                Do
                    Application.DoEvents()
                    Dim 弹幕状态 = 会话.当前弹幕状态
                    If 弹幕状态.已绘制序号 = 1UL AndAlso 弹幕状态.命令数 = 1 Then Exit Do
                    If 阴影收敛.Elapsed >= TimeSpan.FromSeconds(3) Then
                        Throw New TimeoutException("GPU 弹幕软阴影图层没有完成栅格化。")
                    End If
                    Thread.Sleep(5)
                Loop
                Dim 空命令 = Array.Empty(Of 定时文字命令)()
                Dim 呈现前 = 会话.当前快照
                会话.设置定时文字图层(New Size(640, 360), 空命令, 1UL, 60.0F)
                会话.设置弹幕图层(New Size(640, 360), 空命令, 2UL, 60.0F)
                Dim 收敛 = Stopwatch.StartNew()
                Do
                    Application.DoEvents()
                    Dim 字幕状态 = 会话.当前定时文字状态
                    Dim 弹幕状态 = 会话.当前弹幕状态
                    If 字幕状态.已绘制序号 = 1UL AndAlso 弹幕状态.已绘制序号 = 2UL Then Exit Do
                    If 收敛.Elapsed >= TimeSpan.FromSeconds(3) Then Throw New TimeoutException("空图层没有收敛。")
                    Thread.Sleep(5)
                Loop
                ' 已绘制序号在 GPU 命令录制后即可更新；再等待一次交换链边界，
                ' 将最后一个清空帧与“空闲时仍持续 Present”区分开。
                Thread.Sleep(200)
                Application.DoEvents()
                Dim 空图层后 = 会话.当前快照
                Thread.Sleep(500)
                Application.DoEvents()
                Dim 静置后 = 会话.当前快照
                断言(静置后.交换链呈现次数 = 空图层后.交换链呈现次数,
                   $"空字幕/弹幕仍在后台持续 Present：{空图层后.交换链呈现次数}→{静置后.交换链呈现次数}。")
                断言(空图层后.交换链呈现次数 <= 呈现前.交换链呈现次数 + 2UL,
                   "两个空图层产生了多余的独立交换链帧。")
                断言(静置后.交换链呈现次数 + 静置后.已合并视频帧数 >= 静置后.已呈现视频帧数,
                   "真实视频提交既未成功 Present，也未计入合并帧。")
                断言(静置后.音频拒绝帧数 = 0UL,
                   $"音频生产者拒绝了 {静置后.音频拒绝帧数} 帧。")
                断言(静置后.视频队列帧数 <= 8, "视频时间背压超过 8 帧硬上限。")
            End Using
        End Using
    End Sub

    Private Sub 测试播放中HDR交换链切换(视频路径 As String)
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(960, 540), .ShowInTaskbar = False,
            .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .StartPosition = FormStartPosition.CenterScreen,
            .WindowState = FormWindowState.Maximized}
            Using 画面控件 As New 播放器画面控件 With {.Dock = DockStyle.Fill}
                输出窗口.Controls.Add(画面控件)
                输出窗口.Show()
                Application.DoEvents()
                Using 会话 As New 播放器会话(New 播放器配置 With {
                    .解码器 = 解码模式.GPU, .输出窗口句柄 = 画面控件.输出窗口句柄,
                    .色彩模式 = 色彩输出模式.映射到SDR,
                    .SDR峰值尼特 = 100.0F, .HDR峰值尼特 = 1000.0F,
                    .SDR纸白尼特 = 203.0F})
                    会话.设置音量(0.0F, True)
                    会话.打开Async(视频路径).GetAwaiter().GetResult()
                    断言(会话.当前快照.是HDR源, "HDR 切换回归样本没有被识别为 HDR。")
                    Dim 信息命令 = 定时文字命令.创建文字(
                        "HDR switch regression", "Microsoft YaHei", 24.0F,
                        New RectangleF(24.0F, 24.0F, 480.0F, 56.0F),
                        &HFFFFFFFFUI, &HFF000000UI, 1.0F,
                        定时文字对齐.靠前, 定时文字对齐.靠前)
                    Dim SUP高亮命令 = 定时文字命令.创建位图(
                        New Byte() {255, 255, 255, 255}, 1, 1, 4,
                        New RectangleF(540.0F, 24.0F, 120.0F, 56.0F), HDR高亮:=True)
                    Dim 图层画布 = 画面控件.ClientSize
                    会话.设置定时文字图层(图层画布, {SUP高亮命令}, 1UL, 60.0F)
                    会话.设置弹幕图层(图层画布, {信息命令}, 1UL, 60.0F)
                    会话.设置播放器信息图层(图层画布, {信息命令}, 1UL, 60.0F)
                    会话.播放()
                    Dim SDR快照 = 等待快照(会话,
                        Function(x) x.交换链呈现次数 >= 3UL AndAlso x.已呈现视频帧数 >= 3UL,
                        "HDR 样本的 SDR 映射预热")

                    会话.设置色彩模式(色彩输出模式.峰值映射HDR, 100.0F, 1000.0F, 203.0F)
                    Dim HDR快照 = 等待快照(会话,
                        Function(x) x.实际色彩模式 = 色彩输出模式.峰值映射HDR AndAlso
                            x.视频输出位深度 >= 10 AndAlso
                            x.交换链呈现次数 > SDR快照.交换链呈现次数 AndAlso
                            x.已呈现视频帧数 > SDR快照.已呈现视频帧数,
                        "播放中切换到真实 HDR 后继续呈现")
                    Dim HDR图层状态 = 会话.当前定时文字状态
                    Dim HDR弹幕状态 = 会话.当前弹幕状态
                    断言(HDR图层状态.已绘制序号 = 1UL AndAlso HDR图层状态.命令数 = 1 AndAlso
                           HDR弹幕状态.已绘制序号 = 1UL AndAlso HDR弹幕状态.命令数 = 1,
                       "切换到真实 HDR 后字幕或弹幕图层没有完成合成。")
                    Dim HDR持续计时 = Stopwatch.StartNew()
                    Dim 图层序号 = 2UL
                    Dim 下次图层秒数 = 0.0R
                    While HDR持续计时.Elapsed < TimeSpan.FromSeconds(2)
                        If HDR持续计时.Elapsed.TotalSeconds >= 下次图层秒数 Then
                            Dim 移动命令 = 定时文字命令.创建文字(
                                "HDR switch regression", "Microsoft YaHei", 24.0F,
                                New RectangleF(24.0F + CSng((图层序号 Mod 120UL) * 2UL),
                                    24.0F, 480.0F, 56.0F),
                                &HFFFFFFFFUI, &HFF000000UI, 1.0F,
                                定时文字对齐.靠前, 定时文字对齐.靠前)
                            会话.设置定时文字图层(图层画布, {移动命令}, 图层序号, 60.0F)
                            会话.设置弹幕图层(图层画布, {移动命令}, 图层序号, 60.0F)
                            会话.设置播放器信息图层(图层画布, {移动命令}, 图层序号, 60.0F)
                            图层序号 += 1UL
                            下次图层秒数 += 1.0R / 60.0R
                        End If
                        Application.DoEvents()
                        Thread.Sleep(1)
                    End While
                    Dim HDR持续快照 = 会话.当前快照
                    断言(HDR持续快照.实际色彩模式 = 色彩输出模式.峰值映射HDR AndAlso
                           HDR持续快照.交换链呈现次数 >= HDR快照.交换链呈现次数 + 60UL AndAlso
                           HDR持续快照.已呈现视频帧数 >= HDR快照.已呈现视频帧数 + 24UL,
                       $"真实 HDR 大窗口/动态图层持续呈现不足：视频帧/Present " &
                       $"{HDR快照.已呈现视频帧数}→{HDR持续快照.已呈现视频帧数}/" &
                       $"{HDR快照.交换链呈现次数}→{HDR持续快照.交换链呈现次数}。")
                    Dim HDR持续图层状态 = 会话.当前定时文字状态
                    Dim HDR持续弹幕状态 = 会话.当前弹幕状态
                    断言(HDR持续图层状态.已绘制序号 = 图层序号 - 1UL AndAlso
                           HDR持续弹幕状态.已绘制序号 = 图层序号 - 1UL AndAlso
                           HDR持续图层状态.图层呈现帧数 >= HDR图层状态.图层呈现帧数 + 60UI AndAlso
                           HDR持续弹幕状态.图层呈现帧数 >= HDR弹幕状态.图层呈现帧数 + 60UI,
                       "真实 HDR 大窗口内字幕或弹幕图层停止更新。")

                    会话.设置色彩模式(色彩输出模式.映射到SDR, 100.0F, 1000.0F, 203.0F)
                    Dim 恢复快照 = 等待快照(会话,
                        Function(x) x.实际色彩模式 = 色彩输出模式.映射到SDR AndAlso
                            x.交换链呈现次数 > HDR持续快照.交换链呈现次数 AndAlso
                            x.已呈现视频帧数 > HDR持续快照.已呈现视频帧数,
                        "从真实 HDR 切回 SDR 后继续呈现")
                    Console.WriteLine($"HDR 切换：视频帧 {SDR快照.已呈现视频帧数}→{HDR持续快照.已呈现视频帧数}→{恢复快照.已呈现视频帧数}，" &
                                      $"交换链 Present {SDR快照.交换链呈现次数}→{HDR持续快照.交换链呈现次数}→{恢复快照.交换链呈现次数}。")
                End Using
            End Using
        End Using
        测试完整播放器HDR持续呈现(视频路径)
    End Sub

    Private Sub 测试完整播放器HDR持续呈现(视频路径 As String)
        Using 窗口 As New Form1 With {
            .ShowInTaskbar = False,
            .StartPosition = FormStartPosition.CenterScreen,
            .WindowState = FormWindowState.Maximized}
            窗口.Show()
            Dim 标志 = BindingFlags.Instance Or BindingFlags.NonPublic
            Dim 控制器 = DirectCast(GetType(Form1).GetField("播放控制器", 标志)?.GetValue(窗口), 播放器控制器)
            Dim 信息呈现器 = DirectCast(GetType(Form1).GetField("信息图层呈现器", 标志)?.GetValue(窗口), 播放器信息图层呈现器)
            Dim 画面 = DirectCast(GetType(Form1).GetField("画面控件", 标志)?.GetValue(窗口), 播放器画面控件)
            断言(控制器 IsNot Nothing AndAlso 信息呈现器 IsNot Nothing AndAlso 画面 IsNot Nothing,
               "无法取得完整播放器 HDR 回归所需的控制器、信息图层或画面控件。")
            控制器.设置音量(0.0F)
            Dim SDR快照 = 等待控制器快照(控制器,
                Function(x) x.是HDR源 AndAlso x.状态 = 播放状态.正在播放 AndAlso
                    x.实际色彩模式 = 色彩输出模式.映射到SDR AndAlso x.交换链呈现次数 >= 3UL,
                "完整播放器打开 HDR 样本")
            断言(String.Equals(Path.GetFullPath(控制器.当前媒体路径), Path.GetFullPath(视频路径),
                              StringComparison.OrdinalIgnoreCase),
               "完整播放器没有打开 HDR 切换回归指定的样本。")
            信息呈现器.切换调试信息()

            控制器.切换HDR模式()
            等待控制器快照(控制器,
                Function(x) x.实际色彩模式 = 色彩输出模式.原始HDR按SDR呈现,
                "完整播放器切换到原始 HDR")
            控制器.切换HDR模式()
            Dim HDR快照 = 等待控制器快照(控制器,
                Function(x) x.实际色彩模式 = 色彩输出模式.峰值映射HDR AndAlso
                    x.视频输出位深度 >= 10 AndAlso x.交换链呈现次数 > SDR快照.交换链呈现次数,
                "完整播放器切换到真实 HDR")
            If HDR快照.显示器峰值尼特 > 0UI Then
                断言(HDR快照.HDR有效目标峰值尼特 = HDR快照.显示器峰值尼特,
                   $"自动 HDR 目标没有使用 Windows 显示器峰值：" &
                   $"显示器 {HDR快照.显示器峰值尼特}，显示目标 {HDR快照.HDR有效目标峰值尼特} 尼特。")
            End If
            Console.WriteLine($"Windows HDR 峰值/显示目标：" &
                              $"{HDR快照.显示器峰值尼特}/{HDR快照.HDR有效目标峰值尼特} 尼特。")

            Using HDR屏幕首帧 = 捕获控件屏幕(画面)
            Dim 持续计时 = Stopwatch.StartNew()
            While 持续计时.Elapsed < TimeSpan.FromSeconds(2)
                Application.DoEvents()
                Thread.Sleep(1)
            End While
            Using HDR屏幕末帧 = 捕获控件屏幕(画面)
                Dim 变化采样数 = 计算屏幕变化采样数(HDR屏幕首帧, HDR屏幕末帧)
                断言(变化采样数 >= 100,
                   $"完整播放器在真实 HDR 中的桌面合成画面冻结，仅 {变化采样数} 个采样点发生变化。")
                Console.WriteLine($"完整播放器真实 HDR 屏幕变化采样：{变化采样数}。")
            End Using
            Dim HDR持续快照 = 控制器.安全读取快照()
            断言(HDR持续快照 IsNot Nothing AndAlso
                   HDR持续快照.实际色彩模式 = 色彩输出模式.峰值映射HDR AndAlso
                   HDR持续快照.已呈现视频帧数 >= HDR快照.已呈现视频帧数 + 24UL AndAlso
                   HDR持续快照.交换链呈现次数 >= HDR快照.交换链呈现次数 + 24UL,
               $"完整播放器在真实 HDR 中停止呈现：视频帧/Present " &
               $"{HDR快照.已呈现视频帧数}→{HDR持续快照?.已呈现视频帧数}/" &
               $"{HDR快照.交换链呈现次数}→{HDR持续快照?.交换链呈现次数}。")

            控制器.切换HDR模式()
            等待控制器快照(控制器,
                Function(x) x.实际色彩模式 = 色彩输出模式.映射到SDR AndAlso
                    x.交换链呈现次数 > HDR持续快照.交换链呈现次数,
                "完整播放器从真实 HDR 切回 SDR")
            窗口.Close()
            End Using
        End Using
    End Sub

    Private Function 捕获控件屏幕(控件 As Control) As Bitmap
        Dim 大小 = 控件.ClientSize
        Dim 结果 As New Bitmap(Math.Max(1, 大小.Width), Math.Max(1, 大小.Height))
        Using 绘图 = Graphics.FromImage(结果)
            绘图.CopyFromScreen(控件.PointToScreen(Point.Empty), Point.Empty, 结果.Size,
                              CopyPixelOperation.SourceCopy)
        End Using
        Return 结果
    End Function

    Private Function 计算屏幕变化采样数(首帧 As Bitmap, 末帧 As Bitmap) As Integer
        Dim 宽度 = Math.Min(首帧.Width, 末帧.Width)
        Dim 高度 = Math.Min(首帧.Height, 末帧.Height)
        Dim 变化数 = 0
        For y = 2 To 高度 - 1 Step 4
            For x = 2 To 宽度 - 1 Step 4
                Dim a = 首帧.GetPixel(x, y)
                Dim b = 末帧.GetPixel(x, y)
                If Math.Abs(CInt(a.R) - CInt(b.R)) + Math.Abs(CInt(a.G) - CInt(b.G)) +
                   Math.Abs(CInt(a.B) - CInt(b.B)) >= 12 Then 变化数 += 1
            Next
        Next
        Return 变化数
    End Function

    Private Function 等待控制器快照(控制器 As 播放器控制器,
                                条件 As Func(Of 播放器快照, Boolean),
                                操作 As String) As 播放器快照
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 快照 = 控制器.安全读取快照()
            If 快照 IsNot Nothing AndAlso 条件(快照) Then Return 快照
            If 快照 IsNot Nothing AndAlso 快照.状态 = 播放状态.失败 Then
                Throw New InvalidOperationException($"{操作}时播放器失败。")
            End If
            If 计时.Elapsed >= TimeSpan.FromSeconds(15) Then
                Throw New TimeoutException($"等待{操作}超时：状态 {快照?.状态}，" &
                    $"色彩 {快照?.请求色彩模式}/{快照?.实际色彩模式}，" &
                    $"视频帧/Present {快照?.已呈现视频帧数}/{快照?.交换链呈现次数}。")
            End If
            Thread.Sleep(5)
        Loop
    End Function

    Private Sub 测试ASS渲染性能()
        测试ASS半透明像素(Path.GetTempPath())
        Dim 临时路径 = Path.Combine(Path.GetTempPath(),
            "fff-player-ass-render-benchmark-" & Guid.NewGuid().ToString("N") & ".ass")
        Dim 脚本 = String.Join(vbLf, {
            "[Script Info]",
            "ScriptType: v4.00+",
            "PlayResX: 3840",
            "PlayResY: 2160",
            "[V4+ Styles]",
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding",
            "Style: Default,Arial,96,&H80FFFFFF,&H80FFFFFF,&H80000000,&H80000000,0,0,0,0,100,100,0,0,1,3,2,7,0,0,0,1",
            "[Events]",
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
            "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\move(1320,900,1620,900,0,10000)\blur3\p1}m 0 0 l 1200 0 1200 300 0 300{\p0}"})
        Try
            File.WriteAllText(临时路径, 脚本, New UTF8Encoding(False))
            Using 生成器 As New ASS特效字幕帧生成器(临时路径, Path.GetTempPath())
                Dim 预热 = 生成器.生成帧(TimeSpan.Zero, 3840, 2160)
                Dim 样本 As New List(Of Double)(180)
                Dim 内容变化数 = 0
                Dim 上一内容标识 As ULong = 0
                For index = 0 To 179
                    Dim 时间 = TimeSpan.FromTicks(CLng(index * TimeSpan.TicksPerSecond / 60.0R))
                    Dim 开始 = Stopwatch.GetTimestamp()
                    Dim 帧 = 生成器.生成帧(时间, 3840, 2160)
                    Dim 毫秒 = (Stopwatch.GetTimestamp() - 开始) * 1000.0R / Stopwatch.Frequency
                    If 帧 Is Nothing OrElse 帧.像素BGRA.Length = 0 Then
                        Throw New InvalidOperationException("ASS 基准没有生成字幕位图。")
                    End If
                    If Not 是预乘BGRA(帧.像素BGRA) Then
                        Throw New InvalidOperationException("ASS 基准输出不是预乘 BGRA。")
                    End If
                    If 上一内容标识 <> 0 AndAlso 上一内容标识 <> 帧.内容标识 Then 内容变化数 += 1
                    上一内容标识 = 帧.内容标识
                    样本.Add(毫秒)
                Next
                Dim 排序 = 样本.OrderBy(Function(x) x).ToArray()
                Dim 平均 = 样本.Average()
                Dim P95 = 排序(Math.Min(排序.Length - 1, CInt(Math.Floor(排序.Length * 0.95R))))
                Dim 最大 = 排序(排序.Length - 1)
                Console.WriteLine($"ASS 4K 局部遮罩基准：平均 {平均:F2} ms，P95 {P95:F2} ms，最大 {最大:F2} ms，" &
                                  $"理论 {1000.0R / 平均:F1} FPS，内容变化 {内容变化数}/{样本.Count - 1}。")
            End Using
        Finally
            If File.Exists(临时路径) Then File.Delete(临时路径)
        End Try
    End Sub

    Private Sub 测试剪辑模式焦点()
        Using 窗口 As New Form1 With {.ShowInTaskbar = False, .Opacity = 0}
            窗口.Show()
            Application.DoEvents()
            Dim 标志 = BindingFlags.Instance Or BindingFlags.Public Or BindingFlags.NonPublic
            Dim 窗口类型 = GetType(Form1)
            Dim 画面字段 = 窗口类型.GetField("画面控件", 标志)
            Dim 控制器字段 = 窗口类型.GetField("剪辑区间控制器", 标志)
            Dim 模式按钮 = 窗口.Controls.Find("MB_剪辑区间模式", True).FirstOrDefault()
            断言(画面字段 IsNot Nothing AndAlso 控制器字段 IsNot Nothing AndAlso 模式按钮 IsNot Nothing,
                "无法取得剪辑模式焦点测试所需的窗口控件。")
            Dim 画面 = DirectCast(画面字段.GetValue(窗口), Control)
            Dim 控制器 = DirectCast(控制器字段.GetValue(窗口), 播放器剪辑区间控制器)
            断言(画面 IsNot Nothing, "播放器画面尚未在窗口加载时创建。")
            断言(控制器 IsNot Nothing, "独立剪辑区间控制器尚未创建。")
            断言(窗口类型.GetField("剪辑区间模式已启用", 标志) Is Nothing AndAlso
                   窗口类型.GetField("剪辑区间进度条", 标志) Is Nothing AndAlso
                   窗口类型.GetMethod("MB_剪辑区间模式_Click", 标志) Is Nothing,
                "剪辑区间状态或按钮处理仍残留在 Form1。")

            断言(Not 模式按钮.TabStop, "剪辑模式按钮仍参与键盘焦点导航。")
            Dim 点击入口 = 模式按钮.GetType().GetMethod("OnClick", 标志)
            Dim 方向键入口 = 窗口类型.GetMethod("处理方向键快捷键", 标志)
            Dim 界面呈现器 = DirectCast(窗口类型.GetField("界面呈现器", 标志)?.GetValue(窗口), 播放器界面呈现器)
            Dim 精确时间戳字段 = GetType(播放器界面呈现器).GetField("显示精确时间戳", 标志)
            断言(点击入口 IsNot Nothing AndAlso 方向键入口 IsNot Nothing AndAlso
                   界面呈现器 IsNot Nothing AndAlso 精确时间戳字段 IsNot Nothing,
                "无法验证剪辑模式 AddHandler 或时间戳事件绑定。")
            For Each 预期模式 In {True, False}
                模式按钮.Focus()
                点击入口.Invoke(模式按钮, {EventArgs.Empty})
                Application.DoEvents()
                断言(控制器.模式已启用 = 预期模式,
                   "AddHandler 没有在按钮点击时更新独立剪辑模式。")
                断言(CBool(精确时间戳字段.GetValue(界面呈现器)) = 预期模式,
                   "剪辑模式变化事件没有同步时间戳精度。")
                Dim 剪辑快捷键已处理 = CBool(方向键入口.Invoke(窗口, {Keys.Control Or Keys.Left}))
                断言(剪辑快捷键已处理 = 预期模式,
                    "方向键入口没有通过 AddHandler 按剪辑模式接管关键帧快捷键。")
                断言(画面.CanFocus AndAlso 画面.Focused AndAlso ReferenceEquals(窗口.ActiveControl, 画面),
                    "切换剪辑模式后，键盘焦点没有立即返回视频画面。")
            Next

            Dim 时间轴 = 控制器.进度条
            Dim 时间轴类型 = 时间轴.GetType()
            Dim 更新播放状态 = 时间轴类型.GetMethod("更新播放状态", 标志)
            Dim 设为入点 = 时间轴类型.GetMethod("设为入点", 标志)
            Dim 设为出点 = 时间轴类型.GetMethod("设为出点", 标志)
            Dim 入点属性 = 时间轴类型.GetProperty("入点", 标志)
            Dim 出点属性 = 时间轴类型.GetProperty("出点", 标志)
            断言(更新播放状态 IsNot Nothing AndAlso 设为入点 IsNot Nothing AndAlso
                   设为出点 IsNot Nothing AndAlso 入点属性 IsNot Nothing AndAlso 出点属性 IsNot Nothing,
               "剪辑时间轴成员不完整。")

            Dim 入点 = TimeSpan.FromSeconds(2)
            Dim 出点 = TimeSpan.FromSeconds(8)
            更新播放状态.Invoke(时间轴, {TimeSpan.FromSeconds(5), TimeSpan.FromSeconds(10)})
            设为入点.Invoke(时间轴, {入点})
            设为出点.Invoke(时间轴, {出点})
            控制器.媒体已打开(窗口, New 播放器媒体事件参数("same.mp4", Nothing, Nothing, True))
            断言(DirectCast(入点属性.GetValue(时间轴), TimeSpan) = 入点 AndAlso
                   DirectCast(出点属性.GetValue(时间轴), TimeSpan) = 出点,
                "同一媒体切换解码模式后没有保留出入点。")

            控制器.媒体已打开(窗口, New 播放器媒体事件参数("other.mp4", Nothing, Nothing, False))
            断言(入点属性.GetValue(时间轴) Is Nothing AndAlso 出点属性.GetValue(时间轴) Is Nothing,
                "真正打开另一媒体时没有清除出入点。")
            窗口.Close()
        End Using
    End Sub

    Private Sub 测试音量交互()
        Using 窗口 As New Form1 With {.ShowInTaskbar = False, .Opacity = 0}
            窗口.Show()
            Application.DoEvents()

            Dim 标志 = BindingFlags.Instance Or BindingFlags.Public Or BindingFlags.NonPublic
            Dim 窗口类型 = GetType(Form1)
            Dim 画面 = DirectCast(窗口类型.GetField("画面控件", 标志)?.GetValue(窗口), 播放器画面控件)
            Dim 控制器 = DirectCast(窗口类型.GetField("播放控制器", 标志)?.GetValue(窗口), 播放器控制器)
            Dim 信息呈现器 = DirectCast(窗口类型.GetField("信息图层呈现器", 标志)?.GetValue(窗口), 播放器信息图层呈现器)
            Dim 音量条 = 窗口.Controls.Find("ETB_音量条", True).FirstOrDefault()
            断言(画面 IsNot Nothing AndAlso 控制器 IsNot Nothing AndAlso 信息呈现器 IsNot Nothing AndAlso 音量条 IsNot Nothing,
               "无法取得音量交互测试所需的窗口成员。")
            Dim 音量值属性 = 音量条.GetType().GetProperty("Value", 标志)
            断言(音量值属性 IsNot Nothing, "音量控件没有可用的 Value 属性。")

            For Each 百分比 In {40, 35, 30, 25}
                音量值属性.SetValue(音量条, CDbl(百分比))
            Next
            Application.DoEvents()
            断言(Math.Abs(控制器.音量 - 0.25F) < 0.0001F, "滑块音量没有同步到播放器控制器。")

            Dim 消息字段 = 信息呈现器.GetType().GetField("操作消息列表", 标志)
            Dim 消息 = TryCast(消息字段?.GetValue(信息呈现器), System.Collections.IList)
            断言(消息 IsNot Nothing AndAlso 消息.Count = 1, "连续调整音量生成了重复操作提示。")
            Dim 文本字段 = 消息(0).GetType().GetField("文本", 标志)
            断言(String.Equals(CStr(文本字段?.GetValue(消息(0))), "音量 25%", StringComparison.Ordinal),
               "连续调整音量后没有保留最新提示。")

            Dim 滚轮处理 = GetType(播放器画面控件).GetMethod("OnMouseWheel", 标志)
            断言(滚轮处理 IsNot Nothing, "无法取得画面滚轮处理入口。")
            滚轮处理.Invoke(画面, {New MouseEventArgs(MouseButtons.None, 0, 0, 0, 60)})
            断言(CInt(音量值属性.GetValue(音量条)) = 25, "不足一格的高精度滚轮输入被提前应用。")
            滚轮处理.Invoke(画面, {New MouseEventArgs(MouseButtons.None, 0, 0, 0, 60)})
            Application.DoEvents()
            断言(CInt(音量值属性.GetValue(音量条)) = 30 AndAlso Math.Abs(控制器.音量 - 0.3F) < 0.0001F,
               "画面滚轮没有按每格 5% 调整音量。")
            断言(消息.Count = 1 AndAlso String.Equals(CStr(文本字段.GetValue(消息(0))), "音量 30%", StringComparison.Ordinal),
               "滚轮调整没有复用并更新音量提示。")
            信息呈现器.显示操作信息("重复提示")
            信息呈现器.显示操作信息("重复提示")
            断言(消息.Count = 2, "完全相同的操作提示没有自动合并。")
            Dim 命令字段 = 信息呈现器.GetType().GetField("图层命令", 标志)
            Dim 命令 = DirectCast(命令字段?.GetValue(信息呈现器), IEnumerable(Of 定时文字命令)).
                Where(Function(x) CBool(GetType(定时文字命令).GetProperty("是位图", 标志)?.GetValue(x))).ToArray()
            断言(命令.Length > 0 AndAlso
                   命令.All(Function(x) x.X >= 0 AndAlso x.X + x.宽度 <= 画面.ClientSize.Width) AndAlso
                   命令.Any(Function(x) x.宽度 < 画面.ClientSize.Width - 32.0F),
                "操作提示背景没有按文字收缩，或短提示仍然强制占满整行。")
            窗口.Close()
        End Using
    End Sub

    Private Sub 测试未知时长进度条()
        Dim 当前快照 As 播放器快照 = Nothing
        Using 进度条 As New LakeUI.ExcellentTrackBar(),
              音量条 As New LakeUI.ExcellentTrackBar(),
              播放按钮 As New LakeUI.ModernButton(),
              解码按钮 As New LakeUI.ModernButton(),
              HDR按钮 As New LakeUI.ModernButton(),
              视频编码按钮 As New LakeUI.ModernButton(),
              音频编码按钮 As New LakeUI.ModernButton(),
              声道数按钮 As New LakeUI.ModernButton(),
              时间标签 As New LakeUI.HtmlColorLabel(),
              状态栏 As New Panel(),
              HDR占位 As New Panel(),
              视频编码占位 As New Panel(),
              音频编码占位 As New Panel(),
              声道数占位 As New Panel(),
              画面 As New 播放器画面控件(),
              播放图标 As New Bitmap(1, 1),
              暂停图标 As New Bitmap(1, 1),
              呈现器 As New 播放器界面呈现器(进度条, 音量条, 播放按钮,
                  播放图标, 暂停图标, 解码按钮, HDR按钮, 视频编码按钮,
                  音频编码按钮, 声道数按钮, 时间标签, 状态栏, HDR占位,
                  视频编码占位, 音频编码占位, 声道数占位, 画面,
                  Function() 当前快照, Function() False, Function() 解码模式.CPU,
                  Function() 色彩输出模式.映射到SDR)

            当前快照 = 创建进度条快照(TimeSpan.FromSeconds(65), TimeSpan.Zero)
            呈现器.媒体已打开(False)
            呈现器.刷新()
            断言(Math.Abs(进度条.Maximum - 65_000) < 0.5 AndAlso
                   Math.Abs(进度条.Value - 65_000) < 0.5,
                "未知时长媒体没有按当前播放位置扩展进度范围。")
            断言(时间标签.Text.Contains("--:--", StringComparison.Ordinal),
                "未知总时长没有使用占位时间显示。")
            Dim 信息 As New 媒体信息()
            信息.流.Add(New 媒体流信息 With {.索引 = 0, .类型 = "video", .编码 = "h264"})
            呈现器.更新媒体信息(信息, 当前快照)
            断言(String.Equals(视频编码按钮.Text, "AVC", StringComparison.Ordinal),
               $"H.264 视频编码按钮没有显示为 AVC：{视频编码按钮.Text}。")

            当前快照 = 创建进度条快照(TimeSpan.FromSeconds(20), TimeSpan.Zero)
            呈现器.刷新()
            断言(Math.Abs(进度条.Maximum - 65_000) < 0.5 AndAlso
                   Math.Abs(进度条.Value - 20_000) < 0.5,
                "回到较早位置后没有保留已经确认的可回拖范围。")

            当前快照 = 创建进度条快照(TimeSpan.FromSeconds(20), TimeSpan.FromSeconds(120))
            呈现器.刷新()
            断言(Math.Abs(进度条.Maximum - 120_000) < 0.5 AndAlso
                   Math.Abs(进度条.Value - 20_000) < 0.5 AndAlso
                   Not 时间标签.Text.Contains("--", StringComparison.Ordinal),
                "取得总时长后没有恢复标准进度条范围。")

            Dim 时间字体名称 = 选择可用测试字体()
            断言(Not String.IsNullOrWhiteSpace(时间字体名称), "没有可用的时间戳宽度测试字体。")
            Dim 原时间字体 = 时间标签.Font
            Using 时间字体 As New Font(时间字体名称, 11.0F, FontStyle.Regular, GraphicsUnit.Point)
                Try
                    时间标签.Font = 时间字体
                    呈现器.更新Dpi()
                    断言(Not 时间标签.AutoSize,
                        "时间标签仍会在手动测量后按实际数字自动收缩。")
                    Dim 测量文本方法 = GetType(播放器界面呈现器).GetMethod("创建时间戳测量文本",
                        BindingFlags.Static Or BindingFlags.NonPublic)
                    断言(测量文本方法 IsNot Nothing, "无法取得时间戳测量文本生成器。")
                    当前快照 = 创建进度条快照(TimeSpan.FromSeconds(11), TimeSpan.FromSeconds(120))
                    呈现器.刷新()
                    Dim 十一秒宽度 = 时间标签.Width
                    Dim 十一秒测量文本 = CStr(测量文本方法.Invoke(Nothing, {时间标签.Text}))
                    断言(十一秒宽度 = 时间标签.GetPreferredSizeForText(十一秒测量文本, Size.Empty).Width,
                        "时间标签最终宽度没有使用数字归零后的测量结果。")
                    断言(时间标签.Text.Contains(">11</font>", StringComparison.Ordinal),
                        "时间戳测量副本污染了实际显示文本。")
                    当前快照 = 创建进度条快照(TimeSpan.FromSeconds(58), TimeSpan.FromSeconds(120))
                    呈现器.刷新()
                    断言(时间标签.Width = 十一秒宽度,
                        $"非等宽数字仍改变时间戳区域宽度：{十一秒宽度} -> {时间标签.Width}。")
                    断言(时间标签.Text.Contains(">58</font>", StringComparison.Ordinal),
                        "时间戳显示的数字被替换成了测量用的 0。")
                Finally
                    时间标签.Font = 原时间字体
                End Try
            End Using

            呈现器.清除媒体()
            断言(进度条.Maximum = 0 AndAlso 进度条.Value = 0 AndAlso
                   Not 时间标签.Text.Contains("--", StringComparison.Ordinal),
                "清除媒体后没有重置未知时长进度状态。")
        End Using
    End Sub

    Private Function 创建进度条快照(位置 As TimeSpan, 时长 As TimeSpan) As 播放器快照
        Return New 播放器快照(New 原生播放器快照 With {
            .状态 = CUInt(播放状态.已暂停),
            .位置100纳秒 = 位置.Ticks,
            .时长100纳秒 = 时长.Ticks,
            .当前视频流 = 0,
            .当前音频流 = -1})
    End Function

    Private Sub 测试无时间轴媒体播放(媒体路径 As String)
        Using 输出窗口 As New Form With {
            .ClientSize = New Size(320, 180),
            .ShowInTaskbar = False,
            .FormBorderStyle = FormBorderStyle.None,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Point(-10000, -10000)}
            输出窗口.Show()
            Application.DoEvents()
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .输出窗口句柄 = 输出窗口.Handle})
                会话.设置音量(0.0F, True)
                会话.打开Async(媒体路径).GetAwaiter().GetResult()
                断言(会话.当前快照.总时长 = TimeSpan.Zero,
                    "回归样本意外包含总时长，未覆盖未知时长分支。")

                Dim 播放计时 = Stopwatch.StartNew()
                会话.播放()
                Do
                    Dim 快照 = 会话.当前快照
                    If 快照.播放位置 >= TimeSpan.FromMilliseconds(750) Then Exit Do
                    If 快照.状态 = 播放状态.播放结束 Then
                        Throw New InvalidOperationException("无时间轴媒体在进度推进前已经结束。")
                    End If
                    If 播放计时.Elapsed >= TimeSpan.FromSeconds(3) Then
                        Throw New TimeoutException("无时间轴媒体的播放位置没有随合成时钟推进。")
                    End If
                    Thread.Sleep(10)
                Loop
                断言(播放计时.Elapsed >= TimeSpan.FromMilliseconds(500),
                    "无时间轴媒体仍以解码速度瞬间排空，而非按合成时间戳播放。")

                Dim 跳转前代次 = 会话.当前快照.时间轴代次
                会话.跳转(TimeSpan.FromMilliseconds(200))
                Dim 跳转等待 = Stopwatch.StartNew()
                Do While 会话.当前快照.时间轴代次 = 跳转前代次 AndAlso
                         跳转等待.Elapsed < TimeSpan.FromSeconds(2)
                    Thread.Sleep(10)
                Loop
                断言(会话.当前快照.时间轴代次 > 跳转前代次,
                    "无时间轴媒体没有接受已播放范围内的回拖。")

                等待状态(会话, 播放状态.播放结束, TimeSpan.FromSeconds(10))
                Dim 结束快照 = 会话.当前快照
                断言(结束快照.总时长 >= TimeSpan.FromSeconds(1) AndAlso
                       结束快照.播放位置 = 结束快照.总时长,
                    $"播放结束后没有用实际解码时间线补全总时长：位置 {结束快照.播放位置:c}，时长 {结束快照.总时长:c}。")
            End Using
        End Using
    End Sub

    Private Sub 测试信息层交互与文本()
        测试信息层精确文本()
        测试信息层全局字体与HDR提示合并()
        测试媒体信息按钮左右键()
        测试媒体信息响度数据源()
    End Sub

    Private Sub 测试信息层全局字体与HDR提示合并()
        Using 窗口 As New Form1 With {.ShowInTaskbar = False, .Opacity = 0}
            窗口.Show()
            Application.DoEvents()
            Dim 标志 = BindingFlags.Instance Or BindingFlags.Public Or BindingFlags.NonPublic
            Dim 呈现器 = DirectCast(GetType(Form1).GetField("信息图层呈现器", 标志)?.GetValue(窗口),
                               播放器信息图层呈现器)
            Dim 画面 = DirectCast(GetType(Form1).GetField("画面控件", 标志)?.GetValue(窗口), Control)
            断言(呈现器 IsNot Nothing AndAlso 画面 IsNot Nothing, "无法取得信息图层呈现器或画面控件。")

            Dim 原字体 = 设置.实例对象.字体
            Dim 测试字体 = 选择可用测试字体("Microsoft YaHei UI")
            断言(Not String.IsNullOrWhiteSpace(测试字体), "没有可用的全局字体可测试信息层。")
            Try
                设置.实例对象.字体 = 测试字体
                字体控制.更新所有控件字体属性()
                Application.DoEvents()

                呈现器.切换调试信息()
                呈现器.显示操作信息("字体提示", &HFFF0D35DUI, "字体测试")
                Dim 命令字段 = GetType(播放器信息图层呈现器).GetField("图层命令", 标志)
                Dim 文字命令 = DirectCast(命令字段?.GetValue(呈现器), IEnumerable(Of 定时文字命令)).
                    Where(Function(x) Not x.是位图).ToArray()
                断言(文字命令.Length >= 2, "信息层没有同时生成实时信息和操作提示文字命令。")
                断言(文字命令.All(Function(x) String.Equals(x.字体, 测试字体, StringComparison.CurrentCultureIgnoreCase)),
                   "信息层实时信息或操作提示没有使用设置中的全局字体。")
                Dim 预期字号 = CSng(11.0F * Math.Max(1, 画面.DeviceDpi) / 72.0F)
                断言(文字命令.All(Function(x) Math.Abs(x.字号 - 预期字号) < 0.01F AndAlso
                                           x.样式 = 定时文字样式.无),
                   "信息层接入全局字体时误改了字号或字形。")
                For Each 命令 In 文字命令
                    Dim 原生宽度 As Single
                    Dim 结果 = 播放器原生接口.FFF3FP_MeasureTimedTextWidth(
                        命令.文本, 命令.字体, 命令.字号, 原生定时文字标志.无, 原生宽度)
                    断言(结果 = 原生播放器结果.成功 AndAlso Single.IsFinite(原生宽度) AndAlso
                           命令.宽度 + 0.01F >= 原生宽度,
                       $"信息层命令宽度小于最终 DirectWrite 字形：{命令.字体} / {命令.文本} / " &
                       $"{命令.宽度:F3} < {原生宽度:F3}。")
                Next

                Dim HDR状态入口 = GetType(Form1).GetMethod("播放控制器_HDR输出状态已确认", 标志)
                断言(HDR状态入口 IsNot Nothing, "无法取得 HDR 状态提示入口。")
                HDR状态入口.Invoke(窗口, {Nothing, New 播放器HDR状态事件参数("HDR 映射到 SDR")})
                HDR状态入口.Invoke(窗口, {Nothing, New 播放器HDR状态事件参数("HDR10 真实高亮")})
                Dim 消息字段 = GetType(播放器信息图层呈现器).GetField("操作消息列表", 标志)
                Dim 消息 = TryCast(消息字段?.GetValue(呈现器), System.Collections.IList)
                断言(消息 IsNot Nothing AndAlso 消息.Count > 0, "HDR 模式提示没有进入信息层。")
                Dim 文本字段 = 消息(0).GetType().GetField("文本", 标志)
                Dim 操作键字段 = 消息(0).GetType().GetField("操作键", 标志)
                Dim HDR消息 = 消息.Cast(Of Object)().
                    Where(Function(x) String.Equals(CStr(操作键字段?.GetValue(x)), "HDR模式", StringComparison.Ordinal)).
                    ToArray()
                断言(HDR消息.Length = 1, "连续 HDR 模式提示没有合并成一个。")
                断言(String.Equals(CStr(文本字段?.GetValue(HDR消息(0))), "HDR10 真实高亮", StringComparison.Ordinal),
                   "HDR 模式提示没有保留最新内容或稳定合并键。")
            Finally
                设置.实例对象.字体 = 原字体
                字体控制.更新所有控件字体属性()
                窗口.Close()
            End Try
        End Using
    End Sub

    Private Function 选择可用测试字体(ParamArray 排除字体 As String()) As String
        Dim MiSansMedium路径 = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Fonts),
                                         "MiSans-Medium.ttf")
        If File.Exists(MiSansMedium路径) Then
            Try
                Using 字体 As New Font("MiSans Medium", 11.0F, FontStyle.Regular, GraphicsUnit.Point)
                    If 字体.FontFamily.Name.Contains("MiSans", StringComparison.OrdinalIgnoreCase) Then
                        Return "MiSans Medium"
                    End If
                End Using
            Catch
            End Try
        End If
        Dim 候选 = {"Consolas", "Segoe UI", "Arial", "Tahoma", "Microsoft YaHei UI"}.
            Concat(FontFamily.Families.Select(Function(f) f.Name)).
            Distinct(StringComparer.CurrentCultureIgnoreCase)
        For Each 字体名称 In 候选
            Try
                Using 字体 As New Font(字体名称, 11.0F, FontStyle.Regular, GraphicsUnit.Point)
                    Dim 实际字体 = 字体.FontFamily.Name
                    If 排除字体.Any(Function(x) String.Equals(x, 实际字体,
                                                        StringComparison.CurrentCultureIgnoreCase)) Then Continue For
                    Return 实际字体
                End Using
            Catch
            End Try
        Next
        Return String.Empty
    End Function

    Private Sub 测试解码切换可见首帧音频门控(视频路径 As String)
        For Each 模式 In {解码模式.CPU, 解码模式.GPU}
            Using 输出窗口 As New Form With {
                .ClientSize = New Size(640, 360), .ShowInTaskbar = False,
                .FormBorderStyle = FormBorderStyle.None,
                .StartPosition = FormStartPosition.Manual,
                .Location = New Point(-32000, -32000)}
                输出窗口.Show()
                Application.DoEvents()
                Using 会话 As New 播放器会话(New 播放器配置 With {
                    .解码器 = 模式, .输出窗口句柄 = IntPtr.Zero,
                    .色彩模式 = 色彩输出模式.映射到SDR})
                    会话.设置音量(0.0F, True)
                    会话.打开Async(视频路径).GetAwaiter().GetResult()
                    Dim 打开快照 = 会话.当前快照
                    断言(打开快照.当前视频流 >= 0 AndAlso 打开快照.当前音频流 >= 0,
                       $"{模式} 首帧音频门控样本必须同时包含视频和音频。")
                    If 打开快照.总时长 > TimeSpan.FromMinutes(2) Then
                        会话.跳转(TimeSpan.FromTicks(打开快照.总时长.Ticks \ 3))
                    End If
                    会话.设置输出窗口(输出窗口.Handle)
                    会话.播放()

                    Dim 计时 = Stopwatch.StartNew()
                    Dim 已观察门控 As Boolean
                    Dim 首次Present As 播放器快照 = Nothing
                    Do
                        Application.DoEvents()
                        Dim 快照 = 会话.当前快照
                        If 快照.状态 = 播放状态.失败 Then
                            Throw New InvalidOperationException($"{模式} 首帧音频门控播放失败：{会话.最后错误消息}")
                        End If
                        If 快照.交换链呈现次数 = 0UL Then
                            已观察门控 = True
                            断言(快照.已解码音频帧数 = 0UL AndAlso
                                   快照.音频缓冲时长 = TimeSpan.Zero,
                               $"{模式} 在首次 DXGI Present 前已经解码或缓冲音频：" &
                               $"帧 {快照.已解码音频帧数}，缓冲 {快照.音频缓冲时长.TotalMilliseconds:F1} ms。")
                        Else
                            首次Present = 快照
                            Exit Do
                        End If
                        If 计时.Elapsed >= TimeSpan.FromSeconds(30) Then
                            Throw New TimeoutException($"等待{模式}候选会话首次可见帧超时。")
                        End If
                        Thread.Sleep(1)
                    Loop
                    断言(已观察门控 AndAlso 首次Present IsNot Nothing,
                       $"{模式} 没有覆盖首次 Present 前的音频门控窗口。")
                    Dim 音频恢复 = 等待快照(会话,
                        Function(x) x.交换链呈现次数 > 0UL AndAlso x.已解码音频帧数 > 0UL AndAlso
                            x.音频缓冲时长 > TimeSpan.Zero,
                        $"{模式} 首帧后的音频恢复")
                    Console.WriteLine($"{模式} 首帧门控：Present {首次Present.交换链呈现次数}，" &
                                      $"恢复音频帧/缓冲 {音频恢复.已解码音频帧数}/" &
                                      $"{音频恢复.音频缓冲时长.TotalMilliseconds:F1} ms。")
                End Using
            End Using
        Next
    End Sub

    Private Sub 测试信息层精确文本()
        Dim 文档 = SRT字幕解析器.解析(New StringReader(
            "1" & vbCrLf & "00:00:01,000 --> 00:00:02,000" & vbCrLf & "一" & vbCrLf & vbCrLf &
            "2" & vbCrLf & "00:00:03,000 --> 00:00:04,000" & vbCrLf & "二"))
        Using 字幕 As New 外部字幕轨道("C:\diagnostic\secret.srt", 外部字幕格式.SRT,
            New SRT字幕帧生成器(文档, New SRT字幕样式()), Nothing)
            Dim 弹幕 As New 弹幕资料库({
                New 弹幕项目(TimeSpan.FromSeconds(1), 弹幕类型.常规滚动, 1, 25, &HFFFFFFFFUI,
                    0, 0, "a", 1, "一"),
                New 弹幕项目(TimeSpan.FromSeconds(2), 弹幕类型.常规滚动, 1, 25, &HFFFFFFFFUI,
                    0, 0, "b", 2, "二")})
            Dim 字幕状态 As New 定时文字状态(New 原生定时文字状态 With {.命令数 = 1001UI})
            Dim 弹幕状态 As New 定时文字状态(New 原生定时文字状态 With {.命令数 = 2002UI})
            Dim 当前字幕 As 外部字幕轨道 = 字幕
            Dim 当前弹幕 As 弹幕资料库 = 弹幕
            Dim 当前字幕状态 As 定时文字状态 = 字幕状态
            Dim 当前弹幕状态 As 定时文字状态 = 弹幕状态
            Dim 信息 As New 媒体信息()
            信息.流.Add(New 媒体流信息 With {
                .索引 = 0, .类型 = "video", .编码 = "av1", .像素格式 = "yuv420p10le",
                .宽度 = 1920, .高度 = 1080, .平均帧率分子 = 24000, .平均帧率分母 = 1001,
                .比特率 = 5_000_000, .色度抽样 = "4:2:0", .色彩空间 = 9,
                .色彩原色 = 9, .色彩传递 = 16, .色彩范围 = 1,
                .是HDR = True, .HDR格式 = "HDR10+", .HDR兼容规格 = "HDR10"})
            信息.流.Add(New 媒体流信息 With {
                .索引 = 1, .类型 = "audio", .编码 = "flac", .采样率 = 48000,
                .原始采样位数 = 24, .声道数 = 2, .比特率 = 1_411_200,
                .输出采样率 = 48000, .输出有效采样位数 = 32, .输出声道数 = 2,
                .输出浮点 = True})
            Dim 快照 As New 播放器快照(New 原生播放器快照 With {
                .状态 = CUInt(播放状态.正在播放), .解码器 = CUInt(解码模式.CPU),
                .实际色彩模式 = CUInt(色彩输出模式.映射到SDR),
                .是HDR源 = 1UI,
                .位置100纳秒 = TimeSpan.FromHours(1).Ticks + TimeSpan.FromMinutes(2).Ticks + TimeSpan.FromSeconds(3).Ticks,
                .时长100纳秒 = TimeSpan.FromHours(2).Ticks, .当前视频流 = 0, .当前音频流 = 1,
                 .视频队列帧数 = 3UI, .已丢弃视频帧数 = 7000UL, .已合并视频帧数 = 5000UL,
                 .视频实时比特率 = 5_000_000UL, .音频实时比特率 = 1_411_200UL,
                .视频输出位深度 = 10UI,
                .HDR格式 = CUInt(HDR格式.HDR10Plus), .动态HDR元数据有效 = 1UI,
                .源峰值尼特 = 1242UI, .HDR有效目标峰值尼特 = 750UI,
                .音频缓冲100纳秒 = TimeSpan.FromMilliseconds(25).Ticks})

            Using 画面 As New 播放器画面控件 With {.ClientSize = New Size(2560, 1440)}
                Using 呈现器 As New 播放器信息图层呈现器(画面,
                    Function() 快照, Function() 信息, Function() "C:\media\movie.mkv",
                    Function() 当前字幕, Function() 当前弹幕,
                    Function() 当前字幕状态, Function() 当前弹幕状态,
                    Function() WASAPI共享模式.独占,
                    Sub(size, commands, sequence, frameRate) Return)
                    Dim 标志 = BindingFlags.Instance Or BindingFlags.NonPublic
                    GetType(播放器信息图层呈现器).GetField("最近实际帧率", 标志).SetValue(呈现器, 23.976R)
                    GetType(播放器信息图层呈现器).GetField("最近实时丢帧数", 标志).SetValue(呈现器, 2345UL)
                    Dim 实际 = 呈现器.读取调试文本行(信息, 快照, "C:\media\movie.mkv")
                    Dim 预期 As String() = {
                        "文件名：movie.mkv",
                        "时间戳：01:02:03 / 02:00:00 (52%)",
                        "视频：AV1 - CPU",
                        "输入：格式 YUV420P10LE   分辨率 1920x1080   帧率 23.976fps   实时码率 5 Mbps",
                        "色彩：采样 420   颜色矩阵 BT.2020   色域 BT.2020   传输特性 PQ   范围 Limited",
                        "HDR：HDR10+   逐帧动态元数据",
                        "输出：格式 RGB10A2 (10bit)   分辨率 2560x1440   色彩模式 映射 SDR",
                        "渲染：帧率 23.98fps   缓冲池 3帧   实时丢帧 2345   总丢帧 12000",
                        "音频：FLAC - WASAPI 独占",
                        "输入：采样 48000Hz   位深 24bit   声道数 2   实时码率 1.41 Mbps",
                        "输出：FLOAT PCM   采样 48000Hz   位深 32bit   声道数 2   实时延迟 25ms",
                        "字幕：SRT   总数量 2   当前正在渲染 1001",
                        "弹幕：哔哩哔哩 XML   总数量 2   当前正在渲染 2002"}
                    断言(实际.SequenceEqual(预期),
                        "信息层逐字段文本不符合中文标签、三空格分隔或字段白名单。" & vbCrLf &
                        String.Join(vbCrLf, 实际))
                    断言(Not 实际.Any(Function(x) x.Contains(","c)),
                       "信息层数值仍包含英文千分符。")
                    Dim 真实HDR快照 As New 播放器快照(New 原生播放器快照 With {
                        .实际色彩模式 = CUInt(色彩输出模式.峰值映射HDR), .是HDR源 = 1UI,
                        .HDR格式 = CUInt(HDR格式.HDR10Plus), .动态HDR元数据有效 = 1UI,
                        .源峰值尼特 = 1242UI, .HDR有效目标峰值尼特 = 750UI})
                    Dim 真实HDR行 = 呈现器.读取调试文本行(信息, 真实HDR快照, "C:\media\movie.mkv")
                    断言(真实HDR行.Any(Function(x) x = "HDR：HDR10+   逐帧动态元数据   源峰值 1242尼特   显示目标 750尼特"),
                       "真实 HDR 高亮模式没有显示源峰值与显示目标。")
                    Dim 灰HDR快照 As New 播放器快照(New 原生播放器快照 With {
                        .实际色彩模式 = CUInt(色彩输出模式.原始HDR按SDR呈现), .是HDR源 = 1UI,
                        .HDR格式 = CUInt(HDR格式.HDR10Plus), .动态HDR元数据有效 = 1UI,
                        .源峰值尼特 = 1242UI, .HDR有效目标峰值尼特 = 750UI})
                    Dim 灰HDR行 = 呈现器.读取调试文本行(信息, 灰HDR快照, "C:\media\movie.mkv")
                    断言(灰HDR行.Any(Function(x) x = "HDR：HDR10+   逐帧动态元数据") AndAlso
                           Not 灰HDR行.Any(Function(x) x.Contains("源峰值", StringComparison.Ordinal) OrElse
                                               x.Contains("显示目标", StringComparison.Ordinal)),
                       "原始 HDR 按 SDR 呈现模式不应显示亮度映射条目。")
                    断言(Not 实际.Any(Function(x) x.Contains("secret.srt", StringComparison.OrdinalIgnoreCase) OrElse
                                                x.Contains("secret.xml", StringComparison.OrdinalIgnoreCase) OrElse
                                                x.Contains("·", StringComparison.Ordinal)),
                        "信息层仍泄漏字幕/弹幕文件名或使用旧分隔符。")
                    Dim 色彩模式方法 = GetType(播放器信息图层呈现器).GetMethod(
                        "色彩模式文本", BindingFlags.Static Or BindingFlags.NonPublic)
                    Dim SDR快照 As New 播放器快照(New 原生播放器快照 With {
                        .实际色彩模式 = CUInt(色彩输出模式.映射到SDR)})
                    断言(String.Equals(CStr(色彩模式方法?.Invoke(Nothing,
                        New Object() {SDR快照})), "SDR", StringComparison.Ordinal) AndAlso
                        String.Equals(CStr(色彩模式方法?.Invoke(Nothing,
                        New Object() {快照})), "映射 SDR", StringComparison.Ordinal),
                        "信息层没有区分 SDR 片源与 HDR→SDR 映射。")

                    Using 按需字幕 As New 外部字幕轨道("C:\diagnostic\effect.ass",
                        外部字幕格式.ASS, Nothing, Nothing)
                        当前字幕 = 按需字幕
                        当前弹幕 = Nothing
                        当前字幕状态 = Nothing
                        当前弹幕状态 = Nothing
                        Dim 缺失信息 As New 媒体信息()
                        缺失信息.流.Add(New 媒体流信息 With {.索引 = 0, .类型 = "video"})
                        缺失信息.流.Add(New 媒体流信息 With {.索引 = 1, .类型 = "audio"})
                        Dim 缺失快照 As New 播放器快照(New 原生播放器快照 With {
                            .解码器 = CUInt(解码模式.CPU),
                            .实际色彩模式 = CUInt(色彩输出模式.映射到SDR),
                            .当前视频流 = 0, .当前音频流 = 1})
                        Dim 缺失行 = 呈现器.读取调试文本行(缺失信息, 缺失快照, String.Empty)
                        断言(缺失行.Contains("字幕：ASS   总数量 按需解码"),
                           "特效字幕总数量没有显示为按需解码。")
                        断言(缺失行.Contains("弹幕：未加载"),
                           "未加载弹幕没有收敛为单一状态条目。")
                        当前字幕 = Nothing
                        Dim 全未加载行 = 呈现器.读取调试文本行(缺失信息, 缺失快照, String.Empty)
                        断言(全未加载行.Contains("字幕：未加载") AndAlso
                               Not 全未加载行.Any(Function(x) x.StartsWith("字幕：", StringComparison.Ordinal) AndAlso
                                   (x.Contains("总数量", StringComparison.Ordinal) OrElse
                                    x.Contains("当前正在渲染", StringComparison.Ordinal))),
                           "未加载字幕仍显示了数量或渲染条目。")
                        断言(Not 缺失行.Any(Function(x) x.Contains("?", StringComparison.Ordinal) OrElse
                                                     x.StartsWith("文件名：", StringComparison.Ordinal) OrElse
                                                     x.StartsWith("输入：", StringComparison.Ordinal) OrElse
                                                     x.StartsWith("色彩：", StringComparison.Ordinal) OrElse
                                                     x.Contains("当前正在渲染", StringComparison.Ordinal)),
                           "信息层仍显示无法获取的字段或空条目。" & vbCrLf &
                           String.Join(vbCrLf, 缺失行))
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Sub 测试媒体信息按钮左右键()
        Using 窗口 As New Form1 With {.ShowInTaskbar = False, .Opacity = 0}
            窗口.Show()
            Application.DoEvents()
            Dim 标志 = BindingFlags.Instance Or BindingFlags.Public Or BindingFlags.NonPublic
            Dim 按钮 = 窗口.Controls.Find("MB_查看当前媒体信息", True).FirstOrDefault()
            Dim 画面 = DirectCast(GetType(Form1).GetField("画面控件", 标志)?.GetValue(窗口), Control)
            Dim 控制器 = DirectCast(GetType(Form1).GetField("播放控制器", 标志)?.GetValue(窗口),
                                播放器控制器)
            Dim 呈现器 = DirectCast(GetType(Form1).GetField("信息图层呈现器", 标志)?.GetValue(窗口),
                               播放器信息图层呈现器)
            Dim 点击入口 = 按钮?.GetType().GetMethod("OnMouseClick", 标志)
            Dim 可见字段 = GetType(播放器信息图层呈现器).GetField("调试可见", 标志)
            Dim 背景属性 = 按钮?.GetType().GetProperty("BackColor1", 标志)
            断言(按钮 IsNot Nothing AndAlso 画面 IsNot Nothing AndAlso 控制器 IsNot Nothing AndAlso
                   呈现器 IsNot Nothing AndAlso
                   点击入口 IsNot Nothing AndAlso 可见字段 IsNot Nothing AndAlso 背景属性 IsNot Nothing,
                "无法取得媒体信息按钮左右键回归所需成员。")
            Dim 已保存解码方式 = CType(设置.实例对象.解码方式, 解码模式)
            断言(控制器.解码器 = 已保存解码方式 AndAlso 控制器.解码器偏好 = 已保存解码方式,
               "播放器主窗口没有沿用已保存的解码方式。")

            Dim 原窗口数 = Application.OpenForms.OfType(Of Form媒体信息)().Count()
            点击入口.Invoke(按钮, {New MouseEventArgs(MouseButtons.Left, 1, 4, 4, 0)})
            Application.DoEvents()
            Dim 已打开窗口 = Application.OpenForms.OfType(Of Form媒体信息)().ToArray()
            断言(已打开窗口.Length = 原窗口数 + 1, "左键没有且仅没有打开一个媒体信息窗口。")
            断言(Not CBool(可见字段.GetValue(呈现器)), "左键错误切换了播放器信息层。")
            For Each 媒体窗口 In 已打开窗口
                媒体窗口.Close()
            Next
            Application.DoEvents()

            点击入口.Invoke(按钮, {New MouseEventArgs(MouseButtons.Right, 1, 4, 4, 0)})
            Application.DoEvents()
            断言(CBool(可见字段.GetValue(呈现器)), "右键没有打开播放器信息层。")
            断言(DirectCast(背景属性.GetValue(按钮), Color).ToArgb() = Color.Transparent.ToArgb(),
               "打开信息层后媒体信息按钮仍显示焦点背景色。")
            断言(Application.OpenForms.OfType(Of Form媒体信息)().Count() = 原窗口数,
               "右键错误打开了媒体信息窗口。")
            断言(画面.CanFocus AndAlso 画面.Focused, "右键切换信息层后焦点没有返回视频画面。")

            点击入口.Invoke(按钮, {New MouseEventArgs(MouseButtons.Right, 1, 4, 4, 0)})
            Application.DoEvents()
            断言(Not CBool(可见字段.GetValue(呈现器)), "第二次右键没有关闭播放器信息层。")
            窗口.Close()
        End Using
    End Sub

    Private Sub 测试媒体信息响度数据源()
        Using 窗口 As New Form媒体信息(audioPeakProvider:=
            Function() New Single() {1.0F, 0.1F, 0.01F})
            Dim 标志 = BindingFlags.Instance Or BindingFlags.Public Or BindingFlags.NonPublic
            Dim 刷新入口 = GetType(Form媒体信息).GetMethod("刷新响度条", 标志)
            断言(刷新入口 IsNot Nothing, "无法取得媒体信息响度刷新入口。")
            刷新入口.Invoke(窗口, Nothing)
            Dim 期望 = {0, -20, -40, -60}
            Dim 字段名 = {"EPB_L", "EPB_R", "EPB_C", "EPB_LFE"}
            For index = 0 To 字段名.Length - 1
                Dim 进度条 = GetType(Form媒体信息).GetProperty(字段名(index), 标志)?.GetValue(窗口)
                Dim 数值属性 = 进度条?.GetType().GetProperty("Value", 标志)
                Dim 实际值 = If(数值属性 Is Nothing, Integer.MinValue,
                    Convert.ToInt32(数值属性.GetValue(进度条)))
                断言(实际值 = 期望(index),
                   $"媒体信息响度条 {字段名(index)} 没有使用 PCM 峰值数据源：{实际值}，期望 {期望(index)}。")
            Next
        End Using
    End Sub

    Private Sub 测试剪辑区间逐帧(视频路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
            会话.设置音量(0.0F, True)
            会话.打开Async(视频路径).GetAwaiter().GetResult()
            Dim 初始 = 会话.当前快照
            Dim 目标位置 = TimeSpan.FromTicks(Math.Max(TimeSpan.FromSeconds(1).Ticks, 初始.总时长.Ticks \ 2))
            If 目标位置 >= 初始.总时长 Then 目标位置 = TimeSpan.FromTicks(初始.总时长.Ticks \ 2)
            Dim 跳转前呈现帧数 = 初始.已呈现视频帧数
            会话.跳转(目标位置)
            Dim 当前帧 = 等待快照(会话,
                Function(x) x.原始帧PTS <> Long.MinValue AndAlso x.已呈现视频帧数 > 跳转前呈现帧数 AndAlso
                            Math.Abs((x.播放位置 - 目标位置).TotalMilliseconds) < 100.0,
                "跳转到逐帧测试位置")

            会话.上一帧()
            Dim 上一帧 = 等待快照(会话,
                Function(x) x.状态 = 播放状态.已暂停 AndAlso x.原始帧PTS < 当前帧.原始帧PTS,
                "倒退一帧")
            会话.下一帧()
            Dim 返回帧 = 等待快照(会话,
                Function(x) x.状态 = 播放状态.已暂停 AndAlso x.原始帧PTS > 上一帧.原始帧PTS,
                "前进一帧")
            断言(返回帧.原始帧PTS = 当前帧.原始帧PTS,
               $"倒退再前进没有返回原帧：{当前帧.原始帧PTS} → {上一帧.原始帧PTS} → {返回帧.原始帧PTS}。")
            Console.WriteLine($"逐帧 PTS：{当前帧.原始帧PTS} → {上一帧.原始帧PTS} → {返回帧.原始帧PTS}")

            Dim 游标 = 返回帧
            Dim 连续倒退次数 = 0
            ' Keep this boundary regression valid for the supplied source's
            ' frame rate instead of assuming that 300 presses reaches zero.
            Dim 估计单帧秒 = If(返回帧.帧时间基分母 > 0 AndAlso 当前帧.原始帧PTS > 上一帧.原始帧PTS,
                Math.Max(0.0001R, (当前帧.原始帧PTS - 上一帧.原始帧PTS) *
                    CDbl(返回帧.帧时间基分子) / 返回帧.帧时间基分母), 1.0R / 24.0R)
            Dim 最大连续倒退次数 = Math.Max(300, CInt(Math.Ceiling(返回帧.播放位置.TotalSeconds / 估计单帧秒)) + 8)
            While 游标.播放位置 > TimeSpan.FromMilliseconds(20) AndAlso 连续倒退次数 < 最大连续倒退次数
                Dim 倒退前PTS = 游标.原始帧PTS
                Dim 倒退前位置 = 游标.播放位置
                会话.上一帧()
                游标 = 等待快照(会话,
                    Function(x) x.状态 = 播放状态.已暂停 AndAlso x.原始帧PTS < 倒退前PTS AndAlso
                                x.播放位置 < 倒退前位置,
                    $"连续倒退第 {连续倒退次数 + 1} 帧")
                连续倒退次数 += 1
            End While
            断言(游标.播放位置 <= TimeSpan.FromMilliseconds(20),
               $"连续倒退 {连续倒退次数} 帧后停在 {游标.播放位置.TotalSeconds:F3}s。")
            Console.WriteLine($"连续倒退 {连续倒退次数} 帧到 {游标.播放位置.TotalSeconds:F3}s。")

            Dim 突发起点 = TimeSpan.FromTicks(初始.总时长.Ticks * 2 \ 5)
            会话.跳转(突发起点)
            等待快照(会话,
                Function(x) Math.Abs((x.播放位置 - 突发起点).TotalMilliseconds) < 100.0,
                "跳转到突发逐帧测试位置")

            Using 跳转完成 As New ManualResetEventSlim(False)
                Dim 完成处理 As EventHandler(Of 播放器事件参数) =
                    Sub(sender, e)
                        If e.详情JSON.Contains("""operation"":""seek""", StringComparison.Ordinal) Then
                            跳转完成.Set()
                        End If
                    End Sub
                AddHandler 会话.操作完成, 完成处理
                Try
                    Dim 响应计时 = Stopwatch.StartNew()
                    For 索引 = 1 To 1000
                        会话.上一帧()
                    Next
                    会话.跳转(TimeSpan.FromTicks(初始.总时长.Ticks \ 3))
                    断言(跳转完成.Wait(TimeSpan.FromSeconds(5)),
                       "突发逐帧请求阻塞了后续跳转，逐帧命令队列可能仍在无界增长。")
                    Console.WriteLine($"1000 次突发倒退后的跳转响应：{响应计时.Elapsed.TotalMilliseconds:F0} ms。")

                    跳转完成.Reset()
                    响应计时.Restart()
                    For 索引 = 1 To 1000
                        会话.上一关键帧()
                    Next
                    会话.跳转(TimeSpan.FromTicks(初始.总时长.Ticks \ 4))
                    断言(跳转完成.Wait(TimeSpan.FromSeconds(5)),
                       "突发关键帧请求阻塞了后续跳转，导航命令队列可能仍在无界增长。")
                    Console.WriteLine($"1000 次突发关键帧倒退后的跳转响应：{响应计时.Elapsed.TotalMilliseconds:F0} ms。")
                Finally
                    RemoveHandler 会话.操作完成, 完成处理
                End Try
            End Using
        End Using
    End Sub

    Private Function 等待快照(会话 As 播放器会话, 条件 As Func(Of 播放器快照, Boolean), 操作 As String) As 播放器快照
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            If 条件(快照) Then Return 快照
            If 快照.状态 = 播放状态.失败 Then
                Throw New InvalidOperationException($"{操作}时播放器失败：{会话.最后错误消息}")
            End If
            If 计时.Elapsed >= TimeSpan.FromSeconds(10) Then
                Throw New TimeoutException($"等待{操作}超时：位置 {快照.播放位置.TotalSeconds:F3}s，PTS {快照.原始帧PTS}，" &
                    $"色彩 {快照.请求色彩模式}/{快照.实际色彩模式}/{快照.视频输出位深度}bit，" &
                    $"视频帧/Present {快照.已呈现视频帧数}/{快照.交换链呈现次数}，" &
                    $"原生错误：{会话.最后错误消息}")
            End If
            Thread.Sleep(5)
        Loop
    End Function

    Private Sub 测试GPU解码矩阵(目录 As String)
        If Not Directory.Exists(目录) Then Throw New DirectoryNotFoundException(目录)
        Dim 视频 = Directory.EnumerateFiles(目录).
            Where(Function(x) {".mp4", ".mkv", ".mov", ".webm"}.
                Contains(Path.GetExtension(x), StringComparer.OrdinalIgnoreCase)).
            OrderBy(Function(x) x, StringComparer.OrdinalIgnoreCase).
            ToArray()
        断言(视频.Length > 0, $"GPU 解码矩阵目录没有视频：{目录}")

        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(320, 180),
            .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .ShowInTaskbar = False,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(-10000, -10000)
        }
            输出窗口.Show()
            Application.DoEvents()
            For Each 路径 In 视频
                Using 会话 As New 播放器会话(New 播放器配置 With {
                    .解码器 = 解码模式.GPU,
                    .色彩模式 = 色彩输出模式.映射到SDR,
                    .输出窗口句柄 = 输出窗口.Handle
                })
                    会话.设置音量(0.0F, True)
                    会话.打开Async(路径).GetAwaiter().GetResult()
                    Dim 打开快照 = 会话.当前快照
                    断言(打开快照.解码器 = 解码模式.GPU OrElse 打开快照.解码器 = 解码模式.CPU,
                       $"{Path.GetFileName(路径)} 返回了无效解码模式。")
                    会话.播放()
                    Dim 解码快照 = 等待快照(会话,
                        Function(x) x.已解码视频帧数 > 0 AndAlso x.已呈现视频帧数 > 0,
                        $"{Path.GetFileName(路径)} 首帧")
                    Console.WriteLine($"{Path.GetFileName(路径)}：{解码快照.解码器}，" &
                                      $"解码 {解码快照.已解码视频帧数} 帧，呈现 {解码快照.已呈现视频帧数} 帧")
                End Using
            Next
        End Using
    End Sub

    Private Sub 测试视频缩放路径(视频路径 As String)
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(960, 540),
            .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .ShowInTaskbar = False,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(-10000, -10000)
        }
            输出窗口.Show()
            Application.DoEvents()
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.GPU,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .输出窗口句柄 = 输出窗口.Handle
            })
                会话.设置音量(0.0F, True)
                会话.打开Async(视频路径).GetAwaiter().GetResult()
                会话.播放()
                Dim VP快照 = 等待快照(会话, Function(x) x.交换链呈现次数 > 0,
                                   "视频自动渲染首帧")
                Console.WriteLine($"首帧：解码 {VP快照.解码器}，路径 {VP快照.视频缩放}")
                断言(VP快照.解码器 = 解码模式.GPU, "测试视频没有保持 D3D11VA 解码。")
                断言(VP快照.视频缩放 = 视频缩放模式.着色器,
                   "高质量管线没有在受支持的 D3D11VA 输入上激活自有缩放着色器。")
                Dim 第一秒 = 等待快照(会话,
                    Function(x) x.播放位置 >= TimeSpan.FromSeconds(1.1) AndAlso
                        x.视频实时比特率 > 0 AndAlso x.音频实时比特率 > 0,
                    "整秒实时音视频码率")
                Dim 秒索引 = CInt(Math.Floor(第一秒.播放位置.TotalSeconds))
                Dim 视频码率 = 第一秒.视频实时比特率
                Dim 音频码率 = 第一秒.音频实时比特率
                Do
                    Dim 同秒快照 = 会话.当前快照
                    If CInt(Math.Floor(同秒快照.播放位置.TotalSeconds)) <> 秒索引 Then Exit Do
                    断言(同秒快照.解码器 = 解码模式.GPU,
                       "样例视频在持续播放期间自动回退到了 CPU。")
                    断言(同秒快照.视频实时比特率 = 视频码率 AndAlso
                           同秒快照.音频实时比特率 = 音频码率,
                       "实时码率在同一播放秒内发生了变化。")
                    Thread.Sleep(10)
                Loop
                Dim 后续快照 = 等待快照(会话,
                    Function(x) x.播放位置 >= TimeSpan.FromSeconds(4.1),
                    "样例 GPU 持续解码")
                断言(后续快照.解码器 = 解码模式.GPU,
                   "样例视频在持续播放期间自动回退到了 CPU。")
            End Using

            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .输出窗口句柄 = 输出窗口.Handle
            })
                会话.设置音量(0.0F, True)
                会话.打开Async(视频路径).GetAwaiter().GetResult()
                会话.播放()
                Dim CPU快照 = 等待快照(会话,
                    Function(x) x.交换链呈现次数 > 0 AndAlso
                        x.视频缩放 = 视频缩放模式.着色器,
                    "CPU 解码的自有缩放路径")
                断言(CPU快照.解码器 = 解码模式.CPU, "CPU 高质量缩放测试意外改变了解码器。")
            End Using
        End Using
    End Sub

    Private Sub 测试SDR灰阶像素(视频路径 As String, 参考图路径 As String)
        Using 参考图 As New Bitmap(参考图路径)
            断言(参考图.Width = 1280 AndAlso 参考图.Height = 720,
               $"SDR 像素回归要求 1280x720 参考图，实际为 {参考图.Width}x{参考图.Height}。")
            Dim 采样点 = {
                New Point(10, 10), New Point(300, 100), New Point(640, 100),
                New Point(900, 100), New Point(1270, 700)}
            Dim 参考值 = 读取像素平均值(参考图, 采样点)
            断言(参考值.All(Function(value) value = 128),
               $"参考图灰阶采样不是 RGB 128：{String.Join(",", 参考值)}。")

            Using 输出窗口 As New Form With {
                .ClientSize = New Drawing.Size(1280, 720),
                .FormBorderStyle = FormBorderStyle.None,
                .ShowInTaskbar = False,
                .StartPosition = FormStartPosition.Manual,
                .Location = New Point(-10000, -10000)
            }
                输出窗口.Show()
                Application.DoEvents()
                Dim GPU值 As Integer() = Nothing
                Using 会话 As New 播放器会话(New 播放器配置 With {
                    .解码器 = 解码模式.GPU,
                    .色彩模式 = 色彩输出模式.映射到SDR,
                    .输出窗口句柄 = 输出窗口.Handle
                })
                    会话.设置音量(0.0F, True)
                    会话.打开Async(视频路径).GetAwaiter().GetResult()
                    会话.播放()
                    Dim GPU快照 = 等待快照(会话,
                        Function(x) x.交换链呈现次数 > 1 AndAlso
                            x.视频缩放 = 视频缩放模式.着色器,
                        "SDR 自有缩放像素帧")
                    断言(GPU快照.解码器 = 解码模式.GPU,
                       "SDR 像素测试片没有保持 GPU 解码。")
                    GPU值 = 读取视频像素平均值(会话, 采样点)
                End Using

                Dim CPU值 As Integer() = Nothing
                Using 会话 As New 播放器会话(New 播放器配置 With {
                    .解码器 = 解码模式.CPU,
                    .色彩模式 = 色彩输出模式.映射到SDR,
                    .输出窗口句柄 = 输出窗口.Handle
                })
                    会话.设置音量(0.0F, True)
                    会话.打开Async(视频路径).GetAwaiter().GetResult()
                    会话.播放()
                    Dim 快照 = 等待快照(会话,
                        Function(x) x.交换链呈现次数 > 1 AndAlso
                            x.视频缩放 = 视频缩放模式.着色器,
                        "SDR CPU 自有缩放像素帧")
                    断言(快照.解码器 = 解码模式.CPU,
                       "SDR CPU 像素测试意外改变了解码器。")
                    CPU值 = 读取视频像素平均值(会话, 采样点)
                End Using
                Console.WriteLine($"灰阶 RGB：文件 {String.Join(",", 参考值)}；" &
                                  $"GPU shader {String.Join(",", GPU值)}；" &
                                  $"CPU shader {String.Join(",", CPU值)}")
                断言(最大通道差(参考值, GPU值) <= 1,
                   "GPU 自有缩放路径改变了 SDR 灰阶码值。")
                断言(最大通道差(参考值, CPU值) <= 1,
                   "CPU 自有缩放路径改变了 SDR 灰阶码值。")
            End Using
        End Using
    End Sub

    Private Sub 测试SDR桌面像素(图像路径 As String)
        Dim 采样点 = {
            New Point(10, 10), New Point(300, 100), New Point(640, 100),
            New Point(900, 100), New Point(1270, 700)}
        Dim 参考值 As Integer()
        Using 参考图 As New Bitmap(图像路径)
            断言(参考图.Size = New Drawing.Size(1280, 720),
               $"SDR 桌面像素回归要求 1280x720 参考图，实际为 {参考图.Width}x{参考图.Height}。")
            参考值 = 读取像素平均值(参考图, 采样点)
        End Using

        Dim 屏幕边界 = Screen.PrimaryScreen.Bounds
        Dim 屏幕左上 = New Point(
            屏幕边界.Left + (屏幕边界.Width - 1280) \ 2,
            屏幕边界.Top + (屏幕边界.Height - 720) \ 2)
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(1280, 720),
            .FormBorderStyle = FormBorderStyle.None,
            .ShowInTaskbar = True,
            .StartPosition = FormStartPosition.Manual,
            .Location = 屏幕左上
        }
            Using 视频窗口 As New Panel With {.Dock = DockStyle.Fill, .BackColor = Color.Black}
                输出窗口.Controls.Add(视频窗口)
                输出窗口.Show()
                输出窗口.BringToFront()
                Application.DoEvents()
                Using 会话 As New 播放器会话(New 播放器配置 With {
                    .解码器 = 解码模式.CPU,
                    .色彩模式 = 色彩输出模式.映射到SDR,
                    .输出窗口句柄 = 视频窗口.Handle
                })
                    会话.设置音量(0.0F, True)
                    会话.打开Async(图像路径).GetAwaiter().GetResult()
                    等待快照(会话, Function(x) x.交换链呈现次数 > 0,
                             "SDR 图像打开后首帧")
                    Dim 后缓冲值 = 读取视频像素平均值(会话, 采样点)

                    Thread.Sleep(100)
                    Application.DoEvents()
                    Using 桌面截取 As New Bitmap(1280, 720, Imaging.PixelFormat.Format32bppArgb)
                        Using 绘图 = Graphics.FromImage(桌面截取)
                            绘图.CopyFromScreen(视频窗口.PointToScreen(Point.Empty), Point.Empty,
                                               桌面截取.Size, CopyPixelOperation.SourceCopy)
                        End Using
                        Dim 桌面值 = 读取像素平均值(桌面截取, 采样点)
                        Console.WriteLine($"SDR 图像灰阶：原图 {String.Join(",", 参考值)}；" &
                                          $"D3D11后缓冲 {String.Join(",", 后缓冲值)}；" &
                                          $"桌面截取 {String.Join(",", 桌面值)}")
                        断言(最大通道差(参考值, 后缓冲值) = 0,
                           "SDR 图像在应用 D3D11 后缓冲内已发生色值改变。")
                        断言(最大通道差(参考值, 桌面值) = 0,
                           "SDR 图像在 Windows HDR 桌面合成后发生色值改变。")
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Sub 测试图片打开首帧(图像路径 As String)
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(640, 360),
            .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .ShowInTaskbar = False,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(-10000, -10000)
        }
            输出窗口.Show()
            Application.DoEvents()
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.GPU,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .输出窗口句柄 = 输出窗口.Handle
            })
                会话.设置音量(0.0F, True)
                会话.打开Async(图像路径).GetAwaiter().GetResult()
                Dim 快照 = 等待快照(会话,
                    Function(x) x.状态 = 播放状态.就绪 AndAlso
                        x.交换链呈现次数 > 0 AndAlso x.已呈现视频帧数 > 0,
                    "图片打开后首帧")
                断言(快照.解码器 = 解码模式.CPU,
                   "静态图片没有使用稳定的软件首帧解码路径。")
                Dim 输出尺寸 = 输出窗口.ClientSize
                If 快照.视频宽度 <= CUInt(输出尺寸.Width) AndAlso
                   快照.视频高度 <= CUInt(输出尺寸.Height) AndAlso
                   (快照.视频宽度 < CUInt(输出尺寸.Width) OrElse
                    快照.视频高度 < CUInt(输出尺寸.Height)) Then
                    Dim 角落像素 = 会话.读取视频输出像素(0, 0)
                    断言(角落像素.R = 0 AndAlso 角落像素.G = 0 AndAlso 角落像素.B = 0,
                       $"小尺寸图片没有按原始尺寸居中，输出左上角为 " &
                       $"RGB({角落像素.R},{角落像素.G},{角落像素.B})。")
                End If
            End Using
        End Using
    End Sub

    Private Function 读取视频像素平均值(会话 As 播放器会话, 采样点 As Point()) As Integer()
        Dim 总红 As Integer
        Dim 总绿 As Integer
        Dim 总蓝 As Integer
        For Each 点 In 采样点
            Dim 像素 = 会话.读取视频输出像素(点.X, 点.Y)
            总红 += 像素.R
            总绿 += 像素.G
            总蓝 += 像素.B
        Next
        Return {CInt(Math.Round(总红 / CDbl(采样点.Length))),
                CInt(Math.Round(总绿 / CDbl(采样点.Length))),
                CInt(Math.Round(总蓝 / CDbl(采样点.Length)))}
    End Function

    Private Function 读取像素平均值(图像 As Bitmap, 采样点 As Point()) As Integer()
        Dim 总红 As Integer
        Dim 总绿 As Integer
        Dim 总蓝 As Integer
        For Each 点 In 采样点
            Dim 像素 = 图像.GetPixel(点.X, 点.Y)
            总红 += 像素.R
            总绿 += 像素.G
            总蓝 += 像素.B
        Next
        Return {CInt(Math.Round(总红 / CDbl(采样点.Length))),
                CInt(Math.Round(总绿 / CDbl(采样点.Length))),
                CInt(Math.Round(总蓝 / CDbl(采样点.Length)))}
    End Function

    Private Sub 测试ASS编码兼容()
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance)
        Dim 临时目录 = Path.Combine(Path.GetTempPath(), "fff-player-ass-encoding-" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(临时目录)
        Dim 脚本模板 = String.Join(vbLf, {
            "[Script Info]",
            "Title: {TEST_TEXT}",
            "ScriptType: v4.00+",
            "PlayResX: 320",
            "PlayResY: 180",
            "",
            "[V4+ Styles]",
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding",
            "Style: Default,Arial,32,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,5,0,0,0,1",
            "",
            "[Events]",
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
            "Comment: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{TEST_TEXT}",
            "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\p1}m 80 50 l 240 50 240 130 80 130{\p0}"})
        Const Unicode测试文本 = "编码兼容 Café 日本語 한국어"
        Dim 编码样本 As (名称 As String, 编码 As Encoding, 测试文本 As String)() = {
            ("utf8", New UTF8Encoding(False, True), Unicode测试文本),
            ("utf8-bom", New UTF8Encoding(True, True), Unicode测试文本),
            ("utf16-le", New UnicodeEncoding(False, True, True), Unicode测试文本),
            ("utf16-le-nobom", New UnicodeEncoding(False, False, True), Unicode测试文本),
            ("utf16-be", New UnicodeEncoding(True, True, True), Unicode测试文本),
            ("utf16-be-nobom", New UnicodeEncoding(True, False, True), Unicode测试文本),
            ("utf32-le", New UTF32Encoding(False, True, True), Unicode测试文本),
            ("utf32-le-nobom", New UTF32Encoding(False, False, True), Unicode测试文本),
            ("utf32-be", New UTF32Encoding(True, True, True), Unicode测试文本),
            ("utf32-be-nobom", New UTF32Encoding(True, False, True), Unicode测试文本),
            ("gb18030", Encoding.GetEncoding(54936, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback), "简体中文编码检测"),
            ("big5", Encoding.GetEncoding(950, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback), "繁體中文編碼檢測"),
            ("shift-jis", Encoding.GetEncoding(932, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback), "日本語エンコーディング"),
            ("korean", Encoding.GetEncoding(949, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback), "한국어 인코딩"),
            ("windows-1252", Encoding.GetEncoding(1252, EncoderFallback.ExceptionFallback, DecoderFallback.ExceptionFallback), "Café déjà vu")}
        Try
            For Each 样本 In 编码样本
                Dim 路径 = Path.Combine(临时目录, 样本.名称 & ".ass")
                File.WriteAllText(路径, 脚本模板.Replace("{TEST_TEXT}", 样本.测试文本), 样本.编码)
                Using 生成器 As New ASS特效字幕帧生成器(路径, 临时目录)
                    Dim 帧 = 生成器.生成帧(TimeSpan.FromSeconds(1), 320, 180)
                    断言(帧 IsNot Nothing AndAlso 帧.像素BGRA.Length > 0,
                       $"{样本.名称} ASS 没有生成字幕帧。")
                End Using
            Next
        Finally
            Directory.Delete(临时目录, True)
        End Try
    End Sub

    Private Sub 测试ASS文件(字幕路径 As String, 时间 As TimeSpan)
        Using 轨道 = 外部字幕自动加载器.加载字幕(字幕路径, 字幕路径)
            断言(轨道.ASS特效生成器 IsNot Nothing, "播放器没有把 ASS 文件交给 libass 加载。")
            Dim 帧 = 轨道.ASS特效生成器.生成帧(时间, 1280, 720)
            断言(帧 IsNot Nothing AndAlso 帧.像素BGRA.Length > 0,
               $"ASS 在 {时间.TotalMilliseconds:F0} 毫秒没有生成可见字幕帧。")
        End Using
    End Sub

    Private Function 最大通道差(预期 As Integer(), 实际 As Integer()) As Integer
        Return Enumerable.Range(0, Math.Min(预期.Length, 实际.Length)).
            Max(Function(index) Math.Abs(预期(index) - 实际(index)))
    End Function

    Private Sub 测试ASS特效字幕(视频路径 As String, ASS路径 As String)
        Dim 字体目录 = ASS媒体字体发现器.查找字体目录(视频路径)
        断言(字体目录.Any(Function(x) String.Equals(Path.GetFileName(x), "Fonts", StringComparison.OrdinalIgnoreCase)),
           "没有发现视频目录下的 Fonts 文件夹。")

        Dim 已释放生成器 As ASS特效字幕帧生成器
        Using 轨道 = 外部字幕自动加载器.加载字幕(ASS路径, 视频路径)
            断言(轨道.ASS特效生成器 IsNot Nothing, "ASS 轨道没有使用 libass。")
            已释放生成器 = 轨道.ASS特效生成器
            断言(已释放生成器.生成帧(TimeSpan.Zero, 1280, 720) Is Nothing,
               "无字幕时刻没有生成透明清空帧。")
            Dim OP帧 = 已释放生成器.生成帧(TimeSpan.FromSeconds(97), 1280, 720)
            断言(OP帧 IsNot Nothing AndAlso OP帧.像素BGRA.Length > 0,
               "OP 的模糊、定位字幕没有生成位图。")
            断言(是预乘BGRA(OP帧.像素BGRA), "libass 位图没有转换为 Direct2D 所需的预乘 BGRA。")
            测试ASS半透明像素(视频路径)
            Using 画面 As New 播放器画面控件()
                Using 呈现器 As New 播放器定时文字图层呈现器(画面, Function() Nothing,
                    Function() 轨道, Sub(size, commands, sequence, frameRate) Return)
                    Dim 命令 = 呈现器.生成命令(New Size(1280, 720), 1280UI, 720UI,
                        TimeSpan.FromSeconds(97), 轨道)
                    断言(命令.Count = 1 AndAlso 命令(0).是位图 AndAlso 命令(0).位图像素BGRA.Length > 0,
                       "ASS 特效帧没有进入独立 GPU 字幕图层。")
                End Using
            End Using

            Dim 淡入前 = 已释放生成器.生成帧(TimeSpan.FromSeconds(1227.2), 1280, 720)
            Dim 淡入中 = 已释放生成器.生成帧(TimeSpan.FromSeconds(1227.25), 1280, 720)
            Dim 淡入后 = 已释放生成器.生成帧(TimeSpan.FromSeconds(1227.35), 1280, 720)
            断言(淡入前 Is Nothing AndAlso 淡入中 IsNot Nothing AndAlso 淡入后 IsNot Nothing AndAlso
               淡入中.内容标识 <> 淡入后.内容标识,
               "ASS 的 fad/blur 动画没有随媒体时间更新。")
        End Using
        Dim 已释放 = False
        Try
            已释放生成器.生成帧(TimeSpan.Zero, 1280, 720)
        Catch ex As ObjectDisposedException
            已释放 = True
        End Try
        断言(已释放, "ASS 轨道释放后原生 libass 句柄仍可使用。")
        Dim 字体文件 = Directory.EnumerateFiles(字体目录.First()).First(
            Function(x) {".ttf", ".otf", ".ttc"}.Contains(Path.GetExtension(x), StringComparer.OrdinalIgnoreCase))
        Using 字体流 = New FileStream(字体文件, FileMode.Open, FileAccess.Read, FileShare.None)
            断言(字体流.Length > 0, "释放 libass 后字体文件不可读取。")
        End Using
        Console.WriteLine($"ASS 字体目录：{String.Join("；", 字体目录)}")
    End Sub

    Private Function 是预乘BGRA(像素 As Byte()) As Boolean
        For index = 0 To 像素.Length - 4 Step 4
            Dim alpha = 像素(index + 3)
            If 像素(index) > alpha OrElse 像素(index + 1) > alpha OrElse 像素(index + 2) > alpha Then Return False
        Next
        Return True
    End Function

    Private Sub 测试ASS半透明像素(媒体路径 As String)
        Dim 临时路径 = Path.Combine(Path.GetTempPath(),
            "fff-player-ass-alpha-" & Guid.NewGuid().ToString("N") & ".ass")
        Dim 脚本 = String.Join(vbLf, {
            "[Script Info]",
            "ScriptType: v4.00+",
            "PlayResX: 320",
            "PlayResY: 180",
            "",
            "[V4+ Styles]",
            "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding",
            "Style: Default,Arial,48,&H80FFFFFF,&H80FFFFFF,&H80000000,&H80000000,0,0,0,0,100,100,0,0,1,0,0,5,0,0,0,1",
            "",
            "[Events]",
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text",
            "Dialogue: 0,0:00:00.00,0:00:10.00,Default,,0,0,0,,{\p1}m 80 50 l 240 50 240 130 80 130{\p0}"})
        Try
            File.WriteAllText(临时路径, 脚本, New UTF8Encoding(False))
            Using 生成器 As New ASS特效字幕帧生成器(临时路径, 媒体路径)
                Dim 帧 = 生成器.生成帧(TimeSpan.FromSeconds(1), 320, 180)
                断言(帧 IsNot Nothing AndAlso 帧.像素BGRA.Length > 0,
                   "半透明 ASS 测试没有生成位图。")
                Dim 最大Alpha索引 = 3
                For index = 7 To 帧.像素BGRA.Length - 1 Step 4
                    If 帧.像素BGRA(index) > 帧.像素BGRA(最大Alpha索引) Then 最大Alpha索引 = index
                Next
                Dim alpha = CInt(帧.像素BGRA(最大Alpha索引))
                Dim blue = CInt(帧.像素BGRA(最大Alpha索引 - 3))
                Dim green = CInt(帧.像素BGRA(最大Alpha索引 - 2))
                Dim red = CInt(帧.像素BGRA(最大Alpha索引 - 1))
                断言(alpha >= 120 AndAlso alpha <= 132 AndAlso
                   blue >= alpha - 16 AndAlso green >= alpha - 16 AndAlso red >= alpha - 16 AndAlso
                   blue <= alpha AndAlso green <= alpha AndAlso red <= alpha,
                   $"半透明 ASS 像素发生重复预乘：BGRA={blue},{green},{red},{alpha}。")
            End Using
        Finally
            If File.Exists(临时路径) Then File.Delete(临时路径)
        End Try
    End Sub

    Private Sub 测试音频规格回归(视频路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU,
            .色彩模式 = 色彩输出模式.映射到SDR,
            .SDR峰值尼特 = 100.0F,
            .HDR峰值尼特 = 1000.0F,
            .SDR纸白尼特 = 203.0F
        })
            会话.设置音量(0.0F, True)
            会话.打开Async(视频路径).GetAwaiter().GetResult()
            会话.播放()
            等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(60))
            Dim 开头结果 = 采样播放(会话, 4.0, Nothing)
            Console.WriteLine($"《你的名字》开头音频：{格式化播放报告(开头结果)}，" &
                              $"PTS抖动 {开头结果.音频时间戳抖动帧数}、断点 {开头结果.音频不连续次数}、" &
                              $"补零/裁样 {开头结果.音频插入静音帧数}/{开头结果.音频丢弃重叠帧数}")
            验证连续PCM结果(开头结果, "《你的名字》开头")

            会话.跳转(TimeSpan.FromSeconds(1000))
            等待预热(会话, TimeSpan.FromSeconds(1001), TimeSpan.FromSeconds(90))
            Dim 跳转结果 = 采样播放(会话, 4.0, Nothing)
            Console.WriteLine($"《你的名字》1000 秒音频：{格式化播放报告(跳转结果)}，" &
                              $"PTS抖动 {跳转结果.音频时间戳抖动帧数}、断点 {跳转结果.音频不连续次数}、" &
                              $"补零/裁样 {跳转结果.音频插入静音帧数}/{跳转结果.音频丢弃重叠帧数}")
            验证连续PCM结果(跳转结果, "《你的名字》1000 秒跳转")
        End Using
    End Sub

    Private Sub 测试音频延迟回归()
        Dim 临时路径 = Path.Combine(Path.GetTempPath(),
            "fff-player-audio-latency-" & Guid.NewGuid().ToString("N") & ".wav")
        Dim 换片路径 = Path.Combine(Path.GetTempPath(),
            "fff-player-audio-switch-" & Guid.NewGuid().ToString("N") & ".wav")
        Try
            创建音频延迟测试文件(临时路径)
            File.Copy(临时路径, 换片路径)
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .SDR峰值尼特 = 100.0F,
                .HDR峰值尼特 = 1000.0F,
                .SDR纸白尼特 = 203.0F
            })
                ' -54 dB 左右，能检出输出 PCM 峰值而不会产生明显测试声。
                会话.设置音量(0.002F, False)
                会话.打开Async(临时路径).GetAwaiter().GetResult()
                会话.播放()
                等待音频预热(会话, TimeSpan.FromMilliseconds(500), TimeSpan.FromSeconds(10))
                验证音频峰值(会话, "WASAPI 共享")
                Dim 共享结果 = 采样播放(会话, 4.0, Nothing)
                验证纯音频结果(共享结果, "WASAPI 共享")
                测试切换前音频丢弃(会话)
                会话.播放()
                Dim 丢弃恢复目标 = 会话.当前快照.播放位置 + TimeSpan.FromMilliseconds(500)
                等待音频预热(会话, 丢弃恢复目标, TimeSpan.FromSeconds(10))
                验证音频峰值(会话, "切换前音频丢弃后恢复")

                ' 空端点表示继续跟随 Windows 默认设备。即使默认设备标识没有变化，
                ' 这也覆盖与系统热切换相同的渲染器重建和媒体时钟重锚路径。
                Using 已重建 As New ManualResetEventSlim(False)
                    Dim 设备处理 As EventHandler(Of 播放器事件参数) =
                        Sub(sender, e) 已重建.Set()
                    AddHandler 会话.设备变化, 设备处理
                    Try
                        Dim 恢复目标 = 会话.当前快照.播放位置 + TimeSpan.FromMilliseconds(500)
                        会话.设置音频端点(Nothing)
                        断言(已重建.Wait(TimeSpan.FromSeconds(10)), "切回 Windows 默认音频端点超时。")
                        等待音频预热(会话, 恢复目标, TimeSpan.FromSeconds(10))
                        验证音频峰值(会话, "WASAPI 默认端点重建")
                    Finally
                        RemoveHandler 会话.设备变化, 设备处理
                    End Try
                End Using

                切换到独占模式(会话)
                ' 同模式请求也必须确认，控制器的异步换片依赖这一幂等事件合同。
                切换到独占模式(会话)
                Dim 独占目标 = 会话.当前快照.播放位置 + TimeSpan.FromMilliseconds(500)
                等待音频预热(会话, 独占目标, TimeSpan.FromSeconds(10))
                验证音频峰值(会话, "WASAPI 独占")
                Dim 独占结果 = 采样播放(会话, 4.0, Nothing)
                验证纯音频结果(独占结果, "WASAPI 独占")
                测试独占暂停释放与恢复(会话, 换片路径)
            End Using
            测试音频端点不可用仍可播放(临时路径)
            测试控制器独占换片(临时路径, 换片路径)
        Finally
            If File.Exists(临时路径) Then File.Delete(临时路径)
            If File.Exists(换片路径) Then File.Delete(换片路径)
        End Try
    End Sub

    Private Sub 测试切换前音频丢弃(会话 As 播放器会话)
        Dim 丢弃前 = 等待快照(会话,
            Function(x) x.状态 = 播放状态.正在播放 AndAlso x.音频缓冲时长 > TimeSpan.Zero,
            "切换前音频丢弃预热")
        会话.丢弃音频输出()
        Dim 丢弃后 = 等待快照(会话,
            Function(x) x.状态 = 播放状态.已暂停 AndAlso x.音频缓冲时长 = TimeSpan.Zero,
            "切换前音频输出丢弃")
        Dim 峰值 = 会话.读取音频峰值()
        断言(峰值.Length = 0 OrElse 峰值.All(Function(x) x <= 0.000001F),
           $"切换前音频丢弃后仍有 PCM 峰值：{String.Join(", ", 峰值.Select(Function(x) x.ToString("F6")))}。")
        Thread.Sleep(150)
        Dim 静置 = 会话.当前快照
        断言(静置.状态 = 播放状态.已暂停 AndAlso 静置.音频缓冲时长 = TimeSpan.Zero AndAlso
               Math.Abs((静置.播放位置 - 丢弃后.播放位置).TotalMilliseconds) < 30,
           "切换前音频丢弃后旧会话仍在推进或保留设备缓冲。")
        Console.WriteLine($"切换前音频丢弃：位置 {丢弃前.播放位置.TotalMilliseconds:F0} → " &
                          $"{丢弃后.播放位置.TotalMilliseconds:F0} ms，缓冲已清零。")
    End Sub

    Private Sub 测试音频端点不可用仍可播放(音频路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
            Dim 已提示不可用 As Boolean
            Dim 收到错误 As Boolean
            Dim 设备处理 As EventHandler(Of 播放器事件参数) =
                Sub(sender, e)
                    If e.详情JSON.Contains("""audioUnavailable"":true", StringComparison.Ordinal) AndAlso
                        e.详情JSON.Contains("""exclusive"":false", StringComparison.Ordinal) Then
                        已提示不可用 = True
                    End If
                End Sub
            Dim 错误处理 As EventHandler(Of 播放器事件参数) =
                Sub(sender, e) 收到错误 = True
            AddHandler 会话.设备变化, 设备处理
            AddHandler 会话.错误, 错误处理
            Try
                会话.设置音频端点("{FFF-PLAYER-REGRESSION-MISSING-ENDPOINT}")
                会话.打开Async(音频路径).GetAwaiter().GetResult()
                会话.播放()
                等待快照(会话,
                    Function(x) x.状态 = 播放状态.正在播放 AndAlso
                        x.播放位置 >= TimeSpan.FromMilliseconds(150),
                    "音频端点不可用时继续播放")
                断言(已提示不可用, "音频端点不可用时没有发布共享模式提示事件。")
                断言(Not 收到错误, "音频端点不可用时仍把可继续的媒体会话报告为播放错误。")
            Finally
                RemoveHandler 会话.设备变化, 设备处理
                RemoveHandler 会话.错误, 错误处理
            End Try
        End Using
        Console.WriteLine("音频端点不可用时媒体按共享模式继续推进，并仅发布提示事件。")
    End Sub

    Private Sub 测试独占暂停释放与恢复(会话 As 播放器会话, 音频路径 As String)
        会话.暂停()
        Dim 暂停快照 = 等待快照(会话, Function(x) x.状态 = 播放状态.已暂停,
                           "WASAPI 独占暂停")
        Dim 暂停位置 = 暂停快照.播放位置
        Thread.Sleep(150)
        断言(Math.Abs((会话.当前快照.播放位置 - 暂停位置).TotalMilliseconds) < 30,
           "独占暂停后媒体时钟仍在推进。")

        Dim 竞争会话已回退 As Boolean
        Using 竞争会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU,
            .色彩模式 = 色彩输出模式.映射到SDR
        })
            Dim 设备处理 As EventHandler(Of 播放器事件参数) =
                Sub(sender, e)
                    If e.详情JSON.Contains("""exclusiveFallback"":true", StringComparison.Ordinal) Then
                        竞争会话已回退 = True
                    End If
                End Sub
            AddHandler 竞争会话.设备变化, 设备处理
            Try
                竞争会话.设置音量(0.0F, True)
                竞争会话.打开Async(音频路径).GetAwaiter().GetResult()
                切换到独占模式(竞争会话)
                竞争会话.播放()
                等待音频预热(竞争会话, TimeSpan.FromMilliseconds(400), TimeSpan.FromSeconds(10))
                断言(Not 竞争会话已回退,
                   "主会话暂停后仍占用 WASAPI 独占端点，竞争会话被迫回退到共享模式。")
                竞争会话.暂停()
                等待快照(竞争会话, Function(x) x.状态 = 播放状态.已暂停,
                    "竞争会话释放 WASAPI 独占")
            Finally
                RemoveHandler 竞争会话.设备变化, 设备处理
            End Try
        End Using

        Dim 恢复已回退 As Boolean
        Dim 恢复处理 As EventHandler(Of 播放器事件参数) =
            Sub(sender, e)
                If e.详情JSON.Contains("""exclusiveFallback"":true", StringComparison.Ordinal) Then
                    恢复已回退 = True
                End If
            End Sub
        AddHandler 会话.设备变化, 恢复处理
        Try
            会话.播放()
            等待音频预热(会话, 暂停位置 + TimeSpan.FromMilliseconds(400), TimeSpan.FromSeconds(10))
            断言(Not 恢复已回退, "独占暂停恢复时意外回退到了共享模式。")
            验证音频峰值(会话, "WASAPI 独占暂停恢复")
        Finally
            RemoveHandler 会话.设备变化, 恢复处理
        End Try
        Console.WriteLine("WASAPI 独占暂停释放端点，并在继续播放时成功重新独占。")
    End Sub

    Private Sub 测试控制器独占换片(音频路径 As String, 换片路径 As String)
        Using 控制器 As New 播放器控制器(Function() IntPtr.Zero, Nothing)
            控制器.设置音量(0.0F)
            打开并等待(控制器, 音频路径)
            控制器.切换WASAPI模式()
            等待控制器WASAPI模式(控制器, WASAPI共享模式.独占)

            ' 打开不同路径必须经过控制器的候选会话；旧会话此时仍持有独占端点。
            打开并等待(控制器, 换片路径)
            等待控制器WASAPI模式(控制器, WASAPI共享模式.独占)
            Dim 计时 = Stopwatch.StartNew()
            Do
                Application.DoEvents()
                Dim 快照 = 控制器.安全读取快照()
                If 快照 IsNot Nothing AndAlso 快照.状态 = 播放状态.正在播放 AndAlso
                    快照.播放位置 >= TimeSpan.FromMilliseconds(500) AndAlso
                    快照.已解码音频帧数 > 0 Then Exit Do
                If 计时.Elapsed >= TimeSpan.FromSeconds(10) Then
                    Throw New TimeoutException($"独占换片后播放没有推进：" &
                        $"{If(快照 Is Nothing, "无快照", $"{快照.状态} / {快照.播放位置.TotalMilliseconds:F1} ms")}。")
                End If
                Thread.Sleep(5)
            Loop
            Console.WriteLine("WASAPI 独占状态下控制器换片后保持独占并正常推进。")
        End Using
    End Sub

    Private Sub 等待控制器WASAPI模式(控制器 As 播放器控制器, 期望 As WASAPI共享模式)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            If 控制器.WASAPI模式 = 期望 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(10) Then
                Throw New TimeoutException($"等待控制器 WASAPI 模式 {期望} 超时。")
            End If
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 测试纯音频封面回归(音频路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU,
            .色彩模式 = 色彩输出模式.映射到SDR,
            .SDR峰值尼特 = 100.0F,
            .HDR峰值尼特 = 1000.0F,
            .SDR纸白尼特 = 203.0F,
            .输出窗口句柄 = IntPtr.Zero
        })
            会话.设置音量(0.0F, True)
            会话.打开Async(音频路径).GetAwaiter().GetResult()

            Dim 信息 = 会话.当前媒体信息
            Dim 封面流 = 信息.流.FirstOrDefault(Function(x) x.类型 = "video" AndAlso x.是封面图)
            断言(信息.流.Any(Function(x) x.类型 = "audio"), "封面回归输入没有音频流。")
            断言(Not 信息.流.Any(Function(x) x.类型 = "video" AndAlso Not x.是封面图),
               "封面回归输入包含普通视频流，不是纯音频媒体。")
            断言(封面流 IsNot Nothing, "纯音频媒体没有识别出 attached picture 封面流。")
            断言(封面流.宽度 > 0 AndAlso 封面流.高度 > 0,
               $"封面流尺寸无效：{封面流.宽度}×{封面流.高度}。")

            Dim 无窗口快照 = 会话.当前快照
            断言(无窗口快照.视频宽度 = CUInt(封面流.宽度) AndAlso
                   无窗口快照.视频高度 = CUInt(封面流.高度),
               $"封面解码尺寸与流信息不一致：{无窗口快照.视频宽度}×{无窗口快照.视频高度} / " &
               $"{封面流.宽度}×{封面流.高度}。")
            断言(无窗口快照.交换链呈现次数 = 0,
               $"无窗口打开阶段错误创建了交换链呈现：{无窗口快照.交换链呈现次数}。")

            Using 隐藏窗口 As New Form With {
                .ClientSize = New Drawing.Size(320, 320),
                .FormBorderStyle = FormBorderStyle.FixedToolWindow,
                .ShowInTaskbar = False,
                .StartPosition = FormStartPosition.Manual,
                .Location = New Drawing.Point(-10000, -10000)
            }
                Dim 隐藏句柄 = 隐藏窗口.Handle
                会话.设置输出窗口(隐藏句柄)
                Dim 呈现快照 = 等待快照(会话,
                    Function(x) x.交换链呈现次数 > 无窗口快照.交换链呈现次数,
                    "纯音频封面绑定隐藏 HWND 后的交换链呈现")
                断言(呈现快照.视频宽度 = CUInt(封面流.宽度) AndAlso
                       呈现快照.视频高度 = CUInt(封面流.高度),
                   "绑定输出窗口后丢失了封面尺寸。")
                Console.WriteLine($"封面流 {封面流.编码} {封面流.宽度}×{封面流.高度}，" &
                                  $"交换链呈现 {无窗口快照.交换链呈现次数}→{呈现快照.交换链呈现次数}")
                会话.设置输出窗口(IntPtr.Zero)
            End Using
        End Using
    End Sub

    Private Sub 测试LRC歌词回归(歌词路径 As String, 封面音频路径 As String,
                           无封面音频路径 As String)
        Dim 样例 = LRC歌词解析器.解析文件(歌词路径)
        断言(样例.条目.Count = 40, $"样例 LRC 条目数量错误：{样例.条目.Count}。")
        断言(样例.条目(0).开始时间 = TimeSpan.FromMilliseconds(28805) AndAlso
               样例.条目(0).文本.Single().Contains("頑張れ", StringComparison.Ordinal),
               "样例 LRC 首条毫秒时间或正文解析错误。")
        断言(样例.条目.Where(Function(x) x.文本.All(Function(text) String.IsNullOrEmpty(text))).Count() = 8,
               "样例 LRC 空节奏行没有保留。")

        Dim 多语言文本 = String.Join(vbLf, {
            "[ar:ignored]", "[00:01.2][00:02.34]原文<00:01.50>",
            "[00:01.200]Translation", "[00:03]", "metadata without timing"})
        Dim 多语言 = LRC歌词解析器.解析(New StringReader(多语言文本))
        断言(多语言.条目.Count = 3, "一行多时间标签没有展开为独立时间组。")
        断言(多语言.条目(0).文本.SequenceEqual({"原文", "Translation"}),
               "相同时间戳的多语言歌词没有按文件顺序合并。")
        断言(多语言.条目(1).开始时间 = TimeSpan.FromMilliseconds(2340),
               "两位小数 LRC 时间没有按百分之一秒解析。")
        断言(多语言.条目(2).文本.Single() = String.Empty,
               "带时间戳的空正文没有保留为节奏间隔。")
        Try
            LRC歌词解析器.解析(New StringReader("[ar:no timeline]"))
            Throw New InvalidOperationException("无时间轴 LRC 没有被拒绝。")
        Catch ex As NotSupportedException
        End Try

        Dim scanDirectory = Path.Combine(Path.GetTempPath(),
            "fff-player-lyrics-scan-" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(scanDirectory)
        Try
            Dim mediaPath = Path.Combine(scanDirectory, "song.mp3")
            Dim exactLyricsPath = Path.Combine(scanDirectory, "song.lrc")
            File.WriteAllBytes(mediaPath, Array.Empty(Of Byte)())
            File.WriteAllText(Path.Combine(scanDirectory, "song.translation.lrc"),
                              "[00:01.00]不应被扫描", New UTF8Encoding(False))
            Dim noMatch = LRC歌词自动加载器.尝试加载同名歌词Async(
                mediaPath, CancellationToken.None).GetAwaiter().GetResult()
            断言(noMatch Is Nothing, "歌词自动扫描误用了非同名 LRC 文件。")
            File.WriteAllText(exactLyricsPath, "[00:01.00]同名歌词", New UTF8Encoding(False))
            Dim exactMatch = LRC歌词自动加载器.尝试加载同名歌词Async(
                mediaPath, CancellationToken.None).GetAwaiter().GetResult()
            断言(exactMatch IsNot Nothing AndAlso
                   String.Equals(exactMatch.路径, exactLyricsPath, StringComparison.OrdinalIgnoreCase),
                   "歌词自动扫描没有加载同目录的同名 LRC 文件。")
        Finally
            If Directory.Exists(scanDirectory) Then Directory.Delete(scanDirectory, True)
        End Try

        Dim regionWithCover = 播放器歌词呈现器.计算歌词区域(New Size(800, 400), True)
        Dim regionWithoutCover = 播放器歌词呈现器.计算歌词区域(New Size(800, 400), False)
        断言(regionWithCover.Left >= 400 AndAlso regionWithCover.Right <= 800,
               "有封面歌词区域没有限制在右半区。")
        断言(Math.Abs((regionWithCover.Left - 400.0F) - (800.0F - regionWithCover.Right)) < 0.01F AndAlso
               regionWithCover.Right < 800.0F,
               "有封面歌词区域的左右内边距不对称。")
        断言(regionWithoutCover.Left < 100 AndAlso regionWithoutCover.Right > 700,
               "无封面歌词区域没有覆盖完整渲染区域。")

        Using control As New 播放器画面控件 With {.Size = New Size(800, 400)}
            Using renderer As New 播放器歌词呈现器(control, Function() Nothing,
                Function() Nothing, Function() False, Function(a, b, c, d, e) True)
                Dim current = renderer.生成命令(New Size(800, 400),
                    TimeSpan.FromMilliseconds(1200), 多语言, False)
                断言(current.Count >= 2 AndAlso
                       current.Where(Function(x) x.前景色ARGB = &HFFFFFFFFUI).Count() = 2,
                       "同时间多语言歌词没有整体高亮。")
                Dim originalY = current.First(Function(x) x.文本 = "原文").Y
                Dim translationY = current.First(Function(x) x.文本 = "Translation").Y
                断言(translationY - originalY > 24.0F,
                       "同一歌词组内的多语言行没有应用歌词组间距。")
                Dim enteringSize = current.First(Function(x) x.文本 = "原文").字号
                Dim beforeScroll = originalY
                断言(current.Where(Function(x) x.前景色ARGB = &HFFFFFFFFUI).
                           All(Function(x) x.样式 = 定时文字样式.无),
                       "当前歌词仍然使用了粗体样式。")
                Dim activeSize = renderer.生成命令(New Size(800, 400),
                    TimeSpan.FromMilliseconds(1500), 多语言, False).
                    First(Function(x) x.文本 = "原文").字号
                Dim exitingSize = renderer.生成命令(New Size(800, 400),
                    TimeSpan.FromMilliseconds(2250), 多语言, False).
                    First(Function(x) x.文本 = "原文").字号
                断言(activeSize > enteringSize AndAlso exitingSize < activeSize,
                       "当前歌词字号没有执行平滑的切入和切出过渡。")
                Dim scrolling = renderer.生成命令(New Size(800, 400),
                    TimeSpan.FromMilliseconds(2100), 多语言, False)
                Dim afterScroll = scrolling.First(Function(x) x.文本 = "原文").Y
                断言(afterScroll < beforeScroll, "歌词没有在下一时间组到来前平滑上移。")

                Const longChinese = "这是一句用于验证歌词区域自动换行能力的非常长的中文歌词而且不能丢失任何一个字符"
                Dim longChineseLyrics = LRC歌词解析器.解析(
                    New StringReader($"[00:01.00]{longChinese}"))
                Dim wrappedChinese = renderer.生成命令(New Size(800, 400),
                    TimeSpan.FromMilliseconds(1200), longChineseLyrics, True).ToArray()
                断言(wrappedChinese.Length > 1 AndAlso
                       String.Concat(wrappedChinese.Select(Function(x) x.文本)) = longChinese,
                       "长中文歌词没有完整地自动换行。")
                断言(wrappedChinese.All(Function(x) x.X >= regionWithCover.Left AndAlso
                                                   x.X + x.宽度 <= regionWithCover.Right),
                       "换行后的歌词命令越过了右侧歌词区域。")
                Dim longEnglishLyrics = LRC歌词解析器.解析(New StringReader(
                    "[00:01.00]This is an intentionally long English lyric that should wrap at spaces"))
                Dim wrappedEnglish = renderer.生成命令(New Size(800, 400),
                    TimeSpan.FromMilliseconds(1200), longEnglishLyrics, True).ToArray()
                断言(wrappedEnglish.Length > 1 AndAlso
                       wrappedEnglish.All(Function(x) Not x.文本.StartsWith(" ", StringComparison.Ordinal)),
                       "长英文歌词没有在单词边界正常换行。")
            End Using
        End Using

        测试歌词GPU布局(封面音频路径, 样例, True)
        测试歌词GPU布局(无封面音频路径, 样例, False)
    End Sub

    Private Sub 测试封面模糊背景(音频路径 As String)
        Using window As New Form With {
            .ClientSize = New Size(800, 400), .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .ShowInTaskbar = False, .StartPosition = FormStartPosition.Manual,
            .Location = New Point(-10000, -10000)}
            window.Show()
            Application.DoEvents()
            Using session As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU, .色彩模式 = 色彩输出模式.映射到SDR,
                .输出窗口句柄 = window.Handle})
                session.设置音量(0.0F, True)
                session.打开Async(音频路径).GetAwaiter().GetResult()
                Dim info = session.当前媒体信息
                断言(info.流.Any(Function(x) x.类型 = "video" AndAlso x.是封面图),
                       "封面背景测试媒体不包含封面流。")
                等待快照(session, Function(x) x.交换链呈现次数 > 0, "封面背景首帧")
                Dim backdrop As Color
                Dim backdropTimer = Stopwatch.StartNew()
                Do
                    backdrop = session.读取视频输出像素(10, 10)
                    If backdrop.R >= 80 AndAlso backdrop.R <= 130 AndAlso
                       backdrop.G <= 10 AndAlso backdrop.B <= 10 Then Exit Do
                    If backdropTimer.Elapsed >= TimeSpan.FromSeconds(3) Then Exit Do
                    Thread.Sleep(20)
                Loop
                Dim foreground = session.读取视频输出像素(400, 200)
                断言(backdrop.R >= 80 AndAlso backdrop.R <= 130 AndAlso
                       backdrop.G <= 10 AndAlso backdrop.B <= 10,
                       $"封面背景没有应用 ARGB(120,0,0,0) 遮罩：" &
                       $"RGB({backdrop.R},{backdrop.G},{backdrop.B})。")
                断言(foreground.R >= 180 AndAlso foreground.R <= 220 AndAlso
                       foreground.R >= backdrop.R + 60 AndAlso
                       foreground.G <= 10 AndAlso foreground.B <= 10,
                       $"前景封面被背景模糊或遮罩影响：" &
                       $"RGB({foreground.R},{foreground.G},{foreground.B})。")
            End Using
        End Using
    End Sub

    Private Sub 测试歌词GPU布局(音频路径 As String, 歌词 As LRC歌词资料, 期望封面 As Boolean)
        Using window As New Form With {
            .ClientSize = New Size(800, 400), .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .ShowInTaskbar = False, .StartPosition = FormStartPosition.Manual,
            .Location = New Point(-10000, -10000)}
            window.Show()
            Application.DoEvents()
            Using session As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU, .色彩模式 = 色彩输出模式.映射到SDR,
                .输出窗口句柄 = window.Handle})
                session.设置音量(0.0F, True)
                session.打开Async(音频路径).GetAwaiter().GetResult()
                Dim info = session.当前媒体信息
                Dim hasCover = info.流.Any(Function(x) x.类型 = "video" AndAlso x.是封面图)
                断言(hasCover = 期望封面, "歌词布局测试媒体的封面状态与预期不符。")
                Using control As New 播放器画面控件 With {.Size = window.ClientSize}
                    Using renderer As New 播放器歌词呈现器(control, Function() Nothing,
                        Function() Nothing, Function() hasCover, Function(a, b, c, d, e) True)
                        Dim commands = renderer.生成命令(window.ClientSize,
                            TimeSpan.FromMilliseconds(28805), 歌词, hasCover)
                        If hasCover Then
                            断言(commands.All(Function(x) x.X >= window.ClientSize.Width \ 2 AndAlso
                                                   x.X + x.宽度 <= window.ClientSize.Width),
                               "封面+歌词布局存在越过右半区的歌词命令。")
                        End If
                        session.设置歌词图层(window.ClientSize, commands, 1UL, 60.0F,
                            New 歌词呈现设置(30.0F, 3, 4, &H78000000UI,
                                50.0F, 50.0F, 7.5F, 0.0F, 7.5F))
                    End Using
                End Using
                Dim timer = Stopwatch.StartNew()
                Dim state As 定时文字状态 = Nothing
                Do
                    state = session.当前歌词状态
                    If state.已绘制序号 = 1UL AndAlso state.命令数 > 0 AndAlso
                       state.可见像素数 > 0 AndAlso session.当前快照.交换链呈现次数 > 0 Then Exit Do
                    If timer.Elapsed >= TimeSpan.FromSeconds(10) Then
                        Throw New TimeoutException("歌词 GPU 图层没有完成栅格化与呈现。")
                    End If
                    Thread.Sleep(10)
                Loop
            End Using
        End Using
    End Sub

    Private Sub 创建音频延迟测试文件(路径 As String)
        Const 采样率 As Integer = 48000
        Const 声道数 As Integer = 2
        Const 位深 As Integer = 16
        Const 秒数 As Integer = 24
        Const 频率 As Double = 997.0
        Dim 块对齐 = 声道数 * 位深 \ 8
        Dim 数据字节数 = 采样率 * 块对齐 * 秒数
        Using 流 = File.Create(路径)
            Using 写入器 As New BinaryWriter(流, Encoding.ASCII, False)
                写入器.Write(Encoding.ASCII.GetBytes("RIFF"))
                写入器.Write(36 + 数据字节数)
                写入器.Write(Encoding.ASCII.GetBytes("WAVE"))
                写入器.Write(Encoding.ASCII.GetBytes("fmt "))
                写入器.Write(16)
                写入器.Write(CShort(1))
                写入器.Write(CShort(声道数))
                写入器.Write(采样率)
                写入器.Write(采样率 * 块对齐)
                写入器.Write(CShort(块对齐))
                写入器.Write(CShort(位深))
                写入器.Write(Encoding.ASCII.GetBytes("data"))
                写入器.Write(数据字节数)
                For 样本 = 0 To 采样率 * 秒数 - 1
                    Dim 波形 = Math.Sin(2.0R * Math.PI * 频率 * 样本 / 采样率)
                    写入器.Write(CShort(Math.Round(Short.MaxValue * 0.8R * 波形)))
                    写入器.Write(CShort(Math.Round(Short.MaxValue * 0.4R * 波形)))
                Next
            End Using
        End Using
    End Sub

    Private Sub 等待音频预热(会话 As 播放器会话, 目标位置 As TimeSpan, 超时 As TimeSpan)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Dim 快照 = 会话.当前快照
            If 快照.状态 = 播放状态.失败 Then
                Throw New InvalidOperationException($"播放器在音频预热阶段失败：{会话.最后错误消息}")
            End If
            If 快照.播放位置 >= 目标位置 AndAlso 快照.已解码音频帧数 > 0 AndAlso
                快照.音频缓冲时长 > TimeSpan.Zero Then Return
            If 计时.Elapsed >= 超时 Then
                Throw New TimeoutException($"音频播放预热超时：状态 {快照.状态}，" &
                    $"位置 {快照.播放位置.TotalMilliseconds:F1} ms，缓冲 {快照.音频缓冲时长.TotalMilliseconds:F1} ms，" &
                    $"解码帧 {快照.已解码音频帧数}，欠载 {快照.音频欠载次数}，错误 {会话.最后错误消息}。")
            End If
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 切换到独占模式(会话 As 播放器会话)
        Using 完成 As New ManualResetEventSlim(False)
            Dim 已独占 As Boolean
            Dim 错误详情 As String = Nothing
            Dim 设备处理 As EventHandler(Of 播放器事件参数) =
                Sub(sender, e)
                    If e.详情JSON.Contains("""exclusive"":true", StringComparison.Ordinal) Then
                        已独占 = True : 完成.Set()
                    ElseIf e.详情JSON.Contains("""exclusive"":false", StringComparison.Ordinal) Then
                        完成.Set()
                    End If
                End Sub
            Dim 错误处理 As EventHandler(Of 播放器事件参数) =
                Sub(sender, e)
                    If e.详情JSON.Contains("audio-exclusive-mode", StringComparison.Ordinal) Then
                        错误详情 = e.详情JSON : 完成.Set()
                    End If
                End Sub
            AddHandler 会话.设备变化, 设备处理
            AddHandler 会话.错误, 错误处理
            Try
                会话.设置WASAPI独占模式(True)
                If Not 完成.Wait(TimeSpan.FromSeconds(10)) Then Throw New TimeoutException("切换 WASAPI 独占模式超时。")
                断言(已独占, $"WASAPI 独占模式未建立：{If(错误详情, 会话.最后错误消息)}")
            Finally
                RemoveHandler 会话.设备变化, 设备处理
                RemoveHandler 会话.错误, 错误处理
            End Try
        End Using
    End Sub

    Private Sub 验证音频峰值(会话 As 播放器会话, 阶段 As String)
        Dim 计时 = Stopwatch.StartNew()
        Dim 峰值 As Single() = Array.Empty(Of Single)()
        Do
            峰值 = 会话.读取音频峰值()
            If 峰值.Length > 0 AndAlso 峰值.Max() > 0.0005F Then Exit Do
            If 计时.Elapsed >= TimeSpan.FromSeconds(3) Then Exit Do
            Thread.Sleep(5)
        Loop
        断言(峰值.Length > 0, $"{阶段}没有上报输出声道。")
        断言(峰值.All(Function(x) Single.IsFinite(x) AndAlso x >= 0.0F AndAlso x <= 1.0F),
           $"{阶段}上报了无效音频峰值。")
        断言(峰值.Max() > 0.0005F, $"{阶段}没有从实际提交的 PCM 取得响度：{String.Join(", ", 峰值.Select(Function(x) x.ToString("F6")))}。")
        Console.WriteLine($"{阶段}峰值：{String.Join("/", 峰值.Select(Function(x) x.ToString("F6")))}")
    End Sub

    Private Sub 验证纯音频结果(结果 As 播放测量结果, 阶段 As String)
        验证音频缓冲结果(结果, 阶段)
        断言(结果.音频欠载次数 = 0, $"{阶段}纯音频稳定播放出现了 {结果.音频欠载次数} 次欠载。")
        断言(结果.播放速度 >= 0.97 AndAlso 结果.播放速度 <= 1.03,
           $"{阶段}纯音频时钟速度异常：{结果.播放速度:F4}x。")
        Console.WriteLine($"{阶段}：时钟 {结果.播放速度:F4}x，缓冲均值/最小/最大 " &
                          $"{结果.平均音频缓冲毫秒:F1}/{结果.最小音频缓冲毫秒:F1}/" &
                          $"{结果.最大音频缓冲毫秒:F1} ms，音频帧 {结果.已解码音频帧数}、" &
                          $"欠载 {结果.音频欠载次数}")
    End Sub

    Private Sub 验证连续PCM结果(结果 As 播放测量结果, 阶段 As String)
        验证播放结果(结果, 阶段)
        验证音频结果(结果, 阶段)
        断言(结果.平均音画差毫秒 <= 40.0 AndAlso 结果.最大音画差毫秒 <= 100.0,
           $"{阶段}连续 PCM/设备时钟仍有可见音画偏差：" &
           $"{结果.平均音画差毫秒:F1}/{结果.最大音画差毫秒:F1} ms。")
        断言(结果.音频时间戳抖动帧数 > 0, $"{阶段}没有观测到该片源已知的 AAC PTS 量化抖动。")
        断言(结果.音频不连续次数 = 0 AndAlso 结果.音频插入静音帧数 = 0 AndAlso
           结果.音频丢弃重叠帧数 = 0,
           $"{阶段}把连续 AAC PTS 抖动误判为断点：{结果.音频不连续次数}/" &
           $"{结果.音频插入静音帧数}/{结果.音频丢弃重叠帧数}。")
    End Sub

    Private Sub 测试播放中字幕替换(视频路径 As String, SUP路径 As String)
        Dim 临时目录 = Path.Combine(Path.GetTempPath(), "fff-player-subtitle-regression-" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(临时目录)
        Try
            Dim SRT路径 = Path.Combine(临时目录, "replacement.srt")
            Dim ASS路径 = Path.Combine(临时目录, "replacement.ass")
            Dim SSA路径 = Path.Combine(临时目录, "replacement.ssa")
            File.WriteAllText(SRT路径, "1" & vbLf & "00:00:00,000 --> 02:00:00,000" & vbLf & "SRT replacement" & vbLf)
            Dim 脚本 = "[Script Info]" & vbLf & "ScriptType: v4.00+" & vbLf &
                "PlayResX: 1920" & vbLf & "PlayResY: 1080" & vbLf &
                "[V4+ Styles]" & vbLf &
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding" & vbLf &
                "Style: Default,Microsoft YaHei,48,&H00FFFFFF,&H000000FF,&H80000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,20,20,20,1" & vbLf &
                "[Events]" & vbLf &
                "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text" & vbLf &
                "Dialogue: 0,0:00:00.00,2:00:00.00,Default,,0,0,0,,replacement" & vbLf
            File.WriteAllText(ASS路径, 脚本)
            File.WriteAllText(SSA路径, 脚本)

            Using 控制器 As New 播放器控制器(Function() IntPtr.Zero, Nothing)
                打开并等待(控制器, 视频路径)
                控制器.切换播放暂停()
                Dim 基线 = 控制器.安全读取快照()
                Dim 媒体打开次数 = 0
                AddHandler 控制器.媒体已打开, Sub() 媒体打开次数 += 1
                For Each 字幕路径 In {SUP路径, SRT路径, ASS路径, SSA路径}
                    Dim 已加载 As New ManualResetEventSlim(False)
                    Dim 预期路径 = Path.GetFullPath(字幕路径)
                    Dim 处理器 As EventHandler(Of 播放器字幕事件参数) =
                        Sub(sender, e)
                            If String.Equals(Path.GetFullPath(e.路径), 预期路径, StringComparison.OrdinalIgnoreCase) Then 已加载.Set()
                        End Sub
                    AddHandler 控制器.外部字幕已加载, 处理器
                    控制器.替换字幕(字幕路径)
                    断言(已加载.Wait(TimeSpan.FromSeconds(30)), $"替换 {Path.GetExtension(字幕路径)} 字幕超时。")
                    RemoveHandler 控制器.外部字幕已加载, 处理器
                    断言(控制器.当前字幕 IsNot Nothing AndAlso
                       String.Equals(Path.GetFullPath(控制器.当前字幕.路径), 预期路径, StringComparison.OrdinalIgnoreCase),
                       $"{Path.GetExtension(字幕路径)} 没有成为当前字幕轨。")
                Next
                Dim 末快照 = 控制器.安全读取快照()
                断言(媒体打开次数 = 0 AndAlso 末快照 IsNot Nothing AndAlso 基线 IsNot Nothing AndAlso
                   末快照.总时长 = 基线.总时长 AndAlso
                   末快照.当前视频流 = 基线.当前视频流 AndAlso 末快照.当前音频流 = 基线.当前音频流,
                   "字幕替换错误地重建或改动了当前媒体会话。")
            End Using
            Console.WriteLine("播放中 SUP→SRT→ASS→SSA 原子替换通过，媒体会话和流选择保持不变。")
        Finally
            If Directory.Exists(临时目录) Then Directory.Delete(临时目录, True)
        End Try
    End Sub

    Private Sub 测试播放中弹幕替换(视频路径 As String)
        Dim 临时目录 = Path.Combine(Path.GetTempPath(), "fff-player-danmaku-regression-" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(临时目录)
        Try
            Dim 第一份路径 = Path.Combine(临时目录, "first.xml")
            Dim 第二份路径 = Path.Combine(临时目录, "second.XML")
            Dim 损坏路径 = Path.Combine(临时目录, "broken.xml")
            File.WriteAllText(第一份路径,
                "<?xml version=""1.0"" encoding=""UTF-8""?><i>" &
                "<d p=""1.0,1,25,16777215,0,0,user-a,101"">first</d>" &
                "<d p=""2.0,5,30,65280,0,0,user-b,102"">second</d></i>")
            File.WriteAllText(第二份路径,
                "<?xml version=""1.0"" encoding=""UTF-8""?><i>" &
                "<d p=""3.0,1,28,16711680,0,0,user-c,201"">replacement</d></i>")
            File.WriteAllText(损坏路径, "<i><d p=""1.0,1,25,1"">broken")

            断言(弹幕自动加载器.是支持的弹幕文件(第二份路径), "大写 XML 扩展名没有被识别为弹幕文件。")
            Using 控制器 As New 播放器控制器(Function() IntPtr.Zero, Nothing)
                打开并等待(控制器, 视频路径)
                控制器.切换播放暂停()
                Dim 基线 = 控制器.安全读取快照()
                Dim 媒体打开次数 = 0
                AddHandler 控制器.媒体已打开, Sub() 媒体打开次数 += 1

                For Each 测试项 In {(路径:=第一份路径, 数量:=2, 文本:="first"),
                                  (路径:=第二份路径, 数量:=1, 文本:="replacement")}
                    Using 已加载 As New ManualResetEventSlim(False)
                        Dim 预期路径 = Path.GetFullPath(测试项.路径)
                        Dim 处理器 As EventHandler(Of 播放器弹幕事件参数) =
                            Sub(sender, e)
                                If String.Equals(Path.GetFullPath(e.路径), 预期路径, StringComparison.OrdinalIgnoreCase) Then 已加载.Set()
                            End Sub
                        AddHandler 控制器.外部弹幕已加载, 处理器
                        控制器.替换弹幕(测试项.路径)
                        断言(已加载.Wait(TimeSpan.FromSeconds(30)), $"替换弹幕 {Path.GetFileName(测试项.路径)} 超时。")
                        RemoveHandler 控制器.外部弹幕已加载, 处理器
                    End Using
                    Dim 当前资料库 = 控制器.当前弹幕
                    断言(当前资料库 IsNot Nothing AndAlso 当前资料库.数量 = 测试项.数量 AndAlso
                       当前资料库.项目.Any(Function(x) String.Equals(x.文本, 测试项.文本, StringComparison.Ordinal)),
                       $"{Path.GetFileName(测试项.路径)} 没有原子替换当前弹幕。")
                Next

                Dim 替换前资料库 = 控制器.当前弹幕
                Using 已失败 As New ManualResetEventSlim(False)
                    Dim 错误处理器 As EventHandler(Of 播放器错误事件参数) =
                        Sub(sender, e)
                            If String.Equals(e.标题, "无法加载弹幕", StringComparison.Ordinal) Then 已失败.Set()
                        End Sub
                    AddHandler 控制器.播放错误, 错误处理器
                    控制器.替换弹幕(损坏路径)
                    断言(已失败.Wait(TimeSpan.FromSeconds(30)), "损坏 XML 弹幕没有报告加载失败。")
                    RemoveHandler 控制器.播放错误, 错误处理器
                End Using
                断言(ReferenceEquals(控制器.当前弹幕, 替换前资料库), "损坏 XML 替换清空或改动了原有弹幕。")

                Dim 末快照 = 控制器.安全读取快照()
                断言(媒体打开次数 = 0 AndAlso 末快照 IsNot Nothing AndAlso 基线 IsNot Nothing AndAlso
                   末快照.总时长 = 基线.总时长 AndAlso
                   末快照.当前视频流 = 基线.当前视频流 AndAlso 末快照.当前音频流 = 基线.当前音频流 AndAlso
                   Math.Abs((末快照.播放位置 - 基线.播放位置).TotalMilliseconds) <= 100.0,
                   "弹幕替换错误地重建会话，或改变了播放位置及音视频流选择。")
            End Using
            Console.WriteLine("播放中 XML→XML 原子替换及损坏文件回退通过，媒体会话、位置和流选择保持不变。")
        Finally
            If Directory.Exists(临时目录) Then Directory.Delete(临时目录, True)
        End Try
    End Sub

    Private Sub 测试色彩回归(SDR路径 As String, HDR路径 As String)
        测试数值色彩映射()

        Dim SDR源峰值 = 读取源峰值(SDR路径, False)
        Dim HDR源峰值 = 读取源峰值(HDR路径, True)
        断言(SDR源峰值 = 100UI, $"SDR 源的内部峰值不是 100 nit：{SDR源峰值}。")
        断言(HDR源峰值 = 1242UI, $"HDR 源没有采用 MaxCLL 1242 nit：{HDR源峰值}。")
        测试SDR拒绝真实HDR输出(SDR路径)

        Using 控制器 As New 播放器控制器(Function() IntPtr.Zero, Nothing)
            打开并等待(控制器, HDR路径)
            Dim HDR快照 = 控制器.安全读取快照()
            断言(HDR快照 IsNot Nothing AndAlso HDR快照.是HDR源, "HDR 样本没有被播放器识别为 PQ/HLG。")
            断言(HDR快照.请求色彩模式 = 色彩输出模式.映射到SDR,
               "HDR 样本没有以 SDR 映射作为初始模式。")

            控制器.切换HDR模式()
            等待请求色彩模式(控制器, 色彩输出模式.原始HDR按SDR呈现)
            控制器.切换HDR模式()
            等待请求色彩模式(控制器, 色彩输出模式.峰值映射HDR)
            断言(控制器.色彩模式 = 色彩输出模式.峰值映射HDR,
               "测试前置条件失败：HDR 样本没有切换到真实 HDR 请求。")

            打开并等待(控制器, HDR路径)
            Dim 继承HDR快照 = 控制器.安全读取快照()
            断言(继承HDR快照 IsNot Nothing AndAlso 继承HDR快照.是HDR源 AndAlso
                   控制器.色彩模式 = 色彩输出模式.峰值映射HDR AndAlso
                   继承HDR快照.请求色彩模式 = 色彩输出模式.峰值映射HDR,
                "打开新的 HDR 媒体后没有继承真实 HDR 输出策略。")

            打开并等待(控制器, SDR路径)
            Dim SDR快照 = 控制器.安全读取快照()
            断言(SDR快照 IsNot Nothing AndAlso Not SDR快照.是HDR源, "SDR 样本被错误识别为 HDR。")
            断言(控制器.色彩模式 = 色彩输出模式.映射到SDR AndAlso
               SDR快照.请求色彩模式 = 色彩输出模式.映射到SDR AndAlso
               SDR快照.实际色彩模式 = 色彩输出模式.映射到SDR,
               "HDR→SDR 换片后仍沿用了 PQ/BT.2020 真实 HDR 输出状态。")

            打开并等待(控制器, HDR路径)
            Dim SDR后HDR快照 = 控制器.安全读取快照()
            断言(SDR后HDR快照 IsNot Nothing AndAlso SDR后HDR快照.是HDR源 AndAlso
                   控制器.色彩模式 = 色彩输出模式.峰值映射HDR AndAlso
                   SDR后HDR快照.请求色彩模式 = 色彩输出模式.峰值映射HDR,
                "播放 SDR 后再次打开 HDR 没有恢复上次选择的真实 HDR 策略。")
        End Using

        Console.WriteLine($"内部色彩状态：SDR {SDR源峰值} nit，HDR MaxCLL {HDR源峰值} nit；HDR→SDR 换片已回到 BT.709 SDR。")
    End Sub

    Private Sub 测试SDR拒绝真实HDR输出(SDR路径 As String)
        Using 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU,
            .色彩模式 = 色彩输出模式.峰值映射HDR,
            .SDR峰值尼特 = 100.0F,
            .HDR峰值尼特 = 1000.0F,
            .SDR纸白尼特 = 203.0F
        })
            会话.打开Async(SDR路径).GetAwaiter().GetResult()
            Dim 快照 = 会话.当前快照
            断言(Not 快照.是HDR源 AndAlso 快照.请求色彩模式 = 色彩输出模式.映射到SDR AndAlso
               快照.实际色彩模式 = 色彩输出模式.映射到SDR,
               "原生会话接受了 SDR 文件的真实 HDR 输出请求。")
        End Using
    End Sub

    Private Sub 测试数值色彩映射()
        Dim SDR = 执行色彩变换(0UI, 0UI, 0.18F, 0.5F, 1.0F, 100.0F, 203.0F)
        断言(Math.Abs(SDR.输出红 - 0.18F) < 0.0001F AndAlso
           Math.Abs(SDR.输出绿 - 0.5F) < 0.0001F AndAlso
           Math.Abs(SDR.输出蓝 - 1.0F) < 0.0001F,
           "纯 SDR→SDR 路径改变了 BT.709 码值。")

        测试BT2390数值映射()
    End Sub

    Private Sub 测试BT2390数值映射()
        Const 源峰值 As Single = 1242.0F
        Const 目标峰值 As Single = 100.0F
        Dim 源峰值PQ = PQ码值(源峰值)
        Dim 目标峰值PQ = PQ码值(目标峰值)
        Dim 归一目标 = 目标峰值PQ / 源峰值PQ
        Dim 拐点信号 = Math.Clamp(1.5F * 归一目标 - 0.5F, 0.0F, 1.0F)
        Dim 拐点尼特 = PQ尼特(拐点信号 * 源峰值PQ)
        Dim 暗部尼特 = Math.Max(1.0F, 拐点尼特 * 0.5F)

        Dim 暗部 = 执行色彩变换(0UI, 1UI, PQ码值(暗部尼特), PQ码值(暗部尼特),
                              PQ码值(暗部尼特), 源峰值, 203.0F)
        Dim 暗部预期 = BT709码值(暗部尼特 / 目标峰值)
        断言(Math.Abs(暗部.输出红 - 暗部预期) < 0.0005F,
           $"BT.2390 拐点以下改变了绝对亮度：{暗部尼特:F3} nit → {暗部.输出红:F5}，预期 {暗部预期:F5}。")

        Dim 拐点前 = 执行灰阶PQ变换(拐点尼特 * 0.999F, 源峰值)
        Dim 拐点后 = 执行灰阶PQ变换(拐点尼特 * 1.001F, 源峰值)
        断言(拐点后 >= 拐点前 AndAlso 拐点后 - 拐点前 < 0.002F,
           $"BT.2390 拐点不连续：{拐点前:F6} → {拐点后:F6}。")

        Dim 上一个 = -1.0F
        For Each 尼特 In {0.0F, 1.0F, 5.0F, 10.0F, 25.0F, 50.0F, 100.0F,
                         203.0F, 400.0F, 800.0F, 源峰值}
            Dim 当前 = 执行灰阶PQ变换(尼特, 源峰值)
            断言(当前 >= 上一个 AndAlso 当前 >= 0.0F AndAlso 当前 <= 1.00001F,
               $"BT.2390 曲线非单调或越界：{尼特:F1} nit → {当前:F6}。")
            上一个 = 当前
        Next
        断言(上一个 >= 0.9999F AndAlso 上一个 <= 1.00001F,
           $"BT.2390 没有把源峰值映射到 SDR 峰值：{上一个:F6}。")
        Console.WriteLine($"BT.2390：knee {拐点尼特:F2} nit；暗部 {暗部尼特:F2} nit 保持；" &
                          $"源峰值 {源峰值:F0} nit → SDR {上一个:F4}。")
    End Sub

    Private Function 执行灰阶PQ变换(尼特 As Single, 源峰值 As Single) As Single
        Dim 码值 = PQ码值(尼特)
        Return 执行色彩变换(0UI, 1UI, 码值, 码值, 码值, 源峰值, 203.0F).输出红
    End Function

    Private Function 执行色彩变换(模式 As UInteger, 传递函数 As UInteger,
                              红 As Single, 绿 As Single, 蓝 As Single,
                              源峰值 As Single, 纸白 As Single) As 原生色彩变换
        Dim 变换 As New 原生色彩变换 With {
            .大小 = CUInt(Marshal.SizeOf(Of 原生色彩变换)()),
            .版本 = 1UI,
            .色彩模式 = 模式,
            .传递函数 = 传递函数,
            .输入红 = 红,
            .输入绿 = 绿,
            .输入蓝 = 蓝,
            .SDR峰值尼特 = 100.0F,
            .源峰值尼特 = 源峰值,
            .纸白尼特 = 纸白
        }
        Dim 结果 = FFF3FP_EvaluateColorTransform(变换)
        断言(结果 = 0, $"原生色彩变换诊断失败：{结果}。")
        Return 变换
    End Function

    Private Function PQ码值(尼特 As Single) As Single
        Const m1 As Double = 2610.0 / 16384.0
        Const m2 As Double = 2523.0 / 32.0
        Const c1 As Double = 3424.0 / 4096.0
        Const c2 As Double = 2413.0 / 128.0
        Const c3 As Double = 2392.0 / 128.0
        Dim 线性 = Math.Pow(Math.Clamp(CDbl(尼特) / 10000.0, 0.0, 1.0), m1)
        Return CSng(Math.Pow((c1 + c2 * 线性) / (1.0 + c3 * 线性), m2))
    End Function

    Private Function PQ尼特(码值 As Single) As Single
        Const m1 As Double = 2610.0 / 16384.0
        Const m2 As Double = 2523.0 / 32.0
        Const c1 As Double = 3424.0 / 4096.0
        Const c2 As Double = 2413.0 / 128.0
        Const c3 As Double = 2392.0 / 128.0
        Dim 幂 = Math.Pow(Math.Clamp(CDbl(码值), 0.0, 1.0), 1.0 / m2)
        Return CSng(10000.0 * Math.Pow(Math.Max(幂 - c1, 0.0) /
            Math.Max(c2 - c3 * 幂, 0.000001), 1.0 / m1))
    End Function

    Private Function BT709码值(线性值 As Single) As Single
        线性值 = Math.Max(线性值, 0.0F)
        Return Math.Clamp(If(线性值 < 0.018F, 4.5F * 线性值,
            1.099F * CSng(Math.Pow(线性值, 0.45)) - 0.099F), 0.0F, 1.0F)
    End Function

    Private Function 读取源峰值(路径 As String, 应为HDR As Boolean) As UInteger
        Using 隐藏输出 As New Form With {.ClientSize = New Drawing.Size(320, 180), .ShowInTaskbar = False}
            Dim 输出句柄 = 隐藏输出.Handle
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 解码模式.CPU,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .SDR峰值尼特 = 100.0F,
                .HDR峰值尼特 = 1000.0F,
                .SDR纸白尼特 = 203.0F,
                .输出窗口句柄 = 输出句柄
            })
                会话.设置音量(0.0F, True)
                会话.打开Async(路径).GetAwaiter().GetResult()
                断言(会话.当前快照.是HDR源 = 应为HDR, $"片源 HDR 标记与预期不符：{路径}")
                会话.播放()
                Dim 计时 = Stopwatch.StartNew()
                Do
                    Application.DoEvents()
                    Dim 快照 = 会话.当前快照
                    If 快照.已呈现视频帧数 > 0 AndAlso 快照.源峰值尼特 > 0 Then Return 快照.源峰值尼特
                    If 快照.状态 = 播放状态.失败 Then Throw New InvalidOperationException("读取色彩内部数据时播放失败。")
                    If 计时.Elapsed >= TimeSpan.FromSeconds(30) Then Throw New TimeoutException("等待色彩内部数据超时。")
                    Thread.Sleep(5)
                Loop
            End Using
        End Using
    End Function

    Private Sub 打开并等待(控制器 As 播放器控制器, 路径 As String)
        Using 已打开 As New ManualResetEventSlim(False)
            Dim 失败消息 As String = Nothing
            Dim 打开处理 As EventHandler(Of 播放器媒体事件参数) =
                Sub(sender, e)
                    If String.Equals(e.文件路径, 路径, StringComparison.OrdinalIgnoreCase) Then 已打开.Set()
                End Sub
            Dim 错误处理 As EventHandler(Of 播放器错误事件参数) =
                Sub(sender, e)
                    失败消息 = e.消息
                    已打开.Set()
                End Sub
            AddHandler 控制器.媒体已打开, 打开处理
            AddHandler 控制器.播放错误, 错误处理
            Try
                控制器.打开媒体(路径)
                Dim 计时 = Stopwatch.StartNew()
                Do Until 已打开.IsSet
                    Application.DoEvents()
                    If 计时.Elapsed >= TimeSpan.FromSeconds(30) Then Throw New TimeoutException($"等待打开媒体超时：{路径}")
                    Thread.Sleep(5)
                Loop
                If Not String.IsNullOrEmpty(失败消息) Then Throw New InvalidOperationException(失败消息)
            Finally
                RemoveHandler 控制器.媒体已打开, 打开处理
                RemoveHandler 控制器.播放错误, 错误处理
            End Try
        End Using
    End Sub

    Private Sub 等待请求色彩模式(控制器 As 播放器控制器, 模式 As 色彩输出模式)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 快照 = 控制器.安全读取快照()
            If 快照 IsNot Nothing AndAlso 快照.请求色彩模式 = 模式 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(5) Then Throw New TimeoutException($"等待色彩模式 {模式} 超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 测试性能回归(SDR路径 As String, HDR路径 As String)
        Dim 弹幕 = 创建性能弹幕资料库()
        Dim 弹幕配置 As New 弹幕显示配置 With {
            .字体 = "Microsoft YaHei", .字号 = 8.0F, .使用源字号 = False,
            .目标帧率 = 60.0F, .同屏最大数量 = 100,
            .常规滚动最大行数 = 100, .顶部最大行数 = 100,
            .行间距 = 0.0F, .顶部边距 = 0.0F,
            .固定弹幕持续秒数 = 120.0F, .基准视频高度 = 1080.0F}
        Using 字幕 = 创建性能字幕轨道()
            ' 创建真实 HWND 和交换链，但窗口从不 Show；所有判定只读内部计数器。
            Using 输出窗口 As New Form With {
                .ClientSize = New Drawing.Size(1280, 720), .ShowInTaskbar = False,
                .FormBorderStyle = FormBorderStyle.None, .StartPosition = FormStartPosition.Manual,
                .Location = New Drawing.Point(-32000, -32000)}
                Using 画面控件 As New 播放器画面控件 With {.Dock = DockStyle.Fill}
                    输出窗口.Controls.Add(画面控件)
                    Dim 窗口句柄 = 输出窗口.Handle
                    Dim 输出句柄 = 画面控件.输出窗口句柄
                    ' 联合压力固定走 CPU 解码，覆盖用户报告的 4K 软件解码加
                    ' 60 Hz 弹幕路径；GPU 解码仍由常规诊断单独覆盖。
                    Using 会话 As New 播放器会话(New 播放器配置 With {
                        .解码器 = 解码模式.CPU, .色彩模式 = 色彩输出模式.映射到SDR,
                        .SDR峰值尼特 = 100.0F, .HDR峰值尼特 = 1000.0F,
                        .SDR纸白尼特 = 203.0F, .输出窗口句柄 = 输出句柄})
                        会话.设置音量(0.0F, True)
                        Using 字幕呈现器 As New 播放器定时文字图层呈现器(
                            画面控件, Function() 会话.当前快照, Function() 字幕,
                            AddressOf 会话.设置定时文字图层, Nothing, Nothing,
                            定时文字图层内容.仅字幕)
                            Using 弹幕呈现器 As New 播放器定时文字图层呈现器(
                                画面控件, Function() 会话.当前快照, Function() Nothing,
                                AddressOf 会话.设置弹幕图层, Function() 弹幕, 弹幕配置,
                                定时文字图层内容.仅弹幕)
                                ' 两个生产者使用各自的高精度泵和原生图层槽位。
                                Dim 图层泵 As Action = Nothing

                            ' 先播 HDR，再在同一个原生会话和同一个 HWND 上打开 SDR；这条路径
                            ' 能直接捕获 PQ/BT.2020 交换链或真实 HDR 请求被错误继承的问题。
                            会话.打开Async(HDR路径).GetAwaiter().GetResult()
                            断言(会话.当前快照.是HDR源, "性能回归的 HDR 样本没有被识别为 HDR。")
                            会话.播放()
                            等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(30))
                            等待压力图层(字幕呈现器, 弹幕呈现器, 会话)
                            Dim HDR帧率 = 读取视频帧率(会话)
                            Dim HDR结果 = 采样播放(会话, 6.0, 画面控件, 图层泵,
                                                 Function() 会话.当前弹幕状态)
                            Console.WriteLine($"HDR→SDR 联合压力：{格式化播放报告(HDR结果)}")
                            验证性能结果(HDR结果, HDR帧率, "HDR→SDR 联合压力")

                            会话.设置色彩模式(色彩输出模式.峰值映射HDR, 100.0F, 1000.0F, 203.0F)
                            等待色彩模式(会话, 色彩输出模式.峰值映射HDR)
                            会话.打开Async(SDR路径).GetAwaiter().GetResult()
                            Dim SDR打开快照 = 会话.当前快照
                            断言(Not SDR打开快照.是HDR源 AndAlso
                               SDR打开快照.请求色彩模式 = 色彩输出模式.映射到SDR AndAlso
                               SDR打开快照.实际色彩模式 = 色彩输出模式.映射到SDR,
                               "同会话 HDR→SDR 换片后仍继承了真实 HDR/PQ 输出状态。")
                            会话.播放()
                            等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(30))
                            等待压力图层(字幕呈现器, 弹幕呈现器, 会话)
                            断言(会话.当前快照.源峰值尼特 = 100UI,
                               $"SDR 换片后的源峰值不是 100 nit：{会话.当前快照.源峰值尼特}。")
                            Dim SDR帧率 = 读取视频帧率(会话)
                            Dim SDR结果 = 采样播放(会话, 6.0, 画面控件, 图层泵,
                                                 Function() 会话.当前弹幕状态)
                            Console.WriteLine($"SDR 联合压力：{格式化播放报告(SDR结果)}")
                            验证性能结果(SDR结果, SDR帧率, "SDR 联合压力")

                            测试音频切换与跳转(会话, SDR路径, SDR帧率, 画面控件, 图层泵,
                                           Function() 会话.当前弹幕状态)
                            End Using
                        End Using
                    End Using
                End Using
            End Using
        End Using
    End Sub

    Private Function 创建性能弹幕资料库() As 弹幕资料库
        Dim items = Enumerable.Range(0, 100).Select(
            Function(index) New 弹幕项目(TimeSpan.FromSeconds(2),
                弹幕类型.常规滚动, 1, 25.0F,
                &HFFFFFFFFUI, 0, 0, "performance", index + 1,
                $"弹幕压力 {index + 1:000} / danmaku performance"))
        Return New 弹幕资料库(items)
    End Function

    Private Function 创建性能字幕轨道() As 外部字幕轨道
        Const 内容 As String = "1" & vbLf &
            "00:00:00,000 --> 03:00:00,000" & vbLf &
            "字幕与弹幕联合性能回归 / subtitle performance contract" & vbLf
        Using reader As New StringReader(内容)
            Dim document = SRT字幕解析器.解析(reader)
            Return New 外部字幕轨道("internal-performance.srt", 外部字幕格式.SRT,
                New SRT字幕帧生成器(document, New SRT字幕样式()), Nothing)
        End Using
    End Function

    Private Sub 等待压力图层(字幕呈现器 As 播放器定时文字图层呈现器,
                         弹幕呈现器 As 播放器定时文字图层呈现器, 会话 As 播放器会话)
        Dim 失效前字幕序号 = 会话.当前定时文字状态.已提交序号
        Dim 失效前弹幕序号 = 会话.当前弹幕状态.已提交序号
        字幕呈现器.使图层失效()
        弹幕呈现器.使图层失效()
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 字幕状态 = 会话.当前定时文字状态
            Dim 弹幕状态 = 会话.当前弹幕状态
            If 字幕状态.已提交序号 > 失效前字幕序号 AndAlso
                字幕状态.已提交序号 = 字幕状态.已绘制序号 AndAlso 字幕状态.命令数 = 1 AndAlso
                字幕状态.可见像素数 > 0 AndAlso
                弹幕状态.已提交序号 > 失效前弹幕序号 AndAlso
                弹幕状态.已提交序号 = 弹幕状态.已绘制序号 AndAlso 弹幕状态.命令数 = 100 AndAlso
                弹幕状态.可见像素数 > 0 Then
                Dim 字幕序号 = 字幕状态.已提交序号
                Dim 弹幕序号 = 弹幕状态.已提交序号
                Dim 字幕合成帧数 = 字幕状态.图层呈现帧数
                Dim 弹幕合成帧数 = 弹幕状态.图层呈现帧数
                Dim 后备缓冲获取次数 = 字幕状态.后备缓冲获取次数
                Dim 独立计时 = Stopwatch.StartNew()
                While 独立计时.Elapsed < TimeSpan.FromMilliseconds(250)
                    Application.DoEvents()
                    Thread.Sleep(5)
                End While
                字幕状态 = 会话.当前定时文字状态
                弹幕状态 = 会话.当前弹幕状态
                断言(字幕状态.已提交序号 = 字幕序号,
                   "静态字幕被弹幕刷新率驱动并重复提交。")
                断言(弹幕状态.已提交序号 > 弹幕序号,
                   "弹幕没有使用独立于字幕的刷新序号。")
                ' 字幕不需要重绘，但每个最终交换链帧仍必须合成现有字幕与弹幕。
                ' 这条契约捕获双 Present 源交替提交而导致整层闪烁的回归。
                断言(字幕状态.图层呈现帧数 > 字幕合成帧数 AndAlso
                   弹幕状态.图层呈现帧数 > 弹幕合成帧数,
                   "静态字幕或弹幕没有持续进入最终交换链合成帧。")
                Dim 合成增量 = CULng(字幕状态.图层呈现帧数 - 字幕合成帧数)
                Dim 获取增量 = 字幕状态.后备缓冲获取次数 - 后备缓冲获取次数
                断言(获取增量 >= 合成增量 AndAlso 获取增量 <= 合成增量 + 1UL,
                   $"最终合成没有逐帧重新获取 D3D11 flip-model 逻辑后备缓冲：" &
                   $"获取/合成 {获取增量}/{合成增量}。")
                断言(字幕状态.合成像素着色器调用次数 > 0UL AndAlso
                   弹幕状态.合成像素着色器调用次数 > 0UL,
                   "字幕或弹幕最终全屏合成没有产生 GPU 像素着色器调用。")
                Console.WriteLine($"最终合成 GPU 像素调用：字幕 {字幕状态.合成像素着色器调用次数}，" &
                                  $"弹幕 {弹幕状态.合成像素着色器调用次数}。")
                Return
            End If
            If 计时.Elapsed >= TimeSpan.FromSeconds(5) Then
                Throw New TimeoutException($"独立字幕/弹幕图层没有收敛，最后命令数 " &
                    $"{字幕状态.命令数}/{弹幕状态.命令数}。")
            End If
            Thread.Sleep(10)
        Loop
    End Sub

    Private Function 读取视频帧率(会话 As 播放器会话) As Double
        Dim 快照 = 会话.当前快照
        Dim 信息 = 会话.当前媒体信息
        Dim 视频 = 信息?.流.FirstOrDefault(Function(x) x.索引 = 快照.当前视频流 AndAlso x.类型 = "video")
        断言(视频 IsNot Nothing AndAlso 视频.平均帧率 > 0, "媒体信息没有有效的视频平均帧率。")
        Return 视频.平均帧率
    End Function

    Private Sub 测试音频切换与跳转(会话 As 播放器会话, 音频路径 As String,
                               源帧率 As Double, 画面控件 As 播放器画面控件,
                               图层泵 As Action, 图层状态提供器 As Func(Of 定时文字状态))
        会话.加载外部音轨(音频路径)
        等待外部音轨状态(会话, True)
        Dim 外部起点 = 会话.当前快照.播放位置
        等待预热(会话, 外部起点 + TimeSpan.FromSeconds(0.5), TimeSpan.FromSeconds(30))
        Dim 外部结果 = 采样播放(会话, 2.5, 画面控件, 图层泵, 图层状态提供器)
        Console.WriteLine($"外部音轨：{格式化播放报告(外部结果)}")
        验证音频结果(外部结果, "外部音轨")
        验证性能结果(外部结果, 源帧率, "外部音轨", False)

        会话.设置外部音轨偏移(TimeSpan.FromMilliseconds(125))
        等待外部音轨偏移(会话, TimeSpan.FromMilliseconds(125))
        会话.跳转(TimeSpan.FromSeconds(30))
        等待预热(会话, TimeSpan.FromSeconds(30.5), TimeSpan.FromSeconds(30))
        Dim 跳转结果 = 采样播放(会话, 2.5, 画面控件, 图层泵, 图层状态提供器)
        Console.WriteLine($"外部音轨跳转：{格式化播放报告(跳转结果)}")
        验证音频结果(跳转结果, "外部音轨跳转")
        验证性能结果(跳转结果, 源帧率, "外部音轨跳转", False)

        Dim 恢复位置 = 会话.当前快照.播放位置
        会话.清除外部音轨()
        等待外部音轨状态(会话, False)
        等待预热(会话, 恢复位置 + TimeSpan.FromSeconds(0.5), TimeSpan.FromSeconds(30))
        Dim 内置结果 = 采样播放(会话, 2.5, 画面控件, 图层泵, 图层状态提供器)
        Console.WriteLine($"恢复内置音轨：{格式化播放报告(内置结果)}")
        验证音频结果(内置结果, "恢复内置音轨")
        验证性能结果(内置结果, 源帧率, "恢复内置音轨", False)
    End Sub

    Private Sub 等待外部音轨状态(会话 As 播放器会话, 目标 As Boolean)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            If 会话.当前快照.正在使用外部音轨 = 目标 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(10) Then Throw New TimeoutException("等待外部音轨切换超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 等待外部音轨偏移(会话 As 播放器会话, 目标 As TimeSpan)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            If 会话.当前快照.外部音轨偏移 = 目标 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(10) Then Throw New TimeoutException("等待外部音轨偏移更新超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 测试定时文字精确渲染合同()
        测试完整画布弹幕边界()
        测试连续小数位移()
        测试弹幕设置与尺寸热更新()
        测试弹幕自适应帧率()
        测试跳转不回溯弹幕()
        测试文字效果命令与原生合同()
        断言(播放器定时文字图层呈现器.计算刷新间隔毫秒(60) = 16 AndAlso
           播放器定时文字图层呈现器.计算刷新间隔毫秒(120) = 8 AndAlso
           播放器定时文字图层呈现器.计算刷新间隔毫秒(144) = 6,
           "整数毫秒唤醒周期低于目标刷新率。")
    End Sub

    Private Sub 测试弹幕自适应帧率()
        断言(播放器定时文字图层呈现器.计算自适应弹幕帧率(0) = 60 AndAlso
               播放器定时文字图层呈现器.计算自适应弹幕帧率(1) = 60 AndAlso
               播放器定时文字图层呈现器.计算自适应弹幕帧率(48) = 48 AndAlso
               播放器定时文字图层呈现器.计算自适应弹幕帧率(60) = 60 AndAlso
               播放器定时文字图层呈现器.计算自适应弹幕帧率(120) = 120 AndAlso
               播放器定时文字图层呈现器.计算自适应弹幕帧率(240) = 120,
            "弹幕自适应帧率的回退值或 120 FPS 上限不正确。")

        Using 窗口 As New Form With {.ClientSize = New Size(640, 360), .ShowInTaskbar = False}
            Using 控件 As New 播放器画面控件 With {.Dock = DockStyle.Fill}
                窗口.Controls.Add(控件)
                Dim 设备名称 = Screen.FromControl(控件).DeviceName
                Dim 显示器刷新率 = 播放器定时文字图层呈现器.读取显示器刷新率(设备名称)
                Dim 预期帧率 = 播放器定时文字图层呈现器.计算自适应弹幕帧率(显示器刷新率)
                Dim 配置 As New 弹幕显示配置 With {.目标帧率 = 30.0F}
                Dim 已提交帧率 As Single
                Using 呈现器 As New 播放器定时文字图层呈现器(控件, Function() Nothing,
                    Function() Nothing,
                    Sub(size, commands, sequence, frameRate) 已提交帧率 = frameRate,
                    Function() Nothing, 配置, 定时文字图层内容.仅弹幕)
                    断言(呈现器.目标帧率 = 预期帧率 AndAlso 配置.目标帧率 = 预期帧率,
                        $"弹幕没有采用当前显示器刷新率：设备 {设备名称}，读取 {显示器刷新率} Hz，" &
                        $"目标 {呈现器.目标帧率} FPS。")
                    呈现器.提交当前帧(New Size(640, 360), 1920UI, 1080UI,
                        TimeSpan.Zero, Nothing, 96.0F)
                    断言(Math.Abs(已提交帧率 - 预期帧率) < 0.01F,
                        "显示器自适应帧率没有进入原生弹幕图层提交。")
                    Dim 新配置 As New 弹幕显示配置 With {.目标帧率 = 30.0F}
                    呈现器.应用弹幕设置(新配置)
                    断言(新配置.目标帧率 = 预期帧率,
                        "重新应用弹幕设置覆盖了显示器自适应帧率。")
                End Using
            End Using
        End Using
    End Sub

    Private Sub 测试弹幕设置与尺寸热更新()
        Dim 开始时间 = TimeSpan.FromSeconds(1)
        Dim 滚动项目 As New 弹幕项目(开始时间, 弹幕类型.常规滚动,
            1, 25.0F, &HFFFFFFFFUI, 0, 0, "diagnostic", 20, "热更新滚动弹幕")
        Dim 顶部项目 As New 弹幕项目(开始时间, 弹幕类型.顶部,
            5, 25.0F, &HFFFFFFFFUI, 0, 0, "diagnostic", 21, "热更新顶部弹幕")
        Dim 资料库 As New 弹幕资料库({滚动项目, 顶部项目})
        Dim 初始配置 As New 弹幕显示配置 With {
            .基准视频高度 = 1080.0F, .字号 = 36.0F, .滚动速度 = 180.0F,
            .启用类型 = 弹幕类型.常规滚动 Or 弹幕类型.顶部}

        Using 控件 As New 播放器画面控件()
            Using 呈现器 As New 播放器定时文字图层呈现器(控件, Function() Nothing,
                Function() Nothing, Sub(size, commands, sequence, frameRate) Return,
                Function() 资料库, 初始配置, 定时文字图层内容.仅弹幕)
                Dim 初始命令 = 呈现器.生成命令(New Size(1920, 1080), 1920UI, 1080UI,
                    开始时间, Nothing, 96.0F)
                断言(初始命令.Count = 2, "弹幕热更新诊断没有建立初始活动项。")

                Dim 缩放后命令 = 呈现器.生成命令(New Size(1280, 720), 1920UI, 1080UI,
                    开始时间 + TimeSpan.FromMilliseconds(10), Nothing, 96.0F)
                断言(缩放后命令.Count = 2,
                    "视频渲染区域尺寸变化清空了正在显示的弹幕。")
                断言(缩放后命令.All(Function(command) Math.Abs(command.字号 - 24.0F) < 0.01F),
                    "视频渲染区域尺寸变化后没有即时更新活动弹幕字号。")

                Dim 新配置 As New 弹幕显示配置 With {
                    .基准视频高度 = 1080.0F, .字号 = 48.0F, .滚动速度 = 360.0F,
                    .使用源颜色 = False, .颜色ARGB = &HFF20C060UI, .不透明度 = 128,
                    .字体样式 = FontStyle.Bold, .启用类型 = 弹幕类型.常规滚动}
                呈现器.应用弹幕设置(新配置)
                Dim 设置后命令 = 呈现器.生成命令(New Size(1280, 720), 1920UI, 1080UI,
                    开始时间 + TimeSpan.FromMilliseconds(20), Nothing, 96.0F)
                断言(设置后命令.Count = 1 AndAlso 设置后命令(0).文本 = 滚动项目.文本,
                    "应用弹幕设置时没有保留仍满足新配置的活动弹幕。")
                断言(Math.Abs(设置后命令(0).字号 - 32.0F) < 0.01F AndAlso
                       设置后命令(0).前景色ARGB = &H8020C060UI,
                    "字号、颜色或不透明度没有即时应用到活动弹幕。")
                Dim 设置后X = 设置后命令(0).X

                Dim 下一帧命令 = 呈现器.生成命令(New Size(1280, 720), 1920UI, 1080UI,
                    开始时间 + TimeSpan.FromMilliseconds(30), Nothing, 96.0F)
                断言(下一帧命令.Count = 1 AndAlso
                       Math.Abs((设置后X - 下一帧命令(0).X) - 2.4F) < 0.01F,
                    "滚动速度更新后活动弹幕没有按新速度连续移动。")
            End Using
        End Using
    End Sub

    Private Sub 测试完整画布弹幕边界()
        Dim 配置 As New 弹幕显示配置 With {
            .基准视频高度 = 720.0F, .滚动速度 = 5000.0F,
            .字号 = 36.0F, .描边宽度 = 1.0F, .阴影深度 = 1.5F}
        Dim 项目 As New 弹幕项目(TimeSpan.Zero, 弹幕类型.常规滚动, 1, 25.0F,
            &HFFFFFFFFUI, 0, 0, "diagnostic", 1, "完整画布边界 diagnostic")
        Dim 资料库 As New 弹幕资料库({项目})
        Using 控件 As New 播放器画面控件()
            Using 呈现器 As New 播放器定时文字图层呈现器(控件, Function() Nothing,
                Function() Nothing, Sub(size, commands, sequence, frameRate) Return,
                Function() 资料库, 配置, 定时文字图层内容.仅弹幕)
                Dim 开始命令 = 呈现器.生成命令(New Size(1600, 720), 720UI, 720UI,
                    TimeSpan.Zero, Nothing, 96.0F).Single()
                Dim 开始X = 开始命令.X
                Dim 文字宽度 = 开始命令.宽度
                断言(Math.Abs(开始X - 1605.5F) < 0.01F,
                   $"弹幕没有从完整 1600 px 画布外开始：X={开始X:F3}。")
                Dim 高DPI开始X = 呈现器.生成命令(New Size(1600, 720), 720UI, 720UI,
                    TimeSpan.Zero, Nothing, 144.0F).Single().X
                断言(Math.Abs(高DPI开始X - 1605.5F) < 0.01F,
                   $"150% DPI 下弹幕画布没有保持物理客户区宽度：X={高DPI开始X:F3}。")

                Dim 黑边中X = 呈现器.生成命令(New Size(1600, 720), 720UI, 720UI,
                    TimeSpan.FromMilliseconds(50), Nothing, 96.0F).Single().X
                Const 视频右边界 As Single = 1160.0F
                断言(黑边中X > 视频右边界 AndAlso 黑边中X < 1600.0F,
                   $"弹幕没有连续穿过右侧可见黑边：X={黑边中X:F3}，视频右边界={视频右边界:F3}。")

                Dim 阴影外扩 = 配置.阴影深度 * 弹幕显示配置.软阴影扩散倍数
                Dim 左外扩 = 配置.描边宽度 + 阴影外扩
                Dim 右外扩 = 配置.描边宽度 + 阴影外扩
                Dim 结束秒 = (1600.0F + 文字宽度 + 左外扩 + 右外扩) / 配置.滚动速度
                Dim 采样秒 = 0.1R
                While 采样秒 < 结束秒 - 0.00002R
                    呈现器.生成命令(New Size(1600, 720), 720UI, 720UI,
                        TimeSpan.FromSeconds(采样秒), Nothing, 96.0F)
                    采样秒 += 0.05R
                End While
                Dim 消失前 = 呈现器.生成命令(New Size(1600, 720), 720UI, 720UI,
                    TimeSpan.FromSeconds(结束秒 - 0.00001R), Nothing, 96.0F)
                断言(消失前.Count = 1 AndAlso
                   消失前(0).X + 消失前(0).宽度 + 右外扩 > -0.1F,
                   "弹幕在全部文字效果离开画布前被提前销毁。")
                Dim 消失后 = 呈现器.生成命令(New Size(1600, 720), 720UI, 720UI,
                    TimeSpan.FromSeconds(结束秒 + 0.00001R), Nothing, 96.0F)
                断言(消失后.Count = 0, "弹幕全部离开画布后仍被保留。")
            End Using
        End Using
    End Sub

    Private Sub 测试连续小数位移()
        Dim 配置 As New 弹幕显示配置 With {
            .基准视频高度 = 1080.0F, .滚动速度 = 181.3F, .目标帧率 = 60.0F}
        Dim 资料库 As New 弹幕资料库({New 弹幕项目(TimeSpan.Zero, 弹幕类型.常规滚动,
            1, 25.0F, &HFFFFFFFFUI, 0, 0, "diagnostic", 2, "小数位移")})
        Dim 调度器 As New 弹幕调度器(资料库, 配置)
        Dim 区域 As New 视频显示区域(0, 0, 1920, 1080, 1, 96)
        Dim 结果 As New List(Of 弹幕绘制项)(1)
        调度器.生成帧(TimeSpan.Zero, 区域, 结果)
        结果.Clear()
        调度器.生成帧(TimeSpan.FromMilliseconds(10), 区域, 结果)
        Dim 十毫秒X = 结果.Single().X像素
        Dim 十毫秒帧 = 调度器.当前帧序号
        结果.Clear()
        调度器.生成帧(TimeSpan.FromMilliseconds(14), 区域, 结果)
        Dim 十四毫秒X = 结果.Single().X像素
        Dim 十四毫秒帧 = 调度器.当前帧序号
        Dim 实际位移 = 十毫秒X - 十四毫秒X
        Dim 预期位移 = 配置.滚动速度 * 0.004F
        断言(十毫秒帧 = 十四毫秒帧,
           "诊断采样没有落在同一个传统量化帧，无法检出位置量化。")
        断言(Math.Abs(实际位移 - 预期位移) < 0.001F,
           $"弹幕位置仍被帧格量化：{实际位移:F6}/{预期位移:F6} px。")
        断言(Math.Abs(十四毫秒X - MathF.Round(十四毫秒X)) > 0.001F,
           $"弹幕 X 坐标被整数化：{十四毫秒X:F6}。")
    End Sub

    Private Sub 测试跳转不回溯弹幕()
        Dim 配置 As New 弹幕显示配置 With {.基准视频高度 = 1080, .滚动速度 = 10.0F}
        Dim 旧项目 As New 弹幕项目(TimeSpan.FromSeconds(1), 弹幕类型.常规滚动,
            1, 25, &HFFFFFFFFUI, 0, 0, "diagnostic", 10, "跳转前旧弹幕")
        Dim 新项目 As New 弹幕项目(TimeSpan.FromSeconds(5.2), 弹幕类型.常规滚动,
            1, 25, &HFFFFFFFFUI, 0, 0, "diagnostic", 11, "跳转后的弹幕")
        Dim 调度器 As New 弹幕调度器(New 弹幕资料库({旧项目, 新项目}), 配置)
        Dim 区域 As New 视频显示区域(0, 0, 1920, 1080, 1, 96)
        Dim 结果 As New List(Of 弹幕绘制项)()
        调度器.生成帧(TimeSpan.FromSeconds(1), 区域, 结果)
        断言(结果.Count = 1 AndAlso 结果(0).项目.弹幕编号 = 10, "跳转前测试弹幕未进入活动状态。")
        结果.Clear()
        调度器.生成帧(TimeSpan.FromSeconds(5), 区域, 结果)
        断言(结果.Count = 0, "Seek 后仍回溯并恢复了当前位置之前的弹幕。")
        结果.Clear()
        调度器.生成帧(TimeSpan.FromSeconds(5.2), 区域, 结果)
        断言(结果.Count = 1 AndAlso 结果(0).项目.弹幕编号 = 11,
           "Seek 后没有从当前位置继续读取新弹幕。")

        ' 时间轴代次来自实际完成的原生 DoSeek；即使只前跳 10 ms，也不能靠阈值漏判。
        Dim 小幅新项目 As New 弹幕项目(TimeSpan.FromSeconds(1.02), 弹幕类型.常规滚动,
            1, 25, &HFFFFFFFFUI, 0, 0, "diagnostic", 12, "小幅跳转后的弹幕")
        Dim 小幅资料库 As New 弹幕资料库({旧项目, 小幅新项目})
        Using 控件 As New 播放器画面控件()
            Using 呈现器 As New 播放器定时文字图层呈现器(控件, Function() Nothing,
                Function() Nothing, Sub(size, commands, sequence, frameRate) Return,
                Function() 小幅资料库, 配置, 定时文字图层内容.仅弹幕)
                Dim 跳转前 = 呈现器.生成命令(New Size(1920, 1080), 1920UI, 1080UI,
                    TimeSpan.FromSeconds(1), Nothing, 96.0F, 7UL)
                断言(跳转前.Count = 1, "小幅 Seek 诊断没有建立旧弹幕状态。")
                Dim 跳转后 = 呈现器.生成命令(New Size(1920, 1080), 1920UI, 1080UI,
                    TimeSpan.FromSeconds(1.01), Nothing, 96.0F, 8UL)
                断言(跳转后.Count = 0, "时间轴代次变化后仍保留了小幅 Seek 前的弹幕。")
                Dim 新弹幕 = 呈现器.生成命令(New Size(1920, 1080), 1920UI, 1080UI,
                    TimeSpan.FromSeconds(1.02), Nothing, 96.0F, 8UL)
                断言(新弹幕.Count = 1 AndAlso 新弹幕(0).文本 = 小幅新项目.文本,
                   "小幅 Seek 后没有从新位置继续读取弹幕。")
            End Using
        End Using
    End Sub

    Private Sub 测试文字效果命令与原生合同()
        Dim 普通位图 = 定时文字命令.创建位图(New Byte() {255, 255, 255, 255}, 1, 1, 4,
            New RectangleF(0, 0, 1, 1))
        Dim HDR高亮位图 = 定时文字命令.创建位图(New Byte() {255, 255, 255, 255}, 1, 1, 4,
            New RectangleF(0, 0, 1, 1), HDR高亮:=True)
        断言(普通位图.样式 = 定时文字样式.无 AndAlso
               HDR高亮位图.样式 = 定时文字样式.HDR高亮位图,
            "SUP HDR 高亮标记污染了普通 ASS/信息位图，或没有进入位图命令。")

        Const SRT内容 = "1" & vbLf & "00:00:00,000 --> 00:00:02,000" & vbLf & "SRT quality diagnostic" & vbLf
        Dim 文档 = SRT字幕解析器.解析(New StringReader(SRT内容))
        Dim 样式 As New SRT字幕样式 With {.底部边距 = 0.0F}
        Using 轨道 As New 外部字幕轨道("diagnostic.srt", 外部字幕格式.SRT,
            New SRT字幕帧生成器(文档, 样式), Nothing)
            Using 控件 As New 播放器画面控件()
                Using 呈现器 As New 播放器定时文字图层呈现器(控件, Function() Nothing,
                    Function() 轨道, Sub(size, commands, sequence, frameRate) Return,
                    图层内容:=定时文字图层内容.仅字幕)
                    Dim 命令 = 呈现器.生成命令(New Size(1920, 1080), 1920UI, 1080UI,
                        TimeSpan.FromMilliseconds(500), 轨道, 96.0F).Single()
                    断言(命令.描边宽度 = 样式.描边宽度 AndAlso
                       命令.阴影色ARGB = 样式.阴影颜色ARGB AndAlso
                       命令.阴影X偏移 = 样式.阴影偏移 AndAlso 命令.阴影Y偏移 = 样式.阴影偏移,
                        "SRT 最高质量文字效果没有完整进入生产命令。")
                    Dim 测量 As New 原生定时文字测量 With {
                        .大小 = CUInt(Marshal.SizeOf(Of 原生定时文字测量)()), .版本 = 1UI}
                    Dim 测量结果 = 播放器原生接口.FFF3FP_MeasureTimedText(
                        命令.文本, 命令.字体, 命令.字号,
                        CType(命令.样式, 原生定时文字标志), 命令.宽度,
                        命令.描边宽度, 命令.阴影X偏移, 命令.阴影Y偏移,
                        If((命令.阴影色ARGB >> 24) <> 0UI, 1UI, 0UI), 测量)
                    断言(测量结果 = 原生播放器结果.成功 AndAlso 测量.布局高度 > 0 AndAlso
                           测量.可见底部 > 测量.布局高度,
                       "DirectWrite 字幕布局或最终可见底边测量无效。")
                    Dim 最终可见底边 = 命令.Y + 测量.可见底部
                    断言(Math.Abs(最终可见底边 - 1080.0F) < 0.01F,
                       $"字幕底部距离 0 时最终可见像素没有贴齐画布：{最终可见底边:F3}px。")
                End Using
            End Using
        End Using

        Dim 诊断 As New 原生定时文字栅格诊断 With {
            .大小 = CUInt(Marshal.SizeOf(Of 原生定时文字栅格诊断)()), .版本 = 1,
            .描边宽度 = 1.25F, .阴影X偏移 = 2.0F, .阴影Y偏移 = 2.0F}
        断言(FFF3FP_EvaluateTimedTextRasterization(诊断) = 0, "原生定时文字栅格诊断调用失败。")
        断言(Math.Abs(诊断.几何笔宽 - 2.5F) < 0.0001F AndAlso 诊断.仅向外描边 = 1,
           $"描边不是最终可见宽度 1.25 px 的纯外描边：笔宽={诊断.几何笔宽:F3}。")
        断言(Math.Abs(诊断.左外扩 - 1.25F) < 0.0001F AndAlso
           Math.Abs(诊断.上外扩 - 1.25F) < 0.0001F AndAlso
           Math.Abs(诊断.右外扩 - 3.25F) < 0.0001F AndAlso
           Math.Abs(诊断.下外扩 - 3.25F) < 0.0001F,
           "描边和阴影的精确外扩边界不正确。")
        断言(Math.Abs(诊断.阴影角度 - 45.0F) < 0.001F,
           $"默认阴影不是 45 度：{诊断.阴影角度:F4} 度。")
        断言(诊断.自然对称渲染 = 1 AndAlso 诊断.灰度抗锯齿 = 1 AndAlso
           诊断.禁用像素吸附 = 1,
           "原生文字没有启用自然对称渲染、灰度抗锯齿或小数像素定位。")
    End Sub

    Private Function 测试弹幕(视频路径 As String, 资料库 As 弹幕资料库) As TimeSpan
        断言(资料库 IsNot Nothing AndAlso 资料库.数量 > 0, "弹幕 XML 没有解析出任何条目。")
        Dim 自动加载资料库 = 弹幕自动加载器.尝试加载同名弹幕Async(视频路径, CancellationToken.None).
            GetAwaiter().GetResult()
        断言(自动加载资料库 IsNot Nothing AndAlso 自动加载资料库.数量 = 资料库.数量,
           "打开媒体时没有加载同名 XML 弹幕。")
        Dim 配置 As New 弹幕显示配置()
        断言(配置.同屏最大数量 = 100, "弹幕默认同屏上限不是 100。")
        断言(配置.字号 = 36.0F, "弹幕默认基础字号不是 36。")
        断言(配置.常规滚动最大行数 = 5, "弹幕默认显示行数上限不是 5。")
        断言(配置.顶部最大行数 = 5, "顶部弹幕默认显示行数上限不是 5。")
        断言(String.Equals(配置.字体, "Microsoft YaHei UI", StringComparison.Ordinal), "弹幕默认字体不是微软雅黑 UI。")
        断言(配置.目标帧率 = 60.0F, "弹幕默认帧率不是 60 FPS。")

        Dim 区域 = 视频显示区域.计算(1280, 720, 96.0F, 3840, 2160)
        Dim 调度器 As New 弹幕调度器(资料库, 配置)
        Dim 绘制项 As New List(Of 弹幕绘制项)(配置.同屏最大数量)
        Dim 测试位置 = TimeSpan.Zero
        For Each 项 In 资料库.项目
            If (项.类型 And 弹幕类型.常用) = 0 Then Continue For
            绘制项.Clear()
            调度器.生成帧(项.出现时间, 区域, 绘制项)
            If 绘制项.Count = 0 Then Continue For
            绘制项.Clear()
            调度器.生成帧(项.出现时间 + TimeSpan.FromMilliseconds(50), 区域, 绘制项)
            测试位置 = 项.出现时间 + TimeSpan.FromMilliseconds(50)
            Exit For
        Next
        断言(测试位置 > TimeSpan.Zero OrElse 绘制项.Count > 0, "没有生成可显示的基础弹幕。")
        断言(绘制项.Count > 0 AndAlso 绘制项.Count <= 配置.同屏最大数量,
           "弹幕调度器没有遵守同屏数量上限。")
        断言(绘制项.All(Function(x) x.字号像素 > 0 AndAlso x.宽度像素 > 0 AndAlso
                         x.高度像素 > 0 AndAlso Not String.IsNullOrWhiteSpace(x.项目.文本)),
           "弹幕绘制指令包含无效文字或几何数据。")
        测试弹幕行数上限(配置)
        测试弹幕相对缩放(配置)
        测试小尺寸滚动连续性(配置)
        Console.WriteLine($"弹幕：{资料库.数量} 条，默认 100 条/5 行/36 号微软雅黑 UI/60 FPS，" &
                          $"{绘制项.Count} 条基础弹幕在 {测试位置.TotalSeconds:F3}s 的调度通过。")
        Return 测试位置
    End Function

    Private Sub 测试弹幕行数上限(配置 As 弹幕显示配置)
        Dim 区域 = 视频显示区域.计算(1920, 1080, 96.0F, 1920, 1080)
        For Each 类型 In {弹幕类型.常规滚动, 弹幕类型.顶部}
            Dim 密集项目 = Enumerable.Range(1, 20).Select(
                Function(index) New 弹幕项目(TimeSpan.FromMilliseconds(100), 类型, 5, 25.0F,
                    &HFFFFFFFFUI, 0, 0, "test", index, $"密集弹幕 {index}"))
            Dim 调度器 As New 弹幕调度器(New 弹幕资料库(密集项目), 配置)
            Dim 绘制项 As New List(Of 弹幕绘制项)()
            调度器.生成帧(TimeSpan.FromMilliseconds(100), 区域, 绘制项)
            Dim 行数 = 绘制项.Select(Function(item) Math.Round(item.Y像素, 3)).Distinct().Count()
            断言(行数 <= 5, $"{类型}弹幕生成了 {行数} 行，超过 5 行上限。")
        Next
    End Sub

    Private Sub 测试小尺寸滚动连续性(配置 As 弹幕显示配置)
        Dim 开始时间 = TimeSpan.FromSeconds(1)
        Dim 资料库 As New 弹幕资料库({New 弹幕项目(开始时间, 弹幕类型.常规滚动, 5, 25.0F,
            &HFFFFFFFFUI, 0, 0, "test", 1, "小尺寸连续滚动测试")})
        For Each 高度 In {360, 720, 1080, 2160}
            Dim 区域 = 视频显示区域.计算(CInt(高度 * 16.0 / 9.0), 高度, 96.0F, 1920, 1080)
            Dim 调度器 As New 弹幕调度器(资料库, 配置)
            Dim 上一X As Single = Single.NaN
            Dim 预期步长 = 配置.滚动速度 * 区域.高度像素 / 配置.基准视频高度 / 配置.目标帧率
            For 帧 = 0 To 120
                Dim 绘制项 As New List(Of 弹幕绘制项)(1)
                调度器.生成帧(开始时间 + TimeSpan.FromSeconds(帧 / CDbl(配置.目标帧率)), 区域, 绘制项)
                断言(绘制项.Count = 1, $"{高度} 高度的连续滚动测试丢失了第 {帧} 帧。")
                If Single.IsFinite(上一X) Then
                    Dim 实际步长 = 上一X - 绘制项(0).X像素
                    断言(Math.Abs(实际步长 - 预期步长) < 0.01F,
                       $"{高度} 高度的弹幕滚动步长不连续：{实际步长:F3}/{预期步长:F3}。")
                End If
                上一X = 绘制项(0).X像素
            Next
        Next
    End Sub

    Private Sub 测试弹幕相对缩放(配置 As 弹幕显示配置)
        Dim 单条资料库 As New 弹幕资料库({New 弹幕项目(TimeSpan.FromMilliseconds(100), 弹幕类型.顶部, 5, 25.0F,
            &HFFFFFFFFUI, 0, 0, "test", 1, "DPI 缩放测试")})
        Using 画面控件 As New 播放器画面控件()
            Using 呈现器 As New 播放器定时文字图层呈现器(画面控件, Function() Nothing,
                Function() Nothing, Sub(size, commands, sequence, frameRate) Return, Function() 单条资料库, 配置,
                定时文字图层内容.仅弹幕)
                Dim 七百二十命令 = 呈现器.生成命令(New Size(1280, 720), 3840UI, 2160UI,
                    TimeSpan.FromMilliseconds(100), Nothing, 96.0F).Single()
                Dim 一千零八十命令 = 呈现器.生成命令(New Size(1920, 1080), 3840UI, 2160UI,
                    TimeSpan.FromMilliseconds(100), Nothing, 96.0F).Single()
                Dim 高DPI命令 = 呈现器.生成命令(New Size(1920, 1080), 3840UI, 2160UI,
                    TimeSpan.FromMilliseconds(100), Nothing, 144.0F).Single()
                Dim 四K命令 = 呈现器.生成命令(New Size(3840, 2160), 3840UI, 2160UI,
                    TimeSpan.FromMilliseconds(100), Nothing, 192.0F).Single()
                断言(Math.Abs(七百二十命令.字号 - 24.0F) < 0.01F AndAlso
                   Math.Abs(一千零八十命令.字号 - 36.0F) < 0.01F AndAlso
                   Math.Abs(高DPI命令.字号 - 36.0F) < 0.01F AndAlso
                   Math.Abs(四K命令.字号 - 72.0F) < 0.01F,
                   $"弹幕 DPI/视频相对字号异常：{七百二十命令.字号:F2}/" &
                   $"{一千零八十命令.字号:F2}/{高DPI命令.字号:F2}/{四K命令.字号:F2}。")
                断言(一千零八十命令.描边色ARGB = 配置.描边颜色ARGB AndAlso
                   Math.Abs(一千零八十命令.描边宽度 - 1.0F) < 0.01F,
                   "弹幕外描边没有按默认字号基准生成。")
                断言(一千零八十命令.阴影色ARGB = 配置.阴影颜色ARGB AndAlso
                   Math.Abs(一千零八十命令.阴影X偏移 - 配置.阴影深度) < 0.01F AndAlso
                   Math.Abs(一千零八十命令.阴影Y偏移 - 配置.阴影深度) < 0.01F AndAlso
                   (一千零八十命令.样式 And 定时文字样式.软阴影) <> 0,
                   "弹幕软阴影深度或呈现标志没有进入文字命令。")
            End Using
        End Using
    End Sub

    Private Sub 测试字幕(视频路径 As String, ASS路径 As String, SRT路径 As String)
        Dim SRT文档 = SRT字幕解析器.解析文件(SRT路径)
        断言(SRT文档.提示.Count = 2384, $"SRT 条目数异常：{SRT文档.提示.Count}。")

        Dim 区域 = 视频显示区域.计算(1280, 720, 96.0F, 3840, 2160)
        Dim SRT绘制项 As New List(Of SRT字幕绘制项)()
        Dim SRT生成器 As New SRT字幕帧生成器(SRT文档, New SRT字幕样式())
        SRT生成器.生成帧(TimeSpan.FromSeconds(75.5), 区域, SRT绘制项)
        断言(SRT绘制项.Count > 0 AndAlso SRT绘制项.Any(
               Function(x) x.行.Any(Function(y) Not String.IsNullOrWhiteSpace(y.文本))),
               "SRT 在 1:15.5 没有生成可见文本。")

        Using 画面控件 As New 播放器画面控件()
            Using 呈现器 As New 播放器定时文字图层呈现器(画面控件, Function() Nothing,
                                                        Function() Nothing,
                                                        Sub(size, commands, sequence, frameRate) Return)
                Using ASS轨道 = 外部字幕自动加载器.加载字幕(ASS路径, 视频路径)
                    断言(ASS轨道.ASS特效生成器 IsNot Nothing, "ASS 没有创建 libass 渲染器。")
                    断言(统计字幕命令(呈现器, ASS轨道) > 0, "ASS 没有生成 GPU 特效位图命令。")
                End Using
                Using SRT轨道 As New 外部字幕轨道(SRT路径, 外部字幕格式.SRT,
                                                New SRT字幕帧生成器(SRT文档, New SRT字幕样式()),
                                                Nothing)
                    断言(统计字幕命令(呈现器, SRT轨道) > 0, "SRT 没有生成 GPU 文字命令。")
                End Using
            End Using
        End Using

        Using 自动轨道 = 外部字幕自动加载器.尝试加载同名字幕Async(
            视频路径, CancellationToken.None).GetAwaiter().GetResult()
            断言(自动轨道 IsNot Nothing, "后台字幕加载器没有找到同名字幕。")
            断言(自动轨道.格式 = 外部字幕格式.SRT, "后台字幕加载优先级没有选择 SRT。")
        End Using
        Console.WriteLine($"字幕：ASS libass 特效位图、SRT {SRT文档.提示.Count} 条，后台加载与 1:15.5 帧生成通过。")
    End Sub

    Private Function 统计字幕命令(呈现器 As 播放器定时文字图层呈现器,
                                字幕 As 外部字幕轨道) As Integer
        Return 呈现器.生成命令(New Size(1280, 720), 3840UI, 2160UI,
                           TimeSpan.FromSeconds(75.5), 字幕).Count
    End Function

    Private Function 测试播放(视频路径 As String, 字幕路径 As String, 弹幕 As 弹幕资料库,
                          弹幕测试位置 As TimeSpan, 模式 As 解码模式) As 播放测量结果
        Using 输出窗口 As New Form With {
            .ClientSize = New Drawing.Size(1280, 720),
            .FormBorderStyle = FormBorderStyle.FixedToolWindow,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Drawing.Point(20, 20),
            .Text = $"3FP {模式} 自动性能测试"
        }
            Dim 画面控件 As 播放器画面控件 = Nothing
            Dim 字幕呈现器 As 播放器定时文字图层呈现器 = Nothing
            Dim 弹幕呈现器 As 播放器定时文字图层呈现器 = Nothing
            Dim 字幕轨道 As 外部字幕轨道 = Nothing
            If Not String.IsNullOrEmpty(字幕路径) OrElse 弹幕 IsNot Nothing Then
                画面控件 = New 播放器画面控件 With {.Dock = DockStyle.Fill}
                输出窗口.Controls.Add(画面控件)
            End If
            If Not String.IsNullOrEmpty(字幕路径) Then
                Select Case Path.GetExtension(字幕路径).ToLowerInvariant()
                    Case ".srt"
                        Dim 文档 = SRT字幕解析器.解析文件(字幕路径)
                        字幕轨道 = New 外部字幕轨道(字幕路径, 外部字幕格式.SRT,
                            New SRT字幕帧生成器(文档, New SRT字幕样式()), Nothing)
                    Case ".ass", ".ssa"
                        字幕轨道 = 外部字幕自动加载器.加载字幕(字幕路径, 视频路径)
                    Case Else
                        Throw New NotSupportedException($"测试不支持字幕格式：{字幕路径}")
                End Select
            End If
            输出窗口.Show()
            Application.DoEvents()
            Using 会话 As New 播放器会话(New 播放器配置 With {
                .解码器 = 模式,
                .色彩模式 = 色彩输出模式.映射到SDR,
                .SDR峰值尼特 = 100.0F,
                .HDR峰值尼特 = 1000.0F,
                .SDR纸白尼特 = 203.0F,
                .输出窗口句柄 = If(画面控件 Is Nothing, 输出窗口.Handle, 画面控件.输出窗口句柄)
            })
                Try
                    If 画面控件 IsNot Nothing Then
                        If 字幕轨道 IsNot Nothing Then
                            字幕呈现器 = New 播放器定时文字图层呈现器(画面控件,
                                Function() 会话.当前快照, Function() 字幕轨道,
                                AddressOf 会话.设置定时文字图层, Nothing, Nothing,
                                定时文字图层内容.仅字幕)
                        End If
                        If 弹幕 IsNot Nothing Then
                            弹幕呈现器 = New 播放器定时文字图层呈现器(画面控件,
                                Function() 会话.当前快照, Function() Nothing,
                                AddressOf 会话.设置弹幕图层, Function() 弹幕, Nothing,
                                定时文字图层内容.仅弹幕)
                        End If
                    End If
                    会话.设置音量(0.0F, True)
                    会话.打开Async(视频路径).GetAwaiter().GetResult()
                    Dim 信息 = 会话.当前媒体信息
                    断言(信息 IsNot Nothing AndAlso 信息.流.Any(
                           Function(x) x.类型 = "video" AndAlso x.编码 = "av1" AndAlso
                               x.宽度 = 3840 AndAlso x.高度 = 2160),
                           "测试视频没有被识别为 4K AV1。")
                    测试HDR映射状态(会话)
                    会话.播放()
                    等待预热(会话, TimeSpan.FromSeconds(1), TimeSpan.FromSeconds(30))
                    Dim 图层状态提供器 As Func(Of 定时文字状态) = Nothing
                    If 弹幕 IsNot Nothing Then 图层状态提供器 = Function() 会话.当前弹幕状态
                    Dim 顺播结果 = 采样播放(会话, 测量秒数, 画面控件, Nothing, 图层状态提供器)
                    测试暂停停钟(会话)
                    会话.跳转(TimeSpan.FromSeconds(75))
                    等待预热(会话, TimeSpan.FromSeconds(75.5), TimeSpan.FromSeconds(30))
                    Dim 跳转结果 = 采样播放(会话, 6.0, 画面控件, Nothing, 图层状态提供器)
                    验证播放结果(跳转结果, "跳转后")
                    Console.WriteLine($"跳转后：{格式化播放报告(跳转结果)}")
                    If 画面控件 IsNot Nothing AndAlso 字幕轨道 IsNot Nothing Then
                        会话.跳转(TimeSpan.FromSeconds(75))
                        等待预热(会话, TimeSpan.FromSeconds(75.5), TimeSpan.FromSeconds(30))
                        测试屏幕字幕(会话, 画面控件)
                    End If
                    If 画面控件 IsNot Nothing AndAlso 弹幕 IsNot Nothing Then
                        测试屏幕弹幕(会话, 画面控件, 弹幕呈现器, 弹幕测试位置)
                    End If
                    Return 顺播结果
                Finally
                    弹幕呈现器?.释放()
                    字幕呈现器?.释放()
                    字幕轨道?.释放()
                    画面控件?.Dispose()
                End Try
            End Using
        End Using
    End Function

    Private Sub 测试HDR映射状态(会话 As 播放器会话)
        Dim 初始快照 = 会话.当前快照
        If Not 初始快照.是HDR源 Then Return
        断言(初始快照.请求色彩模式 = 色彩输出模式.映射到SDR AndAlso
           初始快照.实际色彩模式 = 色彩输出模式.映射到SDR, "HDR 文件打开后没有使用 SDR 映射。")
        会话.设置色彩模式(色彩输出模式.原始HDR按SDR呈现, 100.0F, 1000.0F, 203.0F)
        等待色彩模式(会话, 色彩输出模式.原始HDR按SDR呈现)
        会话.设置色彩模式(色彩输出模式.峰值映射HDR, 100.0F, 1000.0F, 203.0F)
        等待色彩模式(会话, 色彩输出模式.峰值映射HDR)
        会话.设置色彩模式(色彩输出模式.映射到SDR, 100.0F, 1000.0F, 203.0F)
        等待色彩模式(会话, 色彩输出模式.映射到SDR)
        Dim 最终快照 = 会话.当前快照
        断言(最终快照.实际色彩模式 = 色彩输出模式.映射到SDR,
           "HDR 模式循环后没有恢复到 SDR 映射。")
        Console.WriteLine("HDR：原始 SDR、真实 HDR 请求与再次映射 SDR 的原生状态切换通过。")
    End Sub

    Private Sub 等待色彩模式(会话 As 播放器会话, 请求模式 As 色彩输出模式)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            If 快照.请求色彩模式 = 请求模式 Then Return
            If 计时.Elapsed >= TimeSpan.FromSeconds(3) Then
                Throw New TimeoutException($"等待色彩模式 {请求模式} 超时。")
            End If
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 等待预热(会话 As 播放器会话, 目标位置 As TimeSpan, 超时 As TimeSpan)
        Dim 计时 = Stopwatch.StartNew()
        Dim 上限 = 目标位置 + TimeSpan.FromSeconds(2)
        Do
            Application.DoEvents()
            Dim 快照 = 会话.当前快照
            If 快照.状态 = 播放状态.失败 Then
                Throw New InvalidOperationException($"播放器在预热阶段失败：{会话.最后错误消息}")
            End If
            If 快照.播放位置 >= 目标位置 AndAlso 快照.播放位置 < 上限 AndAlso
                快照.帧序号 >= 0 Then Return
            If 计时.Elapsed >= 超时 Then Throw New TimeoutException("播放预热超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Sub 测试暂停停钟(会话 As 播放器会话)
        会话.暂停()
        等待状态(会话, 播放状态.已暂停, TimeSpan.FromSeconds(3))
        Dim 暂停位置 = 会话.当前快照.播放位置
        Thread.Sleep(750)
        Application.DoEvents()
        Dim 漂移 = Math.Abs((会话.当前快照.播放位置 - 暂停位置).TotalMilliseconds)
        断言(漂移 <= 10.0, $"暂停期间媒体时钟漂移 {漂移:F1} ms。")
        会话.播放()
        等待状态(会话, 播放状态.正在播放, TimeSpan.FromSeconds(3))
        Console.WriteLine($"暂停：750 ms 内媒体时钟漂移 {漂移:F1} ms。")
    End Sub

    Private Sub 测试屏幕字幕(会话 As 播放器会话, 画面控件 As 播放器画面控件)
        会话.暂停()
        等待状态(会话, 播放状态.已暂停, TimeSpan.FromSeconds(3))
        Dim 显示计时 = Stopwatch.StartNew()
        While 显示计时.Elapsed < TimeSpan.FromSeconds(3)
            Application.DoEvents()
            Thread.Sleep(16)
        End While
        Dim 状态 As 定时文字状态 = Nothing
        Dim 收敛计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            状态 = 会话.当前定时文字状态
            If 状态.已绘制序号 > 0 AndAlso 状态.已绘制序号 = 状态.已提交序号 AndAlso
                状态.命令数 > 0 Then Exit Do
            If 收敛计时.Elapsed >= TimeSpan.FromSeconds(3) Then Exit Do
            Thread.Sleep(10)
        Loop
        断言(状态.已绘制序号 > 0 AndAlso 状态.已绘制序号 = 状态.已提交序号,
           "GPU 定时文字图层没有绘制最新命令。")
        断言(状态.命令数 > 0, "GPU 定时文字图层没有可见字幕命令。")
        断言(状态.可见像素数 > 100, $"GPU 定时文字图层只有 {状态.可见像素数} 个可见像素。")
        Console.WriteLine($"GPU 屏幕字幕：在 1:15.5 的暂停帧上持续显示 3 秒，" &
                          $"绘制 {状态.命令数} 条命令/{状态.可见像素数} 个可见像素。")
        会话.播放()
        等待状态(会话, 播放状态.正在播放, TimeSpan.FromSeconds(3))
    End Sub

    Private Sub 测试屏幕弹幕(会话 As 播放器会话, 画面控件 As 播放器画面控件,
                           呈现器 As 播放器定时文字图层呈现器, 位置 As TimeSpan)
        断言(呈现器 IsNot Nothing, "弹幕测试没有创建 GPU 定时文字呈现器。")
        会话.暂停()
        等待状态(会话, 播放状态.已暂停, TimeSpan.FromSeconds(3))
        Dim 快照 = 会话.当前快照
        呈现器.提交当前帧(画面控件.ClientSize, 快照.视频宽度, 快照.视频高度, 位置, Nothing,
                       画面控件.DeviceDpi)
        Dim 状态 As 定时文字状态 = Nothing
        Dim 收敛计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            状态 = 会话.当前弹幕状态
            If 状态.已绘制序号 > 0 AndAlso 状态.已绘制序号 = 状态.已提交序号 AndAlso
                状态.命令数 > 0 Then Exit Do
            If 收敛计时.Elapsed >= TimeSpan.FromSeconds(3) Then Exit Do
            Thread.Sleep(10)
        Loop
        断言(状态.已绘制序号 > 0 AndAlso 状态.已绘制序号 = 状态.已提交序号,
           "GPU 弹幕图层没有绘制最新命令。")
        断言(状态.命令数 > 0 AndAlso 状态.命令数 <= 100,
           $"GPU 弹幕命令数 {状态.命令数} 未落在 1 到 100 的范围内。")
        断言(状态.可见像素数 > 10, $"GPU 弹幕图层只有 {状态.可见像素数} 个可见像素。")
        Console.WriteLine($"GPU 弹幕：绘制 {状态.命令数} 条命令、{状态.可见像素数} 个可见像素。")
        会话.播放()
        等待状态(会话, 播放状态.正在播放, TimeSpan.FromSeconds(3))
        Dim 初始图层呈现数 = 会话.当前弹幕状态.图层呈现帧数
        Dim 帧率计时 = Stopwatch.StartNew()
        While 帧率计时.Elapsed < TimeSpan.FromSeconds(2)
            Application.DoEvents()
            Thread.Sleep(1)
        End While
        Dim 最终图层呈现数 = 会话.当前弹幕状态.图层呈现帧数
        Dim 图层呈现帧率 = (CULng(最终图层呈现数) - 初始图层呈现数) / 帧率计时.Elapsed.TotalSeconds
        断言(图层呈现帧率 >= 55.0 AndAlso 图层呈现帧率 <= 90.0,
           $"弹幕最终合成呈现为 {图层呈现帧率:F2} FPS，超出独立动态图层与视频更新的合理范围。")
        Console.WriteLine($"GPU 弹幕最终合成呈现：{图层呈现帧率:F2} FPS（视频源 {目标帧率:F2} FPS，动态图层目标 60 FPS）。")
    End Sub

    Private Sub 等待状态(会话 As 播放器会话, 目标 As 播放状态, 超时 As TimeSpan)
        Dim 计时 = Stopwatch.StartNew()
        Do
            Application.DoEvents()
            If 会话.当前快照.状态 = 目标 Then Return
            If 计时.Elapsed >= 超时 Then Throw New TimeoutException($"等待状态 {目标} 超时。")
            Thread.Sleep(5)
        Loop
    End Sub

    Private Function 采样播放(会话 As 播放器会话, 秒数 As Double,
                          画面控件 As 播放器画面控件,
                          Optional 图层泵 As Action = Nothing,
                          Optional 图层状态提供器 As Func(Of 定时文字状态) = Nothing) As 播放测量结果
        Dim 进程 = Process.GetCurrentProcess()
        Dim 首快照 = 会话.当前快照
        Dim 首位置 = 首快照.播放位置
        Dim 首呈现帧数 = 首快照.已呈现视频帧数
        Dim 首丢帧数 = 首快照.已丢弃视频帧数
        Dim 首解码视频帧数 = 首快照.已解码视频帧数
        Dim 首解码音频帧数 = 首快照.已解码音频帧数
        Dim 首音频欠载次数 = 首快照.音频欠载次数
        Dim 首时间戳抖动帧数 = 首快照.音频时间戳抖动帧数
        Dim 首音频不连续次数 = 首快照.音频不连续次数
        Dim 首音频插入静音帧数 = 首快照.音频插入静音帧数
        Dim 首音频丢弃重叠帧数 = 首快照.音频丢弃重叠帧数
        Dim 首音频拒绝帧数 = 首快照.音频拒绝帧数
        Dim 首图层状态 = If(画面控件 Is Nothing, Nothing,
                         If(图层状态提供器 Is Nothing, 会话.当前定时文字状态, 图层状态提供器()))
        Dim 首图层呈现数 = If(首图层状态 Is Nothing, 0UL, CULng(首图层状态.图层呈现帧数))
        Dim 首图层提交序号 = If(首图层状态 Is Nothing, 0UL, 首图层状态.已提交序号)
        Dim 首精灵缓存命中 = If(首图层状态 Is Nothing, 0UL, 首图层状态.精灵缓存命中次数)
        Dim 首精灵缓存未命中 = If(首图层状态 Is Nothing, 0UL, 首图层状态.精灵缓存未命中次数)
        ' 状态读取可能执行一次 GPU 诊断回读；测量墙钟必须在所有基线都取得后启动。
        Dim CPU开始 = 进程.TotalProcessorTime
        Dim 托管分配开始 = GC.GetTotalAllocatedBytes(False)
        Dim 第0代开始 = GC.CollectionCount(0)
        Dim 第1代开始 = GC.CollectionCount(1)
        Dim 第2代开始 = GC.CollectionCount(2)
        Dim 墙钟 = Stopwatch.StartNew()
        Dim 上次PTS = 首快照.原始帧PTS
        Dim 媒体视频差总毫秒 As Double
        Dim 媒体视频差样本数 As Integer
        Dim 最大媒体视频差毫秒 As Double
        Dim 音画差总毫秒 As Double
        Dim 音画差样本数 As Integer
        Dim 最大音画差毫秒 As Double
        Dim 视频队列总和 As Long
        Dim 视频队列样本数 As Integer
        Dim 最大视频队列 As Integer
        Dim 音频缓冲总毫秒 As Double
        Dim 最小音频缓冲毫秒 As Double = Double.PositiveInfinity
        Dim 最大音频缓冲毫秒 As Double
        Dim 音频缓冲样本数 As Integer
        Dim 下次图层秒 As Double

        While 墙钟.Elapsed.TotalSeconds < 秒数
            Application.DoEvents()
            If 图层泵 IsNot Nothing AndAlso 墙钟.Elapsed.TotalSeconds >= 下次图层秒 Then
                图层泵()
                下次图层秒 += 1.0R / 60.0R
            End If
            Dim 快照 = 会话.当前快照
            If 快照.状态 = 播放状态.失败 Then Throw New InvalidOperationException("播放器在测量阶段失败。")
            视频队列总和 += 快照.视频队列帧数
            视频队列样本数 += 1
            最大视频队列 = Math.Max(最大视频队列, 快照.视频队列帧数)
            Dim 音频缓冲毫秒 = 快照.音频缓冲时长.TotalMilliseconds
            音频缓冲总毫秒 += 音频缓冲毫秒
            最小音频缓冲毫秒 = Math.Min(最小音频缓冲毫秒, 音频缓冲毫秒)
            最大音频缓冲毫秒 = Math.Max(最大音频缓冲毫秒, 音频缓冲毫秒)
            音频缓冲样本数 += 1
            If 快照.原始帧PTS <> Long.MinValue AndAlso 快照.原始帧PTS <> 上次PTS Then
                上次PTS = 快照.原始帧PTS
            End If
            If 快照.帧时间基分母 > 0 AndAlso 快照.原始帧PTS <> Long.MinValue Then
                Dim 视频位置 = TimeSpan.FromSeconds(快照.原始帧PTS *
                    CDbl(快照.帧时间基分子) / 快照.帧时间基分母)
                Dim 差值 = Math.Abs((快照.播放位置 - 视频位置).TotalMilliseconds)
                媒体视频差总毫秒 += 差值
                媒体视频差样本数 += 1
                最大媒体视频差毫秒 = Math.Max(最大媒体视频差毫秒, 差值)
                If 快照.已解码音频帧数 > 0 Then
                    Dim 音画差 = Math.Abs((快照.音频位置 - 视频位置).TotalMilliseconds)
                    音画差总毫秒 += 音画差
                    音画差样本数 += 1
                    最大音画差毫秒 = Math.Max(最大音画差毫秒, 音画差)
                End If
            End If
            Thread.Sleep(If(图层泵 Is Nothing, 5, 1))
        End While

        墙钟.Stop()
        Dim 测量时长秒 = 墙钟.Elapsed.TotalSeconds
        Dim CPU结束 = 进程.TotalProcessorTime
        Dim 托管分配结束 = GC.GetTotalAllocatedBytes(False)
        Dim 第0代结束 = GC.CollectionCount(0)
        Dim 第1代结束 = GC.CollectionCount(1)
        Dim 第2代结束 = GC.CollectionCount(2)
        Dim 末快照 = 会话.当前快照
        Dim 末位置 = 末快照.播放位置
        Dim 呈现帧数 = 末快照.已呈现视频帧数 - 首呈现帧数
        Dim 丢帧数 = 末快照.已丢弃视频帧数 - 首丢帧数
        Dim 末图层状态 = If(画面控件 Is Nothing, Nothing,
                         If(图层状态提供器 Is Nothing, 会话.当前定时文字状态, 图层状态提供器()))
        Dim 图层呈现数 = If(末图层状态 Is Nothing, 0UL,
                          CULng(末图层状态.图层呈现帧数) - 首图层呈现数)
        进程.Refresh()
        Return New 播放测量结果 With {
            .实际呈现帧率 = CDbl(呈现帧数) / 测量时长秒,
            .丢帧数 = CLng(丢帧数),
            .播放速度 = (末位置 - 首位置).TotalSeconds / 测量时长秒,
            .平均媒体视频差毫秒 = If(媒体视频差样本数 = 0, Double.PositiveInfinity,
                              媒体视频差总毫秒 / 媒体视频差样本数),
            .最大媒体视频差毫秒 = 最大媒体视频差毫秒,
            .平均音画差毫秒 = If(音画差样本数 = 0, Double.PositiveInfinity,
                            音画差总毫秒 / 音画差样本数),
            .最大音画差毫秒 = 最大音画差毫秒,
            .进程CPU占用百分比 = (CPU结束 - CPU开始).TotalSeconds /
                              测量时长秒 / Environment.ProcessorCount * 100.0,
            .平均视频队列帧数 = If(视频队列样本数 = 0, 0.0, CDbl(视频队列总和) / 视频队列样本数),
            .最大视频队列帧数 = 最大视频队列,
            .平均音频缓冲毫秒 = If(音频缓冲样本数 = 0, 0.0, 音频缓冲总毫秒 / 音频缓冲样本数),
            .最小音频缓冲毫秒 = If(Double.IsPositiveInfinity(最小音频缓冲毫秒), 0.0, 最小音频缓冲毫秒),
            .最大音频缓冲毫秒 = 最大音频缓冲毫秒,
            .已解码视频帧数 = CLng(末快照.已解码视频帧数 - 首解码视频帧数),
            .已解码音频帧数 = CLng(末快照.已解码音频帧数 - 首解码音频帧数),
            .音频欠载次数 = CLng(末快照.音频欠载次数 - 首音频欠载次数),
            .音频时间戳抖动帧数 = CLng(末快照.音频时间戳抖动帧数 - 首时间戳抖动帧数),
            .音频不连续次数 = CLng(末快照.音频不连续次数 - 首音频不连续次数),
            .音频插入静音帧数 = CLng(末快照.音频插入静音帧数 - 首音频插入静音帧数),
            .音频丢弃重叠帧数 = CLng(末快照.音频丢弃重叠帧数 - 首音频丢弃重叠帧数),
            .音频拒绝帧数 = CLng(末快照.音频拒绝帧数 - 首音频拒绝帧数),
            .图层呈现帧率 = CDbl(图层呈现数) / 测量时长秒,
            .图层提交帧率 = If(末图层状态 Is Nothing, 0.0,
                           CDbl(末图层状态.已提交序号 - 首图层提交序号) / 测量时长秒),
            .精灵缓存命中次数 = If(末图层状态 Is Nothing, 0L,
                                CLng(末图层状态.精灵缓存命中次数 - 首精灵缓存命中)),
            .精灵缓存未命中次数 = If(末图层状态 Is Nothing, 0L,
                                  CLng(末图层状态.精灵缓存未命中次数 - 首精灵缓存未命中)),
            .托管分配字节每秒 = (托管分配结束 - 托管分配开始) / 测量时长秒,
            .第0代回收次数 = 第0代结束 - 第0代开始,
            .第1代回收次数 = 第1代结束 - 第1代开始,
            .第2代回收次数 = 第2代结束 - 第2代开始
        }
    End Function

    Private Sub 输出播放报告(模式 As 解码模式, 结果 As 播放测量结果)
        Console.WriteLine($"{模式}：{格式化播放报告(结果)}")
    End Sub

    Private Function 格式化播放报告(结果 As 播放测量结果) As String
        Return $"{结果.实际呈现帧率:F2} fps，丢帧 {结果.丢帧数}，时钟 {结果.播放速度:F4}x，" &
               $"媒体/视频 {结果.平均媒体视频差毫秒:F1}/{结果.最大媒体视频差毫秒:F1} ms，" &
               $"音频/视频 {结果.平均音画差毫秒:F1}/{结果.最大音画差毫秒:F1} ms，" &
               $"视频队列均值/峰值 {结果.平均视频队列帧数:F1}/{结果.最大视频队列帧数}，" &
               $"音频缓冲均值/最小/最大 {结果.平均音频缓冲毫秒:F1}/{结果.最小音频缓冲毫秒:F1}/{结果.最大音频缓冲毫秒:F1} ms，" &
               $"音频帧 {结果.已解码音频帧数}、欠载 {结果.音频欠载次数}，" &
               $"图层提交/呈现 {结果.图层提交帧率:F1}/{结果.图层呈现帧率:F1} fps，" &
               $"GC {结果.托管分配字节每秒 / 1024.0 / 1024.0:F2} MiB/s " &
               $"精灵缓存 {结果.精灵缓存命中次数}/{结果.精灵缓存未命中次数}，" &
               $"({结果.第0代回收次数}/{结果.第1代回收次数}/{结果.第2代回收次数})，" &
               $"进程 CPU {结果.进程CPU占用百分比:F1}%"
    End Function

    Private Sub 验证播放结果(结果 As 播放测量结果, 阶段 As String)
        断言(结果.实际呈现帧率 >= 目标帧率 * 0.94,
           $"{阶段}呈现吞吐不足：{结果.实际呈现帧率:F2} fps。")
        断言(结果.丢帧数 <= 2, $"{阶段}测量窗口内丢弃了 {结果.丢帧数} 帧。")
        断言(结果.播放速度 >= 0.97 AndAlso 结果.播放速度 <= 1.03,
           $"{阶段}媒体时钟速度异常：{结果.播放速度:F4}x。")
        断言(结果.平均媒体视频差毫秒 <= 80.0,
           $"{阶段}平均媒体/视频时钟差过大：{结果.平均媒体视频差毫秒:F1} ms。")
        断言(结果.最大媒体视频差毫秒 <= 180.0,
           $"{阶段}最大媒体/视频时钟差过大：{结果.最大媒体视频差毫秒:F1} ms。")
    End Sub

    Private Sub 验证性能结果(结果 As 播放测量结果, 源帧率 As Double, 阶段 As String,
                         Optional 验证图层 As Boolean = True)
        验证播放结果(结果, 阶段)
        断言(结果.实际呈现帧率 >= 源帧率 * 0.94,
           $"{阶段}没有跟上源帧率：{结果.实际呈现帧率:F2}/{源帧率:F2} fps。")
        断言(结果.最大视频队列帧数 <= 8,
           $"{阶段}视频队列超过 8 帧的有界合同：{结果.最大视频队列帧数}。")
        If 验证图层 Then
            ' 这是 100 条同时移动且带描边文字的上限压力，远高于产品默认 5 行；
            ' 动态图层目标 60 FPS，视频更新也可在独立 Present 中及时到达，因此
            ' 最终合成帧率允许落在 55–90 FPS，而不会把视频锁死在 60 Hz。
            断言(结果.图层呈现帧率 >= 55.0 AndAlso 结果.图层呈现帧率 <= 90.0,
               $"{阶段}字幕/弹幕最终合成呈现率异常：{结果.图层呈现帧率:F2} FPS。")
            断言(结果.精灵缓存命中次数 > 0 AndAlso 结果.精灵缓存未命中次数 <= 5,
               $"{阶段}滚动文字没有复用 GPU 精灵：命中/未命中 " &
               $"{结果.精灵缓存命中次数}/{结果.精灵缓存未命中次数}。")
        End If
    End Sub

    Private Sub 验证音频缓冲结果(结果 As 播放测量结果, 阶段 As String)
        断言(结果.已解码音频帧数 > 0, $"{阶段}没有解码任何音频帧。")
        断言(结果.平均音频缓冲毫秒 > 5.0,
           $"{阶段}音频缓冲没有建立：{结果.平均音频缓冲毫秒:F1} ms。")
        断言(结果.平均音频缓冲毫秒 <= 音频缓冲平均上限毫秒,
           $"{阶段}平均音频延迟过高：{结果.平均音频缓冲毫秒:F1} ms。")
        断言(结果.最大音频缓冲毫秒 <= 音频缓冲峰值上限毫秒,
           $"{阶段}峰值音频延迟过高：{结果.最大音频缓冲毫秒:F1} ms。")
        断言(结果.音频欠载次数 <= 1,
           $"{阶段}音频出现 {结果.音频欠载次数} 次欠载。")
        断言(结果.音频拒绝帧数 = 0,
           $"{阶段}因背压拒绝了 {结果.音频拒绝帧数} 个已解码音频帧。")
    End Sub

    Private Sub 验证音频结果(结果 As 播放测量结果, 阶段 As String)
        验证音频缓冲结果(结果, 阶段)
        断言(结果.平均音画差毫秒 <= 350.0 AndAlso 结果.最大音画差毫秒 <= 650.0,
           $"{阶段}音频/视频时钟差过大：{结果.平均音画差毫秒:F1}/{结果.最大音画差毫秒:F1} ms。")
    End Sub

    Private Sub 检查文件(路径 As String)
        If Not File.Exists(路径) Then Throw New FileNotFoundException("测试文件不存在。", 路径)
    End Sub

    Private Sub 断言(条件 As Boolean, 消息 As String)
        If Not 条件 Then Throw New InvalidOperationException(消息)
    End Sub

    Private NotInheritable Class 播放测量结果
        Public Property 实际呈现帧率 As Double
        Public Property 丢帧数 As Long
        Public Property 播放速度 As Double
        Public Property 平均媒体视频差毫秒 As Double
        Public Property 最大媒体视频差毫秒 As Double
        Public Property 平均音画差毫秒 As Double
        Public Property 最大音画差毫秒 As Double
        Public Property 进程CPU占用百分比 As Double
        Public Property 平均视频队列帧数 As Double
        Public Property 最大视频队列帧数 As Integer
        Public Property 平均音频缓冲毫秒 As Double
        Public Property 最小音频缓冲毫秒 As Double
        Public Property 最大音频缓冲毫秒 As Double
        Public Property 已解码视频帧数 As Long
        Public Property 已解码音频帧数 As Long
        Public Property 音频欠载次数 As Long
        Public Property 音频时间戳抖动帧数 As Long
        Public Property 音频不连续次数 As Long
        Public Property 音频插入静音帧数 As Long
        Public Property 音频丢弃重叠帧数 As Long
        Public Property 音频拒绝帧数 As Long
        Public Property 图层呈现帧率 As Double
        Public Property 图层提交帧率 As Double
        Public Property 精灵缓存命中次数 As Long
        Public Property 精灵缓存未命中次数 As Long
        Public Property 托管分配字节每秒 As Double
        Public Property 第0代回收次数 As Integer
        Public Property 第1代回收次数 As Integer
        Public Property 第2代回收次数 As Integer
    End Class
End Module
