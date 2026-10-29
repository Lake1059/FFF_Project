Option Strict On
Option Explicit On

Imports System.Collections.Generic
Imports System.IO
Imports System.Text.Json

''' <summary>
''' AI 字幕扩展包：一个目录 + 一份自描述清单（ai-subtitles.json）。
'''
''' 3FP 本体只认这一个契约，其它一概不管 —— 用哪个 python、模型放包内还是指向
''' 机器上的别处，全由清单自己说。于是设置界面只剩"选到哪个目录"这一行。
'''
''' 清单字段（version 3，2026-10-29 起）：
'''   version      整数，清单版本（当前 3；2 = 只有 ONNX 那套；1 = llama.cpp 时代，不支持）
'''   name         包显示名，状态行用；缺省取目录名
'''   engine       备注字段：llama / onnx。**不参与判定** —— 真正的判据是模型目录
'''                里放的是 *.gguf 还是三个 .onnx，写错也不会把包判死。
'''   worker       worker 脚本（相对包根，或绝对路径）【必填】
'''   python       python 可执行；留空 = PATH 上的 python。
'''                onnx 包：**必须已装好 sherpa-onnx**（原生库与 ONNX Runtime 随那个包走）。
'''                llama 包：只要 numpy，推理在 runtime/ 下的原生进程里跑。
'''   model        模型目录；留空 = 包目录下的 model/。目录内两种形态之一：
'''                onnx  ：conv_frontend / encoder / decoder 三个 .onnx + tokenizer/
'''                llama ：一份主权重 *.gguf + 一份 mmproj*.gguf
'''                可选：selftest.wav + selftest.txt —— 金样本自检用，缺了就跳过自检。
'''   runtime      仅 llama 引擎需要：llama-server 所在的目录（含 ggml-*.dll）。
'''                标准布局是 runtime/vulkan；CUDA 那份不进基础包（1.1 GB 换 4.6%）。
'''   models       可选、**一般不要写**：档名 → 模型目录，只在同一个包里真并排放了
'''                两套完整权重时才用（本项目只有 Qwen3-ASR 0.6B int8 这一份）。
'''   window       推理窗口秒（默认 5）
'''   step         推进步长秒（默认 5）
'''
''' 为什么 v3 又把 runtime 请回来了：v2 的假设是「只有 ONNX Runtime 一条腿，后端是
''' EP 名」。实测下来那份 int8 权重（QOperator 动态量化）在 GPU 上没有融合核，
''' CUDA 整段静默回落 CPU；而同一份权重的 GGUF Q8_0（block-wise weight-only int8）
''' 在 CPU 上比 sherpa 快 2.2–2.6 倍、三家 GPU 用一份 Vulkan 运行时就能吃。
''' 于是「一份 worker + 两种模型容器 + 每容器各自的运行时」成了主线，
''' 清单必须能描述它 —— 但只描述模型与运行时目录，不再有 v1 那种
''' llamaArgs / layers / quant 之类把推理参数塞进清单的字段。
'''
''' 路径解析一律走 <see cref="解析路径"/>：相对路径相对包根，绝对路径原样。
''' </summary>
Friend NotInheritable Class Ai字幕包配置
    ''' <summary>包根目录（用户在设置里选的那一级）。</summary>
    Friend Property 包目录 As String = String.Empty
    ''' <summary>清单 name；空则回落到目录名。</summary>
    Friend Property 名称 As String = String.Empty
    Friend Property Worker脚本 As String = String.Empty
    ''' <summary>python 可执行；空 = PATH 上的 python。</summary>
    Friend Property Python As String = String.Empty
    ''' <summary>默认模型目录（onnx：三个 .onnx + tokenizer/；llama：主 *.gguf + mmproj*.gguf）。</summary>
    Friend Property 模型目录 As String = String.Empty
    ''' <summary>模型容器：llama（GGUF + llama-server）/ onnx（三件套 + sherpa）。
    ''' 由模型目录里实际有什么文件决定，不读清单的 engine 字段。</summary>
    Friend Property 引擎 As String = Ai字幕包.引擎Onnx
    ''' <summary>llama 引擎的运行时目录（含 llama-server.exe）；onnx 引擎恒为空。</summary>
    Friend Property 运行时目录 As String = String.Empty
    ''' <summary>默认档的量化形态，从 encoder 文件名读（encoder.int8.onnx → int8；
    ''' 没有后缀 → fp32）。只用于界面显示，不参与任何加载决策。</summary>
    Friend Property 默认档形态 As String = String.Empty
    ''' <summary>默认档体积（三个权重 + tokenizer/ 的字节数合计）。同上，只给人看。</summary>
    Friend Property 默认档体积 As Long = 0
    ''' <summary>清单 models 里列出的额外档位；空 = 这个包只有默认档。</summary>
    Friend Property 模型档表 As New List(Of Ai字幕模型档)
    Friend Property 窗口秒 As Double = 5.0
    Friend Property 步长秒 As Double = 5.0

    ''' <summary>按档名取模型档。档名为空或不在表里时 命中=False，调用方回落到默认档
    ''' （老设置里记着的档名可能随着换包消失，这里绝不能让启动失败）。</summary>
    Friend Function 取模型档(档名 As String, ByRef 命中 As Boolean) As Ai字幕模型档
        命中 = False
        If String.IsNullOrWhiteSpace(档名) Then Return Nothing
        For Each 档 In 模型档表
            If String.Equals(档.档名, 档名.Trim(), StringComparison.OrdinalIgnoreCase) Then
                命中 = True
                Return 档
            End If
        Next
        Return Nothing
    End Function
End Class

''' <summary>
''' 一个模型档位：清单 models 里的一项，只在包作者真并排放了两套权重时才存在
''' （例如同一份模型的两种量化各占一个目录）。本版随包的候选只有 Qwen3-ASR
''' 0.6B int8 一份，所以设置页平时根本不出现档位选择 —— 见 Form设置_AI字幕
''' 的 刷新模型档。
''' </summary>
Friend NotInheritable Class Ai字幕模型档
    ''' <summary>档名（清单 models 的键，如 int4 / fp16）。</summary>
    Friend Property 档名 As String = String.Empty
    ''' <summary>该档的模型目录（结构同 默认模型目录）。</summary>
    Friend Property 模型目录 As String = String.Empty
    ''' <summary>量化形态（int8 / fp16 / fp32 / int4…），从 encoder 文件名读。</summary>
    Friend Property 形态 As String = String.Empty
    ''' <summary>体积字节数。两档指向同一个目录时显示同样大小 —— 这是对的：体积代价
    ''' 按目录算，不按档算（共用目录就是零额外体积）。</summary>
    Friend Property 体积字节 As Long = 0
