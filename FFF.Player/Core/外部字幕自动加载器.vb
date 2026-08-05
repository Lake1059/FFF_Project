Imports System.IO
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
                   Optional 是内嵌 As Boolean = False)
        Me.路径 = 路径
        Me.格式 = 格式
        SRT生成器 = SRT
        SUP生成器 = SUP
        ASS特效生成器 = ASS特效
        Me.流索引 = 流索引
        Me.是内嵌 = 是内嵌
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 格式 As 外部字幕格式
    Public ReadOnly Property 流索引 As Integer
    Public ReadOnly Property 是内嵌 As Boolean
    Public ReadOnly Property SRT生成器 As SRT字幕帧生成器
    Public ReadOnly Property SUP生成器 As SUP字幕帧生成器
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
End Enum

''' <summary>无需预先打开即可展示在流选择器中的外部字幕。</summary>
Public NotInheritable Class 外部字幕候选
    Friend Sub New(路径 As String, 格式 As 外部字幕格式)
        Me.路径 = 路径
        Me.格式 = 格式
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 格式 As 外部字幕格式
End Class

''' <summary>按固定后缀优先级扫描并加载与媒体文件对应的外部字幕。</summary>
Public NotInheritable Class 外部字幕自动加载器
    Private Shared ReadOnly 候选项 As (扩展名 As String, 格式 As 外部字幕格式)() = {
        (".srt", 外部字幕格式.SRT),
        (".ass", 外部字幕格式.ASS),
        (".ssa", 外部字幕格式.SSA),
        (".sup", 外部字幕格式.SUP)}
    Private Shared ReadOnly 位图字幕编码 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase) From {
        "hdmv_pgs_subtitle", "dvd_subtitle", "dvb_subtitle", "xsub"}

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
                                     取消令牌 As CancellationToken) As Task(Of 外部字幕轨道)
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 加载字幕(字幕路径, 媒体路径, 取消令牌), 取消令牌)
    End Function

    ''' <summary>完整加载指定字幕；成功返回的轨道可原子替换当前轨道。</summary>
    Public Shared Function 加载字幕(字幕路径 As String,
                                Optional 取消令牌 As CancellationToken = Nothing) As 外部字幕轨道
        Return 加载字幕(字幕路径, 字幕路径, 取消令牌)
    End Function

    ''' <summary>完整加载字幕，并从指定媒体所在目录发现此轨道的私有字体。</summary>
    Public Shared Function 加载字幕(字幕路径 As String, 媒体路径 As String,
                                Optional 取消令牌 As CancellationToken = Nothing) As 外部字幕轨道
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Dim 完整路径 = Path.GetFullPath(字幕路径)
        If Not File.Exists(完整路径) Then Throw New FileNotFoundException("字幕文件不存在。", 完整路径)
        Dim 格式 As 外部字幕格式
        If Not 尝试取得格式(Path.GetExtension(完整路径), 格式) Then
            Throw New NotSupportedException("仅支持 SRT、ASS、SSA 和 SUP 外部字幕。")
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
        取消令牌.ThrowIfCancellationRequested()

        If 位图字幕编码.Contains(流.编码) Then
            Dim 生成器 = New SUP字幕帧生成器(完整路径, 流.索引)
            Try
                取消令牌.ThrowIfCancellationRequested()
                Return New 外部字幕轨道(完整路径, 外部字幕格式.SUP, Nothing, 生成器,
                    流索引:=流.索引, 是内嵌:=True)
            Catch
                生成器.Dispose()
                Throw
            End Try
        End If

        Dim 特效生成器 As ASS特效字幕帧生成器 = Nothing
        Try
            特效生成器 = New ASS特效字幕帧生成器(完整路径, 完整路径, 流.索引)
            取消令牌.ThrowIfCancellationRequested()
            Return New 外部字幕轨道(完整路径, 外部字幕格式.ASS, Nothing, Nothing,
                特效生成器, 流.索引, True)
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
            Catch ex As NotSupportedException
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
