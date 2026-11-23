Imports System.Drawing
Imports System.Globalization
Imports System.IO
Imports System.Linq
Imports System.Net
Imports System.Net.Http
Imports System.Net.Http.Headers
Imports System.Reflection
Imports System.Runtime.CompilerServices
Imports System.Security.Cryptography
Imports System.Text
Imports System.Text.Json
Imports System.Threading
Imports System.Threading.Tasks
Imports FFF.Player

Friend Module 安全修复回归
    Private 检查数 As Integer

    Private Sub 检查(条件 As Boolean, 名称 As String)
        If Not 条件 Then Throw New Exception(名称)
        检查数 += 1
        Console.WriteLine("PASS " & 名称)
    End Sub

    Public Function 运行() As Integer
        Try
            路径测试()
            字幕测试()
            换行测试()
            下载清单测试Async().GetAwaiter().GetResult()
            下载测试().GetAwaiter().GetResult()
            Console.WriteLine($"Security regression: {检查数} passed")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine(ex.ToString())
            Return 1
        End Try
    End Function

    Private Sub 路径测试()
        For Each 前缀 In {"\/", "/\", "\\", "//", "\\?\UNC\", "\\.\"}
            Dim 拒绝 = False
            Try
                播放列表.规范本地文件(前缀 & "invalid.example\share\file.mp4")
            Catch ex As ArgumentException
                拒绝 = True
            End Try
            检查(拒绝, "network path rejected before existence check: " & 前缀)
        Next
        Dim 路径 = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString() & "中文.mp4")
        Try
            File.WriteAllBytes(路径, {0})
            检查(播放列表.规范本地文件(路径) = 路径, "local Unicode path")
            检查(播放列表.规范本地文件(Path.GetRelativePath(Environment.CurrentDirectory, 路径)) = 路径, "relative local path")
        Finally
            File.Delete(路径)
        End Try
    End Sub

    Private Function 解析文本(文本 As String) As String
        Using reader As New StringReader("1" & vbLf & "00:00:01,000 --> 00:00:02,000" & vbLf & 文本)
            Dim 文档 = SRT字幕解析器.解析(reader)
            Return String.Join(vbLf, 文档.提示.Single().行.Select(Function(x) x.文本))
        End Using
    End Function

    Private Sub 字幕测试()
        检查(解析文本("<i>Hello</i> 世界") = "Hello 世界", "closed HTML tags")
        检查(解析文本("{\an8}Hello") = "Hello", "closed ASS tag")
        For Each 文本 In {New String("<"c, 300000), String.Concat(Enumerable.Repeat("{\", 150000)), New String("{"c, 300000)}
            Dim watch = Diagnostics.Stopwatch.StartNew()
            检查(解析文本(文本) = 文本, "unclosed tags preserve text")
            检查(watch.Elapsed < TimeSpan.FromSeconds(8), "unclosed tags bounded parse time")
        Next
        检查(解析文本("literal < and {\ unfinished") = "literal < and {\ unfinished", "unfinished markup control")
    End Sub

    Private Sub 换行测试()
        Dim 类型 = GetType(播放器歌词呈现器)
        Dim 对象 = RuntimeHelpers.GetUninitializedObject(类型)
        Dim 方法 = 类型.GetMethod("按字素换行", BindingFlags.NonPublic Or BindingFlags.Instance)
        Using 字体 As New Font("Segoe UI", 18)
            For Each 文本 In {"中文歌词正常换行", "one two three four five", "A😀e" & ChrW(&H301) & "👩‍👩‍👧‍👦B"}
                For Each 宽度 In {1, 80, 500}
                    Dim 行 = DirectCast(方法.Invoke(对象, {文本, 字体, 宽度}), String())
                    Dim 合并 = String.Concat(行).Replace(" ", "")
                    检查(合并 = 文本.Replace(" ", ""), "wrap preserves graphemes")
                    Dim 边界 = StringInfo.ParseCombiningCharacters(文本).ToHashSet()
                    Dim 位置 = 0
                    For Each 一行 In 行
                        While 位置 < 文本.Length AndAlso Char.IsWhiteSpace(文本(位置))
                            位置 += 1
                        End While
                        检查(边界.Contains(位置), "wrap starts at grapheme boundary")
                        位置 += 一行.Length
                    Next
                Next
            Next
            Dim 长文 = String.Concat(Enumerable.Repeat("中😀e" & ChrW(&H301), 10000))
            Dim watch = Diagnostics.Stopwatch.StartNew()
            Dim 长行 = DirectCast(方法.Invoke(对象, {长文, 字体, 1}), String())
            检查(String.Concat(长行) = 长文, "long narrow lyric intact")
            检查(watch.Elapsed < TimeSpan.FromSeconds(15), "long narrow lyric bounded time")
            类型.GetField("已释放标志", BindingFlags.NonPublic Or BindingFlags.Instance).SetValue(对象, 1)
            检查(DirectCast(方法.Invoke(对象, {长文, 字体, 1}), String()).Length = 0, "disposed lyric cancels wrapping")
        End Using
    End Sub

    Private NotInheritable Class 无穷流
        Inherits Stream
        Public 已读 As Long
        Public Overrides ReadOnly Property CanRead As Boolean = True
        Public Overrides ReadOnly Property CanSeek As Boolean = False
        Public Overrides ReadOnly Property CanWrite As Boolean = False
        Public Overrides ReadOnly Property Length As Long
            Get
                Throw New NotSupportedException()
            End Get
        End Property
        Public Overrides Property Position As Long
            Get
                Return 已读
            End Get
            Set(value As Long)
                Throw New NotSupportedException()
            End Set
        End Property
        Public Overrides Function Read(buffer As Byte(), offset As Integer, count As Integer) As Integer
            Array.Clear(buffer, offset, count)
            已读 += count
            Return count
        End Function
        Public Overrides Function ReadAsync(buffer As Byte(), offset As Integer, count As Integer, token As CancellationToken) As Task(Of Integer)
            token.ThrowIfCancellationRequested()
            Return Task.FromResult(Read(buffer, offset, count))
        End Function
        Public Overrides Sub Flush()
        End Sub
        Public Overrides Function Seek(offset As Long, origin As SeekOrigin) As Long
            Throw New NotSupportedException()
        End Function
        Public Overrides Sub SetLength(value As Long)
            Throw New NotSupportedException()
        End Sub
        Public Overrides Sub Write(buffer As Byte(), offset As Integer, count As Integer)
            Throw New NotSupportedException()
        End Sub
    End Class

    Private NotInheritable Class 响应处理器
        Inherits HttpMessageHandler
        Public 响应 As HttpResponseMessage
        Public 构造响应 As Func(Of HttpRequestMessage, HttpResponseMessage)
        Public 请求数 As Integer
        Public 范围 As Long?
        Protected Overrides Function SendAsync(request As HttpRequestMessage, token As CancellationToken) As Task(Of HttpResponseMessage)
            请求数 += 1
            范围 = request.Headers.Range?.Ranges.Single().From
            Return Task.FromResult(If(构造响应 Is Nothing, 响应, 构造响应(request)))
        End Function
    End Class

    Private NotInheritable Class 静默进度
        Implements IProgress(Of Ai字幕包下载器.下载进度)
        Public Sub Report(value As Ai字幕包下载器.下载进度) Implements IProgress(Of Ai字幕包下载器.下载进度).Report
        End Sub
    End Class

    Private Async Function 下载清单测试Async() As Task
        For Each 类型 In {Ai字幕包下载器.包类型.GpuCpu, Ai字幕包下载器.包类型.Npu}
            Dim 是Npu = 类型 = Ai字幕包下载器.包类型.Npu
            Dim 路径列表 = If(是Npu, {"conv_frontend.onnx", "encoder.int8.onnx", "decoder.int8.onnx",
                                    "tokenizer/vocab.json", "tokenizer/merges.txt", "tokenizer/tokenizer_config.json"},
                                   {"Qwen3-ASR-0.6B-Q8_0.gguf", "mmproj-Qwen3-ASR-0.6B-Q8_0.gguf"})
            Dim 提交 As String = Nothing
            Dim 数据 As Byte() = Nothing
            Using 处理器 As New 响应处理器(), 客户端 As New HttpClient(处理器)
                处理器.构造响应 =
                    Function(请求)
                        检查(请求.Method = HttpMethod.Get AndAlso 请求.RequestUri.AbsolutePath.EndsWith("/revision/main", StringComparison.Ordinal) AndAlso
                            请求.RequestUri.Query = "?blobs=true", "read current model metadata")
                        If 是Npu AndAlso 请求.RequestUri.Host = "huggingface.co" Then
                            Return New HttpResponseMessage(HttpStatusCode.OK) With {.Content = New StringContent("{""sha"":""main""}")}
                        End If
                        Dim 文件 As New List(Of Object)
                        For Each 路径 In 路径列表
                            If 路径 = "tokenizer/merges.txt" Then
                                文件.Add(New With {.rfilename = 路径, .size = 数据.Length, .blobId = Git对象哈希(数据)})
                            Else
                                文件.Add(New With {.rfilename = 路径, .size = 数据.Length,
                                    .lfs = New With {.size = 数据.Length, .sha256 = Convert.ToHexString(SHA256.HashData(数据))}})
                            End If
                        Next
                        Return New HttpResponseMessage(HttpStatusCode.OK) With {
                            .Content = New StringContent(JsonSerializer.Serialize(New With {.sha = 提交, .siblings = 文件}))}
                    End Function
                For 版本 As Integer = 1 To 2
                    提交 = New String(If(版本 = 1, "a"c, "b"c), 40)
                    数据 = Enumerable.Repeat(CByte(版本), 100 + 版本).ToArray()
                    Dim 清单 = Await Ai字幕包下载器.组下载清单Async(类型, CancellationToken.None, 客户端)
                    Dim 模型件 = 清单.Where(Function(项) 项.来自模型仓).ToArray()
                    检查(模型件.Select(Function(项) 项.相对路径).SequenceEqual(路径列表.Select(Function(路径) "model/" & 路径)),
                        $"model file layout: {类型}")
                    Dim 源顺序 = If(是Npu, {"hf-mirror.com", "huggingface.co"}, {"huggingface.co", "hf-mirror.com"})
                    For Each 项 In 模型件
                        Dim 路径 = 项.相对路径.Substring("model/".Length)
                        Dim 是Git = 路径 = "tokenizer/merges.txt"
                        Dim 哈希 = If(是Git, Git对象哈希(数据), Convert.ToHexString(SHA256.HashData(数据)))
                        检查(项.源列表.Select(Function(源) New Uri(源.网址).Host).SequenceEqual(源顺序) AndAlso
                            项.源列表.All(Function(源) 源.网址.EndsWith($"/resolve/{提交}/{路径}", StringComparison.Ordinal) AndAlso
                                源.字节 = 数据.Length AndAlso 源.哈希 = 哈希 AndAlso 源.是Git对象 = 是Git),
                            $"current revision, size, checksum and source order: {类型}/{路径} v{版本}")
                    Next
                Next
                检查(处理器.请求数 = If(是Npu, 4, 2), "fresh metadata on every download")
            End Using
        Next
    End Function

    Private Function Git对象哈希(数据 As Byte()) As String
        Dim 对象头 = Encoding.ASCII.GetBytes($"blob {数据.Length}" & ChrW(0))
        Return Convert.ToHexString(SHA1.HashData(对象头.Concat(数据).ToArray()))
    End Function

    Private Function 模型项(目标 As String, 数据 As Byte(), Optional 是Git As Boolean = False) As Ai字幕包下载器.包文件项
        Dim 项 As New Ai字幕包下载器.包文件项 With {.相对路径 = Path.GetFileName(目标), .来自模型仓 = True}
        Dim 哈希 = If(是Git, Git对象哈希(数据), Convert.ToHexString(SHA256.HashData(数据)))
        项.源列表.Add(New Ai字幕包下载器.包源("https://example.invalid/model", 数据.Length, 哈希, 是Git))
        Return 项
    End Function

    Private Async Function 模型续传测试Async(根 As String) As Task
        Dim 旧数据 = Enumerable.Repeat(CByte(1), 100).ToArray()
        Dim 新数据 = Enumerable.Repeat(CByte(2), 100).ToArray()
        For Each 已更新 In {False, True}
            Dim 目标 = Path.Combine(根, If(已更新, "changed-model", "same-model"))
            Dim 上次的模型 = 模型项(目标, 旧数据)
            Using 处理器 As New 响应处理器(), 客户端 As New HttpClient(处理器)
                处理器.响应 = New HttpResponseMessage(HttpStatusCode.OK) With {
                    .Content = New StreamContent(New MemoryStream(旧数据.Take(40).ToArray()))}
                处理器.响应.Content.Headers.ContentLength = Nothing
                Dim 错误 = Await Ai字幕包下载器.下载文件Async(目标, 上次的模型, 0, 100, Nothing, CancellationToken.None, 客户端)
                检查(错误 <> "" AndAlso New FileInfo(目标 & ".part").Length = 40, "partial model bytes are retained")
                Dim 预期 = If(已更新, 新数据, 旧数据)
                Dim 体 = If(已更新, 预期, 预期.Skip(40).ToArray())
                处理器.响应 = New HttpResponseMessage(If(已更新, HttpStatusCode.OK, HttpStatusCode.PartialContent)) With {
                    .Content = New ByteArrayContent(体)}
                If Not 已更新 Then 处理器.响应.Content.Headers.ContentRange = New ContentRangeHeaderValue(40, 99, 100)
                错误 = Await Ai字幕包下载器.下载文件Async(目标, 模型项(目标, 预期), 0, 100, Nothing, CancellationToken.None, 客户端)
                检查(错误 = "" AndAlso File.ReadAllBytes(目标).SequenceEqual(预期) AndAlso Not File.Exists(目标 & ".part.id"),
                    "model verified and published on retry")
                检查(If(已更新, Not 处理器.范围.HasValue, 处理器.范围.GetValueOrDefault(-1) = 40),
                    "resume uses content identity even at the same file size")
            End Using
        Next

        Dim Git目标 = Path.Combine(根, "merges.txt")
        File.WriteAllBytes(Git目标, 旧数据)
        Dim Git项 = 模型项(Git目标, 新数据, True)
        Using 处理器 As New 响应处理器(), 客户端 As New HttpClient(处理器)
            处理器.响应 = New HttpResponseMessage(HttpStatusCode.OK) With {.Content = New ByteArrayContent(旧数据)}
            Dim 错误 = Await Ai字幕包下载器.下载文件Async(Git目标, Git项, 0, 100, Nothing, CancellationToken.None, 客户端)
            检查(错误 <> "" AndAlso File.ReadAllBytes(Git目标).SequenceEqual(旧数据) AndAlso
                Not File.Exists(Git目标 & ".git-download"), "invalid Git blob never replaces verified target")
            处理器.响应 = New HttpResponseMessage(HttpStatusCode.OK) With {.Content = New ByteArrayContent(新数据)}
            错误 = Await Ai字幕包下载器.下载文件Async(Git目标, Git项, 0, 100, Nothing, CancellationToken.None, 客户端)
            检查(错误 = "" AndAlso File.ReadAllBytes(Git目标).SequenceEqual(新数据), "valid Git blob is published")
        End Using
    End Function

    Private Async Function 下载测试() As Task
        Dim 根 = Path.Combine(Path.GetTempPath(), "3fp-download-" & Guid.NewGuid().ToString())
        Directory.CreateDirectory(根)
        Try
            Dim 数据 = Enumerable.Range(0, 100).Select(Function(x) CByte(x)).ToArray()
            Dim 哈希 = Convert.ToHexString(SHA256.HashData(数据))
            For Each 场景 In {"exact", "restart", "resume", "wrong-range", "oversize", "short", "bad-hash", "complete-part"}
                Dim 目标 = Path.Combine(根, 场景)
                Dim 已有 = If(场景 = "restart" OrElse 场景 = "resume" OrElse 场景 = "wrong-range", 40, 0)
                If 场景 = "complete-part" Then 已有 = 数据.Length
                If 已有 > 0 Then File.WriteAllBytes(目标 & ".part", 数据.Take(已有).ToArray())
                Dim 体 = If(场景 = "resume" OrElse 场景 = "wrong-range", 数据.Skip(40).ToArray(), 数据)
                If 场景 = "short" Then 体 = 数据.Take(99).ToArray()
                Dim 流 As Stream = If(场景 = "oversize", DirectCast(New 无穷流(), Stream), New MemoryStream(体))
                Using 处理器 As New 响应处理器(), 客户端 As New HttpClient(处理器)
                    处理器.响应 = New HttpResponseMessage(If(场景 = "resume" OrElse 场景 = "wrong-range", HttpStatusCode.PartialContent, HttpStatusCode.OK)) With {.Content = New StreamContent(流)}
                    ' StreamContent's seekable length is suppressed to exercise ResponseHeadersRead without a size.
                    处理器.响应.Content.Headers.ContentLength = Nothing
                    检查(Not 处理器.响应.Content.Headers.ContentLength.HasValue, "response has no Content-Length: " & 场景)
                    If 处理器.响应.StatusCode = HttpStatusCode.PartialContent Then
                        处理器.响应.Content.Headers.ContentRange = New ContentRangeHeaderValue(If(场景 = "wrong-range", 39, 40), 99, 100)
                    End If
                    Dim 项 As New Ai字幕包下载器.包文件项 With {.相对路径 = 场景}
                    项.源列表.Add(New Ai字幕包下载器.包源("https://example.invalid/test", 100,
                        If(场景 = "bad-hash", New String("0"c, 64), 哈希)))
                    Dim 错误 = Await Ai字幕包下载器.下载文件Async(目标, 项, 0, 100, New 静默进度(), CancellationToken.None, 客户端)
                    Dim 成功 = {"exact", "restart", "resume", "complete-part"}.Contains(场景)
                    检查((错误 = "") = 成功, "download result: " & 场景)
                    If 成功 Then 检查(File.ReadAllBytes(目标).SequenceEqual(数据), "download hash and bytes: " & 场景)
                    If File.Exists(目标 & ".part") Then 检查(New FileInfo(目标 & ".part").Length <= 100, "partial file bounded: " & 场景)
                    If 场景 = "wrong-range" Then 检查(New FileInfo(目标 & ".part").Length = 40, "invalid range writes nothing")
                    If 场景 = "complete-part" Then 检查(处理器.请求数 = 0, "complete partial verifies without HTTP")
                    If 已有 = 40 Then 检查(处理器.范围 = 40, "resume requests correct range")
                End Using
            Next
            Using 取消 As New CancellationTokenSource()
                取消.Cancel()
                Dim 项 As New Ai字幕包下载器.包文件项 With {.相对路径 = "cancelled"}
                项.源列表.Add(New Ai字幕包下载器.包源("https://example.invalid/test", 100, 哈希))
                Dim 已取消 = False
                Try
                    Await Ai字幕包下载器.下载文件Async(Path.Combine(根, "cancelled"), 项, 0, 100, New 静默进度(), 取消.Token)
                Catch ex As OperationCanceledException
                    已取消 = True
                End Try
                检查(已取消 AndAlso Not File.Exists(Path.Combine(根, "cancelled.part")), "download cancellation")
            End Using
            Await 模型续传测试Async(根)
            Dim GPU = Directory.CreateDirectory(Path.Combine(根, "GPU")).FullName
            Dim NPU = Directory.CreateDirectory(Path.Combine(根, "NPU")).FullName
            检查(Ai字幕包.写归属哨兵(GPU, "GpuCpu") AndAlso Ai字幕包.写归属哨兵(NPU, "Npu"), "ownership fixtures")
            Try
                Ai字幕包.删除指定下载包(GPU, "Npu")
            Catch ex As IOException
            End Try
            检查(Directory.Exists(GPU) AndAlso Directory.Exists(NPU), "wrong package type deletes nothing")
            Ai字幕包.删除指定下载包(GPU, "GpuCpu")
            检查(Not Directory.Exists(GPU) AndAlso Directory.Exists(NPU), "only selected package deleted")
        Finally
            Directory.Delete(根, True)
        End Try
    End Function
End Module
