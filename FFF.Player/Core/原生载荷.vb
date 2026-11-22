Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.IO
Imports System.IO.Compression
Imports System.Runtime.CompilerServices
Imports System.Runtime.InteropServices
Imports System.Security.Cryptography
Imports System.Text

''' <summary>
''' 单文件分发的原生载荷：把「本来就要被吐到磁盘」的原生件（内核 + LibRaw + libass 栈 +
''' 光盘栈）压成一个 Brotli 载荷内嵌在宿主程序集里（打包见 tools\native_payload.cs），
''' 启动早期解压到 %LOCALAPPDATA%\1059Studio\3FP\native\&lt;载荷哈希&gt;\，再从该目录预加载内核。
'
''' 为什么不用 .NET 自己的单文件压缩：bundle 压缩只对自包含发布成立——框架依赖发布被
''' SDK 直接拒绝（NETSDK1176），手工压好的 FDD bundle 运行时也拒绝启动
''' （"Compressed files used with a standalone apphost"），而自包含 + 压缩实测 84.85 MB。
'
''' 为什么解包后不需要改 DLL 搜索序：内核按自身模块目录解析全部原生依赖
''' （RawStillSource.cpp 的 libraw 显式路径、FfmpegRuntime.cpp 的 delay-load 钩子
''' 先搜 FFF.Native 目录再搜 exe 目录），所以内核从解包目录加载后，其余 15 件自动同址命中。
'
''' 散装/开发运行时没有载荷资源：确保就绪() 立即返回 Nothing，行为与改动前一致。
''' </summary>
Friend Module 原生载荷
    ''' <summary>与 <c>FFF.Player.vbproj</c> 里 EmbeddedResource 的 LogicalName 一致。</summary>
    Friend Const 资源名 As String = "FFF.Player.NativePayload.bin"

    ''' <summary>完成标记：写在解包目录里、内容是载荷哈希；缺它或内容不符都视为未完成。</summary>
    Private Const 完成标记 As String = ".complete"

    ''' <summary>调试开关：=1 时完全跳过载荷（用于对照散装目录形态，或排查解包问题）。</summary>
    Private Const 跳过环境变量 As String = "FFF_NO_NATIVE_PAYLOAD"

    ''' <summary>可选缓存根目录覆盖：指向放 native-payload-cache\payload 的父目录。
    ''' 单层/外层启动器可把它指向宿主目录，避免内层 exe 的 payload 目录再存一份。</summary>
    Private Const 缓存目录环境变量 As String = "FFF_NATIVE_PAYLOAD_CACHE_DIR"

    ''' <summary>诊断开关：=1 时把解包各步写进 %TEMP%\3fp-native-payload.log（默认关，零开销）。</summary>
    Friend Const 跟踪环境变量 As String = "FFF_NATIVE_PAYLOAD_TRACE"

    Private ReadOnly 同步 As New Object
    Private 已判定 As Boolean
    Private 就绪目录 As String
    ' 持有内核句柄，既防止模块被卸载，也让后续 DllImport("FFF.Native.dll") 直接命中已加载模块。
    Private 内核句柄 As IntPtr = IntPtr.Zero

    ''' <summary>
    ''' 确保原生载荷已解包并完成内核预加载，返回解包目录；无载荷（散装/开发）或解包失败时
    ''' 返回 Nothing。可重复调用，线程安全；失败只降级不回抛，避免阻断启动。
    ''' </summary>
    Friend Function 确保就绪() As String
        跟踪("进入 确保就绪，已判定=" & 已判定)
        If 已判定 Then Return 就绪目录
        SyncLock 同步
            If 已判定 Then Return 就绪目录
            Try
                If Environment.GetEnvironmentVariable(跳过环境变量) = "1" Then
                    就绪目录 = Nothing
                    跟踪("按 " & 跳过环境变量 & "=1 跳过载荷")
                Else
                    就绪目录 = 解包并准备()
                End If
            Catch ex As Exception
                ' 解包不是启动前提：失败就按「原生件在程序目录」的旧形态继续，内核缺件由它自己报。
                Debug.WriteLine("原生载荷：解包失败，回退按程序目录解析：" & ex.Message)
                跟踪("失败：" & ex.ToString())
                就绪目录 = Nothing
            End Try
            已判定 = True
        End SyncLock
        Return 就绪目录
    End Function

    Private Function 解包并准备() As String
        Dim 资源 = GetType(原生载荷).Assembly.GetManifestResourceStream(资源名)
        If 资源 Is Nothing Then
            跟踪("宿主程序集里没有 " & 资源名 & "（散装/开发运行属正常）")
            Return Nothing
        End If

        Using 资源
            Dim 最后异常 As Exception = Nothing
            Dim 标识 = 当前载荷标识(资源)
            For Each 根目录 In 候选缓存根目录()
                Try
                    跟踪("载荷 " & 资源.Length & " 字节，尝试 → " & 根目录)
                    Directory.CreateDirectory(根目录)
                    Dim 目录 = Path.Combine(根目录, 解包目录名)
                    Dim 临时目录 = 目录 & ".new"
                    Try
                        资源.Position = 0
                        If Not IsComplete(目录, 标识) Then
                            ' 解到固定名字的临时目录再整体替换：缓存目录名固定，不随版本累积。
                            清掉目录(临时目录)
                            解包到目录(资源, 临时目录)
                            完成标记写入(临时目录, 标识)
                            替换目录(临时目录, 目录)
                        End If
                        预加载内核(目录)
                        清理陈旧目录(根目录, 目录)
                        跟踪("就绪 " & 目录)
                        Return 目录
                    Finally
                        清掉目录(临时目录)
                    End Try
                Catch ex As Exception
                    ' 这一层不可写/不可用：记下来换下一层，全部失败才向上抛。
                    最后异常 = ex
                    跟踪("该目录不可用：" & ex.GetType().Name & " " & ex.Message)
                End Try
            Next
            If 最后异常 IsNot Nothing Then Throw 最后异常
            Return Nothing
        End Using
    End Function

    ''' <summary>
    ''' 载荷标识：写进完成标记，用来判断「这份解包是不是当前版本」。
    ''' 只在完成标记里用，绝不拼进目录名（Windows 目录名不允许 ':' 等字符）。
    ''' 资源名 + 载荷 SHA256；同长度重建也能区分，避免误复用旧缓存。
    ''' </summary>
    Private Function 当前载荷标识(资源 As Stream) As String
        Dim 原位置 = 资源.Position
        Try
            资源.Position = 0
            Using 哈希 = SHA256.Create()
                Return 资源名 & ":" & Convert.ToHexString(哈希.ComputeHash(资源))
            End Using
        Finally
            资源.Position = 原位置
        End Try
    End Function

    ''' <summary>解包目录名：固定名，不随版本变化，保证磁盘上始终只有一份解包结果。</summary>
    Private Const 解包目录名 As String = "payload"

    ''' <summary>按优先级排出的候选缓存根目录；解包时逐层试写，第一个可写的胜出。</summary>
    Private Function 候选缓存根目录() As IEnumerable(Of String)
        Dim 列表 As New List(Of String)
        ' 可选覆盖：外层启动器/单层缓存把根目录固定在宿主，内层 exe 不再各自解一份。
        Dim 缓存覆盖 = Environment.GetEnvironmentVariable(缓存目录环境变量)
        If Not String.IsNullOrWhiteSpace(缓存覆盖) Then 列表.Add(缓存覆盖.Trim())
        ' ① exe 同目录优先（用户可随程序目录一起删除，符合「绿色/便携」直觉）。
        '    内层 exe 位于 <宿主>\3FP.runtime\payload 时，同目录缓存会落在 payload 里，
        '    等于每次宿主更新都可能丢缓存；这里识别受控宿主布局，把缓存提到宿主根，
        '    让外层启动器和内层复用同一份 native-payload-cache。
        '    Program Files 等只读位置会失败，由 解包并准备 的逐层回退兜底。
        Dim 程序目录 = Path.GetDirectoryName(Environment.ProcessPath)
        If Not String.IsNullOrWhiteSpace(程序目录) Then
            Dim 缓存根 = 程序目录
            Try
                Dim 宿主目录 = 宿主路径.解析宿主目录(程序目录)
                If Not String.IsNullOrWhiteSpace(宿主目录) Then 缓存根 = 宿主目录
            Catch
            End Try
            列表.Add(Path.Combine(缓存根, "native-payload-cache"))
        End If
        ' ② 沿用 .NET bundle 宿主的既有约定：部署侧已用该变量指定可写目录
        '    （见仓库 3FP单文件运行说明.txt；本机默认 %TEMP% 建目录会被判 error 5）。
        Dim 环境 = Environment.GetEnvironmentVariable("DOTNET_BUNDLE_EXTRACT_BASE_DIR")
        If Not String.IsNullOrWhiteSpace(环境) Then 列表.Add(Path.Combine(环境, "FFF.Player", "native"))
        Dim 临时 = Path.GetTempPath()
        If Not String.IsNullOrWhiteSpace(临时) Then 列表.Add(Path.Combine(临时, ".net", "FFF.Player", "native"))
        Dim 本地 = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData)
        If Not String.IsNullOrWhiteSpace(本地) Then 列表.Add(Path.Combine(本地, "1059Studio", "3FP", "native"))
        Return 列表
    End Function

    ''' <summary>目录里必须有完成标记且内容等于当前载荷标识，才算可用（避免半截解包被当成完成态）。</summary>
    Private Function IsComplete(目录 As String, 标识 As String) As Boolean
        Dim 标记 = Path.Combine(目录, 完成标记)
        If Not IO.File.Exists(标记) Then Return False
        Try
            Return String.Equals(IO.File.ReadAllText(标记).Trim(), 标识, StringComparison.Ordinal)
        Catch
            Return False
        End Try
    End Function

    ''' <summary>
    ''' 把载荷内容解到指定目录（调用方保证该目录是空的临时目录）。
    ''' 采用「临时目录 → 整体替换」而不是逐件覆盖：升级时不会出现新旧混杂的半套 DLL。
    ''' </summary>
    Private Sub 解包到目录(资源 As Stream, 目录 As String)
        Directory.CreateDirectory(目录)
        Using brotli As New BrotliStream(资源, CompressionMode.Decompress, leaveOpen:=True)
            Using reader As New BinaryReader(brotli, Encoding.UTF8)
                Do
                    Dim 名称长度 As Integer
                    Try
                        名称长度 = reader.ReadInt32()
                    Catch ex As EndOfStreamException
                        Exit Do
                    End Try
                    If 名称长度 <= 0 OrElse 名称长度 > 4096 Then
                        Throw New InvalidDataException("原生载荷条目名长度非法：" & 名称长度)
                    End If
                    Dim 名称 = Encoding.UTF8.GetString(reader.ReadExactlyBytes(名称长度))
                    ' 载荷是自家构建产物，但仍按不可信输入处理：只接受扁平的单个文件名。
                    If 名称.IndexOfAny(New Char() {"\"c, "/"c, ":"c}) >= 0 OrElse 名称 = "." OrElse 名称 = ".." Then
                        Throw New InvalidDataException("原生载荷条目名非法：" & 名称)
                    End If
                    Dim 数据长度 = reader.ReadInt64()
                    If 数据长度 < 0 OrElse 数据长度 > Integer.MaxValue Then
                        Throw New InvalidDataException("原生载荷条目长度非法：" & 名称 & " " & 数据长度)
                    End If
                    IO.File.WriteAllBytes(Path.Combine(目录, 名称), reader.ReadExactlyBytes(CInt(数据长度)))
                Loop
            End Using
        End Using
    End Sub

    Private Sub 完成标记写入(目录 As String, 标识 As String)
        IO.File.WriteAllText(Path.Combine(目录, 完成标记), 标识)
    End Sub

    ''' <summary>
    ''' 用临时目录整体替换正式目录。目录名固定 = 磁盘上始终只有一份解包结果；
    ''' 另一个实例抢先就位时，丢弃自己的临时目录并复用对方已完成的（标识相同，内容等价）。
    ''' </summary>
    Private Sub 替换目录(临时目录 As String, 目录 As String)
        Try
            If Directory.Exists(目录) Then
                ' 覆盖前先把旧目录挪开，避免 Move 因目标存在而失败（旧目录可能是上一版本）。
                Dim 旧 = 目录 & ".old-" & Guid.NewGuid().ToString("N")
                Directory.Move(目录, 旧)
                清掉目录(旧)
            End If
            Directory.Move(临时目录, 目录)
        Catch ex As IOException
            If Directory.Exists(目录) AndAlso IO.File.Exists(Path.Combine(目录, 完成标记)) Then
                ' 别的实例已完成同一版本，直接复用。
                清掉目录(临时目录)
            Else
                Throw
            End If
        End Try
    End Sub

    ''' <summary>删掉根目录下不属于当前版本的残留（旧版本目录、中断的 .new/.old 目录）。</summary>
    Private Sub 清理陈旧目录(根目录 As String, 当前目录 As String)
        Try
            For Each 子目录 In Directory.GetDirectories(根目录)
                If Not String.Equals(Path.GetFullPath(子目录), Path.GetFullPath(当前目录), StringComparison.OrdinalIgnoreCase) Then
                    清掉目录(子目录)
                End If
            Next
        Catch
            ' 清理失败只影响磁盘占用，不影响本次运行。
        End Try
    End Sub

    Private Sub 清掉目录(目录 As String)
        Try
            If Directory.Exists(目录) Then Directory.Delete(目录, True)
        Catch
        End Try
    End Sub

    ''' <summary>
    ''' 从解包目录按全路径预加载内核：Windows 的 LoadLibrary 对已加载的同名模块直接返回既有句柄，
    ''' 所以后续 <c>DllImport("FFF.Native.dll")</c> 与各个裸名 TryLoad 都会命中这一份，
    ''' 而内核自己再按模块目录解析 libraw/libass/光盘栈。
    ''' </summary>
    Private Sub 预加载内核(目录 As String)
        If 内核句柄 <> IntPtr.Zero Then Return
        Dim 内核路径 = Path.Combine(目录, "FFF.Native.dll")
        If Not IO.File.Exists(内核路径) Then
            Throw New FileNotFoundException("原生载荷里没有 FFF.Native.dll：" & 内核路径)
        End If
        Dim 句柄 As IntPtr
        If Not NativeLibrary.TryLoad(内核路径, 句柄) Then
            Throw New InvalidOperationException("原生载荷里的内核无法加载：" & 内核路径)
        End If
        内核句柄 = 句柄
    End Sub

    ''' <summary>诊断日志：仅在 FFF_NATIVE_PAYLOAD_TRACE=1 时写 %TEMP%，供排查解包问题。</summary>
    Friend Sub 跟踪(消息 As String)
        If Environment.GetEnvironmentVariable(跟踪环境变量) <> "1" Then Return
        Try
            IO.File.AppendAllText(Path.Combine(Path.GetTempPath(), "3fp-native-payload.log"),
                DateTime.Now.ToString("HH:mm:ss.fff") & " [" & Environment.ProcessId & "] " & 消息 & Environment.NewLine)
        Catch
            ' 诊断写盘失败不得影响启动。
        End Try
    End Sub
End Module

Friend Module BinaryReaderExtensions
    ''' <summary>读出固定字节数；不足即视为载荷截断（BinaryReader.ReadBytes 静默短读，不能直接信长度）。</summary>
    <Extension()>
    Friend Function ReadExactlyBytes(reader As BinaryReader, 数量 As Integer) As Byte()
        Dim 数据 = reader.ReadBytes(数量)
        If 数据.Length <> 数量 Then Throw New EndOfStreamException("原生载荷被截断")
        Return 数据
    End Function
End Module
