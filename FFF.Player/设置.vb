Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Text.Json

Public Class 设置
    Public Shared Property 实例对象 As New 设置()

    Public Property 字体 As String = SystemFonts.DefaultFont.FontFamily.Name
    Public Property 初始画面尺寸选项 As Integer = 2
    Public Property 自定义初始画面宽度 As Integer = 1024
    Public Property 自定义初始画面高度 As Integer = 576
    ' 解码方式默认 GPU（2026-10-21 起，此前为 CPU）。理由：硬解不通时内核自己会回退 CPU
    ' （打开时试解首包 + 播放中失败即切换，均已实机验证），用户侧没有"选错就放不出"的风险，
    ' 而默认 CPU 等于让大多数机器白白放弃硬解。主界面「解码」按钮仍可随时翻转，并写回本项。
    ' 注意：本默认值只在设置文件不存在/解析失败时生效；老设置文件里已持久化的值原样尊重
    ' （文件只存 1/2，分不出"从没设置过"与"显式选过 CPU"，故不做迁移）。
    Public Property 解码方式 As 解码模式 = 解码模式.GPU
    Public Property 视频缩放质量选项 As Integer = CInt(视频缩放质量.高画质)
    Public Property GPU解码设备标识 As String = String.Empty
    ' 0 均衡自适应，1 更多缓冲，2 小缓冲压力测试。旧配置缺项时保持原有策略。
    Public Property 视频缓冲区策略 As Integer = 0
    Public Property 音频缓冲区策略 As Integer = 0
    Public Property 启用空间音频 As Boolean = True
    Public Property 自动切换到真实HDR高亮 As Boolean = True
    Public Property HDR峰值亮度选项 As Integer = 0
    Public Property HDR峰值亮度 As Integer = 0
    Public Property HDR映射SDR参考亮度 As Integer = 250
    Public Property 音量百分比 As Integer = 100

    ' 360°视场角由快捷键调整；该字段没有对应的设置控件，但仍随设置文件持久化。
    Public Property 视角360视场角 As Single = 90.0F

    Public Property 字幕第一行字体 As String = "Microsoft YaHei UI"
    Public Property 字幕第一行字号 As Single = 48.0F
    Public Property 字幕第一行样式 As Integer = CInt(FontStyle.Regular)
    Public Property 字幕第一行颜色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 字幕第二行字体 As String = "Microsoft YaHei UI"
    Public Property 字幕第二行字号 As Single = 48.0F
    Public Property 字幕第二行样式 As Integer = CInt(FontStyle.Regular)
    Public Property 字幕第二行颜色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 字幕其他行字体 As String = "Microsoft YaHei UI"
    Public Property 字幕其他行字号 As Single = 48.0F
    Public Property 字幕其他行样式 As Integer = CInt(FontStyle.Regular)
    Public Property 字幕不透明度 As Integer = 255
    Public Property 字幕描边样式 As Integer = 1
    Public Property 字幕阴影样式 As Integer = 1
    Public Property 字幕行间距 As Integer = 10
    Public Property 字幕底部间距 As Integer = 10
    Public Property 字幕底部对齐方式 As Integer = 0
    Public Property 字幕尺寸缩放方式 As Integer = 0

    Public Property 弹幕已启用 As Boolean = True
    Public Property 弹幕字体 As String = "Microsoft YaHei UI"
    Public Property 弹幕字号 As Single = 36.0F
    Public Property 弹幕字体样式 As Integer = CInt(FontStyle.Regular)
    Public Property 弹幕不透明度 As Integer = 255
    Public Property 弹幕描边样式 As Integer = 1
    Public Property 弹幕阴影样式 As Integer = 1
    Public Property 渲染常规滚动弹幕 As Boolean = True
    Public Property 渲染顶部弹幕 As Boolean = True
    Public Property 渲染底部弹幕 As Boolean = True
    Public Property 弹幕最大行数 As Integer = 5
    Public Property 弹幕最大渲染数量 As Integer = 100
    Public Property 弹幕行内前后间距 As Integer = 30
    Public Property 弹幕滚动速度 As Integer = 30
    Public Property 弹幕尺寸缩放方式 As Integer = 0

    Public Property 启用歌词支持 As Boolean = True
    Public Property 歌词字体 As String = "Microsoft YaHei UI"
    Public Property 歌词字号 As Single = 16.5F
    Public Property 歌词字体样式 As Integer = CInt(FontStyle.Regular)
    Public Property 渲染封面图毛玻璃背景 As Boolean = True
    Public Property 渲染封面图 As Boolean = True

    ' 按源的旋转信息自动转正（EXIF / 容器 rotate / display matrix），图片与视频通用。
    ' 默认 True：手机竖拍照片与旋转过的视频本该以正确方向呈现，这属于"本该如此"，
    ' 而不是一个需要用户先发现的选项。关掉它纯粹是给"我要看原始像素"的场景留口子。
    ' 注意：内核在打开时就会套用源旋转；本开关为 False 时由宿主显式归零（见 应用自动转正设置）。
    Public Property 自动转正 As Boolean = True

    ' RTX 视频超分辨率（VSR）：默认关闭。
    ' 三个理由写在这儿，免得以后有人"顺手改成 True"：
    '   ① 内核侧默认也是 Off，宿主替用户打开等于改变既有播放行为；
    '   ② 它只在"真放大 + 源高度 360..1440 + NVIDIA 适配器直驱窗口"时才可能生效，
    '      不满足时是静默不生效（不是错误），默认开只会让人以为坏了；
    '   ③ 画质档位与总开关归 NVIDIA App / 控制面板，本程序只能请求"允许"，
    '      不能保证也不能验证画面确实被增强。
    Public Property RTX视频超分辨率 As Boolean = False

    ' NVIDIA 视频处理器私有流扩展自动 HDR（RTX TrueHDR）：默认关闭。
    ' 与 VSR 同样的三条理由，外加两条自己的：
    '   ④ 内核默认也是 Off——host 侧默认值必须与内核一致，否则"偏好"与"实际"从第一次
    '      打开起就分叉，UI 回显会骗人；
    '   ⑤ 它把 SDR 帧整体改写成 HDR10（BT.2020 + PQ，1000 nit），是**画面色调映射**行为，
    '      不是中性的能力开关。对比/审片类用途（compare 模式）下用户要的就是原样像素，
    '      内核已强制互斥，宿主这边同样不许默认打开。
    ' 生效条件（内核逐帧判定）：NVIDIA 适配器 + 驱动探测通过 + SDR 源 + 已是 10 位 HDR
    ' 输出契约。任一条不满足是静默不生效（不是错误），默认开只会让人以为坏了。
    Public Property NVIDIA自动HDR As Boolean = False
    ' AI 实时字幕（docs/ai-subtitles/，实验分支功能）：默认全关。
    ' 三条理由写在这儿，免得以后有人"顺手改成 True"：
    '   ① 它要拉一个独立 Python 进程 + llama-server 子进程，还要一块显卡显存；
    '      默认开等于改变既有播放行为，而 ASR 是"锦上添花"不是"必需"；
    '   ② 模型文件 1GB 量级不随播放器分发，用户没配好目录时功能静默不可用；
    '   ③ 识别滞后于画面是物理下限（5s 窗 + ~0.2s 推理），默认开会让
    '      第一次用的人以为"字幕坏了"。
    ' 老设置文件没有这些键 → 反序列化留默认 → 同样关闭，无需迁移。
    ' AI 实时字幕（docs/ai-subtitles/，实验分支功能）：本体不内置任何 ASR 能力，
    ' 只认"扩展包"这一个契约 —— 一个含 ai-subtitles.json 的目录，worker / python /
    ' 模型 / llama-server 在哪一律由包自己说清。于是这里只剩一个路径。
    ' 未选包（默认空）就完全不启用：不拉进程、不占显存、不碰播放路径。
    Public Property Ai字幕包目录 As String = ""
    ' 选到有效包之后，用户仍可在菜单里临时关掉。它只决定"这次要不要起 worker"，
    ' 与"装了哪个包"是两件事，所以分开存。
    Public Property Ai字幕已停用 As Boolean = False
    ' 高级项：覆盖清单里的窗口/步长（秒）。0 = 跟随清单；包自身已做钳制，这里只挡脏数据。
    Public Property Ai字幕窗口秒覆盖 As Double = 0
    Public Property Ai字幕步长秒覆盖 As Double = 0
    ' 推理线程数：0 = 自动（worker 取 min(6, 核数)）。启动期参数，改了要重启 worker。
    ' 上限 6 有实测依据 —— 超过 6 时 P+E 混合架构上前台线程会落到 E-core，同步点被
    ' 拖住，反而更慢；全大核机器可以往上调，所以这里不硬钳，只给界面几个预设。
    ' 非法值（负数）在管道里归 0，不让它传到 worker 变成 num_threads=-1。
    Public Property Ai字幕线程数 As Integer = 0
    ' 推理设备：这台机器拿哪块硬件算，取值见 Ai字幕推理档.设备全部
    ' （auto / cpu / nvidia / amd / intel / npu）。
    ' 它**不参与传参**：只决定设置页「后端」下拉列出哪几项，真正发给 worker 的是下面
    ' 那个 后端 字段。分成两个字段是有意的 —— 合成成一个串就要解析，脏值就会解析成
    ' 别的档（v1 那套「设备类别 + 厂商后端 + 二进制扫描」正是这么长出来的）。
    ' 空串与 auto 同义；老设置里 cuda（llama.cpp 时代它同时表示 NVIDIA 卡）由
    ' Ai字幕推理档.规整设备 归到 nvidia，其余旧标识归 auto，不需要迁移代码。
    Public Property Ai字幕运行设备 As String = ""

    ' 推理后端：用哪个运行时，取值见 Ai字幕推理档.后端全部
    ' （auto / cpu / cuda / dml / ov-cpu / ov-gpu / ov-npu）。
    ' 启动期参数 —— 改了要重启 worker 才生效（控制器按这个值判断要不要重启）。
    ' 与设备是一对：换设备后旧后端可能不在新设备的候选表里，读取时由
    ' Ai字幕推理档.规整后端 一次性归位，不落盘（用户一动下拉就写回正确值）。
    Public Property Ai字幕推理后端 As String = ""
    ' 字幕语言："" / "auto" = 不筛（默认，模型按窗自判）；其余取 Ai字幕语言 的
    ' 标识（zh/en/zh-en），配合 仅所选语言 形成过滤器，见 Ai字幕语言 类文档。
    ' ⚠ 换到 sherpa-onnx 之后这条降级为**无操作**：该运行时只返回文本、不返回语言
    ' 标签，所以 worker 报上来的 language 恒为空，下面那个闸门也就压不住任何东西。
    Public Property Ai字幕语言 As String = ""
    ' 只显示所选语言：模型自报语言与所选不符时该窗不上屏。现在恒不触发（见上），
    ' 保留字段是为了不丢老设置，界面按"暂不可用"置灰显示。
    Public Property Ai字幕仅所选语言 As Boolean = False
    ' 双语对照："" = 关闭；"zh-en" = 中文在上、English 在下；"en-zh" 反之。
    ' 它不改变识别本身，只让 worker 在同一窗里多译一行 —— 用的是同一个 llama-server
    ' 的原始 /completion 续写，不额外加载模型、不改任何权重。
    ' ONNX（NPU / Intel）那条腿是纯 ASR，没有文本生成能力，worker 会降级成单语并报
    ' 一条 warning，不静默。
    Public Property Ai字幕双语 As String = ""
    ' 模型档位："" = 跟包清单的默认档（清单 model 字段指向的目录）；其余取清单 models 里的键。
    ' 它属于启动期参数 —— 换档就是换一套 ONNX 权重，必须重启 worker 才生效。
    Public Property Ai字幕模型档 As String = ""
    ' 字幕驻留偏移（秒）：[-3, +3]，0 = 不偏移。作用在状态机守卫判定的播放位置上
    '（播放位置 − 偏移）：正值 = 每行多驻留一会儿（慢读者、或下一句来得慢时不等空窗），
    ' 负值 = 提前收起（嫌字幕拖音）。**它不能让一行字幕更早出现** —— 出现时机由推理
    ' 窗口与推理耗时决定（批式 ASR 的物理下限），要更贴脸请调小窗口/步长（设置页
    ' 「节奏」预置）。纯显示侧：改它不重启 worker。
    Public Property Ai字幕字幕偏移秒 As Double = 0


    ' AI 实时字幕（docs/ai-subtitles/，实验分支功能）：默认全关。
    ' 三条理由写在这儿，免得以后有人"顺手改成 True"：
    '   ① 它要拉一个独立 Python 进程 + llama-server 子进程，还要一块显卡显存；
    
    Public Property 关联常见视频 As Boolean = False
    Public Property 关联不常见视频 As Boolean = False
    Public Property 关联老旧视频 As Boolean = False
    Public Property 关联常见音频 As Boolean = False
    Public Property 关联不常见音频 As Boolean = False
    Public Property 关联老旧音频 As Boolean = False
    Public Property 关联常见图片 As Boolean = False
    Public Property 关联不常见图片 As Boolean = False
    Public Property 关联老旧图片 As Boolean = False

    Public Property SP_窗口边框颜色_A As Integer = 255
    Public Property SP_窗口边框颜色_R As Integer = Color.Gray.R
    Public Property SP_窗口边框颜色_G As Integer = Color.Gray.G
    Public Property SP_窗口边框颜色_B As Integer = Color.Gray.B
    Public Property SP_分层阴影颜色_A As Integer = 255
    Public Property SP_分层阴影颜色_R As Integer = Color.Black.R
    Public Property SP_分层阴影颜色_G As Integer = Color.Black.G
    Public Property SP_分层阴影颜色_B As Integer = Color.Black.B
    Public Property SP_边框宽度 As Integer = 1
    Public Property SP_分割线颜色ARGB As Integer = Color.FromArgb(40, 220, 220, 220).ToArgb()
    Public Property SP_分割线宽度 As Integer = 1
    Public Property SP_毛玻璃模式 As Integer = 0
    Public Property SP_毛玻璃背景来源 As Integer = -1
    Public Property SP_毛玻璃噪点颗粒 As Integer = -1

    Public Shared ReadOnly 设置文件路径 As String = Path.Combine(程序目录, "FFF.Player.Settings.json")

    Public Shared ReadOnly 自定义图标路径 As String = Path.Combine(程序目录, "3FP_Icon")
    Public Shared ReadOnly 自定义背景图路径 As String = Path.Combine(程序目录, "3FP_BackImage")
    Private Shared 当前自有背景图 As Image
    Private Shared 当前自有图标 As Icon

    Public Shared Sub 启动时加载设置()
        Try
            If Not File.Exists(设置文件路径) Then
                If FontFamily.Families.Any(Function(f) f.Name = "微软雅黑") Then 实例对象.字体 = "微软雅黑"
                退出时保存设置()
                Return
            End If
            Dim 读取 = JsonSerializer.Deserialize(Of 设置)(File.ReadAllText(设置文件路径, Text.Encoding.UTF8))
            If 读取 Is Nothing Then Throw New JsonException("设置对象为空。")
            实例对象 = 读取
            实例对象.规范化()
        Catch
            Try
                If File.Exists(设置文件路径) Then
                    File.Copy(设置文件路径, 设置文件路径 & ".broken-" & DateTime.Now.ToString("yyyyMMddHHmmss"), True)
                End If
            Catch
            End Try
            实例对象 = New 设置()
            退出时保存设置()
        End Try
    End Sub

    Public Shared Sub 退出时保存设置()
        Dim 临时路径 As String = Nothing
        Try
            实例对象.规范化()
            临时路径 = 设置文件路径 & ".tmp-" & Guid.NewGuid().ToString("N")
            File.WriteAllText(临时路径, JsonSerializer.Serialize(实例对象, JsonSO), Text.Encoding.UTF8)
            File.Move(临时路径, 设置文件路径, True)
        Catch ex As Exception
            If Not String.IsNullOrWhiteSpace(临时路径) Then
                Try
                    If File.Exists(临时路径) Then File.Delete(临时路径)
                Catch
                End Try
            End If
            MessageBox.Show($"保存设置失败：{ex.Message}", "FFF.Player", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    Public Sub 规范化()
        字体 = 规范字体(字体, SystemFonts.DefaultFont.FontFamily.Name)
        初始画面尺寸选项 = Math.Clamp(初始画面尺寸选项, 0, 9)
        自定义初始画面宽度 = Math.Clamp(自定义初始画面宽度, 320, 7680)
        自定义初始画面高度 = Math.Clamp(自定义初始画面高度, 180, 4320)
        ' 非法值归一到默认档（GPU）：与字段默认值保持一致，破损/越界的配置不会把用户按在 CPU 上。
        If 解码方式 <> CInt(解码模式.CPU) AndAlso 解码方式 <> CInt(解码模式.GPU) Then
            解码方式 = CInt(解码模式.GPU)
        End If
        视频缩放质量选项 = Math.Clamp(视频缩放质量选项, 0, 1)
        GPU解码设备标识 = If(GPU解码设备标识, String.Empty).Trim()
        If 视频缓冲区策略 < 0 OrElse 视频缓冲区策略 > 2 Then 视频缓冲区策略 = 0
        If 音频缓冲区策略 < 0 OrElse 音频缓冲区策略 > 2 Then 音频缓冲区策略 = 0
        HDR峰值亮度选项 = Math.Clamp(HDR峰值亮度选项, 0, 9)
        If HDR峰值亮度 < 0 OrElse HDR峰值亮度 > 10000 Then HDR峰值亮度 = 0
        HDR映射SDR参考亮度 = Math.Clamp(HDR映射SDR参考亮度, 1, 500)
        音量百分比 = Math.Clamp(音量百分比, 0, 100)
        If Not Single.IsFinite(视角360视场角) Then 视角360视场角 = 90.0F
        视角360视场角 = Math.Clamp(视角360视场角, 30.0F, 90.0F)
        规范化字幕字体(字幕第一行字体, 字幕第一行字号, 字幕第一行样式)
        规范化字幕字体(字幕第二行字体, 字幕第二行字号, 字幕第二行样式)
        规范化字幕字体(字幕其他行字体, 字幕其他行字号, 字幕其他行样式)
        字幕不透明度 = Math.Clamp(字幕不透明度, 0, 255)
        字幕描边样式 = Math.Clamp(字幕描边样式, 0, 2)
        字幕阴影样式 = Math.Clamp(字幕阴影样式, 0, 2)
        字幕行间距 = Math.Clamp(字幕行间距, 0, 30)
        字幕底部间距 = Math.Clamp(字幕底部间距, 0, 50)
        字幕底部对齐方式 = Math.Clamp(字幕底部对齐方式, 0, 1)
        字幕尺寸缩放方式 = Math.Clamp(字幕尺寸缩放方式, 0, 3)
        弹幕字体 = 规范字体(弹幕字体, "Microsoft YaHei UI")
        If Not Single.IsFinite(弹幕字号) OrElse 弹幕字号 < 8 OrElse 弹幕字号 > 200 Then 弹幕字号 = 36
        弹幕字体样式 = 规范字体样式(弹幕字体样式)
        弹幕不透明度 = Math.Clamp(弹幕不透明度, 0, 255)
        弹幕描边样式 = Math.Clamp(弹幕描边样式, 0, 2)
        弹幕阴影样式 = Math.Clamp(弹幕阴影样式, 0, 2)
        弹幕最大行数 = Math.Clamp(弹幕最大行数, 1, 20)
        弹幕最大渲染数量 = Math.Clamp(弹幕最大渲染数量, 1, 200)
        弹幕行内前后间距 = Math.Clamp(弹幕行内前后间距, 0, 100)
        弹幕滚动速度 = Math.Clamp(弹幕滚动速度, 1, 100)
        弹幕尺寸缩放方式 = Math.Clamp(弹幕尺寸缩放方式, 0, 3)
        歌词字体 = 规范字体(歌词字体, "Microsoft YaHei UI")
        If Not Single.IsFinite(歌词字号) OrElse 歌词字号 < 8 OrElse 歌词字号 > 200 Then 歌词字号 = 16.5F
        歌词字体样式 = 规范字体样式(歌词字体样式)
        SP_分割线宽度 = Math.Clamp(SP_分割线宽度, 0, 1)
        SP_窗口边框颜色_A = Math.Clamp(SP_窗口边框颜色_A, 0, 255)
        SP_窗口边框颜色_R = Math.Clamp(SP_窗口边框颜色_R, 0, 255)
        SP_窗口边框颜色_G = Math.Clamp(SP_窗口边框颜色_G, 0, 255)
        SP_窗口边框颜色_B = Math.Clamp(SP_窗口边框颜色_B, 0, 255)
        SP_分层阴影颜色_A = Math.Clamp(SP_分层阴影颜色_A, 0, 255)
        SP_分层阴影颜色_R = Math.Clamp(SP_分层阴影颜色_R, 0, 255)
        SP_分层阴影颜色_G = Math.Clamp(SP_分层阴影颜色_G, 0, 255)
        SP_分层阴影颜色_B = Math.Clamp(SP_分层阴影颜色_B, 0, 255)
        SP_边框宽度 = Math.Clamp(SP_边框宽度, 0, 2)
        SP_毛玻璃模式 = Math.Clamp(SP_毛玻璃模式, 0, 3)
        SP_毛玻璃背景来源 = Math.Clamp(SP_毛玻璃背景来源, -1, 1)
        SP_毛玻璃噪点颗粒 = Math.Clamp(SP_毛玻璃噪点颗粒, -1, 2)

        ' AI 字幕：高级覆盖项钳在 [0.5, 30] 秒；0 或脏数据 = 跟随包清单（包自己已钳过一遍）。
        If Not Double.IsFinite(Ai字幕窗口秒覆盖) OrElse Ai字幕窗口秒覆盖 < 0.5 OrElse Ai字幕窗口秒覆盖 > 30 Then
            Ai字幕窗口秒覆盖 = 0
        End If
        If Not Double.IsFinite(Ai字幕步长秒覆盖) OrElse Ai字幕步长秒覆盖 < 0.5 OrElse Ai字幕步长秒覆盖 > 30 Then
            Ai字幕步长秒覆盖 = 0
        End If
        ' AI 字幕：字幕时间偏移钳在 [-3, +3] 秒；NaN / 越界 / 老设置里的脏值归 0（不偏移）。
        If Not Double.IsFinite(Ai字幕字幕偏移秒) OrElse Ai字幕字幕偏移秒 < -3.0 OrElse Ai字幕字幕偏移秒 > 3.0 Then
            Ai字幕字幕偏移秒 = 0
        End If
    End Sub

    Private Shared Sub 规范化字幕字体(ByRef 字体名称 As String, ByRef 字号 As Single,
                                ByRef 样式 As Integer)
        字体名称 = 规范字体(字体名称, "Microsoft YaHei UI")
        If Not Single.IsFinite(字号) OrElse 字号 < 8 OrElse 字号 > 200 Then 字号 = 48
        样式 = 规范字体样式(样式)
    End Sub

    Private Shared Function 规范字体(value As String, fallback As String) As String
        Return If(String.IsNullOrWhiteSpace(value), fallback, value.Trim())
    End Function

    Private Shared Function 规范字体样式(value As Integer) As Integer
        Return value And CInt(FontStyle.Bold Or FontStyle.Italic Or FontStyle.Underline Or FontStyle.Strikeout)
    End Function

    Public Function 取得初始画面尺寸() As Size
        Dim 预设 = {Size.Empty, New Size(640, 360), New Size(854, 480), New Size(960, 540),
                    New Size(1024, 576), New Size(1366, 768), New Size(1600, 900),
                    New Size(1920, 1080), New Size(2560, 1440), New Size(3840, 2160)}
        If 初始画面尺寸选项 = 0 Then Return New Size(自定义初始画面宽度, 自定义初始画面高度)
        Return 预设(初始画面尺寸选项)
    End Function

    Public Function 创建SRT字幕样式() As SRT字幕样式
        Dim 描边宽度 = {0.0F, 2.0F, 4.0F}(字幕描边样式)
        Dim 阴影偏移 = {0.0F, 2.0F, 4.0F}(字幕阴影样式)
        Return New SRT字幕样式 With {
            .第一行字体 = 字幕第一行字体, .第一行字号 = 字幕第一行字号,
            .第一行字体样式 = CType(字幕第一行样式, FontStyle),
            .第一行颜色ARGB = 应用不透明度(字幕第一行颜色ARGB, 字幕不透明度),
            .第二行字体 = 字幕第二行字体, .第二行字号 = 字幕第二行字号,
            .第二行字体样式 = CType(字幕第二行样式, FontStyle),
            .第二行颜色ARGB = 应用不透明度(字幕第二行颜色ARGB, 字幕不透明度),
            .其他行字体 = 字幕其他行字体, .其他行字号 = 字幕其他行字号,
            .其他行字体样式 = CType(字幕其他行样式, FontStyle),
            .颜色ARGB = 应用不透明度(&HFFFFFFFFUI, 字幕不透明度),
            .描边宽度 = 描边宽度,
            .描边颜色ARGB = If(描边宽度 > 0, 应用不透明度(&HC0000000UI, 字幕不透明度), 0UI),
            .阴影偏移 = 阴影偏移,
            .阴影颜色ARGB = If(阴影偏移 > 0, 应用不透明度(&H70000000UI, 字幕不透明度), 0UI),
            .行间距 = 字幕行间距, .底部边距 = 字幕底部间距,
            .底部对齐方式 = 字幕底部对齐方式, .尺寸缩放方式 = 字幕尺寸缩放方式
        }
    End Function

    Public Function 创建弹幕显示配置() As 弹幕显示配置
        Dim 类型 = 弹幕类型.无
        If 渲染常规滚动弹幕 Then 类型 = 类型 Or 弹幕类型.常规滚动
        If 渲染顶部弹幕 Then 类型 = 类型 Or 弹幕类型.顶部
        If 渲染底部弹幕 Then 类型 = 类型 Or 弹幕类型.底部
        Dim 描边宽度 = {0.0F, 1.25F, 2.0F}(弹幕描边样式)
        Dim 阴影深度 = {0.0F, 3.0F, 4.5F}(弹幕阴影样式)
        Return New 弹幕显示配置 With {
            .字体 = 弹幕字体, .字号 = 弹幕字号, .字体样式 = CType(弹幕字体样式, FontStyle),
            .滚动速度 = 弹幕滚动速度 * 6.0F, .同屏最大数量 = 弹幕最大渲染数量,
            .常规滚动最大行数 = 弹幕最大行数, .顶部最大行数 = 弹幕最大行数,
            .行内前后间距 = 弹幕行内前后间距, .尺寸缩放方式 = 弹幕尺寸缩放方式,
            .启用类型 = 类型, .不透明度 = 弹幕不透明度, .描边宽度 = 描边宽度,
            .描边颜色ARGB = If(描边宽度 > 0, 应用不透明度(&HE0000000UI, 弹幕不透明度), 0UI),
            .阴影深度 = 阴影深度,
            .阴影颜色ARGB = If(阴影深度 > 0, 应用不透明度(If(弹幕阴影样式 = 2, &HFF000000UI, &HC0000000UI), 弹幕不透明度), 0UI)
        }
    End Function

    Friend Shared Function 应用不透明度(value As UInteger, opacity As Integer) As UInteger
        Dim 限定不透明度 = Math.Clamp(opacity, 0, 255)
        Dim 原始Alpha = CULng((value >> 24) And &HFFUI)
        Dim 合成Alpha = CUInt((原始Alpha * CULng(限定不透明度) + 127UL) \ 255UL)
        Return (value And &HFFFFFFUI) Or (合成Alpha << 24)
    End Function

    Public Shared Sub 应用SP个性化设置()
        If Not SP_UnLock OrElse Form1.当前主窗体 Is Nothing Then Return
        Dim 窗口 = Form1.当前主窗体.ThisIsYourWindow1
        Dim 边框颜色 = Color.FromArgb(实例对象.SP_窗口边框颜色_A, 实例对象.SP_窗口边框颜色_R,
                                  实例对象.SP_窗口边框颜色_G, 实例对象.SP_窗口边框颜色_B)
        窗口.BorderColor = 边框颜色
        窗口.BorderInactiveColor = 边框颜色
        窗口.LayerShadowColor = Color.FromArgb(实例对象.SP_分层阴影颜色_A, 实例对象.SP_分层阴影颜色_R,
                                           实例对象.SP_分层阴影颜色_G, 实例对象.SP_分层阴影颜色_B)
        窗口.BorderSize = 实例对象.SP_边框宽度
        窗口.CaptionBottomLineColor = Color.FromArgb(实例对象.SP_分割线颜色ARGB)
        窗口.CaptionBottomLineHeight = 实例对象.SP_分割线宽度
        Form1.当前主窗体.JustEmptyControl1.BackColor = 窗口.CaptionBottomLineColor
        Form1.当前主窗体.JustEmptyControl1.Height = 实例对象.SP_分割线宽度
        Form1.当前主窗体.JustEmptyControl1.Visible = 实例对象.SP_分割线宽度 > 0
        Select Case 实例对象.SP_毛玻璃模式
            Case 0
                窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.None
                窗口.BackdropBlurPasses = 0
                窗口.BackdropNoiseOpacity = 0
                清除SP自有背景图()
            Case Else
                窗口.BackdropBlurPasses = If(实例对象.SP_毛玻璃模式 = 1, 0, If(实例对象.SP_毛玻璃模式 = 2, 1, 3))
                窗口.BackdropBlurRadius = If(实例对象.SP_毛玻璃模式 = 2, 10, 24)
                If 实例对象.SP_毛玻璃背景来源 = 0 Then
                    窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.Image
                    加载SP自定义背景图()
                ElseIf 实例对象.SP_毛玻璃背景来源 = 1 Then
                    窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.Auto
                    清除SP自有背景图()
                Else
                    窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.None
                    清除SP自有背景图()
                End If
                窗口.BackdropNoiseOpacity = If(实例对象.SP_毛玻璃噪点颗粒 = 1, 18,
                                                If(实例对象.SP_毛玻璃噪点颗粒 = 2, 36, 0))
        End Select
        Form1.当前主窗体.应用设置窗口玻璃背景(实例对象.SP_毛玻璃模式 > 0)
    End Sub

    Public Shared Sub 加载SP自定义图标()
        If Not SP_UnLock OrElse Form1.当前主窗体 Is Nothing OrElse Not File.Exists(自定义图标路径) Then Return
        Dim newIcon As Icon
        Try
            Using image = 加载图片副本(自定义图标路径)
                newIcon = 从图片创建图标(image)
            End Using
        Catch ex As Exception When TypeOf ex Is ArgumentException OrElse
                                   TypeOf ex Is IOException OrElse
                                   TypeOf ex Is UnauthorizedAccessException OrElse
                                   TypeOf ex Is OutOfMemoryException OrElse
                                   TypeOf ex Is Runtime.InteropServices.ExternalException
            Return
        End Try
        Dim oldIcon = 当前自有图标
        当前自有图标 = newIcon
        Form1.当前主窗体.Icon = newIcon
        oldIcon?.Dispose()
    End Sub

    Public Shared Sub 加载SP自定义背景图()
        If Not SP_UnLock OrElse Form1.当前主窗体 Is Nothing OrElse Not File.Exists(自定义背景图路径) Then Return
        Dim newImage As Image
        Try
            newImage = 加载图片副本(自定义背景图路径)
        Catch ex As Exception When TypeOf ex Is ArgumentException OrElse
                                   TypeOf ex Is IOException OrElse
                                   TypeOf ex Is UnauthorizedAccessException OrElse
                                   TypeOf ex Is OutOfMemoryException OrElse
                                   TypeOf ex Is Runtime.InteropServices.ExternalException
            Return
        End Try
        Dim oldImage = 当前自有背景图
        当前自有背景图 = newImage
        Form1.当前主窗体.ThisIsYourWindow1.BackdropImage = newImage
        oldImage?.Dispose()
    End Sub

    Public Shared Sub 清除SP自有背景图()
        If Form1.当前主窗体 IsNot Nothing Then Form1.当前主窗体.ThisIsYourWindow1.BackdropImage = Nothing
        当前自有背景图?.Dispose()
        当前自有背景图 = Nothing
    End Sub

    Public Shared Sub 释放SP资源()
        清除SP自有背景图()
        当前自有图标?.Dispose()
        当前自有图标 = Nothing
    End Sub

    Private Shared Function 加载图片副本(filePath As String) As Image
        Using stream As New FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)
            Using source = Image.FromStream(stream, False, False)
                Return New Bitmap(source)
            End Using
        End Using
    End Function

    Private Shared Function 从图片创建图标(image As Image) As Icon
        Using bitmap As New Bitmap(image)
            Dim handle = bitmap.GetHicon()
            Try
                Using tempIcon = Icon.FromHandle(handle)
                    Return DirectCast(tempIcon.Clone(), Icon)
                End Using
            Finally
                If handle <> IntPtr.Zero Then DestroyIcon(handle)
            End Try
        End Using
    End Function

    <DllImport("user32.dll")>
    Private Shared Function DestroyIcon(handle As IntPtr) As Boolean
    End Function
End Class
