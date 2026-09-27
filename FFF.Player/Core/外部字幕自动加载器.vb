Imports System.IO
Imports System.Text.Json
Imports System.Threading

''' <summary>外部或内嵌字幕的已加载资源。原生字幕渲染器必须随播放会话释放。</summary>
Public NotInheritable Class 外部字幕轨道
    Implements IDisposable

    Private 活动使用者 As Integer
    Private 已请求释放 As Integer
    Private 已释放资源 As Integer

    Friend Sub New(路径 As String, 格式 As 外部字幕格式, SRT As SRT字幕帧生成器,
                   SUP As SUP字幕帧生成器,
                   Optional ASS特效 As ASS特效字幕帧生成器 = Nothing,
                   Optional 流索引 As Integer = -1,
                   Optional 是内嵌 As Boolean = False,
                   Optional 容器字幕轨 As 媒体流信息() = Nothing)
        Me.路径 = 路径
        Me.格式 = 格式
        SRT生成器 = SRT
        SUP生成器 = SUP
        ASS特效生成器 = ASS特效
        Me.流索引 = 流索引
        Me.是内嵌 = 是内嵌
        Me.容器字幕轨 = 容器字幕轨
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 格式 As 外部字幕格式
    Public ReadOnly Property 流索引 As Integer
    Public ReadOnly Property 是内嵌 As Boolean
    Public ReadOnly Property SRT生成器 As SRT字幕帧生成器
    Public ReadOnly Property SUP生成器 As SUP字幕帧生成器
    ''' <summary>MKS 等字幕容器加载成功时携带全部字幕轨（含未选中轨），供多轨菜单注册；其它格式为 Nothing。</summary>
    Friend ReadOnly Property 容器字幕轨 As 媒体流信息()
    Friend ReadOnly Property ASS特效生成器 As ASS特效字幕帧生成器

    ''' <summary>文本字幕可精确计数；由原生按需解码的 ASS/SUP 则返回 -1。</summary>
    Public ReadOnly Property 条目数 As Integer
        Get
            If SRT生成器 IsNot Nothing Then Return SRT生成器.条目数
            Return -1
        End Get
    End Property

    ''' <summary>
    ''' 定时文字在后台线程读取轨道。替换操作先停止新租约，最后一个读者退出后
    ''' 才释放原生字幕资源，避免原子换轨与正在生成的位图帧互相踩踏。
    ''' </summary>
    Friend Function 尝试进入使用() As Boolean
        If Volatile.Read(已请求释放) <> 0 Then Return False
        Interlocked.Increment(活动使用者)
        If Volatile.Read(已请求释放) = 0 Then Return True
        离开使用()
        Return False
    End Function

    Friend Sub 离开使用()
        If Interlocked.Decrement(活动使用者) = 0 AndAlso Volatile.Read(已请求释放) <> 0 Then
            释放资源一次()
        End If
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If Interlocked.Exchange(已请求释放, 1) = 0 AndAlso Volatile.Read(活动使用者) = 0 Then
            释放资源一次()
        End If
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 释放资源一次()
        If Interlocked.Exchange(已释放资源, 1) = 0 Then
            ASS特效生成器?.Dispose()
            SUP生成器?.Dispose()
        End If
    End Sub
End Class

Public Enum 外部字幕格式
    SRT
    ASS
    SSA
    SUP
    ''' <summary>MKS/MKA 风格的字幕容器：文件内可有多条文本/位图轨。</summary>
    MKS
End Enum

