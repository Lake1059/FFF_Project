Imports System.IO
Imports System.IO.Compression
Imports System.Linq
Imports System.Net.Http
Imports System.Security.Cryptography
Imports System.Threading
Imports System.Threading.Tasks

''' <summary>
''' AI 字幕包下载器（乙′ 方案，2026-11-12；双包 2026-11-13；NPU 全公共源同日）：
'''   * 两种包：GPU+CPU（GGUF Q8_0 / llama 引擎 / Vulkan）与 NPU（ONNX int8 / onnx 引擎 /
'''     OpenVINO）。两者是不同引擎、不同运行时，不能混用（见 docs/157）。
'''   * GGUF 包包括模型、llama.cpp Vulkan 运行时、嵌入式 Python 和 NumPy。
'''   * NPU 模型直链 sherpa 官方维护者 HF 仓（csukuangfj2，六件 SHA256 与验证包逐字节
'''     一致）；运行时 = python.org embeddable + PyPI 官方 wheel（numpy/ort/openvino/
'''     sherpa，版本与验证包精确一致），下载后解压组装 + 写 ._pth。
'''   * 母仓（母仓地址）对两种包都只是**可选加速源**：zip 整包优先、公共源散件兜底。
'''     不配置母仓也能完整下载（零上传零配置）。
'''   * 全部文件按 字节 + SHA256 钉死，断点续传（.part），逐源回退，磁盘空间预检。
''' 生成包 = 目标目录 + ai-subtitles.json(v3) + asr_worker.py + 模型 + 运行时，
''' 下载完成即通过 Ai字幕包.载入 校验并启用。NPU 包额外带 qwen3_ort.py（随 exe 部署）。
''' </summary>
Friend NotInheritable Class Ai字幕包下载器

    ''' <summary>包类型：决定下载清单与生成的清单引擎。GPU+CPU 是默认（有公共回退源），
    ''' NPU 需母仓。与设置页「包类型」下拉一一对应。</summary>
    Friend Enum 包类型
        GpuCpu
        Npu
    End Enum

    ''' <summary>自建母仓的 resolve 根（**不带尾斜杠**）。**可选**：两种包都有完整
    ''' 公共源链，不配置也能下载。配置后 zip 整包优先（GGUF：runtime-vulkan.zip /
    ''' python.zip；NPU：npu-model.zip / npu-python.zip，载荷在 dist\hf-upload\），
    ''' 失败自动回落公共源散件。填成
    ''' https://huggingface.co/&lt;组织&gt;/&lt;仓库&gt;/resolve/main 即启用。</summary>
    Public Const 母仓地址 As String = ""

    Private Const 模型仓官方 As String = "https://huggingface.co/ggml-org/Qwen3-ASR-0.6B-GGUF/resolve/main"
    Private Const 模型仓镜像 As String = "https://hf-mirror.com/ggml-org/Qwen3-ASR-0.6B-GGUF/resolve/main"

    ' 运行时回退源：llama.cpp b12110 官方 Vulkan 构建（37,661,146 B；
    ' sha256 与 GitHub release digest 逐字一致，2026-11-12 实下载复核）。
    Private Const 运行时回退网址 As String =
        "https://github.com/ggml-org/llama.cpp/releases/download/b12110/llama-b12110-bin-win-vulkan-x64.zip"
    Private Const 运行时回退字节 As Long = 37661146
    Private Const 运行时回退哈希 As String = "9732a5e48887ca54d0bfe4af4324dc85fd56cd8cda19b52df3542fa6ebe6874f"

    ' 母仓载荷（dist\hf-upload\，验证版 runtime 88 MB / 包内 Python 93 MB 压缩后）。
    Private Const 母仓运行时字节 As Long = 33371417
    Private Const 母仓运行时哈希 As String = "ff811ac586ffdada5a2b9c147ec87fffb2e4d48475bc8d3bb9df1cdf5831416a"
    Private Const 母仓Python字节 As Long = 31868218
    Private Const 母仓Python哈希 As String = "39d36922271928bceba66fed3ac465a4f82e367f59aacdccda7de26c8b39e913"

    ' 模型（Qwen3-ASR-0.6B GGUF Q8_0）：哈希取自验证包实测文件（即 hf-mirror 下载件）。
    Private Const 主权重字节 As Long = 804749248
    Private Const 主权重哈希 As String = "bca259818b50ca7c4c05e9bdb35a5dc04fa039653a6d6f3f0f331f96f6aa1971"
    Private Const 投影字节 As Long = 214392480
    Private Const 投影哈希 As String = "41a342b5e4c514e968cb756de6cd1b7be39eff43c44c57a2ef5fc6522e36603d"

    ' NPU 包（2026-11-13 全公共源化，docs/157 §六）：模型 = sherpa 官方维护者的公开 HF 仓
    ' （csukuangfj2 = k2-fsa），六个文件 SHA256 与 dist\AI字幕包-NPU 逐字节一致（HF LFS
    ' oid 实核）；运行时 = python.org 官方 embeddable + PyPI 官方 wheel（版本与验证包
    ' 精确一致：numpy 2.5.4 / ort 1.27.0 / openvino 2026.4.1-22982 / sherpa 2.0.1）。
    ' 组装件已在 _3fc_tmp\npu_pub_stage 实测：import 四件套 + OpenVINO 设备探测 +
    ' qwen3_ort 金样本真推理（粤语，与 selftest.txt 一致）。
    Private Const Npu模型仓官方 As String =
        "https://huggingface.co/csukuangfj2/sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25/resolve/main"
    Private Const Npu模型仓镜像 As String =
        "https://hf-mirror.com/csukuangfj2/sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25/resolve/main"

    Private Const NpuConv字节 As Long = 44148281
    Private Const NpuConv哈希 As String = "d22dc4423e0940e49884e903d2ea2f7e5567c14fc1aed97e4e26d6b8f208ef9e"
    Private Const NpuEnc字节 As Long = 182491662
    Private Const NpuEnc哈希 As String = "60748d3e6744a57c9c91e1b17424a6c2990567e8adceb0783940c03ed98fa9d9"
    Private Const NpuDec字节 As Long = 755914231
    Private Const NpuDec哈希 As String = "4f6885be5959ae26af3089d38ee7972c5fafbeeb1cf8d5e76eab6d8b61ca5771"
    Private Const NpuVocab字节 As Long = 2776833
    Private Const NpuVocab哈希 As String = "ca10d7e9fb3ed18575dd1e277a2579c16d108e32f27439684afa0e10b1440910"
    Private Const NpuMerges字节 As Long = 1671853
    Private Const NpuMerges哈希 As String = "8831e4f1a044471340f7c0a83d7bd71306a5b867e95fd870f74d0c5308a904d5"
    Private Const NpuTokCfg字节 As Long = 12487
    Private Const NpuTokCfg哈希 As String = "4942d005604266809309cabc9f4e9cb89ce855d59b14681fdc0e1cc62ea26c4c"

    ' Python 3.12.10 embeddable（python.org 官方，11,133,606 B 实下载核）。
    Private Const Embed网址 As String = "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip"
    Private Const Embed字节 As Long = 11133606
    Private Const Embed哈希 As String = "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3"

    ' PyPI 官方 wheel（files.pythonhosted.org 内容寻址 URL，哈希=PyPI 元数据 digest，
    ' 全部实下载复核）。wheel 本质是 zip：解压进 Lib\site-packages 即完成"安装"
    ' （五件的顶层都只有包目录 + dist-info，无 scripts/data 特殊件，实测确认）。
    Private Const WhlNumpy网址 As String = "https://files.pythonhosted.org/packages/12/33/fefe62073dc8acfd0f2b9ed7c003af2f50aa61555e113e6db02b8f79f145/numpy-2.5.4-cp312-cp312-win_amd64.whl"
    Private Const WhlNumpy字节 As Long = 12573091
    Private Const WhlNumpy哈希 As String = "a7b1b6353e36a7e50de2973a38d705c88ee93adcf120673cee7f45a4a3fa223a"
    Private Const WhlOrt网址 As String = "https://files.pythonhosted.org/packages/4f/88/8ec9db1a4d126bb8b758992beb40d1249df171917d75f44a327eb5f20dda/onnxruntime-1.27.0-cp312-cp312-win_amd64.whl"
    Private Const WhlOrt字节 As Long = 13358769
    Private Const WhlOrt哈希 As String = "20c321cf187ba496e648acf6b4cf90b4d398b0d17c2a77fdaeba365b908cc1c1"
    Private Const WhlSherpa网址 As String = "https://files.pythonhosted.org/packages/61/31/1cac1c087f82f26cb57c76ebf62457771a49e3707e8af3395cd9fa6ee8ce/sherpa_onnx-2.0.1-cp312-cp312-win_amd64.whl"
    Private Const WhlSherpa字节 As Long = 2178304
    Private Const WhlSherpa哈希 As String = "9f25448a1af23a013064264b9e2241e0c5d0e1941aeb82dafa969520c694efb9"
    Private Const WhlOv网址 As String = "https://files.pythonhosted.org/packages/d0/44/66a61b7cfccea1dfa20e95a04b4157f07a0e4dc3f7e894b22a92abb8822b/openvino-2026.4.1-22982-cp312-cp312-win_amd64.whl"
    Private Const WhlOv字节 As Long = 84964415
    Private Const WhlOv哈希 As String = "4e04316abff1b99e29b8cbd38deaef9bde4739eba216d982d4b3981e456ecd87"
    Private Const WhlOvTel网址 As String = "https://files.pythonhosted.org/packages/3b/ac/5ab0ca0aa269ad3c73f7bfc3801b10e5f56f75a31bf68c1ae8bd51cf70a4/openvino_telemetry-2025.2.0-py3-none-any.whl"
    Private Const WhlOvTel字节 As Long = 25227
    Private Const WhlOvTel哈希 As String = "bcb667e83a44f202ecf4cfa49281715c6d7e21499daec04ff853b7f964833599"

    ' 母仓可选加速源（NPU 整包两 zip，2026-11-13 打包实测）：配置了就优先用（少 11 个
    ' 请求），失败自动回落上面的公共源链。qwen3_ort.py 不下载——随 exe 部署复制进包。
    Private Const Npu模型Zip字节 As Long = 841945914
    Private Const Npu模型Zip哈希 As String = "1d8d4ae81e30f0d22b6d1f9f0a7a9f2320aea205f6e6ce23eceef9644e3d1e65"
    Private Const NpuPythonZip字节 As Long = 147339686
    Private Const NpuPythonZip哈希 As String = "45e5814a4d9efa307cde73d667a125a1fd466575245907023c96d73dcd5af9e2"

    ''' <summary>清单里的一项。源列表按顺序回退：前一个下载/校验失败才试下一个。
    ''' 覆盖前缀非空 = 整包 zip 项：它成功落位后，相对路径以该前缀开头的散件全部跳过
    ''' （母仓加速命中时不再重复下载公共源散件）。</summary>
    Friend NotInheritable Class 包文件项
        Friend 相对路径 As String
        Friend 解压到 As String = String.Empty
        Friend 覆盖前缀 As String = String.Empty
        Friend 源列表 As New List(Of 包源)
    End Class

    Friend NotInheritable Class 包源
        Friend Sub New(网址 As String, 字节 As Long, 哈希 As String)
            Me.网址 = 网址
            Me.字节 = 字节
            Me.哈希 = 哈希
        End Sub

        Friend 网址 As String
        Friend 字节 As Long
        Friend 哈希 As String
    End Class

    ''' <summary>按引擎选择模型与运行时，并提供包内 Python 环境。</summary>
    Friend Shared Function 组清单(类型 As 包类型) As List(Of 包文件项)
        If 类型 = 包类型.Npu Then Return 组Npu清单()
        Return 组Gpu清单()
    End Function

    Private Shared Function 组Gpu清单() As List(Of 包文件项)
        Dim 清单 As New List(Of 包文件项)

        Dim 主 As New 包文件项 With {
            .相对路径 = "model/Qwen3-ASR-0.6B-Q8_0.gguf"}
        主.源列表.Add(New 包源(模型仓官方 & "/Qwen3-ASR-0.6B-Q8_0.gguf", 主权重字节, 主权重哈希))
        主.源列表.Add(New 包源(模型仓镜像 & "/Qwen3-ASR-0.6B-Q8_0.gguf", 主权重字节, 主权重哈希))
        清单.Add(主)

        Dim 投影 As New 包文件项 With {
            .相对路径 = "model/mmproj-Qwen3-ASR-0.6B-Q8_0.gguf"}
        投影.源列表.Add(New 包源(模型仓官方 & "/mmproj-Qwen3-ASR-0.6B-Q8_0.gguf", 投影字节, 投影哈希))
        投影.源列表.Add(New 包源(模型仓镜像 & "/mmproj-Qwen3-ASR-0.6B-Q8_0.gguf", 投影字节, 投影哈希))
        清单.Add(投影)

        Dim 运行 As New 包文件项 With {
            .相对路径 = "runtime.zip",
            .解压到 = "runtime/vulkan"}
        If 母仓地址 <> "" Then
            运行.源列表.Add(New 包源(母仓地址 & "/runtime-vulkan.zip", 母仓运行时字节, 母仓运行时哈希))
        End If
        运行.源列表.Add(New 包源(运行时回退网址, 运行时回退字节, 运行时回退哈希))
        清单.Add(运行)

        If 母仓地址 <> "" Then
            Dim py As New 包文件项 With {
                .相对路径 = "python.zip", .解压到 = "runtime/python", .覆盖前缀 = "runtime/python/"}
            py.源列表.Add(New 包源(母仓地址 & "/python.zip", 母仓Python字节, 母仓Python哈希))
            清单.Add(py)
        End If
        添加Python底座(清单)
        Return 清单
    End Function

    ''' <summary>NPU 清单（全公共源，docs/157 §六）：
    ''' 模型六件直链 sherpa 官方 HF 仓（hf-mirror 兜底）；运行时 = python.org embeddable
    ''' + PyPI 五个 wheel（解压进 Lib\site-packages）+ ._pth 配置（下载完成后写）。
    ''' 母仓配置了的话，两 zip 作为可选优先源排在前面（少 11 个请求），失败自动回落。
    ''' qwen3_ort.py 不在清单里——随 exe 部署，组装阶段复制进包。</summary>
    Private Shared Function 组Npu清单() As List(Of 包文件项)
        Dim 清单 As New List(Of 包文件项)

        ' 母仓可选加速：整包两 zip。命中即跳过对应的公共源散件（见 下载包Async 的
        ' 覆盖跳过逻辑），失败则散件照下。
        If 母仓地址 <> "" Then
            Dim 模型zip As New 包文件项 With {
                .相对路径 = "npu-model.zip", .解压到 = "model", .覆盖前缀 = "model/"}
            模型zip.源列表.Add(New 包源(母仓地址 & "/npu-model.zip", Npu模型Zip字节, Npu模型Zip哈希))
            清单.Add(模型zip)
            Dim pyzip As New 包文件项 With {
                .相对路径 = "npu-python.zip", .解压到 = "runtime/python", .覆盖前缀 = "runtime/python/"}
            pyzip.源列表.Add(New 包源(母仓地址 & "/npu-python.zip", NpuPythonZip字节, NpuPythonZip哈希))
            清单.Add(pyzip)
        End If

        ' 模型六件（官方仓 + 镜像逐源回退）。元组元素用 ItemN 访问（VB 的 For Each
        ' 不支持 C# 式解构声明）。
        Dim 模型件 = {
            ("model/conv_frontend.onnx", "conv_frontend.onnx", NpuConv字节, NpuConv哈希),
            ("model/encoder.int8.onnx", "encoder.int8.onnx", NpuEnc字节, NpuEnc哈希),
            ("model/decoder.int8.onnx", "decoder.int8.onnx", NpuDec字节, NpuDec哈希),
            ("model/tokenizer/vocab.json", "tokenizer/vocab.json", NpuVocab字节, NpuVocab哈希),
            ("model/tokenizer/merges.txt", "tokenizer/merges.txt", NpuMerges字节, NpuMerges哈希),
            ("model/tokenizer/tokenizer_config.json", "tokenizer/tokenizer_config.json", NpuTokCfg字节, NpuTokCfg哈希)}
        For Each 件 In 模型件
            Dim 项 As New 包文件项 With {.相对路径 = 件.Item1}
            项.源列表.Add(New 包源(Npu模型仓官方 & "/" & 件.Item2, 件.Item3, 件.Item4))
            项.源列表.Add(New 包源(Npu模型仓镜像 & "/" & 件.Item2, 件.Item3, 件.Item4))
            清单.Add(项)
        Next

        添加Python底座(清单)
        添加PythonWheel(清单, "onnxruntime.whl", WhlOrt网址, WhlOrt字节, WhlOrt哈希)
        添加PythonWheel(清单, "sherpa_onnx.whl", WhlSherpa网址, WhlSherpa字节, WhlSherpa哈希)
        添加PythonWheel(清单, "openvino.whl", WhlOv网址, WhlOv字节, WhlOv哈希)
        添加PythonWheel(清单, "openvino_telemetry.whl", WhlOvTel网址, WhlOvTel字节, WhlOvTel哈希)
        Return 清单
    End Function

    Private Shared Sub 添加Python底座(清单 As List(Of 包文件项))
        Dim 项 As New 包文件项 With {.相对路径 = "python-embed.zip", .解压到 = "runtime/python"}
        项.源列表.Add(New 包源(Embed网址, Embed字节, Embed哈希))
        清单.Add(项)
        添加PythonWheel(清单, "numpy.whl", WhlNumpy网址, WhlNumpy字节, WhlNumpy哈希)
    End Sub

    Private Shared Sub 添加PythonWheel(清单 As List(Of 包文件项), 文件名 As String,
                                      网址 As String, 字节 As Long, 哈希 As String)
        Dim 项 As New 包文件项 With {.相对路径 = 文件名, .解压到 = "runtime/python/Lib/site-packages"}
        项.源列表.Add(New 包源(网址, 字节, 哈希))
        清单.Add(项)
    End Sub

    ''' <summary>整体下载进度：总百分比按「全部清单字节的已完成量」计（跨文件累计），
    ''' 文本包含字节数和下载阶段，由界面配合百分比显示。</summary>
    Friend NotInheritable Class 下载进度
        Friend 总百分比 As Integer
        Friend 文本 As String
        Friend 速度字节每秒 As Double
    End Class

    ''' <summary>下载目标白名单：不存在的目录 / 空目录 / 已有本程序哨兵的目录。
    ''' 返回空串表示放行，否则是给用户看的原因。</summary>
    Private Shared Function 校验下载目标(根 As String) As String
        If String.IsNullOrWhiteSpace(根) Then Return "下载目录为空。"
        Dim 全 = Path.GetFullPath(根).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
        Dim 盘根 = Path.GetPathRoot(全)
        If 盘根 IsNot Nothing AndAlso
           String.Equals(全, 盘根.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                         StringComparison.OrdinalIgnoreCase) Then
            Return "不能直接下载到盘符根目录，请选一个专用子目录（例如 D:\AI字幕包）。"
        End If
        Dim 用户目录 = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)
        If Not String.IsNullOrWhiteSpace(用户目录) AndAlso
           String.Equals(全, 用户目录.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                         StringComparison.OrdinalIgnoreCase) Then
            Return "不能把用户主目录当成下载目录，请选一个专用子目录。"
        End If
        If Not Directory.Exists(全) Then Return ""
        If Ai字幕包.有归属哨兵(全) Then Return ""
        Try
            For Each 项 In Directory.EnumerateFileSystemEntries(全)
                Return "这个目录已经有其它文件，且不是本程序创建的 AI 字幕包目录。" &
                       "请换一个空目录，避免把包文件和你的资料混在一起。"
            Next
        Catch ex As Exception
            Return $"目录不可读：{ex.Message}"
        End Try
        Return ""
    End Function

    ''' <summary>下载整包。返回空串=成功并已生成 ai-subtitles.json；否则返回人话错误
    ''' （.part 保留，可再次点击续传）。进度为结构化：总百分比（0-100，跨文件累计）
    ''' + 下载状态文本。类型决定清单与生成清单的引擎（GGUF/llama 或 ONNX/onnx）。
    ''' 两种包都有完整公共源链，母仓只是可选加速（zip 整包优先，散件兜底）。</summary>
    Friend Shared Async Function 下载包Async(目标目录 As String, 类型 As 包类型,
                                              进度 As IProgress(Of 下载进度),
                                              取消 As CancellationToken) As Task(Of String)
        Dim 清单 = 组清单(类型)
        ' 总字节按「实际会走的链」算：母仓 zip 在清单里时，被它覆盖的散件不计入
        ' （zip 失败回落散件时进度会超 100%，钳位处理——比一开始就按双份算诚实）。
        Dim 有模型zip = 清单.Any(Function(x) x.覆盖前缀 = "model/")
        Dim 有Pyzip = 清单.Any(Function(x) x.覆盖前缀 = "runtime/python/")
        Dim 总字节 As Long = 0
        For Each 项 In 清单
            If 项.覆盖前缀 = "" AndAlso
               ((有模型zip AndAlso 项.相对路径.StartsWith("model/", StringComparison.Ordinal)) OrElse
                (有Pyzip AndAlso (项.相对路径.StartsWith("runtime/python/", StringComparison.Ordinal) OrElse
                                  项.相对路径 = "python-embed.zip" OrElse
                                  项.相对路径.EndsWith(".whl", StringComparison.Ordinal)))) Then
                Continue For
            End If
            总字节 += 项.源列表(0).字节
        Next
        Dim 已完成字节 As Long = 0
        Dim 报告 = Sub(已完成 As Long, 文本 As String)
                       Dim pct = CInt(已完成 * 100.0R / Math.Max(1, 总字节))
                       进度.Report(New 下载进度 With {
                           .总百分比 = Math.Min(100, Math.Max(0, pct)),
                           .文本 = $"{已完成 \ 1048576}/{总字节 \ 1048576} MB　{文本}"})
                   End Sub

        Dim 根 = Path.GetFullPath(目标目录)
        ' 归属与误删防护的第一道闸：绝不往「盘符根 / 用户主目录 / 有内容却非本程序建的
        ' 目录」里下载。旧版下载器只看「文件已存在就跳过」，配合删除按钮的
        ' Directory.Delete(True) 可以整目录抹掉用户资料。
        Dim 目标错误 = 校验下载目标(根)
        If 目标错误 <> "" Then Return 目标错误
        ' 先落哨兵再下文件：中途失败/取消后目录仍归本程序所有，删除按钮仍可用。
        If Ai字幕包.有归属哨兵(根) AndAlso Not Ai字幕包.有指定类型归属哨兵(根, 类型.ToString()) Then
            Return "这个目录已用于另一种推理包，请选择独立目录。"
        End If
        If Not Ai字幕包.写归属哨兵(根, 类型.ToString()) Then
            Return $"无法在下载目录写入归属标记（目录只读或没有权限）：{根}"
        End If
        Dim 驱动器 As New DriveInfo(Path.GetPathRoot(根))
        If 驱动器.AvailableFreeSpace < 总字节 + (总字节 \ 10) Then
            Return $"磁盘空间不足：需要约 {总字节 \ 1048576} MB（含余量），" &
                   $"{驱动器.Name} 只剩 {驱动器.AvailableFreeSpace \ 1048576} MB"
        End If

        ' 母仓 zip 命中后覆盖散件：记录哪些前缀已被整包满足。
        Dim 已覆盖 As New HashSet(Of String)(StringComparer.Ordinal)
        For Each 项 In 清单
            ' 被整包覆盖的散件：整包成功就跳过；整包失败（不在 已覆盖）则照下。
            If 项.覆盖前缀 = "" Then
                Dim 被覆盖 = False
                For Each 前缀 In 已覆盖
                    If 项.相对路径.StartsWith(前缀, StringComparison.Ordinal) OrElse
                       (前缀 = "runtime/python/" AndAlso
                        (项.相对路径 = "python-embed.zip" OrElse
                         项.相对路径.EndsWith(".whl", StringComparison.Ordinal))) Then
                        被覆盖 = True
                        Exit For
                    End If
                Next
                If 被覆盖 Then Continue For
            End If

            Dim 目标 = Path.Combine(根, 项.相对路径.Replace("/"c, Path.DirectorySeparatorChar))
            Directory.CreateDirectory(Path.GetDirectoryName(目标))

            ' 只下载未通过体积和校验哈希检查的文件，允许复用原先来自任一源的缓存。
            Dim 可复用 = False
            If File.Exists(目标) Then
                Dim 长度 = New FileInfo(目标).Length
                If 项.源列表.Any(Function(源) 源.字节 = 长度) Then
                    Dim 哈希 = Await Task.Run(Function() 计算哈希(目标), 取消)
                    可复用 = 项.源列表.Any(Function(源) 源.字节 = 长度 AndAlso
                        String.Equals(源.哈希, 哈希, StringComparison.OrdinalIgnoreCase))
                End If
            End If
            If 可复用 Then
                报告(已完成字节 + 项.源列表(0).字节, $"{项.相对路径} 已校验，复用文件")
            Else
                Dim 错误 = Await 下载文件Async(目标, 项, 已完成字节, 总字节, 进度, 取消)
                If 错误 <> "" Then
                    If 项.覆盖前缀 <> "" Then
                        报告(已完成字节, $"{项.相对路径} 母仓不可用，改用公共源散件…")
                        Continue For
                    End If
                    Return 错误
                End If
            End If
            已完成字节 += 项.源列表(0).字节
            If 项.覆盖前缀 <> "" Then 已覆盖.Add(项.覆盖前缀)

            ' 补装 Python 时可以复用已经解压好的 Vulkan 运行时，正在运行的 DLL 无需重写。
            Dim 保留Vulkan = 可复用 AndAlso 项.解压到 = "runtime/vulkan" AndAlso
                File.Exists(Path.Combine(根, "runtime", "vulkan", "llama-server.exe"))
            If 项.解压到 <> "" AndAlso Not 保留Vulkan Then
                Dim 解压目标 = Path.Combine(根, 项.解压到)
                报告(已完成字节, $"解压 {项.相对路径} → {项.解压到} …")
                Await Task.Run(
                    Sub() ZipFile.ExtractToDirectory(目标, 解压目标, True), 取消)
            End If
        Next
        报告(总字节, "组装扩展包（worker / 清单）…")

        ' worker：随播放器部署的副本复制进包（包契约要求 worker 在包根）。
        Dim 本体worker = Path.Combine(AppContext.BaseDirectory, "ai-subtitles", "asr_worker.py")
        If Not File.Exists(本体worker) Then
            Return "随播放器部署的 ai-subtitles\asr_worker.py 缺失（安装不完整），无法生成扩展包"
        End If
        File.Copy(本体worker, Path.Combine(根, "asr_worker.py"), True)

        ' 第三方许可/来源与处理声明：随播放器部署，包根必须有一份；缺了就说明安装不完整。
        Dim 本体声明 = Path.Combine(AppContext.BaseDirectory, "ai-subtitles", "AI字幕包声明.txt")
        If Not File.Exists(本体声明) Then
            Return "随播放器部署的 ai-subtitles\AI字幕包声明.txt 缺失（安装不完整），无法生成扩展包"
        End If
        File.Copy(本体声明, Path.Combine(根, "AI字幕包声明.txt"), True)

        ' 包内第一方 worker/适配代码仍按 MIT；没有 LICENSE.txt 就谈不上保留版权声明。
        Dim 本体许可 = Path.Combine(AppContext.BaseDirectory, "LICENSE.txt")
        If Not File.Exists(本体许可) Then
            Return "随播放器部署的 LICENSE.txt 缺失（安装不完整），无法生成扩展包"
        End If
        File.Copy(本体许可, Path.Combine(根, "LICENSE.txt"), True)

        ' 第三方许可原文：随播放器部署在 licenses\ai，整目录复制进包；
        ' 缺目录即视为安装不完整，不在包内留下指向不存在的路径。
        Dim 许可源 = Path.Combine(AppContext.BaseDirectory, "licenses", "ai")
        If Not Directory.Exists(许可源) Then
            Return "随播放器部署的 licenses\ai 缺失（安装不完整），无法生成扩展包"
        End If
        Dim 许可文件列表 = Directory.GetFiles(许可源, "*", SearchOption.AllDirectories)
        If 许可文件列表.Length = 0 Then
            Return "随播放器部署的 licenses\ai 为空（安装不完整），无法生成扩展包"
        End If
        For Each 许可文件 In 许可文件列表
            Dim 相对 = 许可文件.Substring(许可源.Length).TrimStart(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            Dim 目标 = Path.Combine(根, "licenses", "ai", 相对)
            Directory.CreateDirectory(Path.GetDirectoryName(目标))
            File.Copy(许可文件, 目标, True)
        Next

        If 类型 = 包类型.Npu Then
            ' qwen3_ort.py：我们自己的推理实现，随 exe 部署（与 worker 同一套路），
            ' 复制进包根——worker 用 Path(__file__).with_name 找它。
            Dim 本体ort = Path.Combine(AppContext.BaseDirectory, "ai-subtitles", "qwen3_ort.py")
            If Not File.Exists(本体ort) Then
                Return "随播放器部署的 ai-subtitles\qwen3_ort.py 缺失（安装不完整），无法生成 NPU 扩展包"
            End If
            File.Copy(本体ort, Path.Combine(根, "qwen3_ort.py"), True)
        End If

        Const 清单Python As String = "runtime/python/python.exe"
        Dim py根 = Path.Combine(根, "runtime", "python")
        If Not File.Exists(Path.Combine(根, 清单Python)) OrElse
           Not File.Exists(Path.Combine(py根, "Lib", "site-packages", "numpy", "__init__.py")) Then
            Return "扩展包缺少 Python 或 NumPy，未生成新的扩展包清单。"
        End If
        File.WriteAllText(Path.Combine(py根, "python312._pth"),
            "python312.zip" & vbCrLf & "." & vbCrLf & "Lib\site-packages" & vbCrLf & "import site" & vbCrLf,
            Text.Encoding.ASCII)
        Dim 清单内容 As String
        If 类型 = 包类型.Npu Then
            ' NPU = onnx 引擎：无 runtime 字段（推理在包内 Python 的 OpenVINO 里跑，
            ' 不是 llama-server 子进程）。qwen3_ort.py 已随清单下载落包根。
            清单内容 =
                "{" & vbCrLf &
                "  ""version"": 3," & vbCrLf &
                "  ""name"": ""Qwen3-ASR-0.6B（ONNX int8 · NPU 官方下载包）""," & vbCrLf &
                "  ""engine"": ""onnx""," & vbCrLf &
                "  ""worker"": ""asr_worker.py""," & vbCrLf &
                $"  ""python"": ""{清单Python}""," & vbCrLf &
                "  ""model"": ""model""," & vbCrLf &
                "  ""window"": 5.0," & vbCrLf &
                "  ""step"": 5.0," & vbCrLf &
                "  ""notice"": ""AI字幕包声明.txt""," & vbCrLf &
                "  ""license"": ""licenses/ai""" & vbCrLf &
                "}"
        Else
            清单内容 =
                "{" & vbCrLf &
                "  ""version"": 3," & vbCrLf &
                "  ""name"": ""Qwen3-ASR-0.6B（GGUF Q8_0 · 官方下载包）""," & vbCrLf &
                "  ""engine"": ""llama""," & vbCrLf &
                "  ""worker"": ""asr_worker.py""," & vbCrLf &
                $"  ""python"": ""{清单Python}""," & vbCrLf &
                "  ""model"": ""model""," & vbCrLf &
                "  ""runtime"": ""runtime/vulkan""," & vbCrLf &
                "  ""window"": 5.0," & vbCrLf &
                "  ""step"": 5.0," & vbCrLf &
                "  ""notice"": ""AI字幕包声明.txt""," & vbCrLf &
                "  ""license"": ""licenses/ai""" & vbCrLf &
                "}"
        End If
        File.WriteAllText(Path.Combine(根, Ai字幕包.清单文件名), 清单内容, Text.Encoding.UTF8)
        Return ""
    End Function

    ''' <summary>下载单个文件：逐源尝试（Range 续传 .part → 校验字节与 SHA256 → 落位）。
    ''' 一切失败保留 .part 供续传；取消原样抛出。进度含整体百分比（基数=此前文件累计）。</summary>
    Friend Shared Async Function 下载文件Async(目标 As String, 项 As 包文件项, 基数 As Long,
                                                总字节 As Long, 进度 As IProgress(Of 下载进度),
                                                取消 As CancellationToken,
                                                Optional 客户端 As HttpClient = Nothing) As Task(Of String)
        Dim 错误 = String.Empty
        For Each 源 In 项.源列表
            取消.ThrowIfCancellationRequested()
            Using 下载 = If(客户端 Is Nothing, New LakeUI.DownloadFile(), New LakeUI.DownloadFile(客户端))
                下载.Url = 源.网址
                下载.SavePath = 目标
                下载.ExpectedLength = 源.字节
                下载.ExpectedSha256 = 源.哈希
                Dim 报告进度 As EventHandler(Of LakeUI.DownloadProgressEventArgs) =
                    Sub(sender, e)
                        If 下载.State <> LakeUI.DownloadState.Running Then Return
                        报告单文件进度(进度, 基数 + e.BytesDownloaded, 总字节,
                            Path.GetFileName(项.相对路径), e.SpeedBytesPerSecond)
                    End Sub
                AddHandler 下载.ProgressChanged, 报告进度
                Try
                    Await 下载.StartAsync(取消)
                Finally
                    RemoveHandler 下载.ProgressChanged, 报告进度
                End Try
                取消.ThrowIfCancellationRequested()
                If 下载.State = LakeUI.DownloadState.Completed Then
                    报告单文件进度(进度, 基数 + 源.字节, 总字节, Path.GetFileName(项.相对路径) & " 校验完成", 0)
                    Return ""
                End If
                错误 = If(下载.LastError?.Message, "下载未完成，部分数据已保留")
            End Using
        Next
        Return $"{项.相对路径}：{错误}"
    End Function

    Private Shared Sub 报告单文件进度(进度 As IProgress(Of 下载进度), 已完成 As Long,
                                    总字节 As Long, 文本 As String, 速度 As Double)
        Dim 百分比 = CInt(Math.Clamp(已完成 * 100.0R / Math.Max(1L, 总字节), 0, 100))
        进度?.Report(New 下载进度 With {
            .总百分比 = 百分比,
            .文本 = $"{已完成 \ 1048576}/{总字节 \ 1048576} MB　{文本}",
            .速度字节每秒 = Math.Max(0, 速度)})
    End Sub

    Private Shared Function 计算哈希(路径 As String) As String
        Using 哈希器 As SHA256 = SHA256.Create()
            Using 流 = New FileStream(路径, FileMode.Open, FileAccess.Read,
                                      FileShare.Read, 1048576, FileOptions.SequentialScan)
                Return BitConverter.ToString(哈希器.ComputeHash(流)).Replace("-", "").ToLowerInvariant()
            End Using
        End Using
    End Function
End Class

