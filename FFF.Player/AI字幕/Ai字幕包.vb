Option Strict On
Option Explicit On

Imports System.Collections.Generic
Imports System.IO
Imports System.Text.Json

''' <summary>
''' AI 字幕扩展包：一个目录 + 一份自描述清单（ai-subtitles.json）。
'''
''' 3FP 本体只认这一个契约，其它一概不管 —— 包里是 Q8_0 还是 Q6_K、模型放包内
''' 还是指向机器上的别处、用哪个 python，全由清单自己说。这样本体的设置界面
''' 只需要"选到哪个目录"这一行，而不用替用户管六个路径。
'''
''' 清单字段（除 worker 外全部可选）：
'''   version      整数，清单版本（当前 1）
'''   name         包显示名，状态行用；缺省取目录名
'''   worker       worker 脚本（相对包根，或绝对路径）【必填】
'''   python       python 可执行；留空 = PATH 上的 python
'''   llamaServer  llama-server.exe；留空 = PATH 上找
'''   servers      可选：后端 → llama-server.exe 的映射（{"cuda": "...", "vulkan": "..."}）。
'''                一个 llama-server 二进制里只编进了它被编译时的那几个后端（本机的
'''                CUDA 构建 --list-devices 只有 CUDA0），所以"换后端"本质是"换二进制"。
'''                某个后端没在这张表里，就回落到 llamaServer —— 纯 CPU 例外：任何构建
'''                都带 CPU 后端，所以不写也能跑（-ngl 0）。
'''   device       包推荐的默认后端（auto/cpu/cuda/vulkan/rocm/sycl）；缺省 = auto
'''   model        主权重 .gguf；留空 = worker 按 --model-dir + 默认量化档自己拼
'''   mmproj       投影权重；留空 = 与主权重同目录的 mmproj-*.gguf
'''   models       可选：档名 → {model, mmproj} 的映射。同一个包可以同时带
'''                标准档（Q8_0）和低配档（Q4_K_M），用户在设置里换档即可，
'''                不用换包。未列出的档在界面上不出现。
'''   window       推理窗口秒（默认 5）
'''   step         推进步长秒（默认 5）
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
    ''' <summary>llama-server.exe；空 = PATH 上找。</summary>
    Friend Property LlamaServer As String = String.Empty
    ''' <summary>后端 → llama-server.exe。换后端要换二进制，所以一个包可以带好几份。</summary>
    Friend Property 服务器映射 As New Dictionary(Of String, String)(StringComparer.OrdinalIgnoreCase)
    ''' <summary>包推荐的后端（清单 device）；空 = auto，由二进制自己挑。</summary>
    Friend Property 推荐后端 As String = String.Empty
    ''' <summary>主权重 .gguf；空 = 交给 worker 按默认量化档拼。</summary>
    Friend Property 模型文件 As String = String.Empty
    ''' <summary>投影权重；空 = 与主权重同目录的 mmproj-*.gguf。</summary>
    Friend Property 投影文件 As String = String.Empty
    ''' <summary>清单 models 里列出的额外档位（低配档在这里）；空 = 这个包只有默认档。</summary>
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

    ''' <summary>按后端取 llama-server 二进制。表里没有这个后端就回落到包自带的默认
    ''' 二进制（纯 CPU 例外：任何构建都带 CPU 后端，回落过去照样能跑 -ngl 0）。
    ''' 命中=False 时状态行要提示用户"这个包没带该后端的服务器"。</summary>
    Friend Function 取服务器(后端 As String, ByRef 命中 As Boolean) As String
        命中 = False
        Dim id = Ai字幕后端.规整(后端)
        If id = Ai字幕后端.自动 Then Return LlamaServer
        Dim 路径 As String = Nothing
        If 服务器映射.TryGetValue(id, 路径) AndAlso Not String.IsNullOrWhiteSpace(路径) Then
            命中 = True
            Return 路径
        End If
        Return LlamaServer
    End Function
End Class

''' <summary>
''' 一个模型档位：清单 models 里的一项。让同一个包能同时带标准档与低配档，
''' 用户在设置里选档，不用换包 —— 换包对普通用户来说成本太高。
''' </summary>
Friend NotInheritable Class Ai字幕模型档
    ''' <summary>档名（清单 models 的键，如 Q8_0 / Q4_K_M）。</summary>
    Friend Property 档名 As String = String.Empty
    Friend Property 模型文件 As String = String.Empty
    Friend Property 投影文件 As String = String.Empty
End Class