End Class

Friend NotInheritable Class Ai字幕包
    Friend Const 清单文件名 As String = "ai-subtitles.json"

    ''' <summary>模型容器标识。与 worker 的 _ENGINE_* 逐一对应，改一边要改另一边。</summary>
    Friend Const 引擎Llama As String = "llama"
    Friend Const 引擎Onnx As String = "onnx"

    ''' <summary>按模型目录里的容器判引擎：有 *.gguf 就是 llama，否则 onnx。
    ''' 这是唯一判据 —— 清单的 engine 字段只是给人看的备注。</summary>
    Friend Shared Function 判定引擎(模型目录 As String) As String
        Try
            If Directory.Exists(模型目录) Then
                For Each 文件 In Directory.EnumerateFiles(模型目录, "*.gguf")
                    Return Ai字幕包.引擎Llama
                Next
            End If
        Catch
            ' 目录读不了就当 onnx，由后面的完整性检查给出人话原因。
        End Try
        Return 引擎Onnx
    End Function

    ''' <summary>一个合格模型目录必须有的权重主干名（tokenizer 是目录，单独查）。
    ''' 官方包把量化标在文件名里（encoder.int8.onnx / decoder.int8.onnx），
    ''' 换 int4 又变成 encoder.int4.onnx，所以这里钉主干、不钉全名。</summary>
    Private Shared ReadOnly 必需权重 As String() = {"conv_frontend", "encoder", "decoder"}
    Private Const 分词器目录 As String = "tokenizer"

    ''' <summary>在模型目录里找 &lt;主干&gt;.onnx；没有就接受唯一的 &lt;主干&gt;.&lt;后缀&gt;.onnx。
    ''' 找不到或有多个候选都返回空串，原因写进 <paramref name="原因"/>。
    ''' 这样「解压官方包直接当 model/ 用」能成立，不必先重命名两个文件。</summary>
    Private Shared Function 找权重(目录 As String, 主干 As String, ByRef 原因 As String) As String
        原因 = String.Empty
        Dim 精确 = Path.Combine(目录, 主干 & ".onnx")
        If File.Exists(精确) Then Return 精确
        Dim 候选 = Directory.GetFiles(目录, 主干 & ".*.onnx")
        If 候选.Length = 1 Then Return 候选(0)
        If 候选.Length > 1 Then
            Dim 名单 = New List(Of String)
            For Each 项 In 候选
                名单.Add(Path.GetFileName(项))
            Next
            原因 = $"{主干} 有多个候选（{String.Join("、", 名单)}），一个模型目录只能留一份"
            Return String.Empty
        End If
        原因 = $"缺 {主干}.onnx（也接受 {主干}.int8.onnx 这类带后缀的）"
        Return String.Empty
    End Function

    ''' <summary>在模型目录里找 GGUF 主权重与 mmproj。合格返回 Nothing，否则返回原因。
    ''' mmproj 是音频编码器（mtmd 的投影层），少它模型只认文字不认音频，
    ''' 所以和主权重一样是硬性必需项。</summary>
    Private Shared Function 找GGUF(目录 As String, ByRef 主权重 As String,
                                    ByRef 投影 As String) As String
        主权重 = String.Empty
        投影 = String.Empty
        Dim 主列表 As New List(Of String)
        Dim 投影列表 As New List(Of String)
        For Each 文件 In Directory.GetFiles(目录, "*.gguf")
            If Path.GetFileName(文件).ToLowerInvariant().Contains("mmproj") Then
                投影列表.Add(文件)
            Else
                主列表.Add(文件)
            End If
        Next
        If 主列表.Count = 0 Then Return "缺主权重 *.gguf"
        If 投影列表.Count = 0 Then Return "缺 mmproj*.gguf（音频编码器，缺了模型不认音频）"
        主列表.Sort()
        投影列表.Sort()
        主权重 = 主列表(0)
        投影 = 投影列表(0)
        Return Nothing
    End Function
    ''' <summary>载入并校验一个扩展包目录。任一必输项不满足时返回 Nothing 并给出人话错误；
    ''' 调用方只需把错误显示出来，不需要知道包内部结构。</summary>
    Friend Shared Function 载入(目录 As String, ByRef 错误 As String) As Ai字幕包配置
        错误 = String.Empty
        If String.IsNullOrWhiteSpace(目录) Then Return Nothing
        If Not Directory.Exists(目录) Then
            错误 = "包目录不存在"
            Return Nothing
        End If

        Dim 清单路径 = Path.Combine(目录, 清单文件名)
        If Not File.Exists(清单路径) Then
            错误 = $"这个目录里没有 {清单文件名}"
            Return Nothing
        End If

        Dim 文档 As JsonDocument = Nothing
        Try
            文档 = JsonDocument.Parse(File.ReadAllText(清单路径))
        Catch ex As Exception
            错误 = $"{清单文件名} 解析失败：{ex.Message}"
            Return Nothing
        End Try

        Using 文档
            Dim 根 = 文档.RootElement
            Dim 配置 As New Ai字幕包配置 With {.包目录 = 目录}

            ' 版本门控：读不到 version 或不在 2/3 就直接拒，不要让一份 v1 清单流进来变成一堆奇怪的“模型目录不存在”。
            ' v3 = 五类实体（多了 runtime，GGUF 包要用），v2 = 四类（ONNX 老包）。两者都还能装。
            Dim 版本元 As JsonElement = Nothing
            Dim 版本 As Integer = 0
            If 根.TryGetProperty("version", 版本元) AndAlso 版本元.ValueKind = JsonValueKind.Number Then
                版本元.TryGetInt32(版本)
            End If
            If 版本 <> 2 AndAlso 版本 <> 3 Then
                If 版本 = 1 Then
                    错误 = "这是 llama.cpp 时代（version 1）的清单，格式已作废：请改成 version 2 或 3"
                Else
                    错误 = $"清单 version 必须是 2 或 3（读到 {版本}）"
                End If
                Return Nothing
            End If

            配置.名称 = 读字符串(根, "name")
            If String.IsNullOrWhiteSpace(配置.名称) Then 配置.名称 = New DirectoryInfo(目录).Name

            ' worker 是唯一的必填项：没有它整个包没有意义。
            Dim worker = 解析路径(目录, 读字符串(根, "worker"))
            If String.IsNullOrWhiteSpace(worker) Then
                错误 = "清单里没有写 worker"
                Return Nothing
            End If
            If Not File.Exists(worker) Then
                错误 = $"worker 脚本不存在：{worker}"
                Return Nothing
            End If
            配置.Worker脚本 = worker

            配置.Python = 解析路径(目录, 读字符串(根, "python"))

            ' 模型目录：清单写了就用它，没写就用包目录下的 model/。
            Dim 模型 = 解析路径(目录, 读字符串(根, "model"))
            If String.IsNullOrWhiteSpace(模型) Then 模型 = Path.Combine(目录, "model")
            ' 引擎按容器判，不读清单的 engine 字段 —— 文件是唯一真相
            ' （清单写错只会让备注与事实不符，不该把包判死）。
            配置.引擎 = 判定引擎(模型)
            Dim 缺 As String = 检查模型目录(模型, 配置.引擎)
            If 缺 IsNot Nothing Then
                错误 = 缺
                Return Nothing
            End If
            配置.模型目录 = 模型
            配置.默认档形态 = 探测形态(模型, 配置.引擎)
            配置.默认档体积 = 求体积(模型, 配置.引擎)

            If 配置.引擎 = Ai字幕包.引擎Llama Then
                ' llama 的运行时：清单写了就用它，没写才猜标准布局 runtime/vulkan。
                ' 猜错了不要紧，缺 llama-server.exe 会在下一行报出来。
                Dim 运行 = 解析路径(目录, 读字符串(根, "runtime"))
                If String.IsNullOrWhiteSpace(运行) Then
                    Dim 候选 = Path.Combine(目录, "runtime", "vulkan")
                    If Directory.Exists(候选) Then 运行 = 候选
                End If
                If String.IsNullOrWhiteSpace(运行) OrElse Not Directory.Exists(运行) Then
                    错误 = "这是 GGUF 包，但找不到 llama 运行时目录（清单里的 runtime，" &
                           "或标准的 runtime/vulkan/）"
                    Return Nothing
                End If
                If Not File.Exists(Path.Combine(运行, "llama-server.exe")) Then
                    错误 = $"运行时目录里没有 llama-server.exe：{运行}"
                    Return Nothing
                End If
                配置.运行时目录 = 运行
            End If

            读模型档表(目录, 根, 配置)

            Dim 窗口 = 读数值(根, "window", 5.0)
            Dim 步长 = 读数值(根, "step", 5.0)
            配置.窗口秒 = Math.Clamp(窗口, 0.5, 30.0)
            配置.步长秒 = Math.Min(Math.Clamp(步长, 0.5, 30.0), 配置.窗口秒)
            Return 配置
        End Using
    End Function

    ''' <summary>模型目录不合格时返回人话原因，合格返回 Nothing。
    ''' 一次性把"缺哪个文件"说清楚 —— 让用户看到"缺 decoder.onnx"而不是一句
    ''' "worker 起不来"（worker 那边只会报同样的文件名，但要等进程起来才看得见）。</summary>
    Private Shared Function 检查模型目录(模型目录 As String, 引擎 As String) As String
        If Not Directory.Exists(模型目录) Then Return $"模型目录不存在：{模型目录}"
        If 引擎 = Ai字幕包.引擎Llama Then
            Dim 主 As String = String.Empty
            Dim 投影 As String = String.Empty
            Dim 原因 As String = 找GGUF(模型目录, 主, 投影)
            If 原因 IsNot Nothing Then Return $"模型目录不合格：{原因}（{模型目录}）"
            Return Nothing
        End If
        For Each 主干 In 必需权重
            Dim 原因 As String = String.Empty
            If String.IsNullOrEmpty(找权重(模型目录, 主干, 原因)) Then
                Return $"模型目录不合格：{原因}（{模型目录}）"
            End If
        Next
        If Not Directory.Exists(Path.Combine(模型目录, 分词器目录)) Then
            Return $"模型目录里缺 {分词器目录}/ 子目录：{模型目录}"
        End If
        Return Nothing
    End Function

    ''' <summary>量化形态：官方把量化写进文件名（encoder.int8.onnx / decoder.int8.onnx），
    ''' 所以取 encoder 文件名第一个点之后的部分；没有点就是 fp32（未量化导出）。
    ''' 只读文件名、不读权重内容 —— 谁把文件改了名就显示改后的名字，这没关系，
    ''' 这一项从来不参与加载决策。</summary>
    Private Shared Function 探测形态(模型目录 As String, 引擎 As String) As String
        If 引擎 = Ai字幕包.引擎Llama Then
            ' GGUF 的量化写在文件名里（Qwen3-ASR-0.6B-Q8_0.gguf → q8_0），
            ' 取最后一个短横线之后那一段；取不到就报 gguf，不装懂。
            Dim 主 As String = String.Empty
            Dim 投影 As String = String.Empty
            Dim GG错 As String = 找GGUF(模型目录, 主, 投影)
            If Not String.IsNullOrEmpty(GG错) Then Return String.Empty
            Dim 名 = Path.GetFileNameWithoutExtension(主)
            Dim 划 = 名.LastIndexOf("-"c)
            If 划 >= 0 AndAlso 划 < 名.Length - 1 Then
                Return 名.Substring(划 + 1).ToLowerInvariant()
            End If
            Return "gguf"
        End If
        Dim 原因 As String = String.Empty
        Dim 文件 = 找权重(模型目录, "encoder", 原因)
        If String.IsNullOrEmpty(文件) Then Return String.Empty
        Dim 主干 = Path.GetFileNameWithoutExtension(文件)      ' encoder.int8 / encoder
        Dim 点 = 主干.IndexOf("."c)
        If 点 < 0 Then Return "fp32"
        Return 主干.Substring(点 + 1).ToLowerInvariant()
    End Function

    ''' <summary>体积：三个权重 + tokenizer/ 里的文件合计。只 stat 已知的那几个文件，
    ''' 不做目录递归 —— 941 MB 的目录递归数出来要几百毫秒，而这里只需要"大概多大"。
    ''' 这个数的用途是让用户看见「再加一档要多付多少」，精确到 MB 就够。</summary>
    Private Shared Function 求体积(模型目录 As String, 引擎 As String) As Long
        Dim 总 As Long = 0
        Try
            If 引擎 = Ai字幕包.引擎Llama Then
                ' llama 的体积 = 主权重 + mmproj（tokenizer 与词表都嵌在 gguf 里）。
                For Each 文件 In Directory.EnumerateFiles(模型目录, "*.gguf")
                    总 += New FileInfo(文件).Length
                Next
                Return 总
            End If
            For Each 主干 In 必需权重
                Dim 原因 As String = String.Empty
                Dim 文件 = 找权重(模型目录, 主干, 原因)
                If Not String.IsNullOrEmpty(文件) Then 总 += New FileInfo(文件).Length
            Next
            Dim 语 = Path.Combine(模型目录, 分词器目录)
            If Directory.Exists(语) Then
                For Each 文件 In Directory.EnumerateFiles(语)
                    总 += New FileInfo(文件).Length
                Next
            End If
        Catch
            ' 体积只是显示项，读不到就不显示，绝不影响载入。
        End Try
        Return 总
    End Function

    ''' <summary>体积人话化。小于 1 MB 返回空（那不是一套权重，是半成品）。</summary>
    Friend Shared Function 体积文本(字节 As Long) As String
        If 字节 < 1024L * 1024L Then Return String.Empty
        If 字节 >= 1024L * 1024L * 1024L Then
            Return (字节 / 1024.0 / 1024.0 / 1024.0).ToString("0.0", Globalization.CultureInfo.InvariantCulture) & " GB"
        End If
        Return (字节 / 1024.0 / 1024.0).ToString("0", Globalization.CultureInfo.InvariantCulture) & " MB"
    End Function

    ''' <summary>相对路径一律相对包根，绝对路径原样；空值保持空（后面的 fallback 认空）。</summary>
    Private Shared Function 解析路径(包目录 As String, 值 As String) As String
        If String.IsNullOrWhiteSpace(值) Then Return String.Empty
        值 = 值.Trim().Trim(""""c)
        Try
            If Path.IsPathRooted(值) Then Return Path.GetFullPath(值)
            Return Path.GetFullPath(Path.Combine(包目录, 值))
        Catch
            Return String.Empty
        End Try
    End Function

    ''' <summary>models：{档名: {dir: 模型目录}} 或 {档名: "模型目录"}（两种写法都收，
    ''' 后者更省字）。任一路径不合格就整档丢弃 —— 宁可让用户少一个选项，也不要
    ''' 让他选到一个注定起不来的档。</summary>
    Private Shared Sub 读模型档表(包目录 As String, 根 As JsonElement, 配置 As Ai字幕包配置)
        If 根.ValueKind <> JsonValueKind.Object Then Return
        Dim 表 As JsonElement = Nothing
        If Not 根.TryGetProperty("models", 表) OrElse 表.ValueKind <> JsonValueKind.Object Then Return
        For Each 属性 In 表.EnumerateObject()
            Dim 目录 As String
            If 属性.Value.ValueKind = JsonValueKind.String Then
                目录 = 解析路径(包目录, 属性.Value.GetString())
            ElseIf 属性.Value.ValueKind = JsonValueKind.Object Then
                目录 = 解析路径(包目录, 读字符串(属性.Value, "dir"))
            Else
                Continue For
            End If
            If String.IsNullOrWhiteSpace(目录) Then Continue For
            ' 档位必须与默认档同一个引擎：一个包里并排放 GGUF 和 ONNX 两套，
            ' worker 只认其中一种容器，混着列出来等于骗人。
            Dim 档引擎 = 判定引擎(目录)
            If 档引擎 <> 配置.引擎 Then Continue For
            If 检查模型目录(目录, 档引擎) IsNot Nothing Then Continue For
            Dim 形态 = 探测形态(目录, 档引擎)
            ' 名不副实的档整档丢掉：档名写着 int4、目录里其实是 int8 时，界面会
            ' 显示「int4（int8 · 941 MB）」，用户以为自己在跑 int4。这种事不抛异常，
            ' 只是让人按错的精度做判断。
            If Not 形态相符(属性.Name, 形态) Then Continue For
            配置.模型档表.Add(New Ai字幕模型档 With {
                .档名 = 属性.Name, .模型目录 = 目录,
                .形态 = 形态, .体积字节 = 求体积(目录, 档引擎)})
        Next
    End Sub

    ''' <summary>档名里写明的量化形态（int8 / int4 / fp16 / fp32）必须与目录里
    ''' 实际的 encoder 文件名一致；档名不带这几个词就不校验。只挡住「写着 int4
    ''' 其实是 int8」这一类 —— 它不会报错，只会让人以为在跑别的精度。</summary>
    Private Shared Function 形态相符(档名 As String, 形态 As String) As Boolean
        If String.IsNullOrEmpty(形态) Then Return True
        For Each 词 In New String() {"int8", "int4", "fp16", "fp32"}
            If 档名.IndexOf(词, StringComparison.OrdinalIgnoreCase) >= 0 Then
                Return String.Equals(词, 形态, StringComparison.OrdinalIgnoreCase)
            End If
        Next
        Return True
    End Function

    Private Shared Function 读字符串(根 As JsonElement, 名 As String) As String
        If 根.ValueKind <> JsonValueKind.Object Then Return String.Empty
        Dim 元素 As JsonElement = Nothing
        If Not 根.TryGetProperty(名, 元素) Then Return String.Empty
        If 元素.ValueKind = JsonValueKind.String Then Return 元素.GetString()
        Return String.Empty
    End Function

    Private Shared Function 读数值(根 As JsonElement, 名 As String, 默认 As Double) As Double
        If 根.ValueKind <> JsonValueKind.Object Then Return 默认
        Dim 元素 As JsonElement = Nothing
        If Not 根.TryGetProperty(名, 元素) Then Return 默认
        If 元素.ValueKind <> JsonValueKind.Number Then Return 默认
        Dim 值 As Double
        If 元素.TryGetDouble(值) Then Return 值
        Return 默认
    End Function
End Class

''' <summary>
''' 推理档：两个下拉合成一个二维选择。**设备**回答「用哪块硬件算」（CPU / NVIDIA /
''' AMD / Intel / NPU），**后端**回答「用哪个运行时压榨它」（onnxruntime CPU、CUDA、
''' DirectML、OpenVINO 的 CPU / GPU / NPU）。两维不是一回事：同一块 NVIDIA 卡既能
''' 走 CUDA（厂商加速）也能走 DirectML（DX12 通用路径）；同一块 Intel 核显既能走
''' OpenVINO（Level Zero + XMX，厂商路径）也能走 DirectML。
'''
''' 设备在前、后端在后，是因为用户先知道自己有什么卡，才谈得上选哪条软件栈。
''' 反过来给一张混着 CUDA / DirectML / OpenVINO 的大列表，就是 llama.cpp 时代的老
''' 毛病：挑了个名字，最后要在日志里才知道到底跑了哪。
'''
''' 后端标识**就是**引擎（tools/ai-subtitles/qwen3_ort.py）BACKENDS 的键，宿主原样
''' 塞进 worker 的 --device，中间不再翻译一层 —— 多一层映射就多一处漂移。
''' 设备维只用来过滤「后端下拉列出哪些项」，不参与传参；所以设置里存两个字段而不是
''' 一个合成串（合成串要解析，脏值就会解析成别的档）。
''' </summary>
Friend NotInheritable Class Ai字幕推理档

    ' ---------------- 设备（硬件） ----------------
    Friend Const 设备自动 As String = "auto"
    Friend Const 设备Cpu As String = "cpu"
    Friend Const 设备Nvidia As String = "nvidia"
    Friend Const 设备Amd As String = "amd"
    Friend Const 设备Intel As String = "intel"
    Friend Const 设备Npu As String = "npu"

    ''' <summary>设备下拉顺序：自动在最前（默认），之后 CPU、三家显卡、NPU。
    ''' 设置里存标识不存下标，所以顺序可以安全调整。</summary>
    Friend Shared ReadOnly 设备全部 As String() = {设备自动, 设备Cpu, 设备Nvidia, 设备Amd, 设备Intel, 设备Npu}

    ''' <summary>未知值 / 空值 / llama.cpp 时代的旧标识一律归 auto。老设置里的
    ''' cuda 保留原意（那时它同时表示「NVIDIA 卡」）落到 nvidia；rocm 落 AMD、
    ''' sycl 落 Intel；vulkan 是通用路径、设备维没有对应物，归 auto。</summary>
    Friend Shared Function 规整设备(值 As String) As String
        If String.IsNullOrWhiteSpace(值) Then Return 设备自动
        Select Case 值.Trim().ToLowerInvariant()
            Case 设备Cpu
                Return 设备Cpu
            Case 设备Nvidia, "cuda"
                Return 设备Nvidia
            Case 设备Amd, "rocm"
                Return 设备Amd
            Case 设备Intel, "sycl"
                Return 设备Intel
            Case 设备Npu
                Return 设备Npu
            Case Else
                Return 设备自动
        End Select
    End Function

    Friend Shared Function 设备显示名(id As String) As String
        Select Case 规整设备(id)
            Case 设备Cpu
                Return "CPU（不用显卡）"
            Case 设备Nvidia
                Return "NVIDIA 显卡"
            Case 设备Amd
                Return "AMD 显卡"
            Case 设备Intel
                Return "Intel 显卡"
            Case 设备Npu
                Return "NPU（Core Ultra）"
            Case Else
                Return "自动（按能力挑）"
        End Select
    End Function

    ''' <summary>选中该设备时「接下来会发生什么」，给状态行用。不是文案修辞 ——
    ''' 每一句都对应一条实测边界（交接文档 §17.3 / §20.4）。</summary>
    Friend Shared Function 设备说明(id As String) As String
        Select Case 规整设备(id)
            Case 设备Cpu
                Return "全在处理器上算，不占显卡"
            Case 设备Nvidia
                Return "CUDA 是厂商加速路径；DirectML 是通用的 DX12 路径"
            Case 设备Amd
                Return "Windows 上只有 DirectML：ROCm / MIGraphX 都没有 Windows 构件"
            Case 设备Intel
                Return "OpenVINO 是厂商路径（Level Zero / XMX）；DirectML 走 DX12"
            Case 设备Npu
                Return "只有 OpenVINO 一条官方路径，且只能吃 fp32 那一段"
            Case Else
                Return "按 CUDA → DirectML → OpenVINO GPU → CPU 的顺序试，第一个可用即采用"
        End Select
    End Function

    ' ---------------- 后端（运行时） ----------------
    Friend Const 后端自动 As String = "auto"
    Friend Const 后端Cpu As String = "cpu"
    ''' <summary>llama 引擎唯一一条显卡路径：--list-devices 选到哪张卡就用哪张。
    ''' 与 cuda 分开是因为它不代表 NVIDIA —— Vulkan 一份运行时覆盖 NV/AMD/Intel。</summary>
    Friend Const 后端Gpu As String = "gpu"
    Friend Const 后端Cuda As String = "cuda"
    Friend Const 后端Dml As String = "dml"
    Friend Const 后端OvCpu As String = "ov-cpu"
    Friend Const 后端OvGpu As String = "ov-gpu"
    Friend Const 后端OvNpu As String = "ov-npu"

    ''' <summary>后端全集：与 qwen3_ort.BACKENDS 的键逐一对应（L0 门禁钉住这一条，
    ''' 两边分叉就是「下拉里有、引擎不认」）。下拉里出现的是 设备后端 过滤后的子集。</summary>
    Friend Shared ReadOnly 后端全部 As String() = {后端自动, 后端Cpu, 后端Cuda, 后端Dml, 后端OvCpu, 后端OvGpu, 后端OvNpu}

    ''' <summary>这个设备在当前引擎下是不是真能落地。GGUF 包没有 NPU 路径
    ''' （weight-only 量化进不了 NPU 的执行单元，NpuBackend.load() 必失败 → 落 CPU），
    ''' 列出来就是「勾着 NPU、实际跑 CPU」。菜单没有 tooltip 可补这一句，所以直接
    ''' 不摆 —— 与「单档不做子菜单」「双语不在 ONNX 包出现」是同一条纪律。
    ''' 设置页保留 NPU 不在此列：那一格会把「需独立包 / 会落 CPU」写进项文字与提示。</summary>
    Friend Shared Function 设备可用(设备 As String,
                                    Optional 引擎 As String = Ai字幕包.引擎Onnx) As Boolean
        If 引擎 = Ai字幕包.引擎Llama Then
            Return 规整设备(设备) <> 设备Npu
        End If
        Return True
    End Function

    ''' <summary>该设备上可选的后端，第一个是该设备的推荐项（换设备后默认落到它）。
    ''' 顺序即下拉顺序：厂商加速在前，通用 / 兜底在后。auto 设备只剩 auto 一项，
    ''' 界面上置灰并把原因写进项文字 —— 下拉水印在 SelectedIndex ≥ 0 时看不见。</summary>
    Friend Shared Function 设备后端(设备 As String,
                                    Optional 引擎 As String = Ai字幕包.引擎Onnx) As String()
        ' llama 只有一条显卡路径（Vulkan 一份二进制管三家），所以「后端」这一维
        ' 已经退化成「要不要上显卡」。仍然保留这一格，是因为它得给用户一个
        ' 明确的落点；但它只有一个选项，界面上置灰（见 Form设置_AI字幕）。
        If 引擎 = Ai字幕包.引擎Llama Then
            Select Case 规整设备(设备)
                Case 设备Cpu
                    Return {后端Cpu}
                Case 设备Npu
                    Return {后端OvNpu}
                Case 设备自动
                    Return {后端自动}
                Case Else
                    Return {后端Gpu}
            End Select
        End If
        Select Case 规整设备(设备)
            Case 设备Cpu
                Return {后端Cpu, 后端OvCpu}
            Case 设备Nvidia
                Return {后端Cuda, 后端Dml}
            Case 设备Amd
                Return {后端Dml}
            Case 设备Intel
                Return {后端OvGpu, 后端Dml}
            Case 设备Npu
                Return {后端OvNpu}
            Case Else
                Return {后端自动}
        End Select
    End Function

    ''' <summary>把存着的后端归位到该设备的候选表：不在表里（换过设备、老设置里的
    ''' cuda）就落到该设备的第一推荐项。只归内存不主动落盘 —— 用户一动下拉就会把
    ''' 正确值写回去（与语言残留值同一套路）。</summary>
    Friend Shared Function 规整后端(设备 As String, 值 As String,
                                    Optional 引擎 As String = Ai字幕包.引擎Onnx) As String
        Dim 候选 = 设备后端(设备, 引擎)
        If Not String.IsNullOrWhiteSpace(值) Then
            Dim id = 值.Trim().ToLowerInvariant()
            For Each 项 In 候选
                If String.Equals(项, id, StringComparison.Ordinal) Then Return 项
            Next
        End If
        Return 候选(0)
    End Function

    Friend Shared Function 后端显示名(id As String,
                                    Optional 引擎 As String = Ai字幕包.引擎Onnx) As String
        If 引擎 = Ai字幕包.引擎Llama Then
            Select Case (If(id, String.Empty)).Trim().ToLowerInvariant()
                Case 后端Cpu
                    Return "CPU"
                Case 后端Gpu
                    Return "显卡（Vulkan）"
                Case 后端OvNpu
                    Return "NPU（需独立包）"
                Case Else
                    Return "自动"
            End Select
        End If
        Select Case (If(id, String.Empty)).Trim().ToLowerInvariant()
            Case 后端Cpu
                Return "CPU"
            Case 后端Cuda
                Return "CUDA"
            Case 后端Dml
                Return "DirectML"
            Case 后端OvCpu
                Return "OpenVINO CPU"
            Case 后端OvGpu
                Return "OpenVINO GPU"
            Case 后端OvNpu
                Return "OpenVINO NPU"
            Case Else
                Return "自动"
        End Select
    End Function

    ''' <summary>这个后端把哪一段推理放到加速器上。不是修辞：实测只有 conv_frontend
    ''' （纯 fp32）在 GPU 上又快又准；encoder / decoder 是 int8 的 QOperator 动态量化，
    ''' 在 GPU 上要么被 Memcpy 切碎变慢，要么直接算错（OpenVINO GPU 出 NaN 且不报错），
    ''' 所以 int8 那两段一律留 CPU。</summary>
    Friend Shared Function 后端说明(id As String,
                                    Optional 引擎 As String = Ai字幕包.引擎Onnx) As String
        If 引擎 = Ai字幕包.引擎Llama Then
            Select Case (If(id, String.Empty)).Trim().ToLowerInvariant()
                Case 后端Cpu
                    Return "整份权重在处理器上跑（GGUF Q8_0，比 ONNX int8 快 2 倍以上）"
                Case 后端Gpu
                    Return "整份权重放进显存，Vulkan 覆盖 NVIDIA / AMD / Intel 三家"
                Case 后端OvNpu
                    Return "NPU 只能吃 ONNX 静态量化，要单独装 NPU 包"
                Case Else
                    Return "有独显就用显卡，没有就用 CPU；核显会主动跳过（实测比 CPU 慢）"
            End Select
        End If
        Select Case (If(id, String.Empty)).Trim().ToLowerInvariant()
            Case 后端Cpu
                Return "三段都算在 CPU 上（基线配置）"
            Case 后端Cuda
                Return "卷积前端上 NVIDIA 显卡，int8 两段留 CPU（在 GPU 上更慢）"
            Case 后端Dml
                Return "卷积前端上 DirectX 显卡，int8 两段留 CPU（更慢）"
            Case 后端OvCpu
                Return "三段都走 OpenVINO CPU 插件（oneDNN）"
            Case 后端OvGpu
                Return "卷积前端上 Intel 显卡，int8 两段留 CPU（iGPU 上出 NaN）"
            Case 后端OvNpu
                Return "卷积前端上 NPU，int8 两段留 CPU（NPU 只吃 fp32）"
            Case Else
                Return "由 worker 按能力挑，第一个能用的即采用"
        End Select
    End Function

    ''' <summary>验证状态：本机跑通 / 代码路径通但没实机 / 未验证。必须显示 ——
    ''' 少这一行，用户会把「选了就在跑」当成事实。</summary>
    Friend Shared Function 后端状态(id As String,
                                    Optional 引擎 As String = Ai字幕包.引擎Onnx) As String
        If 引擎 = Ai字幕包.引擎Llama Then
            Select Case (If(id, String.Empty)).Trim().ToLowerInvariant()
                Case 后端Cpu
                    Return "已验证（单段 5 s 音频 1.24 s）"
                Case 后端Gpu
                    Return "已验证（RTX 5080 / 4060 / Intel 核显）"
                Case 后端OvNpu
                    Return "GGUF 包没有 NPU 路径，选了会说明原因后落 CPU（要 NPU 请装 NPU 包）"
                Case Else
                    Return "自动"
            End Select
        End If
        Select Case (If(id, String.Empty)).Trim().ToLowerInvariant()
            Case 后端Cpu
                Return "已验证"
            Case 后端Cuda
                Return "已验证（要 CUDA 12 运行库 + cuDNN 9）"
            Case 后端Dml
                Return "本包没有 DirectML 版 ONNX Runtime，选了会退回自动"
            Case 后端OvCpu
                Return "可用但慢（三段都走 OpenVINO，比 sherpa 慢约 5 倍），仅供排障"
            Case 后端OvGpu
                Return "已验证（conv 上显卡，int8 两段留 CPU）"
            Case 后端OvNpu
                Return "NPU 包已验证编译链（conv 可编到 NPU 架构）；NPU 上执行未实测"
            Case Else
                Return "自动"
        End Select
    End Function

    ''' <summary>解析成 worker 的 --device 值。设备维不参与传参 —— 引擎的后端标识
    ''' 本身已经区分了设备（ov-gpu 就隐含 Intel 显卡）。</summary>
    Friend Shared Function 解析(设备 As String, 后端 As String,
                                Optional 引擎 As String = Ai字幕包.引擎Onnx) As String
        Dim d = 规整设备(设备)
        If d = 设备自动 Then Return 后端自动
        Return 规整后端(d, 后端, 引擎)
    End Function

    ''' <summary>worker 当前认得的 --device 值。**必须与 asr_worker._CANDIDATES 对齐**，
    ''' 否则界面显示"可用"而 worker 静默按 auto 处理（或反过来，明明是有效档却
    ''' 标成不认）。dml 两边都还没有：包里没有 DirectML 版 ONNX Runtime。</summary>
    ''' <summary>该后端会不会把模型自报的语言标签送回来（决定「只显示…」是不是真在筛）。
    ''' 与 asr_worker 的 `自报语言 = engine == llama or backend.id in _OV_TARGETS` 同一
    ''' 判据：llama 与 OpenVINO 都从 `language X<asr_text>` 模板里取到自报语言，闸门真生效；
    ''' sherpa（ONNX 的 cpu / cuda）的 result 只有 text，闸门形同虚设。
    ''' 这条必须与 worker 同步 —— 两边分叉，界面就会把「在筛」说成「不筛」或反之。</summary>
    Friend Shared Function 后端自报语言(值 As String,
                                        Optional 引擎 As String = Ai字幕包.引擎Onnx) As Boolean
        If 引擎 = Ai字幕包.引擎Llama Then Return True
        Select Case (If(值, String.Empty)).Trim().ToLowerInvariant()
            Case 后端OvCpu, 后端OvGpu, 后端OvNpu
                Return True
            Case Else
                ' auto 最终落到哪个后端由 worker 决定，不能替它断言"能筛"。
                Return False
        End Select
    End Function

    Friend Shared Function 当前worker可用(值 As String,
                                        Optional 引擎 As String = Ai字幕包.引擎Onnx) As Boolean
        If 引擎 = Ai字幕包.引擎Llama Then
            ' llama 引擎认得 auto / gpu / cpu / ov-npu（ov-npu 会明确报「要独立包」后落 CPU）。
            Select Case (If(值, String.Empty)).Trim().ToLowerInvariant()
                Case 后端自动, 后端Gpu, 后端Cpu, 后端OvNpu
                    Return True
                Case Else
                    Return False
            End Select
        End If
        Select Case (If(值, String.Empty)).Trim().ToLowerInvariant()
            Case 后端自动, 后端Cpu, 后端Cuda, 后端Gpu, 后端OvNpu, 后端OvGpu, 后端OvCpu,
                 后端Dml
                Return True
            Case Else
                Return False
        End Select
    End Function
End Class
