Imports System.Globalization
Imports System.IO
Imports System.IO.Compression
Imports System.Linq
Imports System.Net.Http
Imports System.Security.Cryptography
Imports System.Text
Imports System.Text.Json
Imports System.Text.Json.Serialization
Imports System.Threading
Imports System.Threading.Tasks

''' <summary>
''' AI 字幕包下载器。GPU+CPU 包采用 GGUF / llama.cpp Vulkan，NPU 包采用 ONNX / OpenVINO。
''' 每次下载从公开模型仓读取 main 当前提交及文件大小和校验值，再按该提交下载全部模型文件。
''' 运行时使用经过验证的固定版本，支持磁盘空间预检、断点续传与逐源回退。
''' 生成包 = 目标目录 + ai-subtitles.json(v3) + asr_worker.py + 模型 + 运行时，
''' 下载完成即通过 Ai字幕包.载入 校验并启用。NPU 包额外带 qwen3_ort.py（随 exe 部署）。
''' </summary>
Friend NotInheritable Class Ai字幕包下载器

    ''' <summary>包类型决定下载清单与引擎，与设置页「包类型」下拉一一对应。</summary>
    Friend Enum 包类型
        GpuCpu
        Npu
    End Enum

    ''' <summary>可选的运行时加速仓，填 resolve 根地址（不带尾斜杠）。运行时 zip 失败时回落公共源。</summary>
    Public Const 母仓地址 As String = ""

    Private Const Gpu模型仓 As String = "ggml-org/Qwen3-ASR-0.6B-GGUF"
    Private Const Npu模型仓 As String = "csukuangfj2/sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25"
    Private Shared ReadOnly 模型站点 As String() = {"https://huggingface.co", "https://hf-mirror.com"}
    Private Shared ReadOnly 元数据客户端 As New HttpClient With {
        .Timeout = TimeSpan.FromSeconds(20),
        .MaxResponseContentBufferSize = 4L * 1024 * 1024}

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

    ' NPU 自建仓 Python 运行时压缩包。
    Private Const NpuPythonZip字节 As Long = 147339686
    Private Const NpuPythonZip哈希 As String = "45e5814a4d9efa307cde73d667a125a1fd466575245907023c96d73dcd5af9e2"

    Friend NotInheritable Class 包文件项
        Friend 相对路径 As String
        Friend 解压到 As String = String.Empty
        Friend 完整Python As Boolean
        Friend 来自模型仓 As Boolean
        Friend 源列表 As New List(Of 包源)
    End Class

    Friend NotInheritable Class 包源
        Friend Sub New(网址 As String, 字节 As Long, 哈希 As String, Optional 是Git对象 As Boolean = False)
            Me.网址 = 网址
            Me.字节 = 字节
            Me.哈希 = 哈希
            Me.是Git对象 = 是Git对象
        End Sub

        Friend ReadOnly 网址 As String
        Friend ReadOnly 字节 As Long
        Friend ReadOnly 哈希 As String
        Friend ReadOnly 是Git对象 As Boolean
    End Class

    Private NotInheritable Class 模型仓元数据
        <JsonPropertyName("sha")>
        Public Property 提交 As String
        <JsonPropertyName("siblings")>
        Public Property 文件列表 As 模型文件元数据()
    End Class

    Private NotInheritable Class 模型文件元数据
        <JsonPropertyName("rfilename")>
        Public Property 路径 As String
        <JsonPropertyName("size")>
        Public Property 字节 As Long
        <JsonPropertyName("blobId")>
        Public Property Git哈希 As String
        <JsonPropertyName("lfs")>
        Public Property Lfs As 大文件元数据
    End Class

    Private NotInheritable Class 大文件元数据
        <JsonPropertyName("size")>
        Public Property 字节 As Long
        <JsonPropertyName("sha256")>
        Public Property 哈希 As String
    End Class

    Friend Shared Async Function 组下载清单Async(类型 As 包类型, 取消 As CancellationToken,
                                                  Optional 客户端 As HttpClient = Nothing) As Task(Of List(Of 包文件项))
        Dim 仓库 = If(类型 = 包类型.Npu, Npu模型仓, Gpu模型仓)
        Dim 必需文件 = If(类型 = 包类型.Npu,
            {"conv_frontend.onnx", "encoder.int8.onnx", "decoder.int8.onnx",
             "tokenizer/vocab.json", "tokenizer/merges.txt", "tokenizer/tokenizer_config.json"},
            {"Qwen3-ASR-0.6B-Q8_0.gguf", "mmproj-Qwen3-ASR-0.6B-Q8_0.gguf"})
        Dim 错误 As New List(Of String)
        For Each 站点 In 模型站点
            取消.ThrowIfCancellationRequested()
            Dim 主机名 = New Uri(站点).Host
            Try
                Dim 内容 = Await If(客户端, 元数据客户端).
                    GetStringAsync($"{站点}/api/models/{仓库}/revision/main?blobs=true", 取消).ConfigureAwait(False)
                Dim 元数据 = JsonSerializer.Deserialize(Of 模型仓元数据)(内容)
                Dim 清单 = 组模型清单(元数据, 必需文件, 仓库, 站点)
                取消.ThrowIfCancellationRequested()
                清单.AddRange(组运行时清单(类型))
                Return 清单
            Catch ex As OperationCanceledException When Not 取消.IsCancellationRequested
                错误.Add($"{主机名} 请求超时")
            Catch ex As Exception When TypeOf ex Is HttpRequestException OrElse TypeOf ex Is JsonException OrElse
                                       TypeOf ex Is InvalidDataException
                错误.Add($"{主机名}：{ex.Message}")
            End Try
        Next
        取消.ThrowIfCancellationRequested()
        Throw New IOException("无法读取上游模型清单。" & String.Join("；", 错误))
    End Function

    Private Shared Function 组模型清单(元数据 As 模型仓元数据, 必需文件 As String(), 仓库 As String,
                                       元数据站点 As String) As List(Of 包文件项)
        If Not 是十六进制(元数据?.提交, 40) OrElse 元数据.文件列表 Is Nothing Then
            Throw New InvalidDataException("缺少有效的提交编号或文件列表。")
        End If
        Dim 清单 As New List(Of 包文件项)
        ' 镜像若提供了本次元数据，先用它，避免每个文件重复等待官方站点超时。
        Dim 顺序 = {元数据站点, 模型站点.First(Function(站点) 站点 <> 元数据站点)}
        For Each 路径 In 必需文件
            Dim 文件 = 元数据.文件列表.FirstOrDefault(Function(候选) String.Equals(候选?.路径, 路径, StringComparison.Ordinal))
            If 文件 Is Nothing Then Throw New InvalidDataException($"模型仓缺少 {路径}。")
            Dim 是Git对象 = 文件.Lfs Is Nothing
            Dim 哈希 = If(是Git对象, 文件.Git哈希, 文件.Lfs.哈希)
            If 文件.字节 <= 0 OrElse Not 是十六进制(哈希, If(是Git对象, 40, 64)) OrElse
               (Not 是Git对象 AndAlso 文件.Lfs.字节 <> 文件.字节) Then
                Throw New InvalidDataException($"{路径} 的文件大小或校验信息无效。")
            End If
            Dim 项 As New 包文件项 With {.相对路径 = "model/" & 路径, .来自模型仓 = True}
            For Each 站点 In 顺序
                项.源列表.Add(New 包源($"{站点}/{仓库}/resolve/{元数据.提交}/{路径}", 文件.字节, 哈希, 是Git对象))
            Next
            清单.Add(项)
        Next
        Return 清单
    End Function

    Private Shared Function 是十六进制(值 As String, 长度 As Integer) As Boolean
        Return 值 IsNot Nothing AndAlso 值.Length = 长度 AndAlso 值.All(AddressOf Uri.IsHexDigit)
    End Function

    Friend Shared Function 组运行时清单(类型 As 包类型) As List(Of 包文件项)
        Dim 清单 As New List(Of 包文件项)
        Dim 是Npu = 类型 = 包类型.Npu
        If Not 是Npu Then
            Dim 运行 As New 包文件项 With {.相对路径 = "runtime.zip", .解压到 = "runtime/vulkan"}
            If 母仓地址 <> "" Then
                运行.源列表.Add(New 包源(母仓地址 & "/runtime-vulkan.zip", 母仓运行时字节, 母仓运行时哈希))
            End If
            运行.源列表.Add(New 包源(运行时回退网址, 运行时回退字节, 运行时回退哈希))
            清单.Add(运行)
        End If
        If 母仓地址 <> "" Then
            Dim py As New 包文件项 With {
                .相对路径 = If(是Npu, "npu-python.zip", "python.zip"),
                .解压到 = "runtime/python", .完整Python = True}
            py.源列表.Add(New 包源(母仓地址 & "/" & py.相对路径, If(是Npu, NpuPythonZip字节, 母仓Python字节),
                If(是Npu, NpuPythonZip哈希, 母仓Python哈希)))
            清单.Add(py)
        End If
        添加Python底座(清单)
        If 是Npu Then
            添加PythonWheel(清单, "onnxruntime.whl", WhlOrt网址, WhlOrt字节, WhlOrt哈希)
            添加PythonWheel(清单, "sherpa_onnx.whl", WhlSherpa网址, WhlSherpa字节, WhlSherpa哈希)
            添加PythonWheel(清单, "openvino.whl", WhlOv网址, WhlOv字节, WhlOv哈希)
            添加PythonWheel(清单, "openvino_telemetry.whl", WhlOvTel网址, WhlOvTel字节, WhlOvTel哈希)
        End If
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

    ''' <summary>按上游快照下载整包，再写入 ai-subtitles.json。返回空串表示成功。</summary>
    Friend Shared Async Function 下载包Async(目标目录 As String, 类型 As 包类型,
                                              进度 As IProgress(Of 下载进度),
                                              取消 As CancellationToken) As Task(Of String)
        Dim 根 = Path.GetFullPath(目标目录)
        ' 归属与误删防护的第一道闸：绝不往「盘符根 / 用户主目录 / 有内容却非本程序建的
        ' 目录」里下载。旧版下载器只看「文件已存在就跳过」，配合删除按钮的
        ' Directory.Delete(True) 可以整目录抹掉用户资料。
        Dim 目标错误 = 校验下载目标(根)
        If 目标错误 <> "" Then Return 目标错误
        If Ai字幕包.有归属哨兵(根) AndAlso Not Ai字幕包.有指定类型归属哨兵(根, 类型.ToString()) Then
            Return "这个目录已用于另一种推理包，请选择独立目录。"
        End If
        取消.ThrowIfCancellationRequested()
        ' 先落哨兵再下文件，中途取消后仍可在同一目录继续。
        If Not Ai字幕包.写归属哨兵(根, 类型.ToString()) Then
            Return $"无法在下载目录写入归属标记（目录只读或没有权限）：{根}"
        End If
        进度?.Report(New 下载进度 With {.文本 = "正在读取上游模型清单…"})
        Dim 清单 As List(Of 包文件项)
        Try
            清单 = Await 组下载清单Async(类型, 取消)
        Catch ex As IOException
            Return ex.Message
        End Try

        Dim 有整包Python = 清单.Any(Function(项) 项.完整Python)
        Dim 总字节 = 清单.Where(Function(项) Not 有整包Python OrElse Not 是Python散件(项)).
            Sum(Function(项) 项.源列表(0).字节)
        Dim 已完成字节 As Long = 0
        Dim 报告 = Sub(已完成 As Long, 文本 As String) 报告单文件进度(进度, 已完成, 总字节, 文本, 0)
        Dim 驱动器 As New DriveInfo(Path.GetPathRoot(根))
        If 驱动器.AvailableFreeSpace < 总字节 + (总字节 \ 10) Then
            Return $"磁盘空间不足：需要约 {总字节 \ 1048576} MB（含余量），" &
                   $"{驱动器.Name} 只剩 {驱动器.AvailableFreeSpace \ 1048576} MB"
        End If

        Dim Python已解包 As Boolean
        For Each 项 In 清单
            取消.ThrowIfCancellationRequested()
            If Python已解包 AndAlso 是Python散件(项) Then Continue For

            Dim 目标 = Path.Combine(根, 项.相对路径.Replace("/"c, Path.DirectorySeparatorChar))
            Directory.CreateDirectory(Path.GetDirectoryName(目标))
            Dim 可复用 = Await 可复用文件Async(目标, 项, 取消)
            If 可复用 Then
                报告(已完成字节 + 项.源列表(0).字节, $"{项.相对路径} 已校验，复用文件")
            Else
                Dim 错误 = Await 下载文件Async(目标, 项, 已完成字节, 总字节, 进度, 取消)
                If 错误 <> "" Then
                    If 项.完整Python Then
                        总字节 += 清单.Where(AddressOf 是Python散件).Sum(Function(散件) 散件.源列表(0).字节) - 项.源列表(0).字节
                        报告(已完成字节, $"{项.相对路径} 母仓不可用，改用公共源散件…")
                        Continue For
                    End If
                    Return 错误
                End If
            End If
            已完成字节 += 项.源列表(0).字节

            ' 补装 Python 时可以复用已经解压好的 Vulkan 运行时，正在运行的 DLL 无需重写。
            Dim 保留Vulkan = 可复用 AndAlso 项.解压到 = "runtime/vulkan" AndAlso
                File.Exists(Path.Combine(根, "runtime", "vulkan", "llama-server.exe"))
            If 项.解压到 <> "" AndAlso Not 保留Vulkan Then
                Dim 解压目标 = Path.Combine(根, 项.解压到)
                报告(已完成字节, $"解压 {项.相对路径} → {项.解压到} …")
                Await Task.Run(
                    Sub() ZipFile.ExtractToDirectory(目标, 解压目标, True), 取消)
            End If
            If 项.完整Python Then Python已解包 = True
        Next
        取消.ThrowIfCancellationRequested()
        报告(总字节, "组装扩展包（worker / 清单）…")

        Ai字幕包资源.写入扩展包(根, 类型 = 包类型.Npu)

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
            ' 不是 llama-server 子进程）。qwen3_ort.py 由播放器写入包根。
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

    Private Shared Function 是Python散件(项 As 包文件项) As Boolean
        Return 项.相对路径 = "python-embed.zip" OrElse 项.解压到 = "runtime/python/Lib/site-packages"
    End Function

    ''' <summary>逐源下载单个文件，校验完成后才落位。进度基数为此前完成的字节数。</summary>
    Friend Shared Async Function 下载文件Async(目标 As String, 项 As 包文件项, 基数 As Long,
                                                总字节 As Long, 进度 As IProgress(Of 下载进度),
                                                取消 As CancellationToken,
                                                Optional 客户端 As HttpClient = Nothing) As Task(Of String)
        Dim 错误 = String.Empty
        For Each 源 In 项.源列表
            取消.ThrowIfCancellationRequested()
            ' Git blob 校验包含对象头。本地用临时名称接收，校验后再移到正式路径。
            Dim 下载目标 = If(源.是Git对象, 目标 & ".git-download", 目标)
            If 项.来自模型仓 Then Await 准备模型续传Async(目标, 下载目标, 源, 取消)
            Using 下载 = If(客户端 Is Nothing, New LakeUI.DownloadFile(), New LakeUI.DownloadFile(客户端))
                下载.Url = 源.网址
                下载.SavePath = 下载目标
                下载.ExpectedLength = 源.字节
                If Not 源.是Git对象 Then 下载.ExpectedSha256 = 源.哈希
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
                    If 源.是Git对象 Then
                        If Not Await 文件与源相符Async(下载目标, 源, 取消) Then
                            File.Delete(下载目标)
                            错误 = "Git blob 校验失败。"
                            Continue For
                        End If
                        取消.ThrowIfCancellationRequested()
                        File.Move(下载目标, 目标, overwrite:=True)
                    End If
                    If 项.来自模型仓 Then File.Delete(目标 & ".part.id")
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

    Private Shared Async Function 准备模型续传Async(目标 As String, 下载目标 As String,
                                                   源 As 包源, 取消 As CancellationToken) As Task
        Dim 标记路径 = 目标 & ".part.id"
        Dim 标识 = 源.字节.ToString(CultureInfo.InvariantCulture) & ":" & 源.哈希.ToLowerInvariant()
        Dim 有标记 = File.Exists(标记路径)
        If 有标记 AndAlso File.ReadAllText(标记路径) = 标识 Then Return

        ' 无标记的旧部分文件只有完整核验通过才可使用。上游内容变化后不能沿用旧字节。
        Dim 部分文件 = 下载目标 & ".part"
        Dim 复用旧件 = Not 有标记 AndAlso File.Exists(部分文件) AndAlso
            Await 文件与源相符Async(部分文件, 源, 取消)
        取消.ThrowIfCancellationRequested()
        If Not 复用旧件 Then File.Delete(部分文件)
        File.Delete(目标 & ".git-download")
        File.Delete(If(源.是Git对象, 目标 & ".part", 目标 & ".git-download.part"))
        File.WriteAllText(标记路径, 标识)
    End Function

    Private Shared Async Function 可复用文件Async(路径 As String, 项 As 包文件项, 取消 As CancellationToken) As Task(Of Boolean)
        If Not File.Exists(路径) Then Return False
        Dim 长度 = New FileInfo(路径).Length
        For Each 源 In 项.源列表.Where(Function(候选) 候选.字节 = 长度).DistinctBy(Function(候选) 候选.哈希,
                                                                                   StringComparer.OrdinalIgnoreCase)
            If Await 文件与源相符Async(路径, 源, 取消).ConfigureAwait(False) Then Return True
        Next
        Return False
    End Function

    Private Shared Async Function 文件与源相符Async(路径 As String, 源 As 包源, 取消 As CancellationToken) As Task(Of Boolean)
        Const 缓冲大小 As Integer = 128 * 1024
        Using 流 As New FileStream(路径, FileMode.Open, FileAccess.Read, FileShare.Read,
                                  缓冲大小, FileOptions.Asynchronous Or FileOptions.SequentialScan)
            If 流.Length <> 源.字节 Then Return False
            Dim 计算值 As Byte()
            If 源.是Git对象 Then
                Using Git哈希 = IncrementalHash.CreateHash(HashAlgorithmName.SHA1)
                    Git哈希.AppendData(Encoding.ASCII.GetBytes("blob " & 源.字节.ToString(CultureInfo.InvariantCulture) & ChrW(0)))
                    Dim 缓冲(缓冲大小 - 1) As Byte
                    Do
                        Dim 读到 = Await 流.ReadAsync(缓冲.AsMemory(), 取消).ConfigureAwait(False)
                        If 读到 = 0 Then Exit Do
                        Git哈希.AppendData(缓冲, 0, 读到)
                    Loop
                    计算值 = Git哈希.GetHashAndReset()
                End Using
            Else
                计算值 = Await SHA256.HashDataAsync(流, 取消).ConfigureAwait(False)
            End If
            Return String.Equals(Convert.ToHexString(计算值), 源.哈希, StringComparison.OrdinalIgnoreCase)
        End Using
    End Function
End Class