Friend NotInheritable Class Ai字幕包
    Friend Const 清单文件名 As String = "ai-subtitles.json"

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
            配置.LlamaServer = 解析路径(目录, 读字符串(根, "llamaServer"))
            读服务器映射(目录, 根, 配置)
            配置.推荐后端 = Ai字幕后端.规整(读字符串(根, "device"))

            Dim 模型 = 解析路径(目录, 读字符串(根, "model"))
            If Not String.IsNullOrWhiteSpace(模型) AndAlso Not File.Exists(模型) Then
                错误 = $"模型文件不存在：{模型}"
                Return Nothing
            End If
            配置.模型文件 = 模型

            Dim 投影 = 解析路径(目录, 读字符串(根, "mmproj"))
            If Not String.IsNullOrWhiteSpace(投影) Then
                If Not File.Exists(投影) Then
                    错误 = $"投影权重不存在：{投影}"
                    Return Nothing
                End If
            ElseIf Not String.IsNullOrWhiteSpace(模型) Then
                ' 清单没写投影：在主权重旁边找一个 mmproj-*.gguf，找不到就留给 worker 兜底报错。
                Try
                    Dim 命中 = Directory.GetFiles(Path.GetDirectoryName(模型), "mmproj-*.gguf")
                    If 命中.Length > 0 Then 投影 = 命中(0)
                Catch
                End Try
            End If
            配置.投影文件 = 投影
            读模型档表(目录, 根, 配置)

            Dim 窗口 = 读数值(根, "window", 5.0)
            Dim 步长 = 读数值(根, "step", 5.0)
            配置.窗口秒 = Math.Clamp(窗口, 0.5, 30.0)
            配置.步长秒 = Math.Min(Math.Clamp(步长, 0.5, 30.0), 配置.窗口秒)
            Return 配置
        End Using
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

    ''' <summary>servers：{后端: llama-server 路径}。路径规则与其它字段一致；
    ''' 值留空或文件不存在时整项丢弃（宁可回落到默认二进制，也不要起一个注定失败的进程）。</summary>
    Private Shared Sub 读服务器映射(包目录 As String, 根 As JsonElement, 配置 As Ai字幕包配置)
        If 根.ValueKind <> JsonValueKind.Object Then Return
        Dim 表 As JsonElement = Nothing
        If Not 根.TryGetProperty("servers", 表) OrElse 表.ValueKind <> JsonValueKind.Object Then Return
        For Each 属性 In 表.EnumerateObject()
            Dim id = Ai字幕后端.规整(属性.Name)
            If id = Ai字幕后端.自动 Then Continue For
            Dim 路径 = 解析路径(包目录, If(属性.Value.ValueKind = JsonValueKind.String, 属性.Value.GetString(), String.Empty))
            If String.IsNullOrWhiteSpace(路径) OrElse Not File.Exists(路径) Then Continue For
            配置.服务器映射(id) = 路径
        Next
    End Sub

    ''' <summary>models：{档名: {model, mmproj}}，用于低配档（低量化）与标准档并存。
    ''' 任一路径不存在就整档丢弃 —— 宁可让用户少一个选项，也不要让他选到一个注定起不来的档。</summary>
    Private Shared Sub 读模型档表(包目录 As String, 根 As JsonElement, 配置 As Ai字幕包配置)
        Dim 表 As JsonElement = Nothing
        If Not 根.TryGetProperty("models", 表) OrElse 表.ValueKind <> JsonValueKind.Object Then Return
        For Each 属性 In 表.EnumerateObject()
            If 属性.Value.ValueKind <> JsonValueKind.Object Then Continue For
            Dim 模型 = 解析路径(包目录, 读字符串(属性.Value, "model"))
            If String.IsNullOrWhiteSpace(模型) OrElse Not File.Exists(模型) Then Continue For
            Dim 投影 = 解析路径(包目录, 读字符串(属性.Value, "mmproj"))
            If Not String.IsNullOrWhiteSpace(投影) AndAlso Not File.Exists(投影) Then Continue For
            If String.IsNullOrWhiteSpace(投影) Then 投影 = 邻近投影(模型)
            配置.模型档表.Add(New Ai字幕模型档 With {
                .档名 = 属性.Name, .模型文件 = 模型, .投影文件 = 投影})
        Next
    End Sub

    ''' <summary>主权重旁边找一个 mmproj-*.gguf；找不到返回空（留给 worker 兜底报错）。</summary>
    Private Shared Function 邻近投影(模型 As String) As String
        Try
            Dim 命中 = Directory.GetFiles(Path.GetDirectoryName(模型), "mmproj-*.gguf")
            If 命中.Length > 0 Then Return 命中(0)
        Catch
        End Try
        Return String.Empty
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
''' 推理后端标识。宿主这边只管"用户选了哪个"和"把它显示成什么"，
''' 至于这台机器上真有什么设备、设备前缀长什么样，由 worker 去问 llama-server
''' 的 --list-devices（比在本体里猜字符串可靠得多）。
'''
''' 硬件对应（llama.cpp 的后端实现）：
'''   cuda   → NVIDIA（cublas）
'''   vulkan → 通用，N/A/I 三家都能跑，一个二进制走天下，性能通常低于原生后端
'''   rocm   → AMD（HIP，Windows 支持面比 Linux 窄）
'''   sycl   → Intel Arc（oneAPI）
'''   cpu    → 没有可用 GPU / 驱动太老 / 用户不想占显存时的兜底，任何构建都带
''' </summary>
Friend NotInheritable Class Ai字幕后端
    Friend Const 自动 As String = "auto"
    Friend Const 处理器 As String = "cpu"
    Friend Const Cuda As String = "cuda"
    Friend Const Vulkan As String = "vulkan"
    Friend Const Rocm As String = "rocm"
    Friend Const Sycl As String = "sycl"

    ''' <summary>下拉框顺序：自动在最前，CPU 兜底其次，之后按显卡厂商。</summary>
    Friend Shared ReadOnly 全部 As String() = {自动, 处理器, Cuda, Vulkan, Rocm, Sycl}

    ''' <summary>未知值、空值、null 一律归到 auto —— 老设置文件和手写脏数据都往这里落。</summary>
    Friend Shared Function 规整(值 As String) As String
        If String.IsNullOrWhiteSpace(值) Then Return 自动
        值 = 值.Trim().ToLowerInvariant()
        For Each id In 全部
            If String.Equals(值, id, StringComparison.Ordinal) Then Return id
        Next
        Return 自动
    End Function

    Friend Shared Function 显示名(id As String) As String
        Select Case 规整(id)
            Case 处理器
                Return "CPU（不用显卡）"
            Case Cuda
                Return "NVIDIA 显卡（CUDA）"
            Case Vulkan
                Return "通用显卡（Vulkan）"
            Case Rocm
                Return "AMD 显卡（ROCm / HIP）"
            Case Sycl
                Return "Intel 显卡（SYCL）"
            Case Else
                Return "自动（跟随扩展包）"
        End Select
    End Function
End Class