''' <summary>无需预先打开即可展示在流选择器中的外部字幕。</summary>
Public NotInheritable Class 外部字幕候选
    Friend Sub New(路径 As String, 格式 As 外部字幕格式,
                   Optional 字幕轨 As 媒体流信息 = Nothing)
        Me.路径 = 路径
        Me.格式 = 格式
        Me.字幕轨 = 字幕轨
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 格式 As 外部字幕格式
    ''' <summary>字幕容器内被指到的轨；普通字幕文件为 Nothing。</summary>
    Public ReadOnly Property 字幕轨 As 媒体流信息
    ''' <summary>候选指向的容器轨索引；普通字幕文件为 -1。</summary>
    Public ReadOnly Property 轨索引 As Integer
        Get
            Return If(字幕轨?.索引, -1)
        End Get
    End Property
End Class

''' <summary>按固定后缀优先级扫描并加载与媒体文件对应的外部字幕。</summary>
Public NotInheritable Class 外部字幕自动加载器
    Private Shared ReadOnly 候选项 As (扩展名 As String, 格式 As 外部字幕格式)() = {
        (".srt", 外部字幕格式.SRT),
        (".ass", 外部字幕格式.ASS),
        (".ssa", 外部字幕格式.SSA),
        (".sup", 外部字幕格式.SUP),
        (".mks", 外部字幕格式.MKS)}
    Private Shared ReadOnly 位图字幕编码 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase) From {
        "hdmv_pgs_subtitle", "dvb_subtitle", "xsub"}

    Private Sub New()
    End Sub

    Public Shared Function 尝试加载同名字幕Async(媒体路径 As String,
                                              取消令牌 As CancellationToken) As Task(Of 外部字幕轨道)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 尝试加载同名字幕(媒体路径, 取消令牌), 取消令牌)
    End Function

    Public Shared Function 扫描同名字幕Async(媒体路径 As String,
                                         取消令牌 As CancellationToken) As Task(Of IReadOnlyList(Of 外部字幕候选))
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 扫描同名字幕(媒体路径, 取消令牌), 取消令牌)
    End Function

    Public Shared Function 是支持的字幕文件(路径 As String) As Boolean
        If String.IsNullOrWhiteSpace(路径) Then Return False
        Dim 忽略 As 外部字幕格式
        Return 尝试取得格式(Path.GetExtension(路径), 忽略)
    End Function

    Public Shared Function 加载字幕Async(字幕路径 As String,
                                     取消令牌 As CancellationToken) As Task(Of 外部字幕轨道)
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        Return Task.Run(Function() 加载字幕(字幕路径, 取消令牌), 取消令牌)
    End Function

    Public Shared Function 加载字幕Async(字幕路径 As String, 媒体路径 As String,
                                     取消令牌 As CancellationToken,
                                     Optional 指定流索引 As Integer = -1) As Task(Of 外部字幕轨道)
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 加载字幕(字幕路径, 媒体路径, 取消令牌, 指定流索引), 取消令牌)
    End Function

    ''' <summary>完整加载指定字幕；成功返回的轨道可原子替换当前轨道。</summary>
    Public Shared Function 加载字幕(字幕路径 As String,
                                Optional 取消令牌 As CancellationToken = Nothing) As 外部字幕轨道
        Return 加载字幕(字幕路径, 字幕路径, 取消令牌)
    End Function

    ''' <summary>
    ''' 完整加载字幕，并从指定媒体所在目录发现此轨道的私有字体。
    ''' 字幕容器（MKS）可传 <paramref name="指定流索引"/> 精确选轨，-1 表示自动选轨。
    ''' </summary>
    Public Shared Function 加载字幕(字幕路径 As String, 媒体路径 As String,
                                Optional 取消令牌 As CancellationToken = Nothing,
                                Optional 指定流索引 As Integer = -1) As 外部字幕轨道
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Dim 完整路径 = Path.GetFullPath(字幕路径)
        If Not File.Exists(完整路径) Then Throw New FileNotFoundException("字幕文件不存在。", 完整路径)
        Dim 格式 As 外部字幕格式
        If Not 尝试取得格式(Path.GetExtension(完整路径), 格式) Then
            Throw New NotSupportedException("仅支持 SRT、ASS、SSA、SUP 和 MKS 字幕文件。")
        End If
        取消令牌.ThrowIfCancellationRequested()
        Select Case 格式
            Case 外部字幕格式.SRT
                Dim 文档 = SRT字幕解析器.解析文件(完整路径)
                取消令牌.ThrowIfCancellationRequested()
                Return New 外部字幕轨道(完整路径, 格式,
                    New SRT字幕帧生成器(文档, 设置.实例对象.创建SRT字幕样式()), Nothing)
            Case 外部字幕格式.ASS, 外部字幕格式.SSA
                Dim 特效生成器 As ASS特效字幕帧生成器 = Nothing
                Try
                    特效生成器 = New ASS特效字幕帧生成器(完整路径, 媒体路径)
                    取消令牌.ThrowIfCancellationRequested()
                    Return New 外部字幕轨道(完整路径, 格式, Nothing,
                        Nothing, 特效生成器)
                Catch
                    特效生成器?.Dispose()
                    Throw
                End Try
            Case 外部字幕格式.SUP
                Dim 生成器 = New SUP字幕帧生成器(完整路径)
                Try
                    取消令牌.ThrowIfCancellationRequested()
                    Return New 外部字幕轨道(完整路径, 格式, Nothing, 生成器)
                Catch
                    生成器.Dispose()
                    Throw
                End Try
            Case 外部字幕格式.MKS
                Return 加载字幕容器(完整路径, 媒体路径, 指定流索引, 取消令牌)
            Case Else
                Throw New NotSupportedException("不支持此外部字幕格式。")
        End Select
    End Function

    ''' <summary>从媒体容器中完整加载指定字幕流；加载完成前不会影响当前字幕。</summary>
    Public Shared Function 加载内嵌字幕(媒体路径 As String, 流 As 媒体流信息,
                                  Optional 取消令牌 As CancellationToken = Nothing) As 外部字幕轨道
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        ArgumentNullException.ThrowIfNull(流)
        If Not String.Equals(流.类型, "subtitle", StringComparison.OrdinalIgnoreCase) OrElse 流.索引 < 0 Then
            Throw New ArgumentException("指定流不是有效的内嵌字幕。", NameOf(流))
        End If
        Dim 完整路径 = Path.GetFullPath(媒体路径)
        If Not File.Exists(完整路径) Then Throw New FileNotFoundException("媒体文件不存在。", 完整路径)
        Return 分派字幕轨(完整路径, 完整路径, 流, 取消令牌,
            外部字幕格式.SUP, 外部字幕格式.ASS, True)
    End Function

    ''' <summary>
    ''' 加载 MKS 等字幕容器：探测容器内字幕轨并选轨加载。容器自带的字体附件由内核
    ''' 打开轨道时直接加载；轨道的私有字体目录按媒体路径（通常是视频）查找。
    ''' </summary>
    Private Shared Function 加载字幕容器(容器路径 As String, 媒体路径 As String,
                                     指定流索引 As Integer,
                                     取消令牌 As CancellationToken) As 外部字幕轨道
        Dim 探测结果 = 探测字幕容器轨(容器路径)
        If 探测结果 Is Nothing Then
            Throw New NotSupportedException("无法读取此字幕容器的字幕轨（当前内核可能不支持字幕容器探测）。")
        End If
        Dim 选中 = 选择字幕轨(探测结果.流, 指定流索引)
        If 选中 Is Nothing Then
            ' 显式索引找不到（容器内容可能已被更换）与容器完全没有轨是两种
            ' 不同的故障，文案必须区分，否则排障方向会被带偏。
            Throw New NotSupportedException(
                If(指定流索引 >= 0,
                   "此字幕容器内不存在指定的字幕轨（容器内容可能已变化）。",
                   "此字幕容器内没有可用的字幕轨。"))
        End If
        取消令牌.ThrowIfCancellationRequested()
        Return 分派字幕轨(容器路径, 媒体路径, 选中, 取消令牌,
            外部字幕格式.MKS, 外部字幕格式.MKS, False, 探测结果.流)
    End Function

    ''' <summary>
    ''' 从探测到的字幕轨中选轨：显式 <paramref name="指定流索引"/> 优先（找不到时返回
    ''' Nothing，由调用方决定报错或回落）；自动选轨（-1）时 default/forced 轨优先，
    ''' 再退回第一条轨。
    ''' </summary>
    Friend Shared Function 选择字幕轨(字幕轨列表 As IReadOnlyList(Of 媒体流信息),
                                  指定流索引 As Integer) As 媒体流信息
        If 字幕轨列表 Is Nothing OrElse 字幕轨列表.Count = 0 Then Return Nothing
        If 指定流索引 >= 0 Then
            Return 字幕轨列表.FirstOrDefault(Function(x) x.索引 = 指定流索引)
        End If
        Dim 首选 = 字幕轨列表.FirstOrDefault(Function(x) x.是默认流 OrElse x.是强制流)
        Return If(首选, 字幕轨列表(0))
    End Function

    ''' <summary>探测字幕容器内的字幕轨；无探测能力（旧内核）或无字幕轨时返回 Nothing。</summary>
    Friend Shared Function 探测字幕容器轨(容器路径 As String) As 字幕流探测结果
        Dim 探测JSON = 播放器原生接口.探测字幕流JSON(容器路径)
        If String.IsNullOrWhiteSpace(探测JSON) Then Return Nothing
        Try
            Dim 结果 = JsonSerializer.Deserialize(Of 字幕流探测结果)(探测JSON)
            Return If(结果 IsNot Nothing AndAlso 结果.流.Length > 0, 结果, Nothing)
        Catch ex As JsonException
            Return Nothing
        End Try
    End Function

    ''' <summary>
    ''' 按编码分派单条字幕轨的加载：位图编码（PGS/DVB/XSUB）走 SUP 按需解码生成器，
    ''' 其余文本轨走 ASS 特效生成器。<paramref name="字体目录基准"/> 是私有字体目录的
    ''' 查找起点（内嵌为容器自身，MKS 为视频）。
    ''' </summary>
    Private Shared Function 分派字幕轨(字幕路径 As String, 字体目录基准 As String,
                                   流 As 媒体流信息, 取消令牌 As CancellationToken,
                                   位图格式 As 外部字幕格式, 文本格式 As 外部字幕格式,
                                   是内嵌 As Boolean,
                                   Optional 容器字幕轨 As 媒体流信息() = Nothing) As 外部字幕轨道
        If 位图字幕编码.Contains(流.编码) Then
            Dim 生成器 = New SUP字幕帧生成器(字幕路径, 流.索引)
            Try
                取消令牌.ThrowIfCancellationRequested()
                Return New 外部字幕轨道(字幕路径, 位图格式, Nothing, 生成器,
                    流索引:=流.索引, 是内嵌:=是内嵌, 容器字幕轨:=容器字幕轨)
            Catch
                生成器.Dispose()
                Throw
            End Try
        End If

        Dim 特效生成器 As ASS特效字幕帧生成器 = Nothing
        Try
            特效生成器 = New ASS特效字幕帧生成器(字幕路径, 字体目录基准, 流.索引)
            取消令牌.ThrowIfCancellationRequested()
            Return New 外部字幕轨道(字幕路径, 文本格式, Nothing, Nothing,
                特效生成器, 流.索引, 是内嵌, 容器字幕轨)
        Catch
            特效生成器?.Dispose()
            Throw
        End Try
    End Function

    ''' <summary>
    ''' 扫描媒体同目录内的对应字幕。除完全同名文件外，也接受
    ''' “媒体名.语言/版本.后缀”的常见命名，并按 SRT、ASS、SSA、SUP 排序。
    ''' </summary>
    Public Shared Function 扫描同名字幕(媒体路径 As String,
                                    Optional 取消令牌 As CancellationToken = Nothing) As IReadOnlyList(Of 外部字幕候选)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Dim 完整媒体路径 = Path.GetFullPath(媒体路径)
        Dim 目录 = Path.GetDirectoryName(完整媒体路径)
        If String.IsNullOrEmpty(目录) OrElse Not Directory.Exists(目录) Then
            Return Array.Empty(Of 外部字幕候选)()
        End If

        Dim 媒体名 = Path.GetFileNameWithoutExtension(完整媒体路径)
        Dim 带分隔符前缀 = 媒体名 & "."
        Dim 已发现路径 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        Dim 结果 As New List(Of 外部字幕候选)()

        For Each 文件路径 In Directory.EnumerateFiles(目录)
            取消令牌.ThrowIfCancellationRequested()
            Dim 格式 As 外部字幕格式
            If Not 尝试取得格式(Path.GetExtension(文件路径), 格式) Then Continue For

            Dim 字幕名 = Path.GetFileNameWithoutExtension(文件路径)
            If Not String.Equals(字幕名, 媒体名, StringComparison.OrdinalIgnoreCase) AndAlso
                Not 字幕名.StartsWith(带分隔符前缀, StringComparison.OrdinalIgnoreCase) Then Continue For

            Dim 完整字幕路径 = Path.GetFullPath(文件路径)
            If 已发现路径.Add(完整字幕路径) Then 结果.Add(New 外部字幕候选(完整字幕路径, 格式))
        Next

        Return 结果.OrderBy(Function(x) CInt(x.格式)).
            ThenBy(Function(x) If(String.Equals(Path.GetFileNameWithoutExtension(x.路径), 媒体名,
                                                StringComparison.OrdinalIgnoreCase), 0, 1)).
            ThenBy(Function(x) Path.GetFileName(x.路径), StringComparer.OrdinalIgnoreCase).
            ToArray()
    End Function

    ''' <summary>
    ''' 返回首个可成功解析的对应字幕；解析失败时继续尝试下一项，但扫描结果仍可供菜单展示。
    ''' </summary>
    Public Shared Function 尝试加载同名字幕(媒体路径 As String,
                                         Optional 取消令牌 As CancellationToken = Nothing) As 外部字幕轨道
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return 尝试加载候选字幕(扫描同名字幕(媒体路径, 取消令牌), 媒体路径, 取消令牌)
    End Function

    Friend Shared Function 尝试加载候选字幕Async(候选字幕 As IReadOnlyList(Of 外部字幕候选),
                                              媒体路径 As String,
                                              取消令牌 As CancellationToken) As Task(Of 外部字幕轨道)
        ArgumentNullException.ThrowIfNull(候选字幕)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 尝试加载候选字幕(候选字幕, 媒体路径, 取消令牌), 取消令牌)
    End Function

    Private Shared Function 尝试加载候选字幕(候选字幕 As IReadOnlyList(Of 外部字幕候选),
                                         媒体路径 As String,
                                         取消令牌 As CancellationToken) As 外部字幕轨道
        For Each 候选 In 候选字幕
            取消令牌.ThrowIfCancellationRequested()
            Try
                Return 加载字幕(候选.路径, 媒体路径, 取消令牌)
            Catch ex As OperationCanceledException
                Throw
            Catch
                ' 损坏的高优先级字幕不阻止后续格式被自动使用。
            End Try
        Next
        Return Nothing
    End Function

    Private Shared Function 尝试取得格式(扩展名 As String, ByRef 格式 As 外部字幕格式) As Boolean
        For Each 候选 In 候选项
            If String.Equals(扩展名, 候选.扩展名, StringComparison.OrdinalIgnoreCase) Then
                格式 = 候选.格式
                Return True
            End If
        Next
        Return False
    End Function
End Class
